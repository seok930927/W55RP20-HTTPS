# TODO

W55RP20-HTTPS 프로젝트 — 나중에 할 작업 모음. 까먹지 않기 위한 메모.

---

## 1. 환경설정 페이지 매번 로그인 요구

**상태**: 미착수

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

**상태**: 미착수

### 요구사항
SNMP 접근 허용 IP(`allowed_ip`)와 트랩 전송 대상 IP(`trap_ip`)를
현재 각각 2개씩만 등록 가능 → **각각 4개**까지 등록 가능하도록 확장.

### 현재 동작
`struct __snmp_option` (`ConfigData.h`):
```
uint8_t allowed_ip[2][4];
uint8_t trap_ip[2][4];
```
2개 슬롯 고정.

### 작업 범위
- `ConfigData.h` — `[2][4]` → `[4][4]` 두 곳
- `ConfigData.c` — factory init memset 크기 자동 반영되는지 확인 (`sizeof` 사용 중)
- `snmp.c` — `s_allowed_ip[2][4]`, `snmp_set_allowed_ips()` 의 `[2][4]` → `[4][4]`,
  매칭 루프(`memcmp` 2회) 4회로
- `snmpHandler.c` — `snmp_flush_traps()` 의 트랩 대상 루프 `for t<2` → `for t<4`,
  `snmp_set_allowed_ips()` 호출부 캐스팅 `[4][4]` 로
- `httpHandler.c` — config JSON `allowed_ip0/1`, `trap_ip0/1` → `0~3` 로 확장
- `Web_page.html` / `Web_page.h` — 환경설정 폼 입력칸 2 → 4, 재생성

### 주의
- 확장 슬롯은 EXTENSION 섹션이라 `reserved_ext[]` 패딩에서 흡수됨 —
  레거시 레이아웃엔 영향 없음. `ext_version` 올릴지 검토.

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

**상태**: 미착수

### 요구사항
계정 생성 / 비밀번호 변경 시 강도 규칙을 강제해야 함.

### 규칙 (확정)
- **길이 8자 이상 16자 이하**
- 영문 **대문자 1자 이상**
- **특수문자(`!@#$%^&*` 등) 1자 이상**

### 현재 동작
- `Web_page.html` — 계정 추가 폼에서 PW 입력 시 형식 검사 없음
- `httpHandler.c` — `/api/account` 처리 시 길이/형식 검사 없음

### 변경 방향
1. **프론트엔드(Web_page.html)** — 계정 추가 버튼 클릭 시 JS로 1차 검증, 미충족 시 경고 메시지 표시
2. **백엔드(httpHandler.c)** — `/api/account` POST 수신 시 동일 규칙으로 2차 검증, 실패 시 400 응답
3. 규칙은 한 곳(`validate_password()` 함수)에서 관리
4. #9(계정 생성 PW), #10(계정 PW 변경) 작업에서 이 검증 함수를 공통으로 사용

### 작업 범위
- `Web_page.html` / `Web_page.h` — JS 검증 함수 추가, 재생성
- `httpHandler.c` — 서버 측 검증 함수 추가

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

**상태**: 미착수

### 요구사항
기존 별도 Config tool(PC 프로그램)의 기능 일부를 HTTPS 웹페이지 안으로 가져오기.
어떤 기능을 포함할지 범위 확정 필요.

### 검토 필요
- Config tool의 어떤 항목을 웹에서 제공할지 목록화 (네트워크 설정, 시리얼 설정,
  펌웨어 정보, 재부팅/팩토리리셋 등)
- 이미 웹에 있는 항목과 중복 제외
- segcp(UDP/TCP) 기반 설정 항목 중 웹으로 옮길 것 선별

### 작업 범위
- `Web_page.html` / `Web_page.h` — UI 추가, 재생성
- `httpHandler.c` — 대응 `/api/...` 엔드포인트 추가
- 범위 확정 후 착수

---

## 7. 웹 설정화면 — HTTPS 세션 갱신 주기 설정

**상태**: 미착수

### 요구사항
HTTPS 로그인 세션의 유효시간(갱신 주기)을 웹 설정화면에서 변경 가능하게.

### 현재 동작 (하드코딩)
`port/app/platform_handler/inc/httpsAuth.h`:
```
#define HTTPS_SESSION_TIMEOUT_MS  (30 * 60 * 1000U)   // 30분 고정
#define HTTPS_MAX_SESSIONS        5
```
컴파일 타임 상수라 런타임 변경 불가.

### 변경 방향
1. `DevConfig`에 세션 타임아웃(분 단위) 필드 추가 → 플래시 저장
2. `httpsAuth.c`의 만료 체크(`HTTPS_SESSION_TIMEOUT_MS` 3곳: 라인 ~153/180/219)를
   상수 대신 설정값 참조로 변경
3. 웹 설정화면 입력칸 + `/api/...` 저장 처리

### 작업 범위
- `ConfigData.h` / `ConfigData.c` — 필드 추가, factory default(예: 30분)
- `httpsAuth.c` / `httpsAuth.h` — 상수 → 런타임 값
- `httpHandler.c` — 설정 JSON 항목 추가
- `Web_page.html` / `Web_page.h` — 입력칸 추가, 재생성

---

## 8. 웹 설정화면 — HTTPS / SNMP 포트 번호 변경

**상태**: 미착수

### 요구사항
HTTPS 서비스 포트와 SNMP agent 포트를 웹에서 변경 가능하게.

### 현재 동작 (하드코딩)
- HTTPS: `httpHandler.c:19` — `#define HTTPS_SERVER_PORT 443`
  (소켓 open 시 `httpHandler.c:916` 에서 사용)
- SNMP agent: `libraries/ioLibrary_Driver/Internet/SNMP/snmp.h:11` —
  `#define PORT_SNMP_AGENT 161` (트랩은 `PORT_SNMP_TRAP`)

둘 다 컴파일 타임 상수. SNMP 쪽은 **라이브러리(ioLibrary) 내부**라 수정 시
패치 관리(ioLibrary_snmp_patch) 고려 필요.

### 변경 방향
1. `DevConfig`에 https_port / snmp_port 필드 추가 → 플래시 저장
2. HTTPS: 소켓 open 호출부를 설정값 참조로
3. SNMP: 라이브러리 상수 의존 → 가능하면 agent init에 포트 인자로 주입하도록
   포팅 레이어(snmpHandler.c)에서 처리, 어려우면 패치로
4. 포트 0 또는 범위 밖 입력 방어 (1~65535)

### 작업 범위
- `ConfigData.h` / `ConfigData.c` — 필드 추가, default 443 / 161
- `httpHandler.c` — `HTTPS_SERVER_PORT` → 설정값
- `snmpHandler.c` (+ 필요 시 ioLibrary 패치) — agent/trap 포트 설정값화
- `Web_page.html` / `Web_page.h` — 입력칸 2개 추가, 재생성

### 주의
- 포트 변경 후에는 재부팅 또는 소켓 재open 필요 — 적용 시점 정의할 것

---

## 9. 계정 생성용 비밀번호 변경 가능하게 (디폴트는 유지)

**상태**: 미착수

### 요구사항
계정 생성 시 요구되는 "생성용 비밀번호"가 현재 `wiznet_w55rp20`으로 고정이고
수정 불가. 이를 사용자가 변경할 수 있게. **단, 공장 디폴트는 그대로 유지**
(미설정/팩토리리셋 시 `wiznet_w55rp20`).

### 현재 동작 (하드코딩)
`port/app/platform_handler/src/httpsAuth.c:17`:
```
// SHA-256("wiznet_w55rp20")
static const uint8_t CREATION_PASS_HASH[HTTPS_HASH_LEN] = { 0x12,0x1d,... };
```
- 상수 해시와 비교만 함 → 런타임 변경 불가
- 웹 UI: `Web_page.html:297` `add-cpass` 입력(placeholder "wiznet_w55rp20")

### 변경 방향
1. 생성용 PW 해시를 플래시(`STORAGE_AUTH`, `https_auth_store_t`)에 저장하는 필드로
2. 값이 비어있으면(미설정) `CREATION_PASS_HASH`(디폴트)로 폴백 → 디폴트 유지
3. 변경 UI + `/api/...` 처리 시 #4의 PW 규칙 검증 적용
4. 변경 시 SHA-256 해시만 저장(평문 저장 금지)

### 작업 범위
- `httpsAuth.c` / `httpsAuth.h` — 저장 필드 + 폴백 로직, 변경 함수
- `httpHandler.c` — 변경 엔드포인트, #4 검증 호출
- `Web_page.html` / `Web_page.h` — 변경 UI, 재생성

---

## 10. 계정 비밀번호 변경 기능

**상태**: 미착수

### 요구사항
생성된 계정의 로그인 비밀번호를 변경할 수 있는 기능. (#9는 "생성용 PW",
이건 "계정 로그인 PW" — 별개)

### 현재 동작
- `httpsAuth.c` — 계정/세션 저장은 있으나 PW 변경 경로 없음
- `httpHandler.c` — `/api/account`(생성) 있음, 변경 엔드포인트 없음

### 변경 방향
1. 변경 엔드포인트 추가: 기존 PW 확인 → 신규 PW(#4 규칙 검증) → 해시 갱신 저장
2. 신규 PW 해시만 저장(평문 금지), 변경 후 기존 세션 무효화 검토
3. 웹 UI: 계정 화면에 "비밀번호 변경" 폼

### 작업 범위
- `httpsAuth.c` / `httpsAuth.h` — PW 변경/검증 함수
- `httpHandler.c` — 변경 엔드포인트, #4 검증 호출
- `Web_page.html` / `Web_page.h` — 변경 UI, 재생성

---

<!-- 새 작업은 아래에 ## 11, ## 12 ... 형식으로 추가 -->
