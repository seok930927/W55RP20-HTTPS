#!/usr/bin/env python3
"""
make_factory_uf2.py - Append a MAC address UF2 block to a firmware UF2.

The W55RP20 firmware stores the MAC address at flash offset 0x124000
(XIP address 0x10124000). This script embeds that address into the
UF2 image so that the device boots without prompting for a MAC.

Boot sequence with the factory UF2
  1. First boot: DevConfig at 0x120000 is all-0xFF → factory-reset branch runs.
     load_DevConfig_from_storage() already read our MAC from STORAGE_MAC before
     calling set_DevConfig_to_factory_value(), so the alias is built correctly.
     save_DevConfig_to_storage() persists the MAC → reboot.
  2. Second boot: DevConfig valid, MAC OUI matches → check_mac_address() skips.

Usage
  merge mode : python make_factory_uf2.py firmware.uf2 AABBCCDDEEFF output.uf2
  standalone : python make_factory_uf2.py --mac-only AABBCCDDEEFF mac.uf2
  MAC formats: AABBCCDDEEFF  |  AA:BB:CC:DD:EE:FF  |  AA-BB-CC-DD-EE-FF
"""

import sys
import struct
import argparse

UF2_MAGIC_START0 = 0x0A324655  # "UF2\n"
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END    = 0x0AB16F30
RP2040_FAMILY_ID = 0xE48BFF56

# FLASH_MAC_ADDR = FLASH_PRIKEY_ADDR + 0x1000
# FLASH_PRIKEY_ADDR = FLASH_CLICA_ADDR + 0x1000 = 0x123000
# XIP base 0x10000000 + offset 0x124000 = 0x10124000
FLASH_MAC_XIP = 0x10124000

BLOCK_BYTES = 512
DATA_BYTES  = 256


def parse_mac(mac_str: str) -> bytes:
    cleaned = mac_str.replace(":", "").replace("-", "").replace(".", "")
    if len(cleaned) != 12:
        raise ValueError(f"Bad MAC address: '{mac_str}' (need 12 hex digits)")
    return bytes(int(cleaned[i:i+2], 16) for i in range(0, 12, 2))


def read_blocks(path: str) -> list[bytes]:
    with open(path, "rb") as f:
        raw = f.read()
    if len(raw) % BLOCK_BYTES:
        raise ValueError(f"{path}: size {len(raw)} is not a multiple of 512")
    return [raw[i:i+BLOCK_BYTES] for i in range(0, len(raw), BLOCK_BYTES)]


def make_block(addr: int, data: bytes, block_num: int, total_blocks: int) -> bytes:
    assert len(data) == DATA_BYTES
    header = struct.pack("<IIIIIIII",
        UF2_MAGIC_START0,
        UF2_MAGIC_START1,
        0x00002000,       # flags: familyID present
        addr,
        DATA_BYTES,
        block_num,
        total_blocks,
        RP2040_FAMILY_ID,
    )
    payload = data + b"\x00" * (BLOCK_BYTES - len(header) - len(data) - 4)
    return header + payload + struct.pack("<I", UF2_MAGIC_END)


def block_addr(blk: bytes) -> int:
    return struct.unpack_from("<I", blk, 12)[0]


def patch_total_blocks(blocks: list[bytes], total: int) -> list[bytes]:
    out = []
    for blk in blocks:
        m0, m1, flags, addr, psize, bnum, _, fid = struct.unpack_from("<IIIIIIII", blk)
        hdr = struct.pack("<IIIIIIII", m0, m1, flags, addr, psize, bnum, total, fid)
        out.append(hdr + blk[32:508] + blk[508:512])
    return out


def build_mac_block(mac: bytes, block_num: int, total_blocks: int) -> bytes:
    data = mac + b"\xff" * (DATA_BYTES - len(mac))
    return make_block(FLASH_MAC_XIP, data, block_num, total_blocks)


def write_uf2(path: str, blocks: list[bytes]) -> None:
    with open(path, "wb") as f:
        for blk in blocks:
            f.write(blk)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Embed a MAC address into a W55RP20 factory UF2 image."
    )
    parser.add_argument("--mac-only", action="store_true",
                        help="Generate a standalone MAC-only UF2 (no firmware merge)")
    parser.add_argument("args", nargs="+",
                        help="[firmware.uf2] <MAC> <output.uf2>")
    opts = parser.parse_args()

    if opts.mac_only:
        if len(opts.args) != 2:
            parser.error("--mac-only requires: <MAC> <output.uf2>")
        mac_str, out_path = opts.args
        fw_blocks = []
    else:
        if len(opts.args) != 3:
            parser.error("merge mode requires: <firmware.uf2> <MAC> <output.uf2>")
        in_path, mac_str, out_path = opts.args
        fw_blocks = read_blocks(in_path)
        print(f"Firmware blocks : {len(fw_blocks)}")

    mac = parse_mac(mac_str)
    print(f"MAC             : {':'.join(f'{b:02X}' for b in mac)}")
    print(f"MAC XIP address : 0x{FLASH_MAC_XIP:08X}")

    # 기존 MAC 블록이 있으면 교체, 없으면 추가
    existing = [i for i, b in enumerate(fw_blocks) if block_addr(b) == FLASH_MAC_XIP]
    if existing:
        print(f"기존 MAC 블록 교체 (index {existing[0]})")
        fw_blocks = [b for i, b in enumerate(fw_blocks) if i not in existing]

    total = len(fw_blocks) + 1
    fw_blocks = patch_total_blocks(fw_blocks, total)
    mac_blk  = build_mac_block(mac, len(fw_blocks), total)

    write_uf2(out_path, fw_blocks + [mac_blk])
    print(f"Total blocks    : {total}")
    print(f"Output          : {out_path}")


if __name__ == "__main__":
    main()
