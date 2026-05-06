# Sensor Gateway 작업 계획

## 프로젝트 방향

S2E 코드베이스를 기반으로 하되 S2E 기능은 제거하고,
**UART로 들어오는 하위 센서 데이터를 SNMP + HTTPS 두 채널로 노출하는 게이트웨이** 를 구현한다.

```
하위 센서들
    ↓ UART
RP2040 (W55RP20)
    ├── SNMP (UDP 161)  → 네트워크 관리 시스템 (snmpget / snmpwalk)
    └── HTTPS (TCP 443) → 웹 브라우저
```

## 진행 상태

- [ ] 센서 프로토콜 및 채널 정의 확정
- [ ] Phase 1 - 공유 데이터 구조 설계
- [ ] Phase 2 - UART 파싱 Task 신설
- [ ] Phase 3 - SNMP OID 연결
- [ ] Phase 4 - HTTPS 웹페이지 동적화
- [ ] 실기 검증

## 현재 코드베이스 상태

### 존재하는 것 (활용 가능)

| 파일 | 역할 |
|---|---|
| `port/app/platform_handler/src/uartHandler.c` | UART ISR, ring buffer 공급 |
| `port/app/platform_handler/src/bufferHandler.c` | 4096B ring buffer 관리 |
| `port/app/platform_handler/src/snmpHandler.c` | SNMP Agent task (검증 완료) |
| `port/app/platform_handler/src/httpHandler.c` | HTTPS 서버 task (검증 완료) |
| `libraries/ioLibrary_Driver/Internet/SNMP/snmp_custom.c` | OID 테이블 및 getter 콜백 |

### UART 데이터 흐름 (현재)

```
UART ISR (on_uart_rx())
    → put_byte_to_data_buffer()  [ring buffer: data0_buffer_rx, 4096B]
    → seg_u2e_sem 시그널
    → seg_u2e_task()             [현재: S2E 포워딩 → 향후: 센서 파싱으로 교체]
```

### 결정 필요 항목

아래 세 가지가 확정되어야 Phase 2~4를 시작할 수 있다.

1. **센서 프로토콜**: Modbus RTU / ASCII CSV / 독자 바이너리 중 무엇인가?
2. **센서 채널 구성**: 채널 수, 데이터 타입 (float 온도·습도 등 / 정수)
3. **SNMP OID 체계**: 표준 MIB 매핑 vs. Private Enterprise OID (`.1.3.6.1.4.1.XXXXX`)

---

## Phase 1 - 공유 데이터 구조 설계

### 목표

UART 파싱 Task, SNMP getter 콜백, HTTPS 핸들러가 모두 참조하는
단일 진실 출처(single source of truth) 구조체를 만든다.

### 신규 파일

`port/app/platform_handler/inc/sensorData.h`
`port/app/platform_handler/src/sensorData.c`

### 설계 (예시 - 채널 수/타입은 확정 후 수정)

```c
#define SENSOR_CH_MAX   8

typedef struct {
    float    value;
    uint32_t updated_at;  // xTaskGetTickCount() 기준 (ms)
    uint8_t  valid;       // 0: 미수신 또는 timeout, 1: 유효
} sensor_reading_t;

typedef struct {
    sensor_reading_t ch[SENSOR_CH_MAX];
    SemaphoreHandle_t mutex;
} sensor_store_t;

extern sensor_store_t g_sensor;
```

### 접근 규칙

- 쓰기: UART 파싱 Task만 수행. mutex 취득 후 갱신.
- 읽기: SNMP getter, HTTPS 핸들러. mutex 취득 후 읽기.
- Timeout: `updated_at` 기준 일정 시간 이상 갱신 없으면 `valid = 0` 처리.

---

## Phase 2 - UART 파싱 Task 신설

### 목표

`seg_u2e_task()` 를 건드리지 않고 별도 Task를 추가하여 UART 데이터를 파싱하고 `g_sensor` 를 갱신한다.

> `seg_u2e_task()` 는 최종적으로 제거 대상이지만, 우선은 공존시키고 파싱 Task를 먼저 안정화한다.

### 신규 파일

`port/app/platform_handler/inc/uartSensorParser.h`
`port/app/platform_handler/src/uartSensorParser.c`

### Task 구조

```c
#define UART_SENSOR_TASK_STACK_SIZE  2048
#define UART_SENSOR_TASK_PRIORITY    9   // Net_Status_Task(8)보다 한 단계 높게

void uart_sensor_task(void *arg) {
    while (1) {
        // sem 대기 (ring buffer에 데이터 들어올 때까지)
        // ring buffer에서 읽기
        // 프레임 파싱
        // mutex 취득 → g_sensor 갱신 → mutex 반환
    }
}
```

### 프로토콜별 파서 구현 방향

| 프로토콜 | 구현 방향 |
|---|---|
| **Modbus RTU** | 기존 `mbserial.c` 활용. 요청-응답 구조 그대로 사용. |
| **ASCII CSV** | `\r\n` 단위로 라인 수집 후 `sscanf` 또는 직접 파싱. |
| **독자 바이너리** | 헤더/길이/CRC 기반 프레임 파서 신규 작성. |

### App.c에 추가

```c
xTaskCreate(uart_sensor_task, "UART_Sensor_Task",
            UART_SENSOR_TASK_STACK_SIZE, NULL,
            UART_SENSOR_TASK_PRIORITY, NULL);
```

---

## Phase 3 - SNMP OID 연결

### 목표

`snmp_custom.c` 의 OID 테이블에 센서 채널별 getter 콜백을 추가하여
`snmpget` / `snmpwalk` 로 실시간 센서값을 조회할 수 있게 한다.

### getter 콜백 패턴 (`currentUptime` 방식과 동일)

```c
// uartSensorParser.c 또는 snmp_custom.c 내 구현
void get_sensor_ch0(void *ptr, uint8_t *len) {
    xSemaphoreTake(g_sensor.mutex, portMAX_DELAY);
    if (g_sensor.ch[0].valid) {
        *len = snprintf((char *)ptr, 16, "%.1f", g_sensor.ch[0].value);
    } else {
        *len = snprintf((char *)ptr, 16, "N/A");
    }
    xSemaphoreGive(g_sensor.mutex);
}
```

### OID 체계 (확정 필요)

**Option A - Private Enterprise OID**
```
.1.3.6.1.4.1.XXXXX.1.0  → 채널 0
.1.3.6.1.4.1.XXXXX.1.1  → 채널 1
...
```

**Option B - 표준 MIB 매핑**
- 해당 데이터 성격에 맞는 표준 MIB에 매핑 (온도면 ENTITY-SENSOR-MIB 등)
- 구현 복잡도 높음 → 1차에서는 Option A 권장

### snmp_custom.c OID 테이블 정리 항목

- [ ] 테스트용 OID (`OID Test #1`, `#2`) 제거
- [ ] `sysContact` "WIZnet" → 실제 담당자/조직으로 수정
- [ ] `sysLocation` "Unspecified" → 실제 설치 위치로 수정
- [ ] 센서 채널별 OID 항목 추가

---

## Phase 4 - HTTPS 웹페이지 동적화

### 목표

현재 정적 HTML(`Web_page.h` 바이트 배열)을 센서값이 반영된 페이지로 교체한다.

### 단계적 접근

#### 1차: Placeholder 치환 (구조 단순, 새로고침 필요)

응답 시 `%CH0_VAL%` 같은 placeholder를 실제 값으로 치환하여 전송.

```c
// httpHandler.c 내 응답 생성 시
snprintf(body, sizeof(body), page_template,
         g_sensor.ch[0].value,
         g_sensor.ch[1].value, ...);
```

#### 2차: JSON API 엔드포인트 추가 (자동 갱신)

`GET /api/data` 요청에 JSON으로 응답, HTML에서 JS `fetch()` 로 주기적 갱신.

```json
{"ch0": 25.3, "ch1": 60.1, "ch2": null}
```

HTTPS 핸들러에 URL 라우팅 (`/` vs `/api/data`) 추가 필요.

> 1차로 시작하고 UI 요건이 생기면 2차로 발전시킨다.

---

## Task 우선순위 구성 (예정)

| Task | 역할 | Priority |
|---|---|---:|
| `Net_Status_Task` | PHY/DHCP/IP 상태 관리 | 8 |
| `UART_Sensor_Task` | UART 파싱 및 g_sensor 갱신 | 9 |
| `http_webserver_task` | HTTPS 서버 처리 | 23 |
| `SNMP_Agent_Task` | SNMP UDP 161 처리 | 7 |
| FreeRTOS Timer Service | Software timer callback | 31 |

> `UART_Sensor_Task` 를 `Net_Status_Task` 보다 높게 두어 센서 데이터 유실을 최소화한다.
> 실기 확인 후 조정.

---

## 주의 사항

- `seg_u2e_task()` 및 S2E 관련 코드는 즉시 제거하지 않고, 파싱 Task 안정화 후 별도로 정리한다.
- `g_sensor` mutex는 ISR이 아닌 Task context에서만 사용한다. ISR에서는 ring buffer까지만 접근.
- `Heap_Monitor_Task` 는 디버깅 중에만 활성화. 릴리즈 전 제거 또는 주기 대폭 증가.
- SNMP community `public` 은 읽기 전용 1차 구현 기준. 인증 강화는 후속 작업.
