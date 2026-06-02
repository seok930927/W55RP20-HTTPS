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
규칙 자체는 추후 확정 필요 — 아래는 예시.

### 예시 규칙 (확정 전)
- 최소 8자 이상
- 영문 대문자 1자 이상
- 숫자 1자 이상
- 특수문자(`!@#$%^&*` 등) 1자 이상

### 현재 동작
- `Web_page.html` — 계정 추가 폼에서 PW 입력 시 형식 검사 없음
- `httpHandler.c` — `/api/account` 처리 시 길이/형식 검사 없음

### 변경 방향
1. **프론트엔드(Web_page.html)** — 계정 추가 버튼 클릭 시 JS로 1차 검증, 미충족 시 경고 메시지 표시
2. **백엔드(httpHandler.c)** — `/api/account` POST 수신 시 동일 규칙으로 2차 검증, 실패 시 400 응답
3. 규칙은 한 곳(`validate_password()` 함수)에서 관리

### 작업 범위
- `Web_page.html` / `Web_page.h` — JS 검증 함수 추가, 재생성
- `httpHandler.c` — 서버 측 검증 함수 추가
- 규칙 확정 후 작업 시작

---

## 5. USB CDC 디버그 포트 활성화

**상태**: 수정 완료 (2026-05-27)

### 근본 원인 (확인됨)
`configSUPPORT_PICO_TIME_INTEROP 1` 설정으로 인해 `add_alarm_in_ms()`가
FreeRTOS 소프트웨어 타이머로 변환된다. FreeRTOS 타이머는 `vTaskStartScheduler()`
이후에만 동작하는데, `stdio_init_all()`을 `main()`(스케줄러 시작 전)에서
호출했기 때문에 USB polling alarm이 등록되지 않아 `tud_task()`가 절대
호출되지 않았음 → Windows Code 10.

베어메탈 FW는 스케줄러 없이 하드웨어 타이머를 직접 사용하므로 정상 동작.

### 해결
`App.c` — `stdio_init_all()`을 `main()`에서 `start_task()` 맨 앞으로 이동.
스케줄러 시작 후 USB 초기화 → alarm이 FreeRTOS 타이머 데몬에 정상 등록됨.

---

<!-- 새 작업은 아래에 ## 6, ## 7 ... 형식으로 추가 -->
