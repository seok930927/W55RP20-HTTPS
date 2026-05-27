# sensorUart — 시리얼 UART 프로토콜 가이드

> 대상 파일: `port/app/platform_handler/src/sensorUart.c` / `inc/sensorUart.h`

---

## 1. 하드웨어 구성

| 역할 | 트랜시버 | RP2040 UART | TX | RX | DE/nRE |
|------|----------|-------------|----|----|--------|
| RS-232 | XR32330 | uart1 | GPIO4 | GPIO5 | — |
| RS-485 | SP3485EN | uart0 | GPIO0 | GPIO1 | GPIO3 |

**RS-485 DE 핀 (GPIO3) 동작:**
- `LOW` = 수신 대기 (평상시)
- `HIGH` = 송신 중 (전송 완료 후 자동 LOW 복귀)

---

## 2. 데이터 플로우

```
UART RX (RS-232, uart1, GPIO5)  ──┐
                                  ├──► 공유 링 버퍼 ──► sensorUart_task()
UART RX (RS-485, uart0, GPIO1)  ──┘   (bufferHandler)        │
    ISR: semaphore give ──────────────────────────────────────┘
                                                              │
                                                         parse_line()
                                                         ├── S / T ──► parse_write()
                                                         │                 ├── device_setValue()
                                                         │                 └── snmp_notify_device()  ← T 명령만
                                                         └── R ──────► parse_request()
                                                                            └── uart_tx_str()
                                                                                ├── platform_uart_puts()  RS-232
                                                                                └── rs485_tx_str()        RS-485
```

---

## 3. 커맨드 레퍼런스

> ASCII 텍스트, 개행(`\n` 또는 `\r\n`) 종료, 대소문자 무관

### 3.1 쓰기 — `S` 명령

```
S<dev>=<v0>[,<v1>,...][;<v0>,...]\n
```

- `<dev>` : 시작 디바이스 인덱스 (0-based)
- `,` : 같은 디바이스의 다음 값 컬럼
- `;` : 다음 디바이스로 이동 (인덱스 +1, 컬럼 리셋) — **디바이스 번호 재지정 불가**
- 범위 초과(디바이스 / 컬럼) 값은 무시
- SNMP 트랩 없음

**실제 예시 (`DEVICE_VALUE_COLS = 3`):**

```
S3=1,2,3;4,5,6
  │ └───┬───┘ └──┬──┘
  │    device3  device4 (;로 자동 +1)
  └─ 시작 디바이스 = 3

→ device 3 = {1, 2, 3}
→ device 4 = {4, 5, 6}
```

```
S3=1,2,3;4,5,6;7,8,9
→ device 3 = {1, 2, 3}
→ device 4 = {4, 5, 6}
→ device 5 = {7, 8, 9}
```

> `;` 이후 디바이스 번호는 직접 지정 불가, 항상 직전 디바이스 +1 로 자동 증가한다.  
> device 3, 5, 7 처럼 건너뛰어 쓰려면 명령을 분리해서 보내야 한다.  
> `S3=1,2,3` → `S5=4,5,6` → `S7=7,8,9`

### 3.2 쓰기 + 트랩 — `T` 명령

```
T<dev>=<v0>[,<v1>,...][;<v0>,...]\n
```

`S`와 동일하나, 값을 쓴 디바이스마다 SNMP 트랩을 큐에 추가한다.  
트랩은 `snmp_agent_task`가 다음 사이클에 전송 (`snmp_notify_device()` 경유).

### 3.3 요청 — `R` 명령

```
R<dev>\n           디바이스 1개 조회
R<d1>~<d2>\n       디바이스 범위 조회 (순서 무관, 양끝 포함)
```

**응답 형식 (TX):**

```
R<dev>=<v0>,<v1>,...\r\n
```

- 요청된 디바이스마다 1줄 출력
- 범위 초과 디바이스는 건너뜀
- 응답은 **RS-232 + RS-485 동시** 전송

### 3.4 예시 (`DEVICE_VALUE_COLS = 3`, 컬럼: Temperature / Humidity / Alarm)

| 입력 | 동작 |
|------|------|
| `S3=1,2,3` | device 3 → {1, 2, 3} |
| `S3=1,2,3;4,5,6` | device 3 → {1,2,3} / device 4 → {4,5,6} |
| `S3=1,2,3;4,5,6;7,8,9` | device 3,4,5 에 연속 쓰기 |
| `T5=10,20,1` | device 5 → {10,20,1} + SNMP 트랩 |
| `T3=1,2,1;4,5,1` | device 3,4 에 쓰기 + 각각 SNMP 트랩 |
| `R5` | → `R5=1,2,3\r\n` |
| `R3~5` | → `R3=...\r\n` `R4=...\r\n` `R5=...\r\n` |
| `R5~3` | → R3~R5 (순서 역전 허용) |

---

## 4. 디바이스 / 값 모델

> 정의 파일: `port/app/platform_handler/src/sensor.c` / `inc/sensor.h`

```c
#define DEVICE_COUNT       64   // 디바이스(행) 수
#define DEVICE_VALUE_COLS   3   // 디바이스당 값 컬럼 수
```

| 컬럼 | 이름 | 단위 | 스케일 | 비고 |
|------|------|------|--------|------|
| 0 | Temperature | C | ×0.1 | raw 235 → 23.5°C |
| 1 | Humidity | %RH | ×0.1 | raw 600 → 60.0% |
| 2 | Alarm | — | ×1 | 0 / 1 |

**컬럼 구성 변경 방법:**
1. `sensor.h` — `DEVICE_VALUE_COLS` 숫자 수정
2. `sensor.c` — `g_value_columns[]` 배열 내용 수정

SNMP 테이블, 웹 JSON, UART 프로토콜 모두 이 두 곳에서 자동으로 파생된다.

---

## 5. SNMP 연동

| 동작 | 함수 | 호출 조건 |
|------|------|-----------|
| 값 쓰기 | `device_setValue(dev, col, value)` | S / T 명령 수신 시 |
| 트랩 전송 요청 | `snmp_notify_device(dev)` | T 명령 수신 시만 |
| SNMP 에이전트 | `snmp_agent_task()` | 독립 FreeRTOS 태스크 |

`snmp_notify_device()`는 큐에 넣기만 하고 즉시 반환 — ISR-safe.

---

## 6. 보드레이트 / 포맷 설정

양 포트 모두 부팅 시 `DevConfig.serial_option` 값을 읽어 설정된다.

| 항목 | 기본값 |
|------|--------|
| Baud rate | 115200 |
| Data bits | 8 |
| Stop bits | 1 |
| Parity | None |

설정 변경은 웹 설정 페이지 또는 SEGCP를 통해 DevConfig를 수정 후 재부팅.

---

## 7. 사용자 정의 포인트 (코드 수정 위치)

코드 내 `[U1] ~ [U4]` 주석으로 위치가 표시되어 있다.

---

### [U1] `parse_write()` — 쓰기 동작 변경

**파일:** `sensorUart.c`  
**현재 동작:** 값을 `g_devices[]`에 저장, T이면 SNMP 트랩 큐에 추가  
**변경 예시:**

```c
// 값 범위 검증 추가
if (value < 0 || value > 9999) {
    PRT_INFO("sensorUart: dev %d col %u — value %ld out of range\r\n", dev, col, (long)value);
    // 저장 건너뜀
} else {
    device_setValue((uint8_t)dev, col, value);
}
```

---

### [U2] `parse_request()` — 응답 내용 / 포트 변경

**파일:** `sensorUart.c`  
**현재 동작:** `g_devices[]`를 읽어 양 포트로 응답  
**응답 포트 분리 방법:**

```c
// uart_tx_str(buf)  ← 현재: 양 포트 동시 전송
platform_uart_puts((uint8_t *)buf, strlen(buf));  // RS-232만
// 또는
rs485_tx_str(buf);                                 // RS-485만
```

> 어느 포트에서 요청이 왔는지 알려면 **[U4]** 참고.

---

### [U3] `uart_tx_str()` / `rs485_tx_str()` — 송신 함수

**파일:** `sensorUart.c`  
**현재 정책:** 항상 RS-232 + RS-485 동시 전송

```c
static void uart_tx_str(const char *s) {
    platform_uart_puts((uint8_t *)s, (uint16_t)strlen(s));   /* RS-232 */
    rs485_tx_str(s);                                           /* RS-485 */
}
```

포트 선택 로직을 여기에 추가하거나, `parse_request()`에서 직접 개별 함수 호출로 교체.

---

### [U4] ISR 쌍 — 수신 포트 추적

**파일:** `sensorUart.c`  
**현재 문제:** 두 ISR이 같은 링 버퍼를 쓰므로 task에서 어느 포트 수신인지 구분 불가

**Option A — volatile 플래그 (간단):**

```c
// 파일 상단에 추가
static volatile uint8_t s_last_rx_port = 0;  // 0 = RS-232, 1 = RS-485

// RS-232 ISR 내부
s_last_rx_port = 0;
xSemaphoreGiveFromISR(s_uart_sem, &higher);

// RS-485 ISR 내부
s_last_rx_port = 1;
xSemaphoreGiveFromISR(s_uart_sem, &higher);

// parse_request() 내부에서 읽기
if (s_last_rx_port == 0) platform_uart_puts(...);
else                      rs485_tx_str(...);
```

**Option B — 포트별 분리 링 버퍼 (정확, 복잡):**  
양 포트가 동시에 데이터를 보내는 경우 포트 혼용을 방지.  
`bufferHandler`를 2개 인스턴스로 확장 필요.

---

### 새 명령 추가

`parse_line()` 의 `else if` 블록에 새 명령 문자를 추가:

```c
} else if (cmd == 'Q') {
    // 새 명령 처리
    handle_query(rest);
}
```

---

## 8. 태스크 / 초기화 구조

| 항목 | 내용 |
|------|------|
| 초기화 함수 | `sensorUart_init()` — 부팅 시 1회 호출 (`App.c`) |
| FreeRTOS 태스크 | `sensorUart_task()` — 스택 1024, 우선순위 9 |
| 세마포어 | Binary semaphore — ISR이 give, task가 take (타임아웃 5000ms) |
| 디버그 출력 | 5초마다 `[UART] RS232 ISR=X  RS485 ISR=Y` 카운터 출력 |
| LED3 (GPIO19) | RS-485 ISR 진입 시 토글 — USB 없을 때 시각적 확인용 |
