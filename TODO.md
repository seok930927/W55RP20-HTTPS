# TODO

W55RP20-HTTPS 프로젝트 — 나중에 할 작업 모음. 까먹지 않기 위한 메모.

---

## 1. 환경설정 페이지 매번 로그인 요구

**상태**: 해결 완료 (2026-05-27)
- `Web_page.html` — `configUnlocked` 플래그 제거, `showTab('config')` 진입 시
  항상 인증 모달 표시. `Web_page.h` 재생성 완료. FW 변경 없음.

### 요구사항
고객 요청 — 환경설정(config) 탭에 진입할 때마다 비밀번호 인증을 받아야 한다.
현재는 한 번 인증하면 페이지 새로고침 전까지 계속 열린다.

### 현재 동작
`port/app/html_file/Web_page.html` 의 JS:
- `configUnlocked` 플래그가 한 번 `true`가 되면 그대로 유지
- 다른 탭 갔다가 환경설정 다시 눌러도 모달 안 뜸
- F5 새로고침 해야만 다시 `false`로 초기화

### 변경 방향 (권장)
`configUnlocked` 플래그를 없애고 환경설정 진입 시 무조건 인증 모달 표시.
`showTab()`에서 `tab === 'config'`이면 항상 모달.

### 작업 범위
- `Web_page.html` JS 수정 (showTab / verifyPass / configUnlocked 제거)
- `Web_page.h` 재생성 (PowerShell 변환)
- FW 코드 변경 없음 — `/api/verify-pass` 그대로 사용

---

## 2. SNMP allowed IP / Trap 대상 슬롯 2개 → 4개로 확장

**상태**: 해결 완료 (2026-05-27)

### 적용 내용
- `ConfigData.h` — `struct __snmp_option` `[2][4]`→`[4][4]` (매크로 `SNMP_ALLOWED_IP_CNT`/
  `SNMP_TRAP_IP_CNT`=4). 확장 섹션 +16B 흡수 위해 `reserved_ext` 126→110,
  **`DEVCONFIG_EXT_VERSION` 1→2** (기존 장치는 부팅 시 ext 영역 재초기화).
- `ConfigData.c` — factory init은 `sizeof` 사용이라 자동 반영.
- `snmp.c` / `snmp.h` (ioLibrary, 패치본) — `s_allowed_ip[4][4]`,
  `snmp_set_allowed_ips(const uint8_t[4][4])`, allow-any 판정·매칭을 4슬롯 루프로.
- `snmpHandler.c` — `snmp_flush_traps()` 트랩 루프 `t<SNMP_TRAP_IP_CNT`.
- `httpHandler.c` — config JSON/POST를 `allowed_ip0~3`/`trap_ip0~3` 루프로 일반화,
  버퍼 확대(JSON 512, POST 512).
- `Web_page.html` — 폼 입력칸 2→4개씩, `loadConfig`/`saveConfig` 루프화. `Web_page.h` 재생성.
- 빌드 통과 확인.

### 후속
- ✅ **ioLibrary 패치 재생성 완료 (2026-05-27)** — `ioLibrary_snmp_patch.patch`를
  현재 working tree(`git diff` vs base b981401)로 재생성, `git apply --check` 통과.
  #8(agent 포트)까지 포함. HOW_TO 문서 stat/내용 갱신.
- 기존 장치 업그레이드 시 ext_version bump로 **SNMP IP/세션 타임아웃 설정이 초기화**됨
  (레거시 네트워크/시리얼 설정은 보존). 정상 동작이나 운영 공지 필요.

---

## 3. 디바이스 값 플래시 저장 (재부팅 후 유지)

**상태**: 미착수

### 요구사항
현재 `g_devices[]`는 RAM에만 존재하여 재부팅 시 초기화됨.
UART S/T 명령으로 입력된 디바이스 값이 재부팅 후에도 유지되어야 함.

### 현재 동작
- `device_setValue()` → RAM(`g_devices[]`) 만 갱신
- 재부팅 시 `device_init()` → 전부 0 초기화
- `App.c`의 demo 값(`235+d`, `600+d` 등)으로 덮어써짐

### 변경 방향
1. `storageHandler`에 `STORAGE_DEVICES` 타입 추가, 전용 플래시 주소 확보
2. `device_setValue()` 호출 시 또는 별도 저장 명령(예: `W` 커맨드)으로 `g_devices[]` 전체를 플래시에 write
3. 부팅 시 `device_init()` 후 플래시에서 복원 — 유효성 검사(magic/version) 포함
4. `App.c` demo 초기화 코드 제거 (`device_assign` / `device_setValue` 루프)

### 작업 범위
- `sensor.h` / `sensor.c` — 저장/복원 함수 추가 (`device_save()`, `device_load()`)
- `storageHandler.h` / `.c` — `STORAGE_DEVICES` 케이스 추가
- `flashHandler.h` — 디바이스 전용 플래시 주소 정의
- `App.c` — demo 초기화 제거, 부팅 시 `device_load()` 호출

### 주의
- `g_devices[]` 크기: `DEVICE_COUNT(64) × sizeof(Device)` — 플래시 섹터 크기 초과 여부 확인 필요
- 값이 자주 바뀌면 플래시 수명(write cycle) 고려 → 쓰기 시점 제한 검토

---

## 4. 비밀번호 규칙 정의 및 적용

**상태**: 해결 완료 (2026-05-27)

### 규칙 (확정)
- **길이 8자 이상 16자 이하**
- 영문 **대문자 1자 이상**
- **특수문자(인쇄 가능한 비영숫자) 1자 이상**

### 적용 내용
- `httpHandler.c` — `validate_password()` 헬퍼 신설 (NULL=OK, 실패 시 사유 문자열)
  - `/api/accounts/add` (`handle_post_api_account_add`) — 생성 시 검증, 실패 시 사유 JSON 반환
  - `/account` 폼 (`handle_post_account_add`) — 검증, 실패 시 `?err=5` 리다이렉트
- `Web_page.html` — `validatePassword()` JS 1차 검증 (`addAccount()`에서 호출),
  미충족 시 경고. `Web_page.h` 재생성 완료.

### 후속
- #9 / #10 작업 시 이 `validate_password()` 를 공통 호출 (이미 토대 마련됨)

---

## 5. USB CDC 디버그 포트 활성화

**상태**: 해결 완료 (2026-05-27)

### 근본 원인 (확인됨)
진짜 원인은 **picotool VENDOR 리셋 인터페이스** 하나였음.
`pico_stdio_usb`는 CDC 외에 picotool 리셋용 벤더 인터페이스를 복합 장치로
추가한다(`*_ENABLE_RESET_VIA_VENDOR_INTERFACE` 기본 1). Windows에서 이 벤더
인터페이스의 WinUSB/MS-OS-2.0 바인드가 실패하면 "알 수 없는 장치 / Code 10"으로
표시됨. CDC COM 포트 자체는 멀쩡히 잡혀 있어도 이 벤더 인터페이스가 Code 10을 냄.

(삽질 기록: OPT_OS_FREERTOS + 전용 usb_device_task로 우회하려 했으나, 이 방식은
`stdio_usb_out_chars()`가 내부에서 호출하는 `tud_task()`와 동기화 없이 충돌해
SMP 2코어에서 출력이 "조금 뜨고 멈추는" 새 버그를 유발함. SDK 기본 IRQ 워커는
`stdio_usb_mutex`로 out_chars와 직렬화되므로 안전 → 기본 방식으로 복귀.)

### 해결 (최소 변경)
- `main/App/CMakeLists.txt` — App·App_linker 타깃에만
  `PICO_STDIO_USB_ENABLE_RESET_VIA_VENDOR_INTERFACE=0` (순수 CDC 단일 장치化)
- `main/App/App.c` — `stdio_init_all()`을 `start_task()`에서 호출 +
  `vTaskCoreAffinitySet(NULL, 1<<0)`로 Core 0 고정
  (stdio_usb_init()이 default alarm pool 코어=Core0에서 실행되도록; SMP에서
   Core1 스케줄 시 assert 회피)
- USB 서비스는 SDK 기본 방식 그대로(OPT_OS_PICO + IRQ 워커, mutex 직렬화)
- 부트로더는 손대지 않음

### 디버그 출력 경로
- `pico_enable_stdio_uart(0)` — uart1(GPIO4/5)은 RS-232 데이터 포트라 사용 불가
- `pico_enable_stdio_usb(1)` — printf 디버그는 USB CDC COM 포트로 출력

---

## 6. 웹페이지에 Config tool 기능 일부 통합

**상태**: 진행 중 — 6-A 완료, 6-B/C/D 미착수 (2026-05-27)

### 요구사항 (범위 확정 — 레거시 "네트워크 환경설정" 페이지 스크린샷 기준)
레거시 Config tool 웹페이지(FL system)의 항목을 HTTPS 웹페이지로 가져오기.

### 항목 매핑 (스크린샷 기준)
| 레거시 항목 | 우리 현황 | 비고 |
|---|---|---|
| Network: MAC (표시) | 표시만 | config 폼에 read-only 추가 |
| Network: IP / Gateway / Subnet (편집) | **신규** | DevConfig network_common (편집 UI 없음) |
| Network: DHCP on/off | **신규** | network_option.dhcp_use |
| SNMP: 허용 IP | ✅ allowed_ip ×4 (#2) | |
| SNMP: Community | **신규** | 현재 "public" 고정(snmp.c) |
| SNMP: Permission (NO ACCESS/RO/RW) | **신규** | snmp.c 접근제어 |
| Trap: IP | ✅ trap_ip ×4 (#2) | |
| Trap: Community / Accept(Y/N) | **신규** | |
| WEB: PORT | ✅ #8 완료 | |
| WEB: Access IP 1/2 | **신규** | 웹 접속 소스 IP 허용목록 |
| WEB: ID/PW | ✅ 계정관리 (#4/#10) | 레거시는 max12, 우리 규칙 8~16(#4 유지) |

### 서브 항목 / 진행 상태
- **6-A. 네트워크 설정 편집 (IP/GW/SN/DHCP)** — ✅ 완료 (2026-05-27)
  - `httpHandler.c` — `/api/config` GET에 `mac`(표시)/`ip`/`gateway`/`subnet`/`dhcp` 추가,
    POST 파싱(IPv4 검증 + dhcp 0/1) → `network_common`/`network_option` 갱신. 버퍼 768.
  - `Web_page.html` — "네트워크 설정" 섹션(MAC 표시, DHCP 선택, IP/GW/SN 입력) +
    load/save. `Web_page.h` 재생성. 빌드 통과.
  - **재부팅 후 적용** (IP 변경은 현 연결이 끊기므로 즉시적용 안 함).
- 6-B. SNMP Community / Permission — 미착수 (snmp.c=ioLibrary, 패치 영향)
- 6-C. Trap Community / Accept — 미착수 (ioLibrary 영향)
- **6-D. WEB Access Control 소스 IP 허용목록** — ✅ 완료 (2026-05-27)
  - `ConfigData.h` — `web_access_ip[2][4]` 확장 추가, `DEVCONFIG_EXT_VERSION` 4→5,
    `reserved_ext` 97→89, `WEB_ACCESS_IP_CNT 2`. 기본 all-zero(전체 허용).
  - `httpHandler.c` — `web_access_allowed(sock)` 헬퍼(`getSn_DIPR`로 peer IP 비교,
    all-zero면 허용), SOCK_ESTABLISHED에서 **TLS 핸드셰이크 전** 검사 → 거부 시 즉시 close.
    config JSON `web_ip0/1` + POST 파싱.
  - `Web_page.html` — "WEB 접근 허용 IP" 섹션(IP 2개), `CFG_IPS`에 web_ip0/1 추가. 재생성.
  - 즉시 적용(다음 접속부터). ext_version 5 bump.

### 작업 범위 (공통)
- `httpHandler.c` — `/api/config` GET/POST 확장
- `Web_page.html` / `Web_page.h` — 섹션 추가, 재생성
- 네트워크/포트 변경은 재부팅 후 적용

---

## 7. 웹 설정화면 — HTTPS 세션 갱신 주기 설정

**상태**: 해결 완료 (2026-05-27)

### 규칙 (확정)
- 단위: **분**, 범위 **1 ~ 1440**, 기본 **30분**
- 저장값 0/범위 밖 → 런타임에 기본 30분으로 폴백

### 적용 내용
- `ConfigData.h` — 확장 섹션에 `uint16_t https_session_timeout_min` 추가
  (총 크기 유지: `reserved_ext` 128→126). 범위/기본 매크로 정의.
- `ConfigData.c` — `set_DevConfig_ext_to_factory_value()`에서 기본 30 설정.
- `httpsAuth.c` — `ConfigData.h` include, `session_timeout_ms()` 헬퍼 신설,
  만료 체크 3곳을 상수 → 런타임 값으로 교체. (`HTTPS_SESSION_TIMEOUT_MS` 매크로는
  미사용으로 남김)
- `httpHandler.c` — `/api/config` GET JSON에 `session_timeout` 추가,
  POST 파싱(1~1440 검증) 추가. JSON 버퍼 192→256.
- `Web_page.html` — "HTTPS 세션 유효시간" 섹션 + 입력칸,
  `loadConfig`/`saveConfig`에 반영(JS 1~1440 검증). `Web_page.h` 재생성.
- 빌드 통과 확인.

### 비고
- 기존 플래시 호환: 총 sizeof 유지 + 저장값 0이면 기본값 폴백 → 마이그레이션 churn 없음.
- 적용 시점: 저장 즉시 반영(다음 세션 검사부터). 기존 로그인 세션엔 새 값이 다음 만료 계산에 적용됨.

---

## 8. 웹 설정화면 — HTTPS / SNMP 포트 번호 변경

**상태**: 해결 완료 (2026-05-27)

### 적용 내용
- `ConfigData.h` — 확장 섹션에 `uint16_t https_port`, `uint16_t snmp_agent_port` 추가,
  `DEVCONFIG_EXT_VERSION` 3→4, `reserved_ext` 101→97. 기본값 매크로
  `HTTPS_PORT_DEFAULT 443` / `SNMP_AGENT_PORT_DEFAULT 161`.
- `ConfigData.c` — factory default 443 / 161.
- `httpHandler.c` — HTTPS 소켓 open(`socket(...)`)을 `conf->https_port`(0이면 443) 참조로.
  config JSON `https_port`/`snmp_port` 추가, POST 파싱(1~65535).
- `snmp.c` / `snmp.h` (ioLibrary, 패치본) — `s_agent_port` + `snmp_set_agent_port()` 추가,
  agent 소켓 open을 `s_agent_port` 참조로.
- `snmpHandler.c` — `snmp_agent_init()`에서 `snmp_set_agent_port(conf->snmp_agent_port?:161)` 호출,
  로그도 실제 포트 출력.
- `Web_page.html` — "서비스 포트" 섹션(HTTPS/SNMP 입력) + load/save(1~65535 검증). `Web_page.h` 재생성.
- 빌드 통과 확인.

### 적용 시점 / 주의
- **재부팅 후 적용** (UI 명시). HTTPS는 소켓 open 시 설정값을 읽으므로 재부팅 후 새 포트로 listen.
  SNMP는 `snmp_agent_init()`에서 주입 → 재부팅(또는 agent 재init) 후 적용.
- **트랩 목적지 포트(`PORT_SNMP_TRAP`)는 미변경** — 이번 범위는 agent 수신 포트(161)만. 필요 시 후속.
- ioLibrary `snmp.c`/`snmp.h` 또 수정함 → **`ioLibrary_snmp_patch.patch` 재생성 필요**(#2와 동일 후속).
- ext_version 4 bump → 기존 장치 업그레이드 시 ext 영역 1회 초기화(포트도 기본값으로).

---

## 9. 계정 생성용 비밀번호 변경 가능하게 (디폴트는 유지)

**상태**: 보류 — 복구/보안 설계 검증 후 진행 (2026-05-27)

### 보류 사유 (검증 필요)
생성용 PW를 변경 가능하게 하면 **까먹었을 때 복구 경로**가 필요한데, 현재
구조에서 검증이 끝나야 안전하게 진행 가능:

- `device_set_factory_default()` 는 **`DevConfig`만 초기화**하고
  **`STORAGE_AUTH`(HTTPS 계정/생성용 PW)는 erase 하지 않음** → 팩토리 리셋해도
  생성용 PW 복구 안 됨 → 변경 후 분실 시 **영구 락아웃**.
- segcp(UDP) 팩토리 리셋도 `STORAGE_AUTH` 안 건드림 (HTTPS 인증과 격리됨).

### 진행 시 권장 설계 (검증 대상)
- **HW 팩토리 리셋 핀 경로(timerHandler.c)에만** `STORAGE_AUTH` erase 추가
  → 물리 접근 시 생성용 PW가 디폴트(`wiznet_w55rp20`)로 복구.
- **UDP 팩토리 리셋 경로(segcp.c)는 현행 유지** → 원격에서 HTTPS 인증 못 건드리게.
- 위 동작이 의도대로인지(다른 기능 영향, 부팅 시 store 재생성 등) 실기 검증 후 #9 본작업.

### 진행 시 작업 내용 (un-block 후)
- 생성용 PW 해시를 `STORAGE_AUTH`(`https_auth_store_t`)에 저장하는 필드 추가,
  미설정 시 `CREATION_PASS_HASH`(디폴트, `httpsAuth.c:17`)로 폴백 → 디폴트 유지
- 변경 UI(`add-cpass` 인접) + `/api/...` → #4 `validate_password()` 적용,
  SHA-256 해시만 저장(평문 금지)
- 위 "권장 설계"의 HW 핀 복구 경로와 함께 적용

---

## 10. 계정 비밀번호 변경 기능

**상태**: 해결 완료 (2026-05-27)

### 적용 내용
- `httpsAuth.c` / `.h` — `https_auth_change_password(user, old, new)` 추가
  (기존 PW 해시 확인 → 불일치 시 -5, 사용자 없음 -1 → 신규 PW SHA-256 해시로
   교체 후 `store_save()`). 평문 저장 안 함.
- `httpHandler.c` — `/api/accounts/passwd` 엔드포인트
  (`handle_post_api_account_passwd`): 세션 검증 → #4 `validate_password()` 적용 →
  변경. 실패 사유 JSON 반환.
- `Web_page.html` — 계정 관리에 "비밀번호 변경" 폼 + `changePassword()` JS
  (`validatePassword()` 재사용). `Web_page.h` 재생성, 빌드 통과 확인.

### 참고
- 변경 후 기존 세션 무효화는 미적용(현 정책 유지) — 필요 시 후속.

---

## 11. 웹페이지에서 시리얼 포트 설정 (통신속도/포맷)

**상태**: 해결 완료 (2026-05-27)

### 적용 내용
- `App.c` — `set_minimal_runtime_config()`의 `flow_control = flow_none` 강제 제거
  (이게 유일한 걸림돌이었음. baud/data/parity/protocol은 원래 저장값 사용).
- `httpHandler.c` — `/api/config` JSON에 `serial_baud/data/parity/flow/mode` 추가(읽기),
  POST 파싱(enum 범위 검증, 테이블 루프) 추가(쓰기).
- `Web_page.html` — "시리얼 포트 설정" 섹션 + 드롭다운 5종(Baud/Data/Parity/Handshake/Mode),
  `loadConfig`/`saveConfig`에 반영. `Web_page.h` 재생성.
- 빌드 통과 확인.

### 포트별 설정 분리 (2026-05-27 추가)
- **RS-232(uart1)와 RS-485(uart0) 설정을 독립**시킴 (요청 반영):
  - RS-232 → 레거시 `serial_option` (`DATA0_UART_Configuration()`)
  - RS-485 → 신규 `serial_option_485` (확장 섹션, `init_rs485_uart()`가 읽음)
- `ConfigData.h` — `struct __serial_option serial_option_485` 확장 섹션 추가,
  `DEVCONFIG_EXT_VERSION` 2→3, `reserved_ext` 110→101.
- `ConfigData.c` — 485 factory default(115200 8N1, no flow) 설정.
- `sensorUart.c` — `init_rs485_uart()`가 `serial_option_485` 참조.
- `httpHandler.c` — config JSON/POST에 `serial485_*` 5종 추가(232 `serial_*`와 병렬),
  버퍼 512→640.
- `Web_page.html` — "RS-232 / RS-485" 2개 섹션, `CFG_SERIAL` 10개로. `Web_page.h` 재생성.
- 두 포트 모두 동시 동작 + 서로 다른 baud/포맷 가능. 수신 버퍼만 공유(파서 통합).

### 적용 시점 / 주의
- **재부팅 후 적용** (저장 시 UI 명시). 즉시 적용(런타임 UART 재초기화)은 sensorUart ISR
  레이스 위험으로 보류 — 필요 시 후속.
- "Mode"(protocol) 값은 저장되나 현재 sensorUart는 미사용(Modbus 실동작 별도) — 표시/저장만.
- 485 Handshake는 반이중(DE 자동제어)이라 하드웨어 효과 없음 — 저장만(UI에 안내 표기).
- ext_version 3 bump → 기존 장치 업그레이드 시 ext 영역(SNMP IP/세션/485 설정) 1회 초기화.

### 요구사항
현재 시리얼 통신 파라미터가 사실상 고정(부팅 시 강제 설정)되어 있음.
이를 HTTPS 웹페이지 설정화면에서 변경 가능하게.

설정 항목 (Config tool 화면 기준):
| 항목 | DevConfig 필드 | 값 |
|---|---|---|
| Baud | `serial_option.baud_rate` | enum (300 ~ 921600 ~ 8M) |
| Data size | `serial_option.data_bits` | 7 / 8 / 9 |
| Parity | `serial_option.parity` | None / Odd / Even |
| Handshake | `serial_option.flow_control` | OFF(None) / RTS·CTS / XON·XOFF |
| Mode | `serial_option.protocol` | Free(None) / Modbus RTU / Modbus ASCII |

(struct에는 `stop_bits`도 있음 — 필요 시 항목 추가 가능)

### 현재 동작
- `struct __serial_option` (`ConfigData.h:93`) — 필드 **이미 존재 + 플래시 저장됨**.
  → 새 플래시 필드 추가 불필요, **웹 UI + 적용 로직만** 필요.
- `DATA0_UART_Configuration()` (RS-232/uart1) 와 `init_rs485_uart()`
  (RS-485/uart0) 가 **둘 다 같은 `serial_option`을 읽음** → 한 설정이 양 포트에 적용.
- enum 정의: `uartHandler.h` (baud / word_len / parity / stop_bit / flow_ctrl / protocol).
- **걸림돌**: `App.c`의 `set_minimal_runtime_config()`가 부팅마다
  ```
  serial_option.uart_interface = UART_IF_RS232_TTL;
  serial_option.flow_control   = flow_none;
  ```
  로 **강제 덮어씀** → 웹에서 바꿔도 재부팅 시 초기화됨. 이 강제 설정을 제거/조정해야 함.

### 변경 방향
1. `set_minimal_runtime_config()`의 시리얼 강제 설정 제거(또는 최초 1회만)
   → 저장된 `serial_option` 값이 살아남도록
2. 웹 설정화면에 5개 입력(드롭다운) 추가, 현재 값 표시
3. `/api/...` 저장 처리 → `serial_option` 갱신 → 플래시 저장
4. 적용: 저장 후 `DATA0_UART_Configuration()` + `init_rs485_uart()` 재호출
   또는 재부팅 (적용 시점 정의)
5. baud/data/parity/flow/protocol 값 유효성 검사 (enum 범위)

### 작업 범위
- `App.c` — `set_minimal_runtime_config()` 시리얼 강제 설정 제거
- `httpHandler.c` — 시리얼 설정 JSON 항목 + 저장 엔드포인트
- `Web_page.html` / `Web_page.h` — 드롭다운 5종 추가, 재생성
- (선택) 저장 즉시 적용 시 UART 재초기화 경로

### 관련
- 클러스터 B(#2/#7/#8)와 동일 패턴(DevConfig + config JSON + Web_page 폼 + 재생성)
  → 함께 작업하면 `Web_page.h` 재생성·config JSON 편집 반복 최소화.

---

## 12. segcp(UDP) 설정 채널 인증 강화 검토

**상태**: 미착수 (검증/정책 결정 필요)

### 발견 내용
Config tool 의 UDP(segcp) 채널 권한 모델:
- `MA FF:FF:FF:FF:FF:FF`(브로드캐스트) → SET|READ, 응답으로 장치 MAC 노출
- `MA <장치 실제 MAC>` → SET|**WRITE** (네트워크·시리얼 설정 변경, **팩토리 리셋** 가능)
- 이후 `PW<pw>` 가 `config_common.pw_search` 와 비교되는데 **기본값이 공란** →
  MAC만 알면(브로드캐스트로 알아냄) **인증 없이 원격 설정 변경/팩토리 리셋 가능**.

### 영향 범위
- HTTPS 계정/인증(`STORAGE_AUTH`)과는 **격리** → 관리자 PW 자체는 원격 변경 불가.
- 그러나 네트워크/시리얼 설정 변경, DevConfig 팩토리 리셋은 원격 무인증 가능 → 약점.

### 검토 방향 (택1 또는 조합)
1. `pw_search` 기본값을 비공란으로(예: 장치 고유값) → UDP 설정에 비밀번호 요구
2. WRITE 권한 명령(특히 FR 팩토리리셋, 네트워크 변경)에 한해 추가 인증
3. 운영상 segcp 채널을 disable 가능하게 (옵션)

### 주의
- Config tool 호환성 영향 — 기존 툴 동작과의 호환 확인 필요.
- 정책 결정(보안 vs 편의) 먼저.

---

## 13. 설정 UX — 세션 자동 로그아웃 + 웹 재부팅, 그리고 비정렬 쓰기 버그 수정

**상태**: 해결 완료 (2026-05-27)

### (버그) packed DevConfig 16비트 멤버 비정렬 쓰기 → HardFault
- 증상: 환경설정 저장 시 장치가 리셋되거나(포트 변경) 행 후 워치독 리붓(IP 변경).
  입력에 따라 증상이 달라지는 전형적 메모리 오류.
- 원인: `DevConfig`는 `__attribute__((packed))`. `https_port`/`snmp_agent_port`(uint16_t)가
  `serial_option_485`(9B, 홀수) 뒤라 **홀수 오프셋**. config POST 파서가
  `uint16_t *field` 포인터로 그 주소를 받아 `*field = v` 로 쓰면서 컴파일러가
  **정렬 가정 STRH** 생성 → **Cortex-M0+ 비정렬 16비트 쓰기 = HardFault**.
  (uint8_t 멤버·packed 구조체 직접 대입·GET 응답 읽기는 컴파일러가 바이트단위 안전
   코드를 내므로 무사했음 — 오직 포인터 우회만 폴트.)
- 수정(`httpHandler.c`): 포트 파싱을 포인터 우회 제거하고 `conf->https_port = v;`
  처럼 **packed 구조체에 직접 대입**. (교훈: packed 멤버 주소를 정렬 타입 포인터로
  넘기지 말 것.)

### (부수 수정) 진단 중 함께 처리
- `HTTPS_RX_BUF_SIZE` 1024→**2048**: config POST 요청(헤더+본문 ~1.2KB)이 1024 초과로
  잘리던 문제. (GET은 본문 없어 무사했음 — POST만 깨지던 비대칭의 1차 원인)
- `flashHandler.c`: `write_flash`의 `flash_buf`를 매 호출 `pvPortMalloc(4096)` →
  **정적 버퍼**로. 힙 압박 시 NULL 반환 + 무점검 `memset(NULL)` → 하드폴트 잠재 버그 제거
  (모든 플래시 쓰기에 잠재). 

### (A) 세션 만료 시 자동 로그아웃
- `Web_page.html` — `authRedirect(r)` 헬퍼: 서버가 `/login`으로 redirect하면 감지해
  `location.href='/login'`. `poll`/`loadConfig`/`loadAccounts`/`saveConfig`에 적용.
- 세션 만료(예: 1분) 후 다음 센서 폴링(≤3초) 때 자동으로 로그인 화면 전환.

### (B) 설정 저장 후 재부팅 안내 + 웹 재부팅
- `httpHandler.c` — `POST /api/reboot` 엔드포인트(세션 검증 → 응답 flush → 300ms →
  `device_reboot()`). dispatch 등록.
- `Web_page.html` — 저장 성공 시 "재부팅 후 적용" 모달(지금 재부팅 / 나중에).
  `doReboot()` → `/api/reboot` → 6초 후 로그인으로 이동(IP 바뀌면 새 주소로 재접속 안내).
- `Web_page.h` 재생성, 빌드 통과.

### 비고
- ext_version은 추가 안 함(이번 변경은 필드 추가 없음, 코드/UI만).
 