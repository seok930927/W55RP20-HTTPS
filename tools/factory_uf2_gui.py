#!/usr/bin/env python3
"""
factory_uf2_gui.py - Embed a MAC address into a W55RP20 UF2/HEX image.

  HEX  input  → HEX  output
  UF2  input  → UF2  output
  MAC only    → HEX, UF2 or BIN  (user choice)

BIN 은 주소 정보가 없는 raw 이미지라 MAC only 모드에서만 지원한다.
생성된 BIN 은 파일 오프셋 0 이 MAC 첫 바이트이며, 구울 때 0x10124000
오프셋을 지정하면 MAC 섹터 하나만 갱신되고 DevConfig(0x120000) 와
인증서(0x121000~0x123000) 는 보존된다.

  picotool load MAC_XXXXXXXXXXXX.bin -o 0x10124000
"""

import struct
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
import os
import re

UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END    = 0x0AB16F30
RP2040_FAMILY_ID = 0xE48BFF56
FLASH_MAC_XIP    = 0x10124000   # XIP absolute address
FLASH_MAC_OFFSET = 0x00124000   # flash-relative offset (XIP_BASE 제외)
BLOCK_BYTES      = 512
DATA_BYTES       = 256


# ════════════════════════════════════════════════════════
#  Intel HEX  (read + write)
# ════════════════════════════════════════════════════════

def _hex_checksum(data: bytes) -> int:
    return ((~sum(data) & 0xFF) + 1) & 0xFF


def read_hex_mem(path: str) -> dict:
    """HEX 파일 → {절대주소: 바이트} 딕셔너리"""
    mem = {}
    ext = 0
    with open(path, "r") as f:
        for line in f:
            line = line.strip()
            if not line.startswith(":"):
                continue
            raw   = bytes.fromhex(line[1:])
            count = raw[0]
            addr  = (raw[1] << 8) | raw[2]
            rtype = raw[3]
            data  = raw[4:4 + count]
            if rtype == 0x00:
                base = (ext << 16) | addr
                for i, b in enumerate(data):
                    mem[base + i] = b
            elif rtype == 0x04:
                ext = (data[0] << 8) | data[1]
            elif rtype == 0x01:
                break
    return mem


def write_hex(mem: dict, path: str) -> None:
    """절대주소:바이트 딕셔너리 → Intel HEX 파일"""
    ROW = 16
    lines = []
    current_ext = -1

    for base in sorted(set((a // ROW) * ROW for a in mem)):
        upper = base >> 16
        if upper != current_ext:
            current_ext = upper
            d = bytes([upper >> 8, upper & 0xFF])
            rec = bytes([0x02, 0x00, 0x00, 0x04]) + d
            lines.append(f":{rec.hex().upper()}{_hex_checksum(rec):02X}")
        row = bytes(mem.get(base + i, 0xFF) for i in range(ROW))
        if all(b == 0xFF for b in row):
            continue
        lower = base & 0xFFFF
        rec = bytes([ROW, lower >> 8, lower & 0xFF, 0x00]) + row
        lines.append(f":{rec.hex().upper()}{_hex_checksum(rec):02X}")

    lines.append(":00000001FF")
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")


def embed_mac_hex(input_path: str, mac: bytes, output_path: str) -> int:
    """HEX → MAC 삽입 → HEX. 블록 수(바이트행 수) 반환."""
    mem = read_hex_mem(input_path)
    if not mem:
        raise ValueError("HEX 파일에서 데이터를 읽을 수 없습니다.")
    for i, b in enumerate(mac):
        mem[FLASH_MAC_XIP + i] = b
    write_hex(mem, output_path)
    return len(mem)


def mac_only_hex(mac: bytes, output_path: str) -> None:
    """MAC 6바이트만 담긴 HEX 파일 생성."""
    mem = {FLASH_MAC_XIP + i: b for i, b in enumerate(mac)}
    write_hex(mem, output_path)


# ════════════════════════════════════════════════════════
#  UF2
# ════════════════════════════════════════════════════════

def _make_uf2_block(addr, data, block_num, total_blocks):
    header = struct.pack("<IIIIIIII",
        UF2_MAGIC_START0, UF2_MAGIC_START1, 0x00002000,
        addr, DATA_BYTES, block_num, total_blocks, RP2040_FAMILY_ID)
    payload = data + b"\x00" * (BLOCK_BYTES - len(header) - len(data) - 4)
    return header + payload + struct.pack("<I", UF2_MAGIC_END)


def _block_addr(blk: bytes) -> int:
    return struct.unpack_from("<I", blk, 12)[0]


def _read_uf2_blocks(path: str) -> list:
    with open(path, "rb") as f:
        raw = f.read()
    if len(raw) % BLOCK_BYTES:
        raise ValueError(f"UF2 파일 크기가 512 배수가 아닙니다 ({len(raw)} bytes)")
    return [raw[i:i+BLOCK_BYTES] for i in range(0, len(raw), BLOCK_BYTES)]


def _hex_to_uf2_blocks(path: str) -> list:
    mem = read_hex_mem(path)
    if not mem:
        raise ValueError("HEX 파일에서 데이터를 읽을 수 없습니다.")
    addrs = sorted(mem.keys())
    min_a = (addrs[0]  // DATA_BYTES) * DATA_BYTES
    max_a = (addrs[-1] // DATA_BYTES) * DATA_BYTES
    blocks = []
    for base in range(min_a, max_a + DATA_BYTES, DATA_BYTES):
        chunk = bytes(mem.get(base + i, 0xFF) for i in range(DATA_BYTES))
        if all(b == 0xFF for b in chunk):
            continue
        blocks.append(_make_uf2_block(base, chunk, len(blocks), 0))
    return blocks


def _patch_total(blocks, total):
    out = []
    for blk in blocks:
        m0, m1, fl, ad, ps, bn, _, fi = struct.unpack_from("<IIIIIIII", blk)
        out.append(struct.pack("<IIIIIIII", m0, m1, fl, ad, ps, bn, total, fi)
                   + blk[32:508] + blk[508:512])
    return out


def embed_mac_uf2(input_path, mac: bytes, output_path: str) -> int:
    """UF2/HEX → MAC 삽입 → UF2. 블록 수 반환."""
    if input_path:
        ext = os.path.splitext(input_path)[1].lower()
        fw = _read_uf2_blocks(input_path) if ext == ".uf2" else _hex_to_uf2_blocks(input_path)
    else:
        fw = []
    fw = [b for b in fw if _block_addr(b) != FLASH_MAC_XIP]
    total = len(fw) + 1
    fw = _patch_total(fw, total)
    mac_data = mac + b"\xff" * (DATA_BYTES - len(mac))
    fw.append(_make_uf2_block(FLASH_MAC_XIP, mac_data, len(fw), total))
    with open(output_path, "wb") as f:
        for blk in fw:
            f.write(blk)
    return len(fw)


# ════════════════════════════════════════════════════════
#  BIN  (MAC only 전용)
# ════════════════════════════════════════════════════════

def mac_only_bin(mac: bytes, output_path: str) -> None:
    """MAC 6바이트 + 0xFF 패딩 1페이지(256B) BIN 파일 생성.

    UF2 MAC 블록과 동일한 페이로드다. 파일 오프셋 0 이 MAC 첫 바이트이므로
    플래싱 툴에 0x10124000 을 지정해야 올바른 위치에 기록된다.
    """
    with open(output_path, "wb") as f:
        f.write(mac + b"\xff" * (DATA_BYTES - len(mac)))


def bin_flash_hint(filename: str) -> str:
    return (f"picotool load {filename} -o 0x{FLASH_MAC_XIP:08X}\n"
            f"(SWD 사용 시 flash offset 0x{FLASH_MAC_OFFSET:06X})")


# ════════════════════════════════════════════════════════
#  MAC helpers
# ════════════════════════════════════════════════════════

def parse_mac(mac_str: str) -> bytes:
    cleaned = re.sub(r"[:\-.]", "", mac_str).upper()
    if not re.fullmatch(r"[0-9A-F]{12}", cleaned):
        raise ValueError(f"올바르지 않은 MAC 주소입니다: '{mac_str}'")
    return bytes(int(cleaned[i:i+2], 16) for i in range(0, 12, 2))


def mac_to_str(mac: bytes) -> str:
    return "".join(f"{b:02X}" for b in mac)


# ════════════════════════════════════════════════════════
#  GUI
# ════════════════════════════════════════════════════════

class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("W55RP20 Factory UF2 Generator")
        self.resizable(False, False)
        self._build_ui()

    def _build_ui(self):
        pad = dict(padx=12, pady=5)

        # ── 모드 선택 ──────────────────────────────
        mode_frame = tk.LabelFrame(self, text="모드", padx=10, pady=6)
        mode_frame.grid(row=0, column=0, columnspan=3, sticky="ew", padx=12, pady=(10, 4))
        self.mode_var = tk.StringVar(value="firmware")
        tk.Radiobutton(mode_frame, text="펌웨어 + MAC  (UF2 / HEX)",
                       variable=self.mode_var, value="firmware",
                       command=self._on_mode_change).pack(side="left", padx=8)
        tk.Radiobutton(mode_frame, text="MAC 블록만",
                       variable=self.mode_var, value="mac_only",
                       command=self._on_mode_change).pack(side="left", padx=8)

        # ── 입력 파일 ──────────────────────────────
        tk.Label(self, text="입력 파일", anchor="w").grid(row=1, column=0, sticky="w", **pad)
        self.input_var = tk.StringVar()
        self.input_entry = tk.Entry(self, textvariable=self.input_var, width=50)
        self.input_entry.grid(row=1, column=1, **pad)
        self.input_btn = tk.Button(self, text="찾기", command=self._browse_input)
        self.input_btn.grid(row=1, column=2, padx=(0, 12))
        self.input_hint = tk.Label(self, text="UF2 또는 HEX  →  같은 형식으로 출력", fg="gray")
        self.input_hint.grid(row=2, column=1, sticky="w", padx=12, pady=0)

        # ── MAC only 출력 형식 ─────────────────────
        self.fmt_frame = tk.LabelFrame(self, text="출력 형식", padx=10, pady=4)
        self.fmt_frame.grid(row=3, column=0, columnspan=3, sticky="ew", padx=12, pady=(4, 0))
        self.fmt_var = tk.StringVar(value="uf2")
        tk.Radiobutton(self.fmt_frame, text="UF2", variable=self.fmt_var,
                       value="uf2", command=self._update_preview).pack(side="left", padx=8)
        tk.Radiobutton(self.fmt_frame, text="HEX", variable=self.fmt_var,
                       value="hex", command=self._update_preview).pack(side="left", padx=8)
        self.fmt_frame.grid_remove()   # 펌웨어 모드에서는 숨김

        # ── MAC 주소 ───────────────────────────────
        tk.Label(self, text="MAC 주소", anchor="w").grid(row=4, column=0, sticky="w", **pad)
        self.mac_var = tk.StringVar()
        self.mac_var.trace_add("write", self._update_preview)
        tk.Entry(self, textvariable=self.mac_var, width=20, font=("Consolas", 11)).grid(
            row=4, column=1, sticky="w", **pad)
        tk.Label(self, text="예: 0008DC123456  또는  00:08:DC:12:34:56", fg="gray").grid(
            row=5, column=1, sticky="w", padx=12, pady=0)

        # ── 출력 폴더 ──────────────────────────────
        tk.Label(self, text="출력 폴더", anchor="w").grid(row=6, column=0, sticky="w", **pad)
        self.folder_var = tk.StringVar()
        tk.Entry(self, textvariable=self.folder_var, width=50).grid(row=6, column=1, **pad)
        tk.Button(self, text="찾기", command=self._browse_folder).grid(row=6, column=2, padx=(0, 12))

        # ── 출력 파일명 미리보기 ───────────────────
        tk.Label(self, text="출력 파일명", anchor="w").grid(row=7, column=0, sticky="w", **pad)
        self.preview_var = tk.StringVar(value="MAC_<MAC>.uf2")
        tk.Label(self, textvariable=self.preview_var, fg="#0078D4",
                 font=("Consolas", 10)).grid(row=7, column=1, sticky="w", **pad)

        # ── 생성 버튼 ──────────────────────────────
        ttk.Separator(self, orient="horizontal").grid(
            row=8, column=0, columnspan=3, sticky="ew", pady=4)
        tk.Button(self, text="Factory 파일 생성", command=self._generate,
                  bg="#0078D4", fg="white", font=("", 10, "bold"),
                  padx=16, pady=6).grid(row=9, column=0, columnspan=3, pady=8)

        # ── 상태 표시 ──────────────────────────────
        self.status_var = tk.StringVar(value="모드를 선택하고 MAC 주소를 입력하세요.")
        self.status_label = tk.Label(self, textvariable=self.status_var, fg="gray",
                                     wraplength=480, justify="left")
        self.status_label.grid(row=10, column=0, columnspan=3, padx=12, pady=(0, 10))

    # ── 모드 전환 ─────────────────────────────────

    def _on_mode_change(self):
        is_fw = self.mode_var.get() == "firmware"
        state = tk.NORMAL if is_fw else tk.DISABLED
        self.input_entry.config(state=state)
        self.input_btn.config(state=state)
        if is_fw:
            self.fmt_frame.grid_remove()
            if not self.input_var.get():
                self.input_var.set("")
        else:
            self.input_var.set("")
            self.fmt_frame.grid()
        self._update_preview()

    def _output_ext(self) -> str:
        """현재 모드에 맞는 출력 확장자 반환."""
        if self.mode_var.get() == "firmware":
            path = self.input_var.get().strip()
            return os.path.splitext(path)[1].lower() if path else ".uf2"
        return "." + self.fmt_var.get()

    def _update_preview(self, *_):
        try:
            mac = parse_mac(self.mac_var.get())
            ext = self._output_ext()
            self.preview_var.set(f"MAC_{mac_to_str(mac)}{ext}")
        except ValueError:
            ext = self._output_ext()
            self.preview_var.set(f"MAC_<MAC>{ext}")

    # ── 파일/폴더 선택 ────────────────────────────

    def _browse_input(self):
        path = filedialog.askopenfilename(
            title="입력 파일 선택",
            filetypes=[("펌웨어 파일", "*.uf2 *.hex *.ihex"),
                       ("UF2 files",  "*.uf2"),
                       ("HEX files",  "*.hex *.ihex"),
                       ("All files",  "*.*")])
        if path:
            self.input_var.set(path)
            if not self.folder_var.get():
                self.folder_var.set(os.path.dirname(path))
            self._update_preview()

    def _browse_folder(self):
        folder = filedialog.askdirectory(title="출력 폴더 선택")
        if folder:
            self.folder_var.set(folder)

    # ── 생성 ─────────────────────────────────────

    def _generate(self):
        mode       = self.mode_var.get()
        input_path = self.input_var.get().strip() if mode == "firmware" else None
        mac_str    = self.mac_var.get().strip()
        folder     = self.folder_var.get().strip()

        if mode == "firmware":
            if not input_path:
                messagebox.showerror("오류", "입력 파일을 선택하세요.")
                return
            if not os.path.isfile(input_path):
                messagebox.showerror("오류", f"파일을 찾을 수 없습니다:\n{input_path}")
                return
        if not folder:
            messagebox.showerror("오류", "출력 폴더를 선택하세요.")
            return

        try:
            mac = parse_mac(mac_str)
        except ValueError as e:
            messagebox.showerror("MAC 오류", str(e))
            return

        ext      = self._output_ext()
        filename = f"MAC_{mac_to_str(mac)}{ext}"
        out_path = os.path.join(folder, filename)

        try:
            if ext == ".hex":
                if input_path:
                    embed_mac_hex(input_path, mac, out_path)
                else:
                    mac_only_hex(mac, out_path)
            else:
                embed_mac_uf2(input_path, mac, out_path)
        except Exception as e:
            messagebox.showerror("생성 실패", str(e))
            return

        mac_fmt = ":".join(f"{b:02X}" for b in mac)
        label   = "MAC only" if not input_path else os.path.basename(input_path)
        self.status_var.set(f"완료!  [{label}]  MAC={mac_fmt}  →  {filename}")
        self.status_label.config(fg="green")
        messagebox.showinfo("완료", f"파일 생성 완료!\n\n{out_path}")


if __name__ == "__main__":
    app = App()
    app.mainloop()
