# 빌드 환경 Setup

W55RP20-HTTPS 프로젝트를 처음부터 빌드하기 위한 환경 구성 가이드.

---

## 필수 도구 목록

| 도구 | 최소 버전 | 역할 |
|---|---|---|
| [ARM GNU Embedded Toolchain](https://developer.arm.com/downloads/-/gnu-rm) | 10.3-2021.10 | Cortex-M0+ 크로스 컴파일러 (arm-none-eabi-gcc) |
| [CMake](https://cmake.org/download/) | 3.12 | 빌드 시스템 생성기 |
| [Ninja](https://github.com/ninja-build/ninja/releases) | 1.10 이상 | 실제 빌드 실행기 |
| [Python 3](https://www.python.org/downloads/) | 3.8 이상 | 빌드 전/후 처리 스크립트 실행 |
| [Git](https://git-scm.com/downloads) | 2.x | 소스 및 서브모듈 관리 |

> 현재 개발 환경: arm-none-eabi-gcc 10.3.1, CMake 3.29.8, Ninja 1.11.0, Python 3.13.7, Git 2.50.1

---

## 1. 도구 설치 (Windows)

### 1-1. ARM GNU Embedded Toolchain

1. https://developer.arm.com/downloads/-/gnu-rm 에서 Windows installer (.exe) 다운로드
2. 설치 완료 후 **"Add path to environment variable"** 체크
3. 확인:
   ```powershell
   arm-none-eabi-gcc --version
   ```

### 1-2. CMake

1. https://cmake.org/download/ 에서 Windows installer (.msi) 다운로드
2. 설치 시 **"Add CMake to the system PATH"** 선택
3. 확인:
   ```powershell
   cmake --version
   ```

### 1-3. Ninja

1. https://github.com/ninja-build/ninja/releases 에서 `ninja-win.zip` 다운로드
2. `ninja.exe`를 PATH에 포함된 디렉토리에 복사 (예: `C:\tools\`)
3. 확인:
   ```powershell
   ninja --version
   ```

### 1-4. Python 3

1. https://www.python.org/downloads/ 에서 Windows installer 다운로드
2. 설치 시 **"Add Python to PATH"** 체크
3. 확인:
   ```powershell
   py -3 --version
   ```

> 빌드 스크립트가 `py -3` 명령어를 사용함. Python 설치 시 `py` 런처가 함께 설치됨.

### 1-5. Git

1. https://git-scm.com/downloads 에서 Windows installer 다운로드
2. 설치 완료 후 확인:
   ```powershell
   git --version
   ```

---

## 2. 소스 클론

서브모듈이 포함된 전체 소스를 클론한다.

```bash
git clone --recurse-submodules <repo-url>
cd W55RP20-HTTPS
```

이미 클론된 상태에서 서브모듈이 없다면:

```bash
git submodule update --init --recursive
```

### 포함된 서브모듈 라이브러리

| 라이브러리 | 경로 | 역할 |
|---|---|---|
| pico-sdk | `libraries/pico-sdk` | RP2040 공식 SDK (하드웨어 드라이버, pico_stdlib 등) |
| FreeRTOS-Kernel | `libraries/FreeRTOS-Kernel` | RTOS 커널 (RP2040 포트 포함) |
| ioLibrary_Driver | `libraries/ioLibrary_Driver` | WIZnet W5500 드라이버, SNMP, DHCP, DNS |
| mbedtls | `libraries/mbedtls` | TLS 1.2/1.3 라이브러리 (HTTPS에 사용) |
| aws-iot-device-sdk-embedded-C | `libraries/aws-iot-device-sdk-embedded-C` | MQTT 전송 인터페이스 |

---

## 3. ioLibrary SNMP 패치 적용

SNMP 소켓 버그 수정 및 장비 OID 정보 패치. **최초 클론 후 반드시 적용해야 함.**

```bash
# ioLibrary를 패치 베이스 커밋으로 이동
cd libraries/ioLibrary_Driver
git checkout b981401
cd ../..

# 패치 적용 (프로젝트 루트에서)
cd libraries
git apply ../ioLibrary_snmp_patch.patch
cd ..
```

적용 확인:

```bash
cd libraries
git diff --stat
# 출력: Internet/SNMP/snmp.c, Internet/SNMP/snmp_custom.c 수정됨
```

자세한 내용은 [ioLibrary_snmp_patch_HOW_TO_APPLY.md](ioLibrary_snmp_patch_HOW_TO_APPLY.md) 참조.

---

## 4. 보드 선택

`CMakeLists.txt` 최상단에서 대상 보드를 지정한다.

```cmake
# CMakeLists.txt (39번째 줄 근처)
set(BOARD_NAME PLATYPUS_S2E)   # ← 보드에 맞게 변경
```

| BOARD_NAME | 칩셋 | MAC OUI | 설명 |
|---|---|---|---|
| `PLATYPUS_S2E` | W5500 | EC:74:CD | 현재 개발 중인 보드 (기본값) |
| `W55RP20_S2E` | W5500 | 00:08:DC | WIZnet W55RP20-S2E |
| `W232N` | W5500 | 00:08:DC | WIZnet W232N |
| `IP20` | W5500 | 00:08:DC | WIZnet IP20 |

---

## 5. CMake 구성 및 빌드

프로젝트 루트에서 실행:

```powershell
# CMake 구성 (최초 1회 또는 CMakeLists.txt 변경 시)
cmake -G Ninja -B build

# 빌드
cmake --build build
```

또는 build 디렉토리 안에서:

```powershell
cd build
ninja
```

### 빌드 결과물

빌드가 성공하면 `bin_files/` 디렉토리에 출력됨:

| 파일 | 설명 |
|---|---|
| `Boot.uf2` | 부트로더 단독 UF2 |
| `App_linker.uf2` | 애플리케이션 단독 UF2 |
| `Boot-App_linker_Merged.uf2` | 부트로더 + 앱 통합 UF2 **(플래싱 시 이걸 사용)** |
| `Boot-App_linker_Merged.hex` | 통합 Intel HEX (J-Link 등 사용 시) |

### 빌드 중 자동 실행되는 스크립트

CMake 빌드 안에서 다음 Python 스크립트가 자동으로 실행됨:

| 스크립트 | 실행 시점 | 역할 |
|---|---|---|
| `tools/html_to_c_header.py` | 빌드 전 | `Web_page.html` → `Web_page.h` 변환 |
| `tools/merge_hex.py` | 빌드 후 | Boot + App HEX를 통합, bin_files/에 복사 |
| `tools/hex_to_uf2_converter.py` | merge_hex 후 | 통합 HEX → UF2 변환 |
| `style/restyle.py` | 빌드 후 | 코드 스타일 정리 |

---

## 6. 펌웨어 플래싱

### BOOTSEL 모드로 UF2 드래그 앤 드롭

1. 보드의 **BOOTSEL** 버튼을 누른 채로 USB 연결
2. Windows에서 `RPI-RP2` 드라이브로 인식됨
3. `bin_files/Boot-App_linker_Merged.uf2` 파일을 드라이브에 복사
4. 복사 완료 후 자동 재부팅

### picotool 사용 (선택)

```bash
picotool load bin_files/Boot-App_linker_Merged.uf2 --force
picotool reboot
```

---

## 7. VS Code 개발 환경 (선택)

### 권장 확장

- **CMake Tools** (ms-vscode.cmake-tools) — CMake 빌드 통합
- **C/C++** (ms-vscode.cpptools) — IntelliSense, 디버깅
- **Cortex-Debug** (marus25.cortex-debug) — SWD/JTAG 디버깅 (J-Link, OpenOCD)

### CMake Tools 설정

VS Code에서 프로젝트를 열면 CMake Tools가 자동으로 `CMakeLists.txt`를 감지.  
Kit 선택: **GCC arm-none-eabi** 선택.

---

## 8. 자주 발생하는 문제

### `py` 명령을 찾을 수 없음

Python 설치 시 **py launcher** 설치 항목을 체크해야 함.  
또는 `CMakeLists.txt`의 `py -3`을 `python3`으로 변경.

### 서브모듈 디렉토리가 비어 있음

```bash
git submodule update --init --recursive
```

### CMake re-configure 필요 (빌드 오류 시)

```powershell
Remove-Item -Recurse -Force build
cmake -G Ninja -B build
cmake --build build
```

### arm-none-eabi-gcc를 찾을 수 없음

PATH 환경 변수에 toolchain `bin/` 경로가 포함됐는지 확인.  
예: `C:\Program Files (x86)\GNU Arm Embedded Toolchain\10 2021.10\bin`

---

## 9. 프로젝트 디렉토리 구조 (요약)

```
W55RP20-HTTPS/
├── CMakeLists.txt          # 최상위 빌드 설정, 보드 선택
├── pico_sdk_import.cmake   # pico-sdk 임포트 헬퍼
├── FreeRTOS_Kernel_import.cmake
├── main/
│   ├── App/
│   │   ├── App.c           # main(), FreeRTOS 태스크 생성
│   │   └── CMakeLists.txt
│   └── Boot/               # 부트로더
├── port/
│   └── app/
│       ├── platform_handler/   # 핵심 핸들러 (HTTP, SNMP, UART, 센서 등)
│       ├── configuration/      # ConfigData, segcp
│       ├── mbedtls/            # TLS 포팅 레이어
│       ├── html_file/          # Web_page.html, Web_page.h
│       └── tusb_config.h       # TinyUSB 설정
├── libraries/
│   ├── pico-sdk/
│   ├── FreeRTOS-Kernel/
│   ├── ioLibrary_Driver/   # SNMP 패치 적용 필요
│   ├── mbedtls/
│   └── aws-iot-device-sdk-embedded-C/
├── tools/                  # 빌드 보조 Python 스크립트
├── bin_files/              # 빌드 출력물 (UF2, HEX, BIN)
├── docs/                   # 개발 문서
└── TODO.md                 # 미착수 작업 목록
```
