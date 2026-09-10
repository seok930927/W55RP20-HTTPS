# APP_DEV_GUIDE — 앱 펌웨어 확장 가이드

> 대상 브랜치: `HTTPS_SNMP`
> 대상 코드: `main/App/`, `port/app/` (부트로더 `port/boot/` 는 참고용으로만 다룬다)
> 이 문서의 모든 내용은 실제 파일·줄번호 근거가 있다. 줄번호는 작성 시점 기준이므로,
> 코드가 움직였으면 함수 이름으로 다시 찾으면 된다.

---

## 목차

- [1장 개요](#1장-개요)
- [2장 구조 온보딩](#2장-구조-온보딩)
- [3장 드라이버 계층 API](#3장-드라이버-계층-api)
- [4장 확장 레시피](#4장-확장-레시피)
- [5장 함정 모음](#5장-함정-모음)
- [6장 작업 규칙](#6장-작업-규칙)

---

# 1장 개요

## 1.1 이 장비가 하는 일

W55RP20 (RP2040 + W5500 이더넷) 보드다. **시리얼 2포트로 들어온 센서 값을 모아두고, 이더넷 쪽으로 웹(HTTPS)과 SNMP로 보여준다.**

```
   [시리얼 장비]                    [게이트웨이 (이 펌웨어)]                [관제]

  온습도 센서 ──RS-485(uart0)──┐
  (Modbus/ASCII)               │
                               ├──►  device bank  ──┬──► SNMP Agent (UDP 161) ──► NMS
  UPS / 호스트 ──RS-232(uart1)─┘   g_devices[64]    │
  (S/T/R 텍스트 or Modbus)          × 3 value col   └──► HTTPS 웹 UI (TCP 443) ──► 브라우저
```

핵심 개념 하나만 기억하면 된다 — **device bank**.
`g_devices[64]` 배열이 중앙 저장소이고, 시리얼에서 들어온 값은 전부 여기 쓰이고,
SNMP 테이블과 웹 JSON은 전부 여기서 읽어간다.
새 센서/프로토콜을 붙인다는 건 결국 **"device bank 에 값을 쓰는 코드를 하나 더 만든다"** 는 뜻이다.

| 파일 | 역할 |
|---|---|
| [port/app/platform_handler/inc/sensor.h](port/app/platform_handler/inc/sensor.h) | device bank 정의 (`DEVICE_COUNT 64`, `DEVICE_VALUE_COLS 3`) |
| [port/app/platform_handler/src/sensor.c](port/app/platform_handler/src/sensor.c) | 값 컬럼 카탈로그 `g_value_columns[]` (Temperature / Humidity / Alarm) |

## 1.2 독자별 읽는 법

| 당신이 하려는 일 | 읽을 곳 |
|---|---|
| "웹 화면에 항목 하나만 더 넣고 싶다" | [4.2](#42-웹-설정-항목-추가) |
| "보드가 바뀌어서 DE 핀 번호만 바꾸면 된다" | [3.5](#35-레시피) — 설정만으로 끝난다. 코드 수정 불필요 |
| "새 센서(다른 프로토콜)를 붙여야 한다" | [1.1](#11-이-장비가-하는-일) → [3장](#3장-드라이버-계층-api) → [4.4](#44-새-시리얼-프로토콜-추가) |
| "SNMP OID 를 추가해야 한다" | [4.3](#43-snmp-oid-추가--트랩-추가) |
| "설정 항목(플래시에 저장되는 값)을 추가해야 한다" | [2.4](#24-devconfig-이중구조--legacy-vs-ext) → [4.1](#41-devconfig-ext-에-설정-필드-추가) |
| "일단 전체 구조부터" | [2장](#2장-구조-온보딩) 전체 |

## 1.3 전제조건

| 항목 | 내용 |
|---|---|
| 툴체인 | arm-none-eabi-gcc, CMake ≥ 3.12, Python 3 (`py -3` 로 호출됨), astyle |
| 서브모듈 | pico-sdk 2.2.0, FreeRTOS-Kernel V11.1.0(SMP), ioLibrary_Driver, mbedtls 3.6.5, aws-iot-sdk |
| **최초 1회 필수** | ioLibrary 서브모듈에 `ioLibrary_snmp_patch.patch` 적용 — [Setup.md](Setup.md) 3절 참조. 안 하면 SNMP 가 통째로 안 붙는다 |
| 보드 선택 | [CMakeLists.txt:39](CMakeLists.txt#L39) `set(BOARD_NAME PLATYPUS_S2E)` |
| 빌드 결과물 | `bin_files/Boot-App_linker_Merged.uf2` (BOOTSEL 로 이 파일 하나 넣으면 부트+앱 전체) |
| 빌드 타입 | Release, `-O3 -DNDEBUG` — **`assert()` 는 무효화되어 있다** ([5.9](#59-configassert-는-릴리스에서-무효다--태스크-우선순위가-조용히-잘린다) 참조) |

## 1.4 용어

| 약칭 | 뜻 |
|---|---|
| **device bank** | `g_devices[64]`. 센서 값 중앙 저장소 |
| **value column** | 장치 하나가 노출하는 값의 종류. 현재 3개 (온도/습도/알람) |
| **LEGACY / EXT** | DevConfig 구조체의 두 구역. [2.4](#24-devconfig-이중구조--legacy-vs-ext) |
| **DE 핀** | RS-485 송수신 방향 제어 GPIO (Driver Enable) |
| **S2E** | Serial-to-Ethernet. 원본 WIZnet 펌웨어의 기능. **이 펌웨어에서는 동작하지 않는다** ([5.10](#510-s2e-경로는-죽어있다)) |

---

# 2장 구조 온보딩

## 2.1 디렉터리 지도

### 최상위

| 경로 | 내용 | 건드릴 일 |
|---|---|---|
| [main/](main/) | 실행 진입점 3종 (App / Boot / SPI_Mode_Master) | 태스크 추가할 때 |
| [port/app/](port/app/) | **앱 펌웨어 본체.** 작업의 95% 가 여기 | 항상 |
| [port/boot/](port/boot/) | 부트로더. 앱과 별개 소스트리 | 거의 없음 (읽기만) |
| [libraries/](libraries/) | 서브모듈 5개 + 라이브러리 CMake | SNMP 손댈 때만 |
| [tools/](tools/) | 빌드 보조 파이썬 스크립트 | HTML 재생성 시 |
| [style/](style/) | astyle 설정 + `restyle.py` (**빌드 때 자동으로 소스를 재포맷한다**) | [6.1](#61-빌드가-자동으로-건드리는-것들) 참조 |
| [Dev_data/](Dev_data/) | 규격서·캡처 로그·인수인계 문서 | 프로토콜 붙일 때 |
| `bin_files/` | 빌드 산출물 (git 무시) | — |

### `port/app/` 내부

| 경로 | 무엇이 들었나 | CMake 타겟 |
|---|---|---|
| [board/](port/app/board/) | 보드 핀 정의 `WIZnet_board.h`, LED/스트랩핀 | `APP_BOARD_FILES` |
| [configuration/](port/app/configuration/) | **DevConfig**(설정 구조체·플래시 저장), SEGCP(설정 프로토콜), 공통 상수 `common.h` | `APP_CONFIG_FILES` |
| [platform_handler/](port/app/platform_handler/) | **작업의 중심.** UART·SNMP·HTTPS·센서·GPIO·플래시 핸들러 전부 | `APP_PLATFORM_FILES` |
| [html_file/](port/app/html_file/) | `Web_page.html` (원본) + `Web_page.h` (**생성물**) | — (헤더만 include) |
| [serial_to_ethernet/](port/app/serial_to_ethernet/) | S2E 게이트웨이 로직 (현재 미동작) | `APP_S2E_FILES` |
| [modbus/](port/app/modbus/) | Modbus 라이브러리 (CRC 함수만 실제로 쓰임) | `APP_MODBUS_FILES` |
| [mbedtls/](port/app/mbedtls/) | TLS 포팅 레이어 + `SSLConfig.h` | `APP_MBEDTLS_FILES` |
| [ioLibrary_Driver/](port/app/ioLibrary_Driver/) | W5500 SPI/PIO 포팅 | `APP_IOLIBRARY_FILES` |
| [timer/](port/app/timer/), [http_server/](port/app/http_server/) | 타이머, 구버전 HTTP 서버(현재 `EXCLUDE_FROM_ALL`) | `APP_TIMER_FILES` / `APP_HTTPSERVER_FILES` |
| [FreeRTOS-Kernel/inc/FreeRTOSConfig.h](port/app/FreeRTOS-Kernel/inc/FreeRTOSConfig.h) | RTOS 설정 | — |

### `platform_handler/` 파일별 역할 (여기가 제일 중요)

| 파일 | 역할 |
|---|---|
| `uartHandler.c/.h` | **UART 드라이버 계층.** 포맷 설정, RS-485 DE 제어, 바이트 입출력 |
| `sensorUart.c/.h` | S/T/R 텍스트 프로토콜 파서 + 두 포트 RX ISR |
| `modbusMaster.c/.h` | Modbus-RTU 마스터 폴러. 값 컬럼 수만큼 입력 레지스터를 읽는다 |
| `protoTemplate.c/.h` | **새 프로토콜 골격.** 복사해서 고치라고 있는 파일 ([4.4](#44-새-시리얼-프로토콜-추가)) |
| `digitalInput.c/.h` | CN10/CN11 접점 입력 16개 → device bank. `DIN_COUNT` 가 없는 보드에서는 태스크가 즉시 종료 |
| `sensor.c/.h` | device bank + 값 컬럼 카탈로그 `g_value_columns[]` |
| `snmpHandler.c/.h` | SNMP 에이전트 태스크, 트랩 큐, 런타임 설정 주입 |
| `snmpBuffer.c/.h` | 레거시 SNMP 데이터 버퍼 (웹 "통신데이타" 탭에서만 사용) |
| `httpHandler.c/.h` | **HTTPS 서버 전체** — 라우팅, JSON GET/POST, 세션 |
| `httpsAuth.c/.h` | 계정/세션 관리 (플래시 저장) |
| `netHandler.c/.h` | PHY 링크 / DHCP 상태머신 |
| `deviceHandler.c/.h` | 플래시 맵, 리부트, 워치독, 펌웨어 업데이트 |
| `bufferHandler.c/.h` | 링 버퍼 매크로 + 데이터 버퍼 |
| `gpioHandler.c/.h` | `GPIO_Configuration()` 등 GPIO 래퍼 |
| `flashHandler.c`, `storageHandler.c` | 플래시 읽기/쓰기/소거 |
| `timerHandler.c/.h` | 1 ms 반복 타이머 + uptime |
| `spiHandler.c` | SPI 슬레이브 모드 (현재 미사용) |

---

## 2.2 부팅 순서 + 태스크 표

### 2.2.1 부팅 순서

`main()` 은 태스크 하나만 만들고 스케줄러를 켠다. 실제 초기화는 전부 `start_task` 안에서 일어난다.

| # | 호출 | 위치 | 하는 일 / 주의 |
|---|---|---|---|
| 0 | `main()` | [App.c:117](main/App/App.c#L117) | `start_task` 생성 후 `vTaskStartScheduler()` |
| 1 | `vTaskCoreAffinitySet(NULL, 1<<0)` | [App.c:208](main/App/App.c#L208) | Core 0 고정. `stdio_usb_init()` 이 Core 0 을 요구 |
| 2 | `stdio_init_all()` | [App.c:214](main/App/App.c#L214) | **스케줄러 시작 후**에 호출. 별도 `tud_task()` 태스크 만들지 말 것 |
| 3 | `RP2040_Init()` | [App.c:216](main/App/App.c#L216) | `flash_critical_section_init()` → `flash_critical_sem` 생성 |
| 4 | `RP2040_W5X00_Init()` | [App.c:217](main/App/App.c#L217) | W5500 SPI 초기화 → `wizchip_critical_sem` 생성 |
| 5 | `load_DevConfig_from_storage()` | [App.c:225](main/App/App.c#L225) | 플래시 → RAM. 마이그레이션 판정 ([2.4.3](#243-부팅-시-마이그레이션-판정)) |
| 6 | `RP2040_Board_Init()` | [App.c:226](main/App/App.c#L226) | 팩토리리셋핀·IF셀렉트핀·LED·상태IO |
| 7 | `set_minimal_runtime_config()` | [App.c:227](main/App/App.c#L227) | **`serial_intf_sel` → `serial_option.uart_interface` 복사** ([5.1](#51-부트로더와-앱의-uart_interface-enum-이-어긋난다)) |
| 8 | `DATA0_UART_Configuration()` | [App.c:228](main/App/App.c#L228) | uart1(RS-232) 설정. `check_mac_address()` 보다 먼저여야 함 |
| 9 | `check_mac_address()` | [App.c:229](main/App/App.c#L229) | MAC OUI 가 다르면 UART 로 MAC 입력받고 리부트 |
| 10 | `Net_Conf()` | [App.c:231](main/App/App.c#L231) | W5500 에 IP/GW/SN 주입 |
| 11 | `set_W5X00_NetTimeout()` | [App.c:235](main/App/App.c#L235) | RCR/RTR |
| 12 | `Timer_Configuration()` | [App.c:236](main/App/App.c#L236) | 1 ms 반복 타이머 시작 ([2.2.4](#224-1-ms-타이머-콜백)) |
| 13 | `device_init()` | [App.c:239](main/App/App.c#L239) | device bank 0으로 클리어 |
| 14 | 데모값 주입 | [App.c:253-268](main/App/App.c#L253-L268) | `TH-1`~`TH-4` 4개 슬롯에 고정값. Modbus 응답이 오면 덮어씀 |
| 15 | `sensorUart_init()` | [App.c:271](main/App/App.c#L271) | uart0 항상 초기화 + uart1 은 Modbus 모드가 아닐 때만 |
| 16 | 세마포어 5개 생성 | [App.c:273-277](main/App/App.c#L273-L277) | ([2.2.3](#223-세마포어--타이머)) |
| 17 | 태스크 생성 | [App.c:283-291](main/App/App.c#L283-L291) | ([2.2.2](#222-태스크-표)) |
| 18 | `watchdog_enable(8388, 0)` | [App.c:294](main/App/App.c#L294) | 8.388 초. RP2040 워치독 최대치 |
| 19 | `vTaskDelete(NULL)` | [App.c:296](main/App/App.c#L296) | start_task 자기 자신 삭제 |

### 2.2.2 태스크 표

스택 값은 **워드 단위**다 (`StackType_t` = 4 B). 실제 바이트 = ×4.

| 태스크 이름 | 함수 | 정의 위치 | 요청 우선순위 | **실제 우선순위** | 스택(word / byte) | 역할 |
|---|---|---|---|---|---|---|
| `Start_Task` | `start_task` | [App.c:201](main/App/App.c#L201) | 65 | **31** ⚠ | 512 / 2 KB | 초기화 후 self-delete |
| `Net_Status_Task` | `net_status_task` | [netHandler.c:40](port/app/platform_handler/src/netHandler.c#L40) | 8 | 8 | 1024 / 4 KB | PHY 링크 감시, DHCP 갱신 |
| `http_webserver_task` | `http_webserver_task` | [httpHandler.c:1358](port/app/platform_handler/src/httpHandler.c#L1358) | 23 | 23 | 2048 / 8 KB | HTTPS 서버 (소켓 4·5·6) |
| `SNMP_Agent_Task` | `snmp_agent_task` | [snmpHandler.c:119](port/app/platform_handler/src/snmpHandler.c#L119) | 7 | 7 | 2048 / 8 KB | SNMP 요청 처리 + 트랩 송신 (소켓 7) |
| `SEGCP_udp_Task` | `segcp_udp_task` | [segcp.c:1709](port/app/configuration/src/segcp.c#L1709) | 52 | **31** ⚠ | 1024 / 4 KB | 설정툴 UDP 검색 (소켓 1) |
| `SEGCP_tcp_Task` | `segcp_tcp_task` | [segcp.c:1724](port/app/configuration/src/segcp.c#L1724) | 51 | **31** ⚠ | 1024 / 4 KB | 설정툴 TCP 접속 (소켓 2) |
| `SEGCP_serial_Task` | `segcp_serial_task` | [segcp.c:1740](port/app/configuration/src/segcp.c#L1740) | 50 | **31** ⚠ | 1024 / 4 KB | 시리얼 AT 커맨드 (`+++` 진입) |
| `Sensor_UART_Task` | `sensorUart_task` | [sensorUart.c:360](port/app/platform_handler/src/sensorUart.c#L360) | 9 | 9 | 1024 / 4 KB | S/T/R 라인 파싱 |
| `Modbus_ch0` / `Modbus_ch1` | `modbusMaster_task` | [modbusMaster.c](port/app/platform_handler/src/modbusMaster.c) | 9 | 9 | 1024 / 4 KB | **포트별 조건부 생성** — 그 포트의 `protocol == modbus_rtu` 일 때만. 인자로 `&g_serial_port[p]` 전달. 스택·우선순위는 `g_serial_protocol[]` 행에서 온다 |
| `Digital_Input_Task` | `digitalInput_task` | [digitalInput.c:146](port/app/platform_handler/src/digitalInput.c#L146) | 8 | 8 | 512 / 2 KB | CN10/CN11 접점 16개를 200 ms 마다 읽어 뱅크에 기록. **시리얼 프로토콜 태스크보다 뒤에 생성** — 뱅크 행을 먼저 온 순서로 나눠주기 때문 |
| `Tmr Svc` | (FreeRTOS 내장) | — | 31 | 31 | 1024 / 4 KB | 소프트웨어 타이머 |
| `Heap_Monitor_Task` | `heap_monitor_task` | [App.c:191](main/App/App.c#L191) | 6 | — | 1024 / 4 KB | **주석 처리됨** ([App.c:292](main/App/App.c#L292)) |

⚠ **`configMAX_PRIORITIES` 가 32라서 32 이상은 전부 31로 잘린다.** 상세는 [5.9](#59-configassert-는-릴리스에서-무효다--태스크-우선순위가-조용히-잘린다).

### FreeRTOS 설정 요약 ([FreeRTOSConfig.h](port/app/FreeRTOS-Kernel/inc/FreeRTOSConfig.h))

| 설정 | 값 | 의미 |
|---|---|---|
| `configNUMBER_OF_CORES` | 2 | **SMP.** 태스크가 Core 0/1 어디서든 돈다 |
| `configRUN_MULTIPLE_PRIORITIES` | 1 | 서로 다른 우선순위가 동시에 실행될 수 있음 |
| `configUSE_CORE_AFFINITY` | 1 | `vTaskCoreAffinitySet()` 사용 가능 |
| `configMAX_PRIORITIES` | 32 | 유효 범위 0~31 |
| `configTICK_RATE_HZ` | 1000 | 1 tick = 1 ms |
| `configTOTAL_HEAP_SIZE` | 96 KB | heap_4 |
| `configCHECK_FOR_STACK_OVERFLOW` | 1 | 오버플로 시 `vApplicationStackOverflowHook()` ([App.c:312](main/App/App.c#L312)) |
| `configASSERT` | `assert()` | 릴리스(`-DNDEBUG`)에서 **무효** |

### 2.2.3 세마포어 / 타이머

`App.c` 상단에 전역 세마포어가 17개 선언돼 있지만 **실제로 생성되는 건 7개뿐**이다. 나머지는 S2E 경로용이라 `NULL` 로 남는다 ([5.10](#510-s2e-경로는-죽어있다)).

| 이름 | 생성 위치 | 종류 | 용도 |
|---|---|---|---|
| `net_http_webserver_sem` | [App.c:273](main/App/App.c#L273) | Counting | 링크 복구 시 HTTPS 태스크 깨우기 |
| `net_segcp_udp_sem` / `net_segcp_tcp_sem` | App.c:274-275 | Counting | 링크 복구 시 SEGCP 깨우기 |
| `segcp_udp_sem` / `segcp_tcp_sem` | App.c:276-277 | Counting | SEGCP 내부 |
| `wizchip_critical_sem` | [w5x00_spi.c:207](port/app/ioLibrary_Driver/src/w5x00_spi.c#L207) | Counting(1) | W5500 SPI 배타 접근 |
| `flash_critical_sem` | [flashHandler.c:43](port/app/platform_handler/src/flashHandler.c#L43) | Mutex | 플래시 배타 접근 |
| `s_uart_sem` | [sensorUart.c:137](port/app/platform_handler/src/sensorUart.c#L137) | Binary | RX ISR → `sensorUart_task` 기상 신호 |

소프트웨어 타이머는 `reset_timer` 하나만 생성된다 ([App.c:282](main/App/App.c#L282), 5초 원샷).

### 2.2.4 1 ms 타이머 콜백

`Timer_Configuration()` → `add_repeating_timer_us(-1000, ...)` → `repeating_timer_callback()` ([timerHandler.c:38](port/app/platform_handler/src/timerHandler.c#L38))

| 주기 | 호출 |
|---|---|
| 1 ms | `seg_timer_msec()`, `segcp_timer_msec()`, `gpio_handler_timer_msec()` |
| 10 ms | `SNMP_time_handler()` (sysUpTime 카운터) |
| 1 s | `DHCP_time_handler()`, `DNS_time_handler()`, `LED_Toggle(LED3)`, 조건부 `device_wdt_reset()` |

### 2.2.5 워치독

| 항목 | 값 |
|---|---|
| 활성 조건 | `__USE_WATCHDOG__` 정의됨 ([WIZnet_board.h:30](port/app/board/inc/WIZnet_board.h#L30)) |
| 타임아웃 | 8388 ms |
| 리셋 주체 | `vApplicationPassiveIdleHook()` ([App.c:299](main/App/App.c#L299)) — 두 코어가 번갈아 idle 에 들어갈 때마다 |
| 추가 리셋 | 1초 타이머, `platform_uart_putc/puts`, `https_write_all()`, HTTPS 루프 |

> **긴 블로킹 루프를 새로 만들면 반드시 `device_wdt_reset()` 을 넣어라.** 8.4초 넘게 idle 이 안 돌면 리부트한다.

---

## 2.3 데이터 흐름

### 2.3.1 시리얼 → device bank

**모든 수신 바이트는 `serial_port_getc()` 하나를 통과한다.** 그 아래에서 AT 이스케이프가 걸러지고,
통과한 바이트만 프로토콜 핸들러에게 간다.

```
[공통 수신 경로]

  uart RX ─► serial_port_getc(port)
                ├─ 채널 0 이면 check_modeswitch_trigger()  → +++ 이면 삼킴(RET_NOK)
                └─ 나머지는 그대로 반환

[경로 A] S/T/R 텍스트 프로토콜   (그 포트의 protocol != modbus_rtu 일 때)

  sensorUart_rs232_rx_isr()  ─► serial_port_getc(ch0) ─► put_byte_to_data_buffer(ch, 0)
  sensorUart_rs485_rx_isr()  ─► serial_port_getc(ch1) ─► put_byte_to_data_buffer(ch, 1)
                                                              │
                              opmode == AT ? segcp_uart_sem : s_uart_sem
                                                              │
                              sensorUart_task() ─► sensorUart_drain(port, line, &pos)
                                                     └─ parse_line(port, line)
                                                          ├─ 'S' ─► parse_write(trap=0) ─► device_setValue()
                                                          ├─ 'T' ─► parse_write(trap=1) ─► device_setValue() + snmp_notify_device()
                                                          └─ 'R' ─► parse_request(port) ─► device_getValue()
                                                                      └─ uart_tx_str(port) [명령이 온 포트로만]

[경로 B] Modbus-RTU 마스터   (그 포트의 protocol == modbus_rtu 일 때, ISR 없음)

  modbusMaster_task(&g_serial_port[p]) ─► modbus_read_values(port, slave, v)
       ├─ serial_port_tx_enable/disable() 로 DE 를 감싸 요청 송신
       ├─ mb_recv(port, ...) ─► serial_port_getc() 로 응답 수신
       └─ device_bank_setValue(src, idx, c, v[c])   c = 0 .. DEVICE_VALUE_COLS-1

[경로 C] 접점 입력   (시리얼과 무관, 보드가 DIN_COUNT 를 정의할 때만)

  digitalInput_task() ─► digitalInput_poll()   200 ms 주기
       └─ gpio_get(pin) ─► 값이 바뀐 입력만
            device_bank_setValue(DEVICE_SRC_CONTACT, idx, 2, 1=정상 / 2=경보)
```

**뱅크 행은 요청해서 받는다 — 직접 고르지 않는다.**

```c
int base = device_bank_reserve(개수, source);      /* 시작할 때 한 번 */
device_bank_assign  (source, idx, "이름");          /* 이후로는 idx 로만 */
device_bank_setValue(source, idx, col, value);
int row = device_bank_row(source, idx);            /* 트랩용 행 번호가 필요할 때 */
```

`source` 는 시리얼 프로토콜이면 그 포트의 채널 번호, 아니면 `DEVICE_SRC_*` 값이다.
같은 `source` 로 두 번 예약하면 블록이 이어 붙어서 `idx` 가 0부터 한 줄로 유지된다.

> 예전에는 프로토콜마다 시작 행을 상수로 들고 있었고(`MODBUS_BANK_BASE_CH0/CH1`),
> 범위가 겹치면 서로 값을 덮어쓰면서 아무 말도 남기지 않았다. 예약제로 바꾼 뒤로는
> 겹칠 수가 없고, 새 프로토콜을 붙이는 사람이 남의 행 번호를 알 필요가 없다.
> S/T/R 은 예약하지 않는다 — `S5=...` 처럼 명령이 행을 직접 지목하는 프로토콜이라
> 설계상 뱅크 전체를 주소로 쓴다.

| 포인트 | 사실 |
|---|---|
| 링 버퍼는 **포트마다 하나** | 두 포트 바이트가 섞이지 않는다 (`51f4a17`) |
| R 명령 응답은 **명령이 온 포트로만** | `uart_tx_str(port, ...)` |
| 포트 소유권 | `sensorUart_claim()` 이 그 포트의 `protocol` 을 보고 잡거나 비킨다 |
| 뱅크 행 배정 | `device_bank_reserve()` 가 **태스크가 시작한 순서대로** 나눠준다 |
| Modbus 슬레이브 범위 | 1~4 고정 (`MODBUS_SLAVE_FIRST/LAST`) |
| Modbus 가 읽는 레지스터 수 | `MODBUS_REG_COUNT` = `DEVICE_VALUE_COLS`. 컬럼을 늘리면 요청도 같이 늘어난다 |
| 폴링 주기 | 슬레이브당 50 ms 간격 + 사이클당 1000 ms |
| ⚠ 폴링 실패 시 | **뱅크에 옛 값이 그대로 남는다.** 의도된 동작이다 — `device_assign()` 은 값을 지우지 않는다. 연결이 끊긴 장치는 0 을 보고하는 대신 마지막으로 말한 값을 계속 보여준다 |
| ⚠ 신선도 | 값이 언제 갱신됐는지 읽는 쪽이 없다. 오래된 값과 방금 값이 화면에서 구분되지 않는다 |

### 2.3.2 device bank → SNMP

```
snmp_agent_task()  (10 ms 주기)
   ├─ snmp_limit_scan()       trap_scan_sec 마다 뱅크를 임계값과 대조 → 큐에 넣기
   ├─ snmp_flush_traps()      트랩 큐 배출 → snmp_custom_sendValueTrap()
   ├─ snmp_custom_refresh()   g_devices[] → snmpData[] 전량 복사
   └─ snmpd_run()             UDP 수신 → parseSNMPMessage() → 응답
```

**언제 트랩을 쏠지는 SNMP 계층이 정한다.** 값을 쓰는 프로토콜은 관여하지 않는다 —
뱅크에 쓰기만 하면 임계값 감시가 따라온다. 나중에 붙는 프로토콜도 이 파일의 존재를
알 필요가 없다. 자세한 것은 [4.3.3](#433-트랩).

| 항목 | 값 | 위치 |
|---|---|---|
| Enterprise 번호 | **22210** (BER 3바이트 `81 AD 42`) | [snmp_custom.c:54-64](libraries/ioLibrary_Driver/Internet/SNMP/snmp_custom.c#L54-L64) |
| deviceTable | `1.3.6.1.4.1.22210.2` | 〃 |
| deviceEntry | `1.3.6.1.4.1.22210.2.1` | 〃 |
| 셀 OID | `…2.1.<컬럼>.<행>` — 행 = 장치 1~64, 컬럼 1=index, 2=name, 3+ = value[] | 〃 |
| 트랩 OID | `1.3.6.1.4.1.22210.0.1` | [snmp_custom.c:63](libraries/ioLibrary_Driver/Internet/SNMP/snmp_custom.c#L63) |
| `sysObjectID` | `1.3.6.1.4.1.22210.1.0` — **이스케이프 문자열로 하드코딩** `"\x2b\x06\x01\x04\x01\x81\xad\x42\x01\x00"` | [snmp_custom.c:97-98](libraries/ioLibrary_Driver/Internet/SNMP/snmp_custom.c#L97-L98) |
| 테이블 크기 | `7 + (2+3)×64 = 327` 엔트리 | [snmp_custom.c:47-48](libraries/ioLibrary_Driver/Internet/SNMP/snmp_custom.c#L47-L48) |
| 정렬 | **컬럼 우선(column-major)** — OID 오름차순 유지가 목적 ([5.6](#56-snmpdata-는-oid-오름차순이어야-한다)) |

### 2.3.3 device bank → 웹

| 엔드포인트 | 메서드 | 핸들러 | 내용 |
|---|---|---|---|
| `/` | GET | `handle_get_root` | `Web_page.h` 통째로 전송 (세션 필요) |
| `/api/sensors` | GET | `https_send_sensor_json` | device bank → JSON, **chunked 스트리밍** (RAM 에 전체를 안 만듦) |
| `/api/config` | GET | [`https_send_config_json`:549](port/app/platform_handler/src/httpHandler.c#L549) | DevConfig → JSON (`body[1536]` 한 방에 조립) |
| `/api/config` | POST | [`https_handle_config_post`:662](port/app/platform_handler/src/httpHandler.c#L662) | JSON 파싱 → DevConfig 갱신 → 저장 → GET 과 같은 JSON 응답 |
| `/api/accounts*`, `/login`, `/setup`, `/logout`, `/api/reboot` | | [dispatch_request:1226](port/app/platform_handler/src/httpHandler.c#L1226) | 계정·세션·리부트 |

`/api/sensors` 의 모양:

```jsonc
{
  "columns": [ {"name":"Temperature","unit":"C","scale":-1}, ... ],   // g_value_columns[]
  "ports":   [ {"ch":0,"if":"TTL/RS-232","proto":"Modbus RTU",
                "baud":115200,"bits":8,"par":"N","stop":1,
                "tx":4,"rx":5,"de":255}, ... ],                       // 포트당 한 개
  "devices": [ {"index":1,"name":"TH-1","src":1,"values":[111,23,0]}, ... ],
  "comm":    { "status":0, "recv_cs":0, "calc_cs":0, "check":0, "flag":0 }
}
```

| 필드 | 쓰임 |
|---|---|
| `src` | 그 행을 발행한 주체. 시리얼 채널 번호이거나 `DEVICE_SRC_*`. **페이지가 이걸로 포트별 표를 나눈다** |
| `ports` | 각 페이지 상단 띠. `serial_port_setup()` 이 실제로 잡은 값이라 HTML 에 사본을 두지 않는다 |
| `ports[].bits` | 하드웨어가 실제로 내보내는 값. PL011 에 9비트 모드가 없어서 9 는 8 로 보고된다 |
| `ports[].de` | RS-485 계열일 때만 핀 번호, 아니면 255. 모든 포트가 DE 를 해석해 갖고 있어서 그대로 내보내면 없는 선을 안내하게 된다 |
| `index` | 1부터. **SNMP 셀 OID 의 행 번호와 같은 값이다** |

### 2.3.4 소켓 배정 ([common.h:25-47](port/app/configuration/inc/common.h#L25-L47))

| 소켓 | 용도 |
|---|---|
| 0 | `SOCK_DATA` (S2E, 미사용) → **SNMP 트랩이 임시로 빌려 씀** |
| 1 / 2 | SEGCP UDP / TCP |
| 3 | DHCP / DNS / 펌웨어 업데이트 (공유) |
| 4 / 5 / 6 | HTTPS 서버 ×3 |
| 7 | SNMP 에이전트 |

---

## 2.4 DevConfig 이중구조 — LEGACY vs EXT

### 2.4.1 왜 둘로 나뉘어 있나

같은 플래시 블록(`STORAGE_CONFIG`)을 **부트로더와 앱이 같이 쓴다.** 그런데 두 쪽 구조체가 다르다.

| | 앱 `port/app/configuration/inc/ConfigData.h` | 부트 `port/boot/configuration/inc/ConfigData.h` |
|---|---|---|
| `sizeof(DevConfig)` | **1596 B** | **1284 B** |
| `offsetof(serial_option)` | **232** | **232** (동일) |
| `device_option` | `..._connect_data` 3종 (32 B ×3) 포함 | **없음** (96 B 짧음) |
| 확장 구역 | 있음 (offset 1444~) | **없음** |

부트로더가 설정을 저장하면 `write_storage(..., sizeof(DevConfig))` 로 **앞 1284 바이트만** 덮어쓴다.
→ **offset 1444 이후(EXT 구역)는 부트로더가 절대 건드릴 수 없다.**
이게 EXT 구역이 존재하는 이유다.

### 2.4.2 구조체 레이아웃

```
offset      구역                                        비고
------------------------------------------------------------------------
   0 ┌──────────────────────────────────────────┐
     │ LEGACY SECTION (레이아웃 동결)            │  CONFIG Tool / 부트로더가
 232 │   … serial_option (9 B) …                │  이 오프셋 맵에 의존
     │   … mqtt_option, device_option …          │
1376 │   devConfigVer (uint32) = 104             │  ← LEGACY 무결성 마커
1380 ├──────────────────────────────────────────┤
     │ reserved_legacy[64]                       │  legacy 쪽 증설용 (현재 전부 0)
1444 ├──────────────────────────────────────────┤ ← 부트로더가 닿지 못하는 경계
     │ ext_magic  = 0x57495A45 ('WIZE')          │
     │ ext_version = 5                           │
     │ ext_reserved0                             │
     │ snmp_option (allowed_ip×4, trap_ip×4)     │
     │ https_session_timeout_min                 │
     │ serial_option_485 (9 B)                   │  RS-485(uart0) 전용 설정
     │ https_port / snmp_agent_port              │
     │ web_access_ip[2][4]                       │
     │ serial_intf_sel     ← uart1 라인드라이버   │
     │ serial485_intf_sel  ← uart0 라인드라이버   │
     │ serial485_de_pin    ← uart0 DE GPIO       │
     │ serial_de_pin       ← uart1 DE GPIO       │
     │ snmp_community[16] / trap_community[16]   │
     │ snmp_perm / trap_disable                  │
     │ value_limit[4] (5 B ×4 = 20 B)            │  트랩 임계값, 값 컬럼별
     │ trap_scan_sec / trap_repeat_sec           │
1567 │ reserved_ext[29]                          │  ← 남은 확장 예산
1596 └──────────────────────────────────────────┘
```

**핵심 불변식: EXT 구역의 "이름 붙은 필드 + `reserved_ext`" 총합은 128 B 로 고정이다.**
[ConfigData.h:210-217](port/app/configuration/inc/ConfigData.h#L210-L217) 의 주석이 그 계산을 그대로 적어둔 것이다.

```
29 = 128 − 2(https_session_timeout_min) − 16(snmp_option 증설분) − 9(serial_option_485)
         − 2(https_port) − 2(snmp_agent_port) − 8(web_access_ip)
         − 1(serial_intf_sel) − 1(serial485_intf_sel)
         − 1(serial485_de_pin) − 1(serial_de_pin)
         − 16(snmp_community) − 16(trap_community) − 1(snmp_perm) − 1(trap_disable)
         − 20(value_limit) − 1(trap_scan_sec) − 1(trap_repeat_sec)
```

**`ext_version` 을 올리지 않고 필드를 늘린 예가 위 세 줄이다.** 전부 "0 = 이 기능
쓰기 전과 같은 동작" 이 되도록 골랐기 때문이다 — `use` 가 0 이면 감시하지 않고,
주기가 0 이면 기본값을 쓴다. 기존 장비의 `reserved_ext` 는 이미 전부 0 이라,
새 펌웨어를 올려도 마이그레이션이 돌지 않고 동작도 그대로다.
버전을 올려야 하는 경우는 [5.2](#52-ext-필드를-추가할-때-reserved_ext-를-안-줄이면-기존-장비가-팩토리-리셋된다) 참고.

> ⚠ **16비트 값을 `int16_t` 로 그냥 넣지 마라.** `DevConfig` 는 packed 라 멤버가
> 홀수 오프셋에 앉을 수 있고, Cortex-M0+ 는 정렬 안 된 `LDRH` 에서 HardFault 한다.
> `value_limit` 이 `uint8_t lo[2]` 로 되어 있고 `value_limit_get()/_set()` 을 거치는
> 이유가 이것이다.

### 2.4.3 부팅 시 마이그레이션 판정

`load_DevConfig_from_storage()` ([ConfigData.c:259](port/app/configuration/src/ConfigData.c#L259))

| 판정 | 조건 | 동작 |
|---|---|---|
| **완전 무효** | `packet_size == 0` 또는 `0xFFFF` 또는 `devConfigVer != 104` | 전체 팩토리 리셋 + 저장 + **강제 리부트** |
| **EXT 만 낡음** | `packet_size != sizeof(DevConfig)` 또는 `ext_magic` 불일치 또는 `ext_version != 5` | **LEGACY 는 보존**하고 EXT 만 재초기화 + `packet_size` 갱신 + 저장 |
| **정상** | 위 둘 다 아님 | 그대로 사용 |

그 후 무조건 실행되는 것:

| 코드 | 이유 |
|---|---|
| `serial_option.uart_interface` 재계산 ([ConfigData.c:315-319](port/app/configuration/src/ConfigData.c#L315-L319)) | 플래시 값을 절대 신뢰하지 않음 — 부트로더가 다른 enum 으로 덮어썼을 수 있음 |
| `fw_ver` 갱신 | 앱 버전으로 덮어씀 |

그리고 `set_minimal_runtime_config()` ([App.c:171](main/App/App.c#L171))이 **EXT 의 `serial_intf_sel` 을 `serial_option.uart_interface` 로 복사**해서 최종 확정한다.

### 2.4.4 팩토리 값 함수 두 개

| 함수 | 위치 | 범위 |
|---|---|---|
| `set_DevConfig_to_factory_value()` | [ConfigData.c:33](port/app/configuration/src/ConfigData.c#L33) | LEGACY 전부 + 끝에서 `set_DevConfig_ext_to_factory_value()` 호출 |
| `set_DevConfig_ext_to_factory_value()` | [ConfigData.c:220](port/app/configuration/src/ConfigData.c#L220) | **EXT 만.** 새 EXT 필드 추가하면 여기에 기본값을 넣어야 한다 |

---

# 3장 드라이버 계층 API

> 이 장은 **"어떻게 구현됐나"가 아니라 "어떻게 쓰나"** 를 다룬다.
> 대상 헤더: [port/app/platform_handler/inc/uartHandler.h](port/app/platform_handler/inc/uartHandler.h)

드라이버는 **프로토콜을 모른다.** S/T/R 이든 Modbus 든 SEC UPS 든, 아래 함수 여섯 개만 쓰면 된다.

## 3.1 포트 객체 — `SerialPort`

포트에 딸린 모든 것이 구조체 하나에 들어 있다. 호출부는 "이게 어느 포트지"를 다시 묻지 않는다.

```c
#define SERIAL_PORT_CNT     2
#define SERIAL_PIN_NONE     0xFF

typedef struct __serial_port {
    /* ── 보드 배선 — 컴파일 타임 상수 ── */
    uart_inst_t            *uart;          /* uart0 / uart1                    */
    uint8_t                 irq;           /* UART0_IRQ / UART1_IRQ            */
    int                     channel;       /* SEG_DATA0_CH / SEG_DATA1_CH      */
    uint8_t                 tx_pin, rx_pin;
    uint8_t                 cts_pin, rts_pin;   /* SERIAL_PIN_NONE 이면 미배선 */
    uint8_t                 de_pin_board;  /* 이 보드가 배선한 DE              */

    /* ── serial_port_setup() 이 설정에서 채움 ── */
    uint8_t                 de_pin;        /* 설정값, 없으면 de_pin_board      */
    uint8_t                 intf;          /* TTL / RS-422 / RS-485 / reverse  */
    uint8_t                 protocol;      /* enum protocol                    */
    struct __serial_option *opt;           /* 그 포트의 설정 블록              */
} SerialPort;

extern SerialPort g_serial_port[SERIAL_PORT_CNT];
```

포트를 얻는 법:

```c
SerialPort *p = &g_serial_port[SEG_DATA0_CH];   /* RS-232 포트 (uart1) */
SerialPort *q = &g_serial_port[SEG_DATA1_CH];   /* RS-485 포트 (uart0) */
```

테이블 정의는 [uartHandler.c:56-68](port/app/platform_handler/src/uartHandler.c#L56-L68) 에 있다.

> **`opt` 는 `serial_port_setup()` 전까지 `NULL` 이다.**
> `serial_port_puts()` 가 `port->opt->data_bits` 를 읽으므로, setup 전에 송신하면 죽는다.
> [App.c](main/App/App.c) 가 `check_mac_address()` 앞에서 `serial_port_init_all()` 을 부르는 이유다.

## 3.2 채널 번호가 헷갈리는 이유

채널 번호는 **RP2040 인스턴스 번호가 아니다.** `DATA0_UART_*` 매크로를 따른다.

| | 채널 | RP2040 | 회로도 표기 | 드라이버 칩 |
|---|---|---|---|---|
| 원래 데이터 포트 | `SEG_DATA0_CH` = 0 | **uart1** | "UART0" | XR32330 |
| 나중에 추가된 포트 | `SEG_DATA1_CH` = 1 | **uart0** | "UART1" | SP3485EN |

세 가지 번호 체계가 서로 엇갈린다. `DATA0_UART_TX_PIN` 이 이미 uart1 을 가리키고 있어서, 채널 번호를 그쪽에 맞췄다.
(`SEG_DATA0_CH` 를 uart0 에 주면 같은 `DATA0` 이름이 서로 다른 포트를 뜻하게 된다.)

채널 상수 정의: [seg.h:24-25](port/app/serial_to_ethernet/inc/seg.h#L24-L25)

## 3.3 두 포트 물리 정보

| 항목 | 채널 0 (RS-232) | 채널 1 (RS-485) |
|---|---|---|
| RP2040 UART | **uart1** (`UART_ID` 매크로) | **uart0** |
| 회로도 표기 | "UART0" | "UART1" (헷갈리니 주의) |
| 드라이버 칩 | XR32330 | SP3485EN |
| TX 핀 | GPIO4 (`DATA0_UART_TX_PIN`) | GPIO0 (`RS485_UART_TX_PIN`) |
| RX 핀 | GPIO5 (`DATA0_UART_RX_PIN`) | GPIO1 (`RS485_UART_RX_PIN`) |
| CTS / RTS | GPIO6 / GPIO7 | **없음** (`SERIAL_PIN_NONE`) |
| DE 핀 (보드 기본) | GPIO7 (`DATA0_UART_RTS_PIN` 겸용) | GPIO3 (`RS485_UART_DE_PIN`) |
| DE 핀 설정 필드 | `serial_de_pin` (EXT) | `serial485_de_pin` (EXT) |
| 라인드라이버 설정 필드 | `serial_intf_sel` (EXT) | `serial485_intf_sel` (EXT) |
| 통신 파라미터 필드 | `serial_option` (LEGACY) | `serial_option_485` (EXT) |
| IRQ | `UART1_IRQ` | `UART0_IRQ` |
| **AT 이스케이프 감시** | **함** | 안 함 ([3.6](#36-at-이스케이프)) |

핀 정의 원본: [WIZnet_board.h:107-116](port/app/board/inc/WIZnet_board.h#L107-L116)

기타 스트랩 핀:

| 매크로 | GPIO | 의미 | 상태 |
|---|---|---|---|
| `UART_IF_SEL_PIN` | 12 | High = 485/422, Low/NC = TTL/232 | 레거시 |
| `UART_SPI_IF_SEL_PIN` | 13 | High = SPI, Low/NC = UART | — |
| `HW_TRIG_PIN` | 14 | 리셋 시 Low → AT 모드 | ⚠ **읽는 코드 없음** |
| `BOOT_MODE_PIN` | 15 | 리셋 시 Low → AT 모드 | — |
| `FAC_RSTn_PIN` | 18 | 5초 이상 Low → 팩토리 리셋 | 동작 |
| `LED3_PIN` | 19 | 1초 블링크 | 동작 |

> RS-485 관련 코드는 `#ifdef __USE_UART_485_422__` 안에 있다.
> 이 보드군에서는 [WIZnet_board.h:32](port/app/board/inc/WIZnet_board.h#L32) 에서 항상 정의된다.

---

## 3.4 공개 함수

### (A) 초기화

| 시그니처 | 용도 | 주의사항 |
|---|---|---|
| `void serial_port_init_all(void)` | **두 포트를 설정에서 전부 세운다** | 부팅 시 `check_mac_address()` **앞에서** 한 번. 이게 `opt` 를 채운다 |
| `void serial_port_setup(SerialPort *port)` | 한 포트만 다시 세운다 | **여러 번 불러도 안전.** 내부에서 `uart_deinit()` 후 재초기화. 범위 밖 설정값은 DevConfig 를 기본값으로 되돌려 쓴다(부작용 있음) |
| `void uart_set_format_parity(uart_inst_t *uart, uint8_t data_bits, uint8_t stop_bits, uint8_t parity_sel)` | 데이터/스톱/패리티. **space·mark(stick parity) 지원** | `parity_sel` 은 `uart_parity_t` 가 아니라 **`enum parity`**. pico-sdk `uart_set_format()` 대신 항상 이걸 써라 ([5.7](#57-stick-parityspacemark는-pico-sdk-로는-안-된다)) |

`serial_port_setup()` 이 하는 일 ([uartHandler.c:163~](port/app/platform_handler/src/uartHandler.c#L163)):

1. 채널 → 설정 블록 매핑 (**이걸 아는 유일한 곳**)
2. `de_pin` · `intf` · `protocol` · `opt` 를 설정에서 채움
3. `uart_deinit` → `uart_init` → 핀 배정 → baud → 포맷 → FIFO
4. RS-485 계열이면 DE 핀을 유휴(수신) 레벨로

### (B) 송수신

| 시그니처 | 용도 | 주의사항 |
|---|---|---|
| `int32_t serial_port_getc(SerialPort *port)` | 1바이트 수신 | 비었으면 `RET_NOK`. **이스케이프에 삼켜져도 `RET_NOK`** — 프로토콜 데이터로 취급하면 안 된다 |
| `int32_t serial_port_putc(SerialPort *port, uint16_t ch)` | 1바이트 송신 | 그 포트의 워드길이로 마스킹. **DE 제어 안 함** |
| `int32_t serial_port_puts(SerialPort *port, const uint8_t *buf, uint16_t bytes)` | 프레임 송신 | **DE 를 프레임 전체 동안 유지.** raw 출력이라 바이너리 프레임이 그대로 나간다 |

> **모든 수신은 `serial_port_getc()` 를 통과해야 한다.**
> `uart_getc()` 를 직접 부르면 AT 이스케이프 감시를 건너뛴다 ([3.6](#36-at-이스케이프)).

### (C) 방향(DE) 제어

프레임을 직접 쓸 때만 쓴다. `serial_port_puts()` 는 내부에서 이미 감싼다.

| 시그니처 | 용도 | 주의사항 |
|---|---|---|
| `void serial_port_tx_enable(SerialPort *port)` | 송신 직전 — 드라이버 켜기 | RS-485 → HIGH, REVERSE → LOW. **RS-422/TTL 은 no-op** |
| `void serial_port_tx_disable(SerialPort *port)` | 송신 직후 — 드라이버 끄기 | 내부에서 `uart_tx_wait_blocking()` 으로 **시프트 레지스터가 다 빠질 때까지 기다린다.** 이거 없이 DE 를 내리면 마지막 바이트가 잘린다 |

**반드시 짝으로 쓴다.**

### (D) 레거시 채널 0 래퍼

옛 코드(`seg.c`, `segcp.c`, `ConfigData.c`, `mbserial.c`)가 쓰던 무인자 API. 전부 **채널 0 고정**이다.

| 시그니처 | 대응 |
|---|---|
| `int32_t platform_uart_putc(uint16_t ch)` | `serial_port_putc(&g_serial_port[SEG_DATA0_CH], ch)` |
| `int32_t platform_uart_puts(uint8_t *buf, uint16_t bytes)` | `serial_port_puts(&g_serial_port[SEG_DATA0_CH], ...)` |
| `void DATA0_UART_Configuration(void)` | `serial_port_setup(&g_serial_port[SEG_DATA0_CH])` — ⚠ 현재 호출부 0곳 |

**새 코드에서는 쓰지 마라.** 포트를 못 고른다.

### (E) 링 버퍼 — 채널 인자를 받는다

`bufferHandler.h`. **포트당 하나씩 총 2개**, 각 `SEG_DATA_BUF_SIZE` = 4096 B.
시그니처는 업스트림 [W55RP20-S2E `2Port` 브랜치](https://github.com/WIZnet-ioNIC/W55RP20-S2E/tree/2Port)와 동일하게 맞췄다 — 채널이 **마지막 인자**다.

| 시그니처 |
|---|
| `void put_byte_to_data_buffer(uint8_t ch, int channel)` |
| `int32_t data_buffer_getc(int channel)` / `data_buffer_getc_nonblk(int channel)` |
| `int32_t data_buffer_gets(uint8_t *buf, uint16_t bytes, int channel)` |
| `int8_t is_data_buffer_empty(int channel)` / `is_data_buffer_full(int channel)` |
| `uint16_t get_data_buffer_usedsize(int channel)` / `freesize(int channel)` |
| `uint8_t *get_data_buffer_ptr(int channel)` |
| `void data_buffer_flush(int channel)` |

> `get_data_buffer_ptr()` 는 `segcp.c` 와 `spiHandler.c` 가 **SPI 전송용 스크래치 버퍼로 재활용**한다. 함부로 용도를 바꾸지 말 것.

### (F) 흐름 제어

| 시그니처 | 용도 | 주의사항 |
|---|---|---|
| `void check_uart_flow_control(uint8_t flow_ctrl)` | XON/XOFF 소프트 흐름제어 | ⚠ **포트 인자가 없다.** 내부가 채널 0 고정. 유일한 호출처가 `seg.c` 인데 그 태스크가 안 돈다 |
| `get_uart_cts_pin` / `set_uart_rts_pin_high` / `low` | GPIO 하드웨어 흐름제어 | `__USE_GPIO_HARDWARE_FLOWCONTROL__` 미정의 → **컴파일 제외** |

---

## 3.5 레시피

### 레시피 1 — 프레임 하나 보내기

**대부분은 이거면 끝난다.** DE 제어가 안에 들어 있다.

```c
SerialPort *port = &g_serial_port[SEG_DATA1_CH];   /* RS-485 포트 */
uint8_t frame[8] = { 0x01, 0x04, 0x00, 0x00, 0x00, 0x02, 0x71, 0xCB };

serial_port_puts(port, frame, sizeof(frame));
```

프레임을 직접 써야 할 때만 DE 를 손으로 감싼다 ([modbusMaster.c](port/app/platform_handler/src/modbusMaster.c) 방식):

```c
#ifdef __USE_UART_485_422__
    serial_port_tx_enable(port);
#endif
    for (int i = 0; i < 8; i++) {
        uart_putc_raw(port->uart, req[i]);
    }
    uart_tx_wait_blocking(port->uart);
#ifdef __USE_UART_485_422__
    serial_port_tx_disable(port);
#endif
```

| ☐ | 확인 |
|---|---|
| ☐ | `serial_port_init_all()` 이 이미 불렸는가 (`opt` 가 `NULL` 이 아닌가) |
| ☐ | `tx_enable` / `tx_disable` 이 짝을 이루는가 |
| ☐ | RS-422/TTL 에서는 둘 다 no-op 이라 그냥 둬도 된다 |

### 레시피 2 — 바이트 받기

```c
SerialPort *port = &g_serial_port[SEG_DATA1_CH];

while (uart_is_readable(port->uart)) {
    int32_t ch = serial_port_getc(port);
    if (ch == RET_NOK) {
        continue;                     /* 이스케이프 감시가 가져갔다 */
    }
    /* ch 를 프로토콜 데이터로 처리 */
}
```

폴링 방식이면 타임아웃과 양보를 잊지 말 것 ([modbusMaster.c `mb_recv()`](port/app/platform_handler/src/modbusMaster.c) 참고):

```c
TickType_t start = xTaskGetTickCount();
int got = 0;
while (got < want) {
    int32_t ch;
    while (got < want && (ch = serial_port_getc(port)) != RET_NOK) {
        buf[got++] = (uint8_t)ch;
    }
    if (got >= want) break;
    if ((xTaskGetTickCount() - start) >= pdMS_TO_TICKS(timeout_ms)) break;
    vTaskDelay(pdMS_TO_TICKS(2));     /* RX FIFO 32B 가 버텨준다 */
}
```

### 레시피 3 — 통신 포맷 바꾸기

설정을 고친 뒤 그 포트만 다시 세운다.

```c
DevConfig *cfg = get_DevConfig_pointer();
cfg->serial_option_485.baud_rate = baud_9600;
cfg->serial_option_485.parity    = parity_even;

serial_port_setup(&g_serial_port[SEG_DATA1_CH]);
```

> 웹에서 바꾼 설정은 **재부팅해야 적용된다.** 런타임에 다시 세우는 경로가 없다.

### 레시피 4 — DE 핀 바꾸기 (다른 보드에 올릴 때)

| 방법 | 어디 | 비고 |
|---|---|---|
| 설정으로 | `serial485_de_pin` (웹 "DE 핀") | 재부팅 후 적용. 0 이거나 >29 면 보드 기본값으로 폴백 |
| 보드 기본값 자체를 | [WIZnet_board.h](port/app/board/inc/WIZnet_board.h) 의 `RS485_UART_DE_PIN` / `DATA0_UART_RTS_PIN` | 테이블의 `de_pin_board` 가 여기서 온다 |

두 포트 모두 웹 "DE 핀" 칸에서 바꿀 수 있다. RS-422/485 를 골랐을 때만 그 행이 보인다.

---

## 3.6 AT 이스케이프

`serial_port_getc()` 는 **채널 0 에서만** 모드전환 트리거(`+++`)를 감시한다.

```
바이트 도착
   └─ serial_port_getc()
        ├─ 채널 0 → check_modeswitch_trigger(ch)
        │            └─ 시퀀스 일부면 삼키고 RET_NOK 반환
        └─ 채널 1 → 그대로 통과
```

| 항목 | 값 |
|---|---|
| 트리거 코드 | `+++` (`0x2B` × 3), `serial_command.serial_trigger[]` 에서 변경 가능 |
| 판정 조건 | 앞뒤로 **500 ms 침묵** (`DEFAULT_MODESWITCH_INTER_GAP`) |
| 타이밍 카운터 | `seg_timer_msec()` — 1 ms 타이머에서 돈다 |
| 성공 시 | `segcp_uart_sem` 기상 → `segcp_serial_task()` 가 AT 모드 진입 |

### 왜 채널 0 만 감시하나

`seg.c` 의 트리거 상태(`triggercode_idx`, `ch_tmp[3]`, `modeswitch_time`)가 **장비 전체에 1벌**이고,
실패한 시퀀스를 되돌리는 `restore_serial_data()` 가 **채널 0 에 고정**돼 있다.

두 포트가 동시에 감시하면 서로의 판정을 깨뜨리고, 채널 1 의 바이트가 채널 0 버퍼로 흘러간다.

### 프로토콜을 새로 만들 때

**수신 루프에서 `serial_port_getc()` 를 쓰기만 하면 자동으로 지원된다.** 별도로 할 일이 없다.
반대로 `uart_getc()` 를 직접 부르면 그 포트에서 AT 진입이 죽는다.

AT 모드 동안 채널 0 을 점유하는 프로토콜은 폴링을 멈춰야 한다:

```c
if ((port->channel == SEG_DATA0_CH) && (opmode == DEVICE_AT_MODE)) {
    vTaskDelay(pdMS_TO_TICKS(100));
    continue;
}
```

---


## 3.7 enum ↔ 테이블 대응표

정의: [uartHandler.h:46-102](port/app/platform_handler/inc/uartHandler.h#L46-L102)
문자열 테이블: [uartHandler.c:29-37](port/app/platform_handler/src/uartHandler.c#L29-L37)

### `enum baud` ↔ `baud_table[]`

**저장되는 값은 인덱스다.** `baud_table[serial_option.baud_rate]` 가 실제 보율.

| idx | enum | bps | | idx | enum | bps |
|---|---|---|---|---|---|---|
| 0 | `baud_300` | 300 | | 10 | `baud_38400` | 38400 |
| 1 | `baud_600` | 600 | | 11 | `baud_57600` | 57600 |
| 2 | `baud_1200` | 1200 | | 12 | `baud_115200` | 115200 |
| 3 | `baud_1800` | 1800 | | 13 | `baud_230400` | 230400 |
| 4 | `baud_2400` | 2400 | | 14 | `baud_460800` | 460800 |
| 5 | `baud_4800` | 4800 | | 15 | `baud_921600` | 921600 |
| 6 | `baud_9600` | 9600 | | 16 | `baud_1M` | 1000000 |
| 7 | `baud_14400` | 14400 | | 17 | `baud_2M` | 2000000 |
| 8 | `baud_19200` | 19200 | | 18 | `baud_4M` | 4000000 |
| 9 | `baud_28800` | 28800 | | 19 | `baud_8M` | 8000000 |

> `W232N` 보드만 테이블이 14개로 짧다 ([uartHandler.c:28-32](port/app/platform_handler/src/uartHandler.c#L28-L32)).
> 범위 검사 코드가 `sizeof(baud_table)/sizeof(...)` 를 쓰는 곳과 `< 20` 하드코딩인 곳이 섞여 있다
> (`sensorUart.c:104`, `modbusMaster.c:52`). 보드를 바꾸면 확인할 것.

### 나머지 enum

| enum | 값 | 배열 | 웹 select 값 | POST 검증 상한 |
|---|---|---|---|---|
| `word_len` | 0=`word_len7`(7), 1=`word_len8`(8), 2=`word_len9`(9) | `word_len_table[]` | 0/1/2 | 2 |
| `stop_bit` | 0=`stop_bit1`(1), 1=`stop_bit2`(2) | `stop_bit_table[]` | (웹 미노출) | — |
| `parity` | 0=none, 1=odd, 2=even, **3=space, 4=mark** | `parity_table[]` = `"N","ODD","EVEN","SPACE","MARK"` | 0~4 | 4 |
| `flow_ctrl` | 0=none, 1=xon_xoff, 2=rts_cts, 3=rtsonly, 4=reverserts | `flow_ctrl_table[]` | 0/1/2 만 노출 | 4 |
| `protocol` | 0=none(S/T/R), 1=`modbus_rtu`, 2=`modbus_ascii`, 3=`sec_ups`, 4=`protocol_custom` | — | **0~1** | 1 |
| `UART_IF_*` | 0=`RS232_TTL`, 1=`RS422`, 2=`RS485`, 3=`RS485_REVERSE`, 4=`SPI_IF_SLAVE` | `uart_if_table[]` | 0~3 | 3 |

**enum 에 이름이 있는 것과 고를 수 있는 것은 다르다.** 드롭다운과 POST 검증은 전부 [serialProtocol.c](port/app/platform_handler/src/serialProtocol.c) 의 표에서 나오고, 그 표는 **구현된 것만** 담는다 — 현재 `none`(S/T/R) 과 `modbus_rtu` 둘이다.

`modbus_ascii`(2), `sec_ups`(3), `protocol_custom`(4) 는 enum 에만 있고 표에는 없다. 구현이 없는 항목을 드롭다운에 두면 **고르면 저장까지 되는데 아무 일도 안 일어나기 때문**이다. 구현하면 표에 행을 추가한다 — [4.4](#44-새-시리얼-프로토콜-추가) 참조.

### 같은 필드에 두 개의 enum이 있다

`serial_option.protocol` 은 두 헤더에서 각각 이름이 붙어 있다. **값은 0/1/2 가 일치**하므로 실사용에 문제는 없지만, 새 값을 추가할 땐 양쪽을 다 봐야 한다.

| 헤더 | 이름 |
|---|---|
| [uartHandler.h:97-102](port/app/platform_handler/inc/uartHandler.h#L97-L102) | `protocol_none`, `modbus_rtu`, `modbus_ascii`, `sec_ups` |
| [seg.h:59-63](port/app/serial_to_ethernet/inc/seg.h#L59-L63) | `SEG_SERIAL_PROTOCOL_NONE`, `SEG_SERIAL_MODBUS_RTU`, `SEG_SERIAL_MODBUS_ASCII` |

### 검증·표시가 흩어져 있는 곳 (값 하나 추가하면 전부 봐야 함)

| 위치 | 무엇 |
|---|---|
| [uartHandler.h](port/app/platform_handler/inc/uartHandler.h) | enum 정의 |
| [uartHandler.c:29-37](port/app/platform_handler/src/uartHandler.c#L29-L37) | 문자열/값 테이블 |
| [Web_page.html](port/app/html_file/Web_page.html) | `<option value=…>` 목록 |
| [httpHandler.c:480-524](port/app/platform_handler/src/httpHandler.c#L480-L524) | `cfg_num_table()` — POST 검증 범위와 GET 표시 기본값 |
| [ConfigData.c](port/app/configuration/src/ConfigData.c) | 팩토리 기본값 |
| [segcp.c](port/app/configuration/src/segcp.c) | 설정툴(SEGCP) 프로토콜 표시/파싱 |

---

# 4장 확장 레시피

## 4.1 DevConfig EXT 에 설정 필드 추가

**기존 장비의 설정을 날리지 않으면서** 저장되는 값을 하나 추가하는 절차다.

### 지켜야 할 두 가지 규칙

> **규칙 1 — `sizeof(DevConfig)` 를 절대 바꾸지 마라.**
> 새 필드 N 바이트를 넣었으면 `DEVCONFIG_RESERVED_EXT_SIZE` 를 **정확히 N 만큼 줄여라.**
> 안 그러면 `packet_size != sizeof(DevConfig)` 판정에 걸려 EXT 가 리셋된다.

> **규칙 2 — 새 필드는 값이 0 일 때 "기존 동작"이어야 한다.**
> 기존 장비의 `reserved_ext[]` 는 전부 0 이다. 필드가 그 자리에 생기면 초기값이 0 이 된다.
> `0 = 활성` 같은 의미를 주면 업데이트한 순간 동작이 바뀐다.
> 실제 예: `serial_de_pin` 은 `0 = 미설정 → 보드 기본값`, `snmp_perm` 은 `0 = R/W(기존 동작)`.

### 체크리스트

| # | 파일 | 할 일 |
|---|---|---|
| 1 | [ConfigData.h](port/app/configuration/inc/ConfigData.h) | `reserved_ext[]` **바로 앞**에 필드 추가. 위치는 항상 끝(reserved 직전) |
| 2 | [ConfigData.h:217](port/app/configuration/inc/ConfigData.h#L217) | `DEVCONFIG_RESERVED_EXT_SIZE` 를 추가 바이트만큼 감소 |
| 3 | [ConfigData.h:210-216](port/app/configuration/inc/ConfigData.h#L210-L216) | 위 계산식 주석에 뺄셈 항 추가 (다음 사람을 위해) |
| 4 | [ConfigData.c: `set_DevConfig_ext_to_factory_value()`](port/app/configuration/src/ConfigData.c#L220) | 팩토리 기본값 대입 |
| 5 | (필요시) [ConfigData.h:208](port/app/configuration/inc/ConfigData.h#L208) | **0 이 기존 동작과 다르면** `DEVCONFIG_EXT_VERSION` 을 +1 하고 `ext_version history` 주석에 한 줄 추가 → 기존 장비의 EXT 가 팩토리 값으로 재초기화된다 |
| 6 | 웹에 노출한다면 | [4.2](#42-웹-설정-항목-추가) 로 진행 |

### 검증

```bash
# 반드시 1596 이 나와야 한다 (필드 추가 전후 동일)
```
```c
/* ConfigData.h 맨 아래에 임시로 넣고 빌드해보면 즉시 잡힌다 */
_Static_assert(sizeof(DevConfig) == 1596, "DevConfig layout changed!");
```

| ☐ | 항목 |
|---|---|
| ☐ | `sizeof(DevConfig)` 가 1596 그대로인가 |
| ☐ | `reserved_ext` 크기를 정확히 줄였는가 |
| ☐ | 새 필드가 `reserved_ext` **앞**에 있는가 (뒤에 두면 기존 장비에서 0 이 아닌 쓰레기를 읽는다) |
| ☐ | 값 0 이 기존 동작인가? 아니면 `ext_version` 을 올렸는가 |
| ☐ | LEGACY 구역은 **손대지 않았는가** (설정툴/부트로더가 오프셋에 의존) |

### 실제 사례 — `serial_de_pin` 추가

```diff
-#define DEVCONFIG_RESERVED_EXT_SIZE    52
+#define DEVCONFIG_RESERVED_EXT_SIZE    51

     uint8_t  serial485_de_pin;
+    /*  uart1 RS-485 DE / nRE GPIO number. Same 0-means-unset rule as above;
+        0 => board default DATA0_UART_RTS_PIN. */
+    uint8_t  serial_de_pin;
```
```diff
     dev_config.serial485_de_pin = RS485_UART_DE_PIN;
+    dev_config.serial_de_pin = DATA0_UART_RTS_PIN;
```
`ext_version` 을 안 올린 이유: **0 이면 보드 기본값**이라 기존 장비 동작이 그대로다.

---

## 4.2 웹 설정 항목 추가

**숫자 설정**이면 3곳이면 끝난다. 문자열·IP 는 여전히 4곳이다.

```
 ① HTML 입력 필드  ──►  ② Web_page.h 재생성
                             │
 ③ cfg_num_table() 에 CFG_NUM 한 줄
      └─► GET 직렬화와 POST 파싱·검증이 그 한 줄에서 같이 나온다
```

예전에는 GET 의 `snprintf` 체인과 POST 의 `sfields[]` 에 **따로** 적어야 했다.
한쪽만 적으면 저장은 되는데 화면에 안 뜨거나, 화면엔 뜨는데 저장이 안 됐고,
**양쪽 다 컴파일은 통과했다.** 지금은 한 줄이라 어긋날 수가 없다.

### 체크리스트 — 숫자 설정

| # | 파일 | 할 일 |
|---|---|---|
| 1 | [Web_page.html](port/app/html_file/Web_page.html) | `<input id="…">` 또는 `<select id="…">` 추가. **id 가 JSON 키가 된다** |
| 2 | [Web_page.html:790-794](port/app/html_file/Web_page.html#L790-L794) | 시리얼 항목이면 `CFG_SERIAL[]`, IP 항목이면 `CFG_IPS[]` 에 id 추가 → load/save 자동 처리 |
| 3 | (그 외 항목) | [`loadConfig()`:810](port/app/html_file/Web_page.html#L810) 과 [`saveConfig()`:853](port/app/html_file/Web_page.html#L853) 에 개별 라인 추가 |
| 4 | **재생성** | `py -3 tools/html_to_c_header.py` (프로젝트 루트에서) |
| 5 | [httpHandler.c: `cfg_num_table()`:480](port/app/platform_handler/src/httpHandler.c#L480) | `CFG_NUM` 한 줄 추가 |
| 6 | [httpHandler.c:471](port/app/platform_handler/src/httpHandler.c#L471) | `CFG_NUM_CNT` 를 **행 수와 함께** +1 |
| 7 | [4.1](#41-devconfig-ext-에-설정-필드-추가) | 플래시에 저장되는 값이면 DevConfig 필드부터 |

### ①,② HTML → Web_page.h

`Web_page.html` 을 고치면 **반드시** 헤더를 재생성해야 한다.

```bash
py -3 tools/html_to_c_header.py
```

| 사실 | 내용 |
|---|---|
| 출력 | `port/app/html_file/Web_page.h` — `static const unsigned char _acWeb_page[…]` |
| CMake 자동화 | [CMakeLists.txt:150-159](CMakeLists.txt#L150-L159) 의 `html_to_c_header` 타겟이 `Boot`, `App_linker` 빌드 전에 자동 실행 |
| ⚠ 함정 | 의존성이 걸린 건 `App_linker` 이고 **`App` 타겟에는 안 걸려 있다.** `App` 만 빌드하면 재생성이 안 된다 |
| 커밋 | `Web_page.h` 는 **git 에 추적되는 생성물**이다. HTML 과 같이 커밋해야 한다 |

### ③ `CFG_NUM` 한 줄

```c
/* httpHandler.c 의 cfg_num_table() 안 */
CFG_ADD(CFG_NUM("my_field", my_field, 0, 7, 0));
/*               │          │         │  │  └ GET 이 범위 밖 값을 만났을 때 보여줄 값
                 │          │         │  └─── POST 허용 최댓값
                 │          │         └────── POST 허용 최솟값
                 │          └──────────────── DevConfig 멤버 이름
                 └─────────────────────────── JSON 키 = HTML 의 id           */
```

| 인자 | 뜻 |
|---|---|
| `key` | JSON 키. HTML 의 `id` 와 **글자까지 같아야** 폼이 채워진다 |
| `f` | `DevConfig` 멤버 이름. `width` 는 `sizeof` 로 자동으로 채워진다 — 손으로 적지 않는다 |
| `mn`, `mx` | POST 가 받아주는 범위. 벗어난 값은 **저장하지 않고 버린다** |
| `dv` | GET 이 범위 밖 저장값을 만났을 때 대신 내보낼 값 |

**0 이 "미설정" 인 필드**는 `CFG_NUM_Z` 를 쓴다. POST 가 0 도 받아서 보드 기본값으로
되돌릴 수 있고, GET 은 0 대신 `dv` 를 보여준다.

```c
CFG_ADD(CFG_NUM_Z("serial_de", serial_de_pin, 1, 29, DATA0_UART_RTS_PIN));
```

**음수를 받는 필드**는 폭·부호·범위를 직접 적는 `CFG_RAW` 를 쓴다. `DevConfig` 멤버
이름으로 가리킬 수 없는 것 — 배열 원소나 바이트 쌍의 한쪽 — 도 이쪽이다.

```c
/*        key          가리킬 곳                     폭 부호 zero_ok  min     max  def */
CFG_ADD(CFG_RAW("lim0_lo", &conf->value_limit[0].lo[0], 2,  1,   0,  -32768, 32767, 0));
```

`CFG_NUM` 은 `sizeof` 로 폭을 채워주지만 `CFG_RAW` 는 손으로 적는 값이라 **필드와
어긋날 수 있다.** 가리키는 필드 바로 옆에 두고, 꼭 필요할 때만 쓴다.

> ⚠ **행을 추가하면 `CFG_NUM_CNT` 도 같이 올려라.**
> 지금은 `18 + 3 × VALUE_LIMIT_CNT + 2` 로, 이름 붙은 18행 + 값 컬럼당 3행(사용/하한/상한)
> + 트랩 주기 2행이다. 안 올리면 넘친 행이 조용히 빠지고
> `cfg_num_table: N rows, only M fit` 로그가 뜬다. 배열 밖으로 쓰지는 않는다 — `CFG_ADD` 가 막는다.

### ④ 숫자가 아닌 값 — 손으로 붙인다

문자열·IPv4·의미가 뒤집힌 값(`dhcp_use`, `trap_disable`)은 테이블에 안 들어간다.
[`https_send_config_json()`:549](port/app/platform_handler/src/httpHandler.c#L549) 과
[`https_handle_config_post()`:662](port/app/platform_handler/src/httpHandler.c#L662) 양쪽에 직접 적어야 한다.

**패턴 A — 문자열.** [`parse_json_str()`:641](port/app/platform_handler/src/httpHandler.c#L641) 사용.

```c
if (parse_json_str(actual_body, "\"my_str\":", conf->my_str, sizeof(conf->my_str))) {
    changed = 1;
}
```

**패턴 B — IPv4.** [`netf[]`:695](port/app/platform_handler/src/httpHandler.c#L695) 또는
[`web_ip%d` 루프:727](port/app/platform_handler/src/httpHandler.c#L727) 패턴을 복사.

> ⚠ **packed 구조체 멤버의 주소를 `uint16_t *` 로 잡아 쓰지 마라.**
> `DevConfig` 는 `packed` 라서 `uint16_t` 멤버가 홀수 오프셋에 앉는다 — `https_port` 가 그렇다
> (9바이트 `serial_option_485` 바로 뒤). 비정렬 `LDRH`/`STRH` 가 나와 Cortex-M0+ 에서 **HardFault** 한다.
> 멤버에 직접 대입(`conf->필드 = 값;`)하거나, 테이블처럼 **바이트 단위로 쪼개라**
> ([`cfg_num_get()`/`cfg_num_set()`:533](port/app/platform_handler/src/httpHandler.c#L533) 참조).

### 버퍼 크기 표 (넘치면 조용히 잘린다)

| 버퍼 | 크기 | 위치 | 넘치면 |
|---|---|---|---|
| `https_rx_buf[]` | 2048 | [httpHandler.c:26](port/app/platform_handler/src/httpHandler.c#L26) | 요청 헤더+본문 잘림 |
| `post_extra_buf[]` | 1536 | [httpHandler.c:666](port/app/platform_handler/src/httpHandler.c#L666) | **뒤쪽 필드가 조용히 누락, UI 는 "저장 완료"** ([5.3](#53-post-본문이-버퍼보다-길면-뒤쪽-필드가-조용히-사라진다)) |
| `body[]` (config GET) | 1536 | [httpHandler.c:560](port/app/platform_handler/src/httpHandler.c#L560) | 현재 최악 약 970 B, 여유 약 565 B |
| `chunk[]` (sensor GET) | 512 | [httpHandler.c:320](port/app/platform_handler/src/httpHandler.c#L320) | 해당 device 항목만 건너뜀 |

`body[]` 의 여유는 **`cfg_num_table()` 의 행과 `g_serial_protocol[]` 의 행이 같이 나눠 쓴다.**
설정을 늘리든 프로토콜을 늘리든 같은 565 B 를 깎아 먹는다.

### 최종 확인

| ☐ | 항목 |
|---|---|
| ☐ | `py -3 tools/html_to_c_header.py` 실행했는가 |
| ☐ | `Web_page.h` 를 커밋에 포함했는가 |
| ☐ | HTML 의 `id` 와 `CFG_NUM` 의 `key` 가 **글자까지 같은가** (`loadConfig()` 가 GET 응답으로 폼을 채운다) |
| ☐ | `CFG_NUM_CNT` 를 행 수와 함께 올렸는가 |
| ☐ | `mn`/`mx` 가 enum 최댓값과 맞는가 |
| ☐ | `body[]` 와 `post_extra_buf` 에 여유가 있는가 |
| ☐ | 재부팅이 필요한 항목이면 HTML 섹션 제목에 "(재부팅 후 적용)" 을 넣었는가 |

---

## 4.3 SNMP OID 추가 / 트랩 추가

> ⚠ SNMP 코드는 **서브모듈(`libraries/ioLibrary_Driver`) 안**에 있다.
> 고치면 반드시 `ioLibrary_snmp_patch.patch` 를 재생성해야 한다 ([6.2](#62-서브모듈-패치-워크플로)).

### 4.3.1 먼저 확인 — 값 컬럼만 늘리면 되는 경우가 대부분이다

장치마다 노출하는 값(온도/습도/알람)을 **하나 더 늘리는 것**이라면 SNMP 코드를 건드릴 필요가 없다.
테이블은 `DEVICE_VALUE_COLS` 에서 자동 생성된다.

| # | 파일 | 할 일 |
|---|---|---|
| 1 | [sensor.h:32](port/app/platform_handler/inc/sensor.h#L32) | `DEVICE_VALUE_COLS` 를 3 → 4 |
| 2 | [sensor.c:11-15](port/app/platform_handler/src/sensor.c#L11-L15) | `g_value_columns[]` 에 `{ "Pressure", "hPa", -1 }` 추가 (배열 길이 = `DEVICE_VALUE_COLS`) |
| 3 | — | SNMP 테이블·웹 표·웹 임계값 행·S/T/R 컬럼 수가 **자동으로** 따라온다 |

⚠ **하지만 자동으로 따라오지 않는 것이 둘 있다.**

| 따라오는 것 | 어떻게 |
|---|---|
| **Modbus 요청 프레임** | `MODBUS_REG_COUNT` = `DEVICE_VALUE_COLS` 라서 **요청 수량이 3 → 4 로 바뀐다.** 응답 길이도 11 → 13 바이트. 레지스터가 3개뿐인 슬레이브는 그 순간부터 예외 응답을 보내고 폴링이 끊긴다. 센서가 못 따라오면 컬럼을 늘리지 말거나, 이 상수를 `DEVICE_VALUE_COLS` 에서 떼어내라 |
| **임계값 저장 칸** | `VALUE_LIMIT_CNT` 는 4 로 고정이다(플래시 레이아웃 고정용). 컬럼이 그보다 많아지면 **넘는 컬럼은 감시되지 않는다** — 조용히. 늘리려면 `reserved_ext` 에서 5 B ×(추가분) 을 더 떼어내야 한다 ([4.1](#41-devconfig-ext-에-설정-필드-추가)) |

제약:

| 제약 | 값 | 이유 |
|---|---|---|
| `DEVICE_COUNT` ≤ 127 | 현재 64 | 셀 OID 서브식별자가 1바이트를 유지해야 함 |
| `2 + DEVICE_VALUE_COLS` ≤ 127 | 현재 5 | 〃 |
| `DEVICE_VALUE_COLS` ≤ 8 | 현재 3 | 임계값 감시 상태가 디바이스당 1바이트 비트마스크 (`s_limit_out[]`) |
| `1 ≤ MODBUS_REG_COUNT ≤ 125` | 현재 3 | 요청 수량·응답 바이트 수가 각각 1바이트. `_Static_assert` 로 막아둠 |
| `snmpData[]` 크기 | `7 + (2+COLS)×ROWS` | 컬럼 하나 늘 때마다 64 엔트리 증가 = **RAM 약 +5.6 KB** |

> RAM 여유를 확인하라. 힙은 96 KB 이고 `snmpData[]` 는 정적(.bss) 이다.

### 4.3.2 새 OID 서브트리를 추가하는 경우

| # | 파일 | 할 일 |
|---|---|---|
| 1 | [snmp_custom.c: `initTable()`:92](libraries/ioLibrary_Driver/Internet/SNMP/snmp_custom.c#L92) | 엔트리 채우기 (`oidlen`, `oid[]`, `dataType`, `dataLen`, `u.*`) |
| 2 | [snmp_custom.c:47-48](libraries/ioLibrary_Driver/Internet/SNMP/snmp_custom.c#L47-L48) | `snmpData[]` 배열 크기와 `maxData` 를 함께 늘림 |
| 3 | **OID 순서** | 새 엔트리가 **오름차순 위치**에 들어가야 한다 ([5.6](#56-snmpdata-는-oid-오름차순이어야-한다)) |
| 4 | `oidlen` | **`MAX_OID = 12` 를 넘으면 안 된다** ([snmp.h:17](libraries/ioLibrary_Driver/Internet/SNMP/snmp.h#L17)). 여유 없음 |
| 5 | 값 동기화 | 동적 값이면 `snmp_custom_refresh()`(:131) 에 갱신 코드 추가, 또는 `getfunction` 콜백 등록 |
| 6 | 패치 재생성 | [6.2](#62-서브모듈-패치-워크플로) |

`dataEntryType` 필드 ([snmp.h:99-110](libraries/ioLibrary_Driver/Internet/SNMP/snmp.h#L99-L110)):

| 필드 | 의미 |
|---|---|
| `oidlen` / `oid[12]` | BER 인코딩된 OID (첫 바이트 `0x2b` = `1.3`) |
| `dataType` | `SNMPDTYPE_INTEGER`(0x02), `_OCTET_STRING`(0x04), `_OBJ_ID`(0x06), `_TIME_TICKS`(0x43) … |
| `dataLen` | INTEGER 는 4, OCTET STRING 은 실제 길이 (최대 `MAX_STRING`=64) |
| `u.intval` / `u.octetstring[64]` | 값 |
| `getfunction(void*, uint8_t*)` | 읽을 때 호출 (예: `currentUptime`) |
| `setfunction(int32_t)` | SET 요청 처리. **NULL 이면 읽기 전용** |

Enterprise 번호(22210)가 박혀 있는 곳 — 바꾸려면 **전부** 고쳐야 한다:

| 위치 | 내용 |
|---|---|
| [snmp_custom.c:59-60](libraries/ioLibrary_Driver/Internet/SNMP/snmp_custom.c#L59-L60) | `ENTRY_OID_PREFIX[10]` — deviceEntry |
| [snmp_custom.c:63-64](libraries/ioLibrary_Driver/Internet/SNMP/snmp_custom.c#L63-L64) | `NOTIFY_OID[10]` — 트랩 |
| [snmp_custom.c:97-98](libraries/ioLibrary_Driver/Internet/SNMP/snmp_custom.c#L97-L98) | `sysObjectID` — **이스케이프 문자열** `"\x2b…\x81\xad\x42\x01\x00"` |
| [snmp_custom.c:198-199](libraries/ioLibrary_Driver/Internet/SNMP/snmp_custom.c#L198-L199) | `initial_Trap()` 의 두 OID |

> 22210 은 BER 3바이트(`81 AD 42`)로 인코딩된다. **다른 번호로 바꾸면 인코딩 길이가 달라져 `oidlen` 이 전부 틀어질 수 있다.**

### 4.3.3 트랩

**대부분의 경우 할 일이 없다.** 값을 뱅크에 쓰기만 하면 된다.

```
[임계값 경로 — 기본]

  아무 프로토콜 ──► device_bank_setValue()        SNMP 를 모른다
                          │
                     device bank
                          │
  snmp_agent_task ──► snmp_limit_scan()           trap_scan_sec 마다
                          ├─ 컬럼별 상/하한과 대조
                          └─ 방금 벗어난 칸만 ──► snmp_notify_cell(dev, col)
                                                      │  ring queue 32칸
                      snmp_flush_traps() ──► snmp_custom_sendValueTrap()
```

임계값은 **값 컬럼마다 하나**다. 디바이스마다가 아니다 — 온도 컬럼에 상한을 걸면
온도를 보고하는 모든 디바이스에 걸린다. 지금 것도, 내년에 붙는 것도.

| 설정 | 저장 위치 | 뜻 |
|---|---|---|
| 하한 / 상한 | `value_limit[col].lo` / `.hi` | **원시 단위.** 웹이 `g_value_columns[].scale` 로 환산해서 보여준다 |
| 사용 여부 | `value_limit[col].use` | `VALUE_LIMIT_USE_LO` / `_HI` 비트. 둘 다 0 이면 그 컬럼은 감시 안 함 |
| 검사 주기 | `trap_scan_sec` | 뱅크를 훑는 간격(초). 0 이면 `TRAP_SCAN_SEC_DEFAULT` |
| 재알림 | `trap_repeat_sec` | 계속 벗어나 있을 때 다시 알리는 간격(초). **0 이면 벗어나는 순간 한 번만** |

동작 규칙:

| | |
|---|---|
| 벗어나는 순간 | 그 칸 하나에 트랩 1발 |
| 계속 벗어나 있으면 | `trap_repeat_sec` 마다 다시. 0 이면 안 보냄 |
| **정상 복귀** | **조용하다.** 복귀 트랩은 없다 — 비트만 지워서 다음 크로싱에 대비한다 |
| 감시 상태 | `s_limit_out[]` 에 디바이스당 1바이트, 컬럼당 1비트 |

> 아날로그 값에 `trap_repeat_sec` 를 짧게 두면 트랩이 쏟아진다. 큐는 32칸이고
> 넘치면 조용히 버린다. 온도처럼 계속 흔들리는 값이면 수십 초 단위로 잡아라.

#### 직접 쏘고 싶을 때

임계값과 무관하게 "지금 이 일이 일어났다" 를 알려야 하는 프로토콜만 쓴다.
트리에서는 S/T/R 의 `T` 명령 하나뿐이다 — 상대가 알려준 사건이라 스윕이 알아채기를
기다리면 알려준 의미가 없어진다.

```c
snmp_notify_cell(dev, col);   /* 값 하나 */
snmp_notify_device(dev);      /* 그 디바이스의 모든 값 컬럼 */
```

둘 다 큐에 넣기만 하므로 **어느 태스크에서 불러도 안전하다.**

#### 새로운 종류의 트랩

값 셀이 아닌 것을 알려야 하면 [`snmp_custom_sendValueTrap()`](libraries/ioLibrary_Driver/Internet/SNMP/snmp_custom.c#L159) 을 본떠 함수를 만든다.

주의:

| ☐ | 항목 |
|---|---|
| ☐ | 트랩 송신은 **`snmp_agent_task` 안에서만** — 트랩 소켓(소켓 0)을 공유하므로 |
| ☐ | 큐가 꽉 차면 조용히 버린다 (32칸) |
| ☐ | 큐는 **셀 단위**다. 디바이스 단위였을 때는 접점 하나가 바뀌어도 컬럼 수만큼 나갔다 |
| ☐ | 트랩 목적지는 `snmp_option.trap_ip[4]`, `0.0.0.0` 슬롯은 건너뜀 |
| ☐ | `trap_disable` 이 1 이면 큐만 비우고 안 보낸다 |
| ☐ | 트랩 community 는 `snmp_get_trap_community()` 로 얻는다 (에이전트 community 와 별개) |

### 4.3.4 런타임 SNMP 설정 API

앱 → SNMP 코어로 값을 주입하는 함수들. 전부 `snmpd_run()` 전에 호출해야 한다.
호출처: [`snmp_agent_init()` snmpHandler.c:43](port/app/platform_handler/src/snmpHandler.c#L43)

| 함수 | 인자 | 기본 동작 |
|---|---|---|
| `snmp_set_agent_port(uint16_t)` | 0 이면 무시 | 161 |
| `snmp_set_allowed_ips(const uint8_t[4][4])` | 전부 0 → 모두 허용 | 모두 허용 |
| `snmp_set_community(const char*)` | `""`/NULL → `"public"` | `"public"` |
| `snmp_set_permission(uint8_t)` | 0=R/W, 1=R/O, 2=차단 | R/W |
| `snmp_set_trap_community(const char*)` | 〃 | `"public"` |
| `snmp_get_trap_community(void)` | — | — |

설정 변경 후 **재적용**: 웹 POST 가 `snmp_request_reinit()` 을 부르면 소켓이 닫히고 다음 사이클에 `snmp_agent_init()` 이 다시 돈다 ([httpHandler.c:813](port/app/platform_handler/src/httpHandler.c#L813)).

---

## 4.4 새 시리얼 프로토콜 추가

**이미 만들어진 예가 하나 있다 — S/T/R 이다.** [sensorUart.c](port/app/platform_handler/src/sensorUart.c) 가 그것이고, 이 펌웨어에 붙은 커스텀 프로토콜은 지금 이 하나뿐이다. 직렬로 들어온 한 줄을 파싱해서 device bank 에 넣고 답을 돌려주는, 프로토콜이 해야 할 일을 전부 갖춘 최소 예제다. **새로 만들 때 이것과 같은 모양으로 하면 된다.**

빈 골격에서 시작하고 싶으면 [protoTemplate.c](port/app/platform_handler/src/protoTemplate.c) 를 복사한다. 빌드되는 상태로 들어 있고 포트 세팅·DE 제어·AT 모드 양보·뱅크 등록이 되어 있으며, 채울 곳은 `protoTemplate_poll()` 안의 TODO 두 개뿐이다.

> 두 파일 모두 웹 Mode 드롭다운에는 나오지 않는다. 드롭다운은
> [serialProtocol.c](port/app/platform_handler/src/serialProtocol.c) 의 표 그대로이고,
> 그 표는 **장비를 쓰는 사람이 고를 수 있는 것**만 담는다. 펌웨어를 고치는 사람에게만
> 의미가 있는 항목은 넣지 않는다. 만든 프로토콜은 표에 **자기 이름으로** 추가한다.

### 두 가지 구현 스타일

| 스타일 | 언제 | 예시 | 특징 |
|---|---|---|---|
| **동기 폴러** | 마스터로서 우리가 먼저 묻는다 | `modbusMaster.c` | RX ISR 없음. 태스크가 직접 FIFO 를 훑음. 단순함 |
| **ISR + 파서 태스크** | 상대가 언제든 보낸다 | `sensorUart.c` | RX ISR → 링버퍼 → 세마포어 → 태스크 |

새 프로토콜은 대개 **동기 폴러**가 맞다.

### 체크리스트

프로토콜이 존재한다는 사실은 **[serialProtocol.c](port/app/platform_handler/src/serialProtocol.c) 의 `g_serial_protocol[]` 한 곳**에만 적는다.
태스크 생성·소유권 판정·웹 검증 상한·Mode 드롭다운이 전부 이 표에서 나온다.

> ⚠ **행을 추가하기 전에 상한 두 개를 먼저 풀어라.**
> [uartHandler.c:191](port/app/platform_handler/src/uartHandler.c#L191) 은 `sec_ups`(3),
> [segcp.c:894](port/app/configuration/src/segcp.c#L894) 는 `modbus_ascii`(2) 를 상한으로
> 갖고 있다. 드라이버는 부팅 시 그 위의 번호를 `protocol_none` 으로 덮어쓰므로,
> 4번 이상으로 추가하면 구현을 다 해도 태스크가 뜨지 않는다.
> 둘을 `serial_protocol_max_id()` 로 바꾸면 된다.

| # | 파일 | 할 일 |
|---|---|---|
| 1 | [uartHandler.h](port/app/platform_handler/inc/uartHandler.h) `enum protocol` | 값 추가. `sec_ups`(3) 와 `protocol_custom`(4) 는 이미 있다 |
| 2 | `platform_handler/{inc,src}/myproto.[ch]` | `protoTemplate.[ch]` 를 복사해서 이름만 바꾼다 |
| 3 | [port/app/CMakeLists.txt](port/app/CMakeLists.txt) | `APP_PLATFORM_FILES` 의 `target_sources` 에 `.c` 추가 ([4.6](#46-소스-파일-추가-cmake-등록)) |
| 4 | [serialProtocol.c](port/app/platform_handler/src/serialProtocol.c) `g_serial_protocol[]` | **한 줄 추가** — id, 표시 이름, 태스크, 스택, 우선순위 |
| 5 | — | 값을 `device_setValue()` 로 device bank 에 쓴다 → SNMP·웹은 그대로 동작 |

```c
/* 4번은 이 한 줄이 전부다 */
{ my_protocol, "My Protocol", myproto_task, 1024, 9 },
```

**손대지 않는 것**: `App.c`, `sensorUart.c`, `httpHandler.c`, `Web_page.html`, `Web_page.h`.
태스크 이름은 `"<이름>_ch<채널>"` 로 자동 생성되고, Mode 드롭다운은 `/api/config` 가 내려주는
`protocols` 배열로 페이지가 직접 만든다.

| 표의 항목 | 뜻 |
|---|---|
| `task` 가 `NULL` | 전용 핸들러 없음 → sensorUart 가 그 포트를 잡고 S/T/R 을 돌린다 |
| `stack` | 워드 단위 (`xTaskCreate` 와 같은 단위) |
| `priority` | **31 이하** ([5.9](#59-configassert-는-릴리스에서-무효다--태스크-우선순위가-조용히-잘린다)) |

### 골격 코드

```c
/* myproto.c */
#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"
#include "uartHandler.h"   /* SerialPort, serial_port_* */
#include "sensor.h"        /* device_bank_reserve, device_setValue */
#include "WIZ5XXSR-RP_Debug.h"

#define MYPROTO_DEVICE_CNT  1      /* 이 프로토콜이 쓸 device bank 행 수 */

void myproto_init(SerialPort *port) {
    serial_port_setup(port);            /* baud/포맷/핀/DE 전부 */
    serial_port_hw_flow_disable(port);  /* 이 프로토콜은 RTS/CTS 안 씀 */

    PRT_INFO("myproto: ready (ch%d)\r\n", port->channel);
}

void myproto_task(void *argument) {
    SerialPort *port = (SerialPort *)argument;
    uint8_t src;

    if (port == NULL) {
        vTaskDelete(NULL);
        return;
    }

    /*  행을 고르지 말고 받아라. 다른 프로토콜이 뭘 썼는지 몰라도 겹치지 않는다.
        src 는 "누가 발행하는가" — 시리얼 프로토콜이면 그 포트의 채널 번호다.
        웹이 이 값으로 포트별 표를 나눈다. */
    src = (uint8_t)port->channel;
    if (device_bank_reserve(MYPROTO_DEVICE_CNT, src) < 0) {
        vTaskDelete(NULL);              /* bank 에 자리가 없다 */
        return;
    }

    myproto_init(port);

    /*  받은 블록 안의 순번으로만 쓴다. 행 번호는 뱅크가 기억한다. */
    device_bank_assign(src, 0, "UPS-1");     /* 응답 전에도 화면에 보이도록 */

    while (1) {
        /*  커맨드 모드가 그 포트를 가져간 동안은 폴링을 멈춘다. 안 그러면
            설정 처리기와 같은 FIFO 를 놓고 싸운다.
            포트 번호를 따질 필요는 없다 — 헬퍼가 판단한다. */
        if (serial_port_in_command_mode(port)) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        serial_port_flush_rx(port);     /* 지난 교신의 잔여 바이트 버리기 */

        /* 1) 요청 송신 — DE 는 serial_port_puts() 가 알아서 감싼다 */
        serial_port_puts(port, req, sizeof(req));

        /* 2) 응답 수신 — 반드시 serial_port_getc() 로 (AT 이스케이프 감시) */

        /*  3) 파싱 → 뱅크에 쓴다. 여기서 끝이다.

            컬럼을 하나도 빼지 마라. 안 쓴 컬럼은 이전 값이 그대로 남고, 웹은
            그걸 "값 없음" 이 아니라 측정값으로 그린다.

                for (uint8_t c = 0; c < DEVICE_VALUE_COLS; c++) {
                    device_bank_setValue(src, 0, c, value_from(rsp, c));
                }

            트랩은 신경 쓰지 않아도 된다. SNMP 에이전트가 뱅크를 임계값과
            대조해서 알아서 쏜다 ([4.3.3](#433-트랩)). 임계값과 무관하게
            "지금 이 일이 일어났다" 를 알려야 할 때만 snmp_notify_cell() 을
            직접 부른다. */

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

주의:

| ☐ | 항목 |
|---|---|
| ☐ | 수신은 **반드시 `serial_port_getc()`** 로. `uart_getc()` 를 직접 부르면 그 포트에서 AT 진입이 죽는다 ([3.6](#36-at-이스케이프)) |
| ☐ | 태스크 인자는 **`SerialPort *`** 다. `uart_inst_t *` 를 넘기면 `port->channel` 에서 죽는다 |
| ☐ | device bank 행은 **`device_bank_reserve()` 로 받아라.** 상수로 고르면 다른 프로토콜과 겹쳐도 아무도 모른다 |
| ☐ | `port->uart` 를 직접 만지지 마라. 흐름제어 끄기·수신 비우기까지 `serial_port_*` 에 다 있다 |
| ☐ | 수신 대기 루프에서 **`vTaskDelay()` 로 양보**하라. 바쁜 대기는 워치독을 굶긴다 |
| ☐ | RX FIFO 는 32바이트다. 그보다 긴 프레임은 대기 중에도 계속 긁어와야 한다 |
| ☐ | 그 포트를 가져가면 `sensorUart_claim()` 이 비키도록 조건을 추가하라 |
| ☐ | 새 태스크 우선순위는 **31 이하** ([5.9](#59-configassert-는-릴리스에서-무효다--태스크-우선순위가-조용히-잘린다)) |

---

## 4.5 새 태스크 추가

| # | 파일 | 할 일 |
|---|---|---|
| 1 | [App.c:42-67](main/App/App.c#L42-L67) | `#define MYTASK_STACK_SIZE` / `MYTASK_PRIORITY` 추가 |
| 2 | [App.c:283-291](main/App/App.c#L283-L291) | `xTaskCreate(my_task, "My_Task", MYTASK_STACK_SIZE, arg, MYTASK_PRIORITY, NULL);` |
| 3 | 태스크 함수 | 무한 루프 + `vTaskDelay()` |

```c
#define MYTASK_STACK_SIZE  1024      /* 워드 단위 → 4 KB */
#define MYTASK_PRIORITY    9         /* 0 ~ 31 */

xTaskCreate(my_task, "My_Task", MYTASK_STACK_SIZE, NULL, MYTASK_PRIORITY, NULL);
```

| ☐ | 항목 |
|---|---|
| ☐ | 우선순위 ≤ 31. **32 이상은 조용히 31로 잘린다** |
| ☐ | 스택은 **워드 단위**. `printf`/`snprintf` 를 쓰면 최소 1024 워드는 잡아라 |
| ☐ | 힙 여유 확인 (`configTOTAL_HEAP_SIZE` 96 KB, `xPortGetFreeHeapSize()` 로 확인 가능) |
| ☐ | 긴 블로킹이 있으면 `device_wdt_reset()` |
| ☐ | Core 0 에 고정해야 하면 태스크 첫 줄에 `vTaskCoreAffinitySet(NULL, 1U << 0);` |
| ☐ | W5500 접근이 있으면 다른 소켓 사용자와 충돌하지 않는지 ([2.3.4](#234-소켓-배정-commonh25-47)) |
| ☐ | 인자를 넘길 땐 `xTaskCreate` 의 4번째 인자 (예: `modbusMaster_task` 에 `uart1` 전달) |

---

## 4.6 소스 파일 추가 (CMake 등록)

| 새 파일 위치 | 등록할 타겟 | 등록 위치 |
|---|---|---|
| `port/app/platform_handler/src/*.c` | `APP_PLATFORM_FILES` | [port/app/CMakeLists.txt:190](port/app/CMakeLists.txt#L190) |
| `port/app/configuration/src/*.c` | `APP_CONFIG_FILES` | [:123](port/app/CMakeLists.txt#L123) |
| `port/app/board/src/*.c` | `APP_BOARD_FILES` | [:105](port/app/CMakeLists.txt#L105) |
| `port/app/serial_to_ethernet/src/*.c` | `APP_S2E_FILES` | [:164](port/app/CMakeLists.txt#L164) |
| `port/app/modbus/src/*.c` | `APP_MODBUS_FILES` | [:231](port/app/CMakeLists.txt#L231) |
| `port/app/mbedtls/src/*.c` | `APP_MBEDTLS_FILES` | [:83](port/app/CMakeLists.txt#L83) |
| `libraries/…` (서브모듈) | [libraries/CMakeLists.txt](libraries/CMakeLists.txt) 의 해당 라이브러리 | — |

```cmake
target_sources(APP_PLATFORM_FILES PUBLIC
        ...
        ${APP_PORT_DIR}/platform_handler/src/uartHandler.c
        ${APP_PORT_DIR}/platform_handler/src/myproto.c      # ← 추가
)
```

**새 디렉터리**를 만들었다면 include 경로도 추가해야 한다:

```cmake
target_include_directories(APP_PLATFORM_FILES PUBLIC
        ...
        ${APP_PORT_DIR}/myproto/inc                          # ← 추가
)
```

| ☐ | 항목 |
|---|---|
| ☐ | `.c` 를 `target_sources` 에 넣었는가 (헤더는 넣을 필요 없음) |
| ☐ | 새 `inc/` 디렉터리를 `target_include_directories` 에 넣었는가 |
| ☐ | 다른 라이브러리의 심볼을 쓰면 `target_link_libraries` 에 의존성이 있는가 |
| ☐ | **`App` 과 `App_linker` 두 실행 파일이 같은 라이브러리 목록을 쓴다** ([main/App/CMakeLists.txt](main/App/CMakeLists.txt)) — 라이브러리에 추가하면 양쪽에 자동 반영 |
| ☐ | CMake 캐시가 꼬이면 `build/` 를 지우고 다시 configure |

---

# 5장 함정 모음

## 5.1 부트로더와 앱의 `uart_interface` enum 이 어긋난다

**증상.** 웹에서 TTL/RS-232 를 골랐는데 펌웨어 업데이트 후 RS-422 로 동작한다.

**원인.** 부트로더 헤더에는 `UART_IF_RS232 = 1` 이 **하나 더** 있어서 그 아래 값이 전부 밀린다.

| 값 | 앱 ([uartHandler.h:21-25](port/app/platform_handler/inc/uartHandler.h#L21-L25)) | 부트 ([boot/uartHandler.h:21-26](port/boot/platform_handler/inc/uartHandler.h#L21-L26)) |
|---|---|---|
| 0 | `UART_IF_RS232_TTL` | `UART_IF_RS232_TTL` / `UART_IF_TTL` |
| 1 | `UART_IF_RS422` | **`UART_IF_RS232`** ← 앱에 없는 값 |
| 2 | `UART_IF_RS485` | `UART_IF_RS422` |
| 3 | `UART_IF_RS485_REVERSE` | `UART_IF_RS485` |
| 4 | `SPI_IF_SLAVE` | `UART_IF_RS485_REVERSE` |

그리고 **둘은 같은 플래시 바이트를 쓴다** — `serial_option.uart_interface`, offset **232**, 앱·부트 동일.
부트로더가 여기에 1(자기 기준 RS-232)을 쓰면, 앱은 그걸 RS-422 로 읽는다.

**방어 코드 (버그 아님).** 두 군데서 이 값을 절대 신뢰하지 않는다.

```c
/* ConfigData.c:315-319 — 플래시에서 읽은 값을 무조건 재계산 */
if (flow_control == flow_rtsonly || flow_control == flow_reverserts) {
    dev_config.serial_option.uart_interface = UART_IF_RS422;
} else {
    dev_config.serial_option.uart_interface = get_uart_if_sel_pin();
}

/* App.c:181-185 — 진짜 설정값(EXT의 serial_intf_sel)을 덮어쓴다 */
uint8_t intf = dev_config->serial_intf_sel;
if (intf > UART_IF_RS485_REVERSE) intf = UART_IF_RS232_TTL;
dev_config->serial_option.uart_interface = intf;
```

**규칙.**

| ☐ | 항목 |
|---|---|
| ☐ | **`serial_option.uart_interface` 는 부팅마다 재계산되는 스크래치 값**이다. 저장소가 아니다 |
| ☐ | 진짜 설정은 `serial_intf_sel`(uart1) / `serial485_intf_sel`(uart0) — 둘 다 EXT 구역 |
| ☐ | `App.c` 의 강제 대입을 지우지 마라 |
| ☐ | 새 라인드라이버 모드를 추가하려면 **앱과 부트 양쪽 enum** 을 봐야 한다 |

**왜 EXT 에 두면 안전한가.** 부트로더의 `DevConfig` 는 **1284 B**, 앱은 **1596 B**.
부트로더가 저장할 때 `write_storage(..., sizeof(DevConfig))` = 앞 1284 B 만 쓴다.
EXT 구역은 offset **1444** 부터라서 **손이 닿지 않는다.**

---

## 5.2 EXT 필드를 추가할 때 `reserved_ext` 를 안 줄이면 기존 장비가 팩토리 리셋된다

**증상.** 펌웨어 업데이트 후 현장 장비의 IP·SNMP 설정이 전부 초기화된다.

**원인.** `sizeof(DevConfig)` 가 바뀌면 `packet_size != sizeof(DevConfig)` 판정에 걸려 EXT 가 재초기화된다.
(더 나쁜 경우 `devConfigVer` 위치까지 어긋나면 **전체** 팩토리 리셋 + 리부트.)

**대응.**

```c
/* N 바이트짜리 필드를 추가했다면 */
#define DEVCONFIG_RESERVED_EXT_SIZE    (51 - N)
```

**추가 규칙 — 값 0 이 기존 동작이어야 한다.**
기존 장비의 `reserved_ext[]` 는 전부 0 이다. 새 필드가 그 자리를 차지하면 초기값 0 으로 읽힌다.

| 좋은 설계 | 나쁜 설계 |
|---|---|
| `serial_de_pin`: `0 = 미설정 → 보드 기본값` | `de_pin`: `0 = GPIO0` (기존 장비가 GPIO0 을 DE 로 잡아버림) |
| `snmp_perm`: `0 = R/W` (= 기존 무제한 동작) | `snmp_perm`: `0 = 차단` (업데이트 순간 SNMP 죽음) |
| `trap_disable`: `0 = 트랩 켜짐` (기존 동작) | `trap_enable`: `0 = 트랩 꺼짐` |

0 이 기존 동작과 다를 수밖에 없다면 **`DEVCONFIG_EXT_VERSION` 을 올려라.** 그러면 EXT 만 팩토리 값으로 재초기화되고 LEGACY(네트워크 설정 등)는 보존된다.

---

## 5.3 POST 본문이 버퍼보다 길면 뒤쪽 필드가 조용히 사라진다

**증상.** 웹에서 "저장 완료" 가 뜨는데 마지막 몇 개 항목이 반영되지 않는다.

**원인.** `/api/config` 본문은 TLS 레코드 여러 개로 쪼개져 도착한다.
`https_handle_config_post()` 가 `post_extra_buf[1536]` 에 모으는데, **버퍼가 차면 거기서 멈춘다.**
파싱은 `strstr()` 로 키를 찾는 방식이라 **없는 키는 그냥 건너뛴다 → 에러가 안 난다.**
`changed` 가 1 이면 저장하고 200 을 돌려주므로 UI 는 성공으로 보인다.

```c
/* httpHandler.c:518-536 */
static unsigned char post_extra_buf[1536];
int total = 0;
while (total < (int)sizeof(post_extra_buf) - 1) {
    int r = mbedtls_ssl_read(tls_ctx->ssl, post_extra_buf + total,
                             sizeof(post_extra_buf) - 1 - total);
    if (r <= 0) break;
    total += r;
}
```

**대응.**

| ☐ | 항목 |
|---|---|
| ☐ | 필드를 추가할 때마다 본문 길이를 어림하고 `post_extra_buf` 여유를 확인하라 (주석 기준 현재 약 850 B) |
| ☐ | 저장 후 **GET 응답으로 실제 저장된 값이 돌아오는지** 확인하라 (`saveConfig()` 가 응답으로 폼을 다시 채운다 — 값이 안 바뀌면 누락된 것) |
| ☐ | 같은 이유로 `https_rx_buf[2048]` 도 요청 헤더+본문을 다 담아야 한다 |

---

## 5.4 `Web_page.html` 을 고치면 `Web_page.h` 를 재생성해야 한다

**증상.** HTML 을 고치고 빌드했는데 브라우저에 옛날 화면이 나온다.

**원인.** 펌웨어가 서빙하는 건 HTML 파일이 아니라 **`Web_page.h` 안의 바이트 배열**이다.

```bash
py -3 tools/html_to_c_header.py    # 프로젝트 루트에서
```

| 사실 |
|---|
| CMake `html_to_c_header` 타겟이 `Boot` / `App_linker` 빌드 전에 자동 실행된다 |
| ⚠ **`App` 타겟에는 의존성이 안 걸려 있다** ([CMakeLists.txt:158-159](CMakeLists.txt#L158-L159)) |
| `Web_page.h` 는 git 추적 대상이다 — HTML 과 **같은 커밋**에 넣어라 |
| 헤더 상단 날짜(`data:`)가 매번 바뀌므로 재생성만 해도 diff 가 생긴다 |

---

## 5.5 부트로더가 앱의 `device_option` 앞부분을 덮어쓴다

부트로더 `DevConfig` 에는 앱에 있는 세 필드가 없다:

```c
uint8_t device_serial_connect_data[32];
uint8_t device_serial_disconnect_data[32];
uint8_t device_eth_connect_data[32];
```

그래서 부트로더가 저장하면:

| 앱 오프셋 | 앱 필드 | 부트로더가 쓰는 것 |
|---|---|---|
| 1240–1279 | `device_group` | `device_group` (일치) |
| **1280–1283** | `device_serial_connect_data[0..3]` | **부트로더의 `devConfigVer` (=103)** ⚠ |
| 1284–1375 | 나머지 connect_data | 건드리지 않음 |
| 1376–1379 | 앱 `devConfigVer` (=104) | 건드리지 않음 ✓ |
| 1444– | EXT 구역 | 건드리지 않음 ✓ |

**실무적 영향.** `device_*_connect_data` 는 이 펌웨어에서 사용하지 않으므로 지금은 무해하다.
**하지만 그 필드를 실제로 쓰기 시작하면 부트로더 저장 후 앞 4바이트가 깨진다.**

---

## 5.6 `snmpData[]` 는 OID 오름차순이어야 한다

**증상.** `snmpwalk` 이 중간에 멈추거나 일부 OID 를 건너뛴다.

**원인.** GET-NEXT 구현 `findNextEntry()` 가 **배열을 앞에서부터 선형 스캔**해서
"요청 OID 보다 사전순으로 큰 첫 엔트리" 를 반환한다 ([snmp.c:327-339](libraries/ioLibrary_Driver/Internet/SNMP/snmp.c#L327-L339)).
배열이 정렬돼 있지 않으면 워크가 깨진다.

`initTable()` 이 **컬럼 우선(column-major)** 으로 채우는 게 바로 이 때문이다:

```c
/* snmp_custom.c:108-110 — 이 중첩 순서를 바꾸면 안 된다 */
for (uint8_t col = 1; col <= DEV_TABLE_COLS; col++)
    for (uint8_t row = 1; row <= DEV_TABLE_ROWS; row++)
        …snmpData[CELL_IDX(col, row)]…
```

**추가 제약.**

| 항목 | 값 | 결과 |
|---|---|---|
| `MAX_OID` | **12** | 셀 OID 가 정확히 12 바이트(prefix 10 + col 1 + row 1). **여유가 없다** |
| `DEVICE_COUNT` | ≤ 127 | row 서브식별자가 1바이트를 유지해야 함 |
| `2 + DEVICE_VALUE_COLS` | ≤ 127 | col 서브식별자 |

> 서브트리를 하나 더 붙이려면 OID 를 12바이트 안에 넣거나 `MAX_OID` 를 늘려야 한다.
> `MAX_OID` 를 늘리면 `dataEntryType` 이 커져 `snmpData[]` 327개 × 증가분만큼 RAM 이 더 든다.

---

## 5.7 stick parity(space/mark)는 pico-sdk 로는 안 된다

**증상.** 웹에서 SPACE/MARK 를 골랐는데 통신이 안 된다. 또는 한 번 SPACE 를 쓰고 NONE 으로 바꿔도 계속 stick parity 로 나간다.

**원인.** pico-sdk 의 `uart_set_format()` 은 none/even/odd 만 안다.
PL011 의 stick-parity 비트(`LCR_H.SPS`, bit 7)는 **쓰기 마스크에 아예 없어서 건드리지도, 지우지도 않는다.**
→ 한 번 켜지면 계속 남는다.

**해결.** 항상 `uart_set_format_parity()` 를 써라. 양방향으로 명시 제어한다.

```c
/* uartHandler.c:108-139 */
uart_set_format(uart, data_bits, stop_bits, par);   /* PEN/EPS 설정 */
if (stick) hw_set_bits(&uart_get_hw(uart)->lcr_h, UART_UARTLCR_H_SPS_BITS);
else       hw_clear_bits(&uart_get_hw(uart)->lcr_h, UART_UARTLCR_H_SPS_BITS);
```

| 모드 | PEN | EPS | SPS | 패리티 비트 |
|---|---|---|---|---|
| space | 1 | 1 | 1 | 항상 0 |
| mark | 1 | 0 | 1 | 항상 1 |

> ⚠ `parity_sel` 인자는 **`enum parity`(0~4)** 이지 `uart_parity_t` 가 아니다. 헷갈리면 조용히 틀린 패리티가 나간다.

---

## 5.8 RS-485 방향 제어는 순수 GPIO 다 — 하드웨어 RTS 를 쓰면 안 된다

**RP2040 에는 RS-485 하드웨어 지원이 없다.** DE 핀은 소프트웨어가 토글하는 그냥 GPIO 다.

**PL011 의 하드웨어 RTS(`uart_set_hw_flow`)를 DE 로 쓸 수 없는 이유:**
하드웨어 RTS 는 **RX FIFO 수위**에 따라 움직인다. 송신과 아무 상관이 없다.
DE 로 연결하면 수신 버퍼가 찰 때마다 드라이버가 켜져서 버스를 물어버린다.

**그래서 이렇게 한다:**

| 단계 | 코드 |
|---|---|
| 초기화 | `serial_port_setup(port)` 가 DE 를 GPIO 출력 + 유휴(수신) 레벨로 잡는다 |
| 송신 전 | `serial_port_tx_enable(port)` |
| 송신 후 | `serial_port_tx_disable(port)` — **`uart_tx_wait_blocking()` 로 드레인 대기 후** DE 내림 |

> 프레임 하나를 통째로 보낼 거면 `serial_port_puts(port, buf, len)` 하나면 된다. 위 세 단계가 안에 들어 있다.

| ☐ | 항목 |
|---|---|
| ☐ | RS-485 포트에서는 `uart_set_hw_flow(uart, false, false)` 로 하드웨어 흐름제어를 **꺼둬야 한다** |
| ☐ | `disable` 의 드레인 대기를 빼면 **마지막 바이트가 잘린다** (FIFO 가 비어도 시프트 레지스터에 남아 있다) |
| ☐ | uart1 에서 RS-422/485 를 쓰면 GPIO7(RTS)이 방향 제어로 전용된다 → 웹 Handshake 설정은 무시된다 |
| ☐ | 반이중이므로 **송신 중에는 수신이 안 된다.** 에코가 필요한 프로토콜은 타이밍을 확인하라 |

---

## 5.9 `configASSERT` 는 릴리스에서 무효다 — 태스크 우선순위가 조용히 잘린다

**사실:**

```c
/* FreeRTOSConfig.h:128 */
#define configASSERT(x)   assert(x)
/* 빌드 플래그: -O3 -DNDEBUG  →  assert() 는 아무것도 안 한다 */
```

```c
/* FreeRTOS-Kernel/tasks.c:1892-1897 */
configASSERT( uxPriority < configMAX_PRIORITIES );        /* ← 릴리스에서 무시됨 */
if( uxPriority >= configMAX_PRIORITIES )
    uxPriority = configMAX_PRIORITIES - 1;                /* ← 조용히 clamp */
```

`configMAX_PRIORITIES = 32` 이므로:

| 태스크 | 소스에 적힌 우선순위 | **실제** |
|---|---|---|
| `Start_Task` | 65 | 31 |
| `SEGCP_udp_Task` | 52 | 31 |
| `SEGCP_tcp_Task` | 51 | 31 |
| `Tmr Svc` | 31 | 31 |

즉 **SEGCP 두 태스크와 FreeRTOS 타이머 서비스가 같은 우선순위(31)** 로 돈다.
소스만 보고 "SEGCP 가 타이머보다 높다" 고 착각하지 마라.

| ☐ | 항목 |
|---|---|
| ☐ | 새 태스크 우선순위는 **0~31** 안에서 정하라 |
| ☐ | 우선순위 관계를 따질 땐 clamp 후 값으로 따져라 |
| ☐ | 다른 `configASSERT` 도 릴리스에서 전부 무효다 — 검증을 assert 에 의존하지 마라 |

---

## 5.10 S2E 경로는 죽어있다

`port/app/serial_to_ethernet/seg.c` 는 컴파일·링크되지만 **동작하지 않는다.**

| 항목 | 상태 |
|---|---|
| `seg_task` / `seg_u2e_task` | **`xTaskCreate` 되지 않음** ([App.c:283-291](main/App/App.c#L283-L291) 에 없음) |
| `do_seg()` | `seg_task` 안에서만 호출 → 실행 안 됨 |
| `DATA0_UART_Interrupt_Enable()` / `on_uart_rx()` | **호출처 없음** — uart1 RX ISR 은 `sensorUart` 가 잡는다 |
| `seg_sem`, `seg_u2e_sem`, `seg_critical_sem` 등 | 선언만 되고 **생성되지 않음 (NULL)** |
| `SOCK_DATA`(소켓 0) | 비어 있음 → **SNMP 트랩이 임시로 빌려 쓴다** ([common.h:40-43](port/app/configuration/inc/common.h#L40-L43)) |

**주의.** `seg.c` 를 고쳐도 동작에 영향이 없다.
반대로 **S2E 를 되살리려면** 소켓 0 을 SNMP 트랩과 나눠 쓰는 문제부터 해결해야 한다.

---

## 5.11 링 버퍼는 포트마다 하나다 — 하지만 옛 API 는 채널 0 고정이다

`51f4a17` 에서 포트별로 분리됐다. **두 포트의 바이트가 섞이던 문제는 사라졌다.**

| | 이전 | 현재 |
|---|---|---|
| 링 버퍼 | 전역 1개 | **포트당 1개** (`data_buffer_rx[2][4096]`) |
| 바이트 출처 | 구분 불가 | `channel` 인자로 구분 |
| R 명령 응답 | 양쪽 포트 동시 송신 | **명령이 온 포트로만** |

남은 함정은 **무인자 레거시 API** 다.

```c
put_byte_to_data_buffer(ch, SEG_DATA0_CH);   /* 현재 — 채널을 받는다 */
platform_uart_puts(buf, len);                /* 레거시 — 채널 0 고정 */
```

`platform_uart_putc` / `platform_uart_puts` / `DATA0_UART_Configuration` 은 **전부 채널 0 래퍼**다.
`seg.c` · `segcp.c` · `ConfigData.c` · `mbserial.c` 가 아직 이걸 쓴다. 새 코드에서는 `serial_port_*` 를 써라.

그리고 `check_uart_flow_control()` 은 **아직 포트 인자가 없다.** XON/XOFF 를 실제로 쓰게 되면 손봐야 한다.

커스터마이징 지점 마커 (`sensorUart.c` 안):

| 마커 | 무엇 |
|---|---|
| `[U1]` | `parse_write()` — S/T 명령 도착 시 동작 |
| `[U2]` | `parse_request()` — R 명령의 응답 내용 |
| `[U3]` | `uart_tx_str()` — 응답 라우팅 (현재: 명령이 온 포트) |

---

## 5.12 그 밖의 작은 함정들

| 함정 | 내용 |
|---|---|
| `DATA1_UART_Configuration()` / `DEBUG_UART_Configuration()` | **선언만 있고 구현이 없다.** 호출하면 링크 에러 |
| `body[1024]` 오버런 | `https_send_config_json()` 은 `n` 을 검사하지 않는다. 필드를 계속 추가하면 터진다 |
| `baud_table` 범위 검사 불일치 | `< 20` 하드코딩(`sensorUart.c:104`, `modbusMaster.c:52`) vs `sizeof()` 계산(`uartHandler.c:165`). `W232N` 보드는 테이블이 14개뿐 |
| `get_uart_rs485_sel()` 부작용 | GPIO7 을 **입력으로 재설정**한다. DE 로 쓰는 중에 부르면 방향 제어가 깨진다. 현재 유일한 호출 지점이 도달 불가라 실행되진 않는다 |
| `serial_port_putc()` 외부 호출 0곳 | `platform_uart_putc()` 래퍼만 쓴다. 대칭을 위해 남겨둔 함수 |
| 태스크 인자 타입 | `modbusMaster_task` 는 이제 **`SerialPort *`** 를 받는다. `uart_inst_t *` 를 넘기면 `port->channel` 에서 죽는다 |
| 데모 데이터 | `App.c:253-268` 이 부팅 시 `TH-1`~`TH-4` 에 고정값을 넣는다. 실제 센서가 없어도 웹/SNMP 에 값이 보이는 이유다. 출하 전에 지울지 판단하라 |
| 진단용 `printf` | `sensorUart_task()` 가 **수신 바이트마다** `printf("RX %02X…")` 를 한다 (:384). 고속 통신에서는 성능 문제. 5초마다 ISR 카운터도 찍는다 |
| LED3 토글 | RS-485 RX ISR 이 `gpio_xor_mask(1u << 19)` 로 LED3 을 토글한다 (:74). 디버그용 |
| `sizeof(DevConfig)` 확인법 | `_Static_assert(sizeof(DevConfig) == 1596, "…")` 를 임시로 넣고 빌드 |

---

# 6장 작업 규칙

## 6.1 빌드가 자동으로 건드리는 것들

CMake 커스텀 타겟이 빌드마다 소스 트리를 수정한다. **커밋 전에 `git status` 를 반드시 확인하라.**

| 타겟 | 스크립트 | 시점 | 결과 |
|---|---|---|---|
| `html_to_c_header` | [tools/html_to_c_header.py](tools/html_to_c_header.py) | `Boot`/`App_linker` **전** | `port/app/html_file/Web_page.h` **덮어씀** (날짜 줄 포함) |
| `restyle` | [style/restyle.py](style/restyle.py) | 빌드 **후** (ALL) | ⚠ **`main/` 과 `port/` 의 모든 `.c`/`.h` 를 astyle 로 재포맷** |
| `merge_hex` | [tools/merge_hex.py](tools/merge_hex.py) | 빌드 후 (ALL) | `bin_files/` 재생성 |
| `hex_to_uf2_converter` | [tools/hex_to_uf2_converter.py](tools/hex_to_uf2_converter.py) | `merge_hex` 후 | `Boot-App_linker_Merged.uf2` |

> **`restyle` 이 제일 성가시다.** 코드를 한 줄만 고쳐도 빌드 후 주변 파일 포맷이 바뀔 수 있다.
> astyle 규칙: Java 스타일, 스페이스 4칸, 연산자 주변 공백, `if/for/while` 뒤 공백, **중괄호 자동 추가**
> ([style/astyle_core.conf](style/astyle_core.conf))
> → **처음부터 astyle 규칙에 맞춰 쓰면** 무관한 diff 가 안 생긴다.

### 생성물 / 추적 대상 정리

| 파일 | git | 재생성 방법 |
|---|---|---|
| `port/app/html_file/Web_page.h` | **추적됨** | `py -3 tools/html_to_c_header.py` |
| `ioLibrary_snmp_patch.patch` | **추적됨** | [6.2](#62-서브모듈-패치-워크플로) |
| `bin_files/*` | 무시 | 빌드 |
| `build/*` | 무시 | 빌드 |

## 6.2 서브모듈 패치 워크플로

`libraries/ioLibrary_Driver` 는 **업스트림 SHA `b981401` 에 고정**돼 있고, 로컬 수정은 저장소 루트의
`ioLibrary_snmp_patch.patch` 로 관리된다.

**패치가 덮는 파일:** `Internet/SNMP/snmp.c`, `snmp.h`, `snmp_custom.c`, `snmp_custom.h`

### 최초 적용 (클론 직후)

```bash
cd libraries/ioLibrary_Driver
git checkout b981401
cd ..
git apply ../ioLibrary_snmp_patch.patch
cd ..
```

### 수정 후 패치 재생성

```bash
cd libraries/ioLibrary_Driver
git diff > ../../ioLibrary_snmp_patch.patch
cd ../..

# 검증 — 깨끗한 트리에 적용 가능한지 확인
cd libraries/ioLibrary_Driver
git stash
git apply --check ../../ioLibrary_snmp_patch.patch   # 에러 없으면 OK
git stash pop
cd ../..
```

| ☐ | 항목 |
|---|---|
| ☐ | 서브모듈 SNMP 파일을 고쳤으면 **반드시 패치 재생성** |
| ☐ | `git apply --check` 통과 확인 |
| ☐ | 패치 파일을 코드 변경과 **같은 커밋**에 넣어라 |
| ☐ | `git status` 에 `m libraries/ioLibrary_Driver` (소문자 m = 서브모듈 내용 변경)가 뜨는 건 정상이다 |
| ☐ | 서브모듈 SHA 자체를 바꾸지 마라 — 다른 사람 트리가 깨진다 |

> 참고: `Setup.md:122` 가 `ioLibrary_snmp_patch_HOW_TO_APPLY.md` 를 가리키지만 **그 파일은 저장소에 없다.** 위 절차를 쓰면 된다.

## 6.3 커밋 규칙

| ☐ | 항목 |
|---|---|
| ☐ | 관련 없는 포맷 변경을 섞지 마라 (`restyle` 결과 확인) |
| ☐ | HTML 을 고쳤으면 `Web_page.h` 동봉 |
| ☐ | 서브모듈을 고쳤으면 `.patch` 동봉 |
| ☐ | `DevConfig` 레이아웃을 건드렸으면 커밋 메시지에 명시 (현장 장비 설정에 영향) |
| ☐ | `ext_version` 을 올렸으면 `ConfigData.h` 의 history 주석에 이유 한 줄 |

## 6.4 변경 후 확인 목록

| 변경한 것 | 확인할 것 |
|---|---|
| DevConfig 필드 | `sizeof(DevConfig) == 1596`, 기존 장비 부팅 시 설정 유지 |
| HTML | `Web_page.h` 재생성, GET/POST 키 일치, `post_extra_buf` 여유 |
| SNMP 테이블 | `snmpwalk` 이 끝까지 돌아가는지, `oidlen ≤ 12`, RAM 여유 |
| 시리얼 드라이버 | 두 포트 각각 송수신, DE 파형(오실로스코프), 마지막 바이트 안 잘리는지 |
| 새 태스크 | 우선순위 ≤ 31, 스택 오버플로 훅 안 뜨는지, 워치독 리셋 안 걸리는지 |
| 서브모듈 | `git apply --check` 통과 |

---

## 부록 A — 자주 쓰는 참조

### 주요 상수

| 상수 | 값 | 위치 |
|---|---|---|
| `MAJOR/MINOR/MAINTENANCE_VER` | 1.2.2 | [common.h:10-12](port/app/configuration/inc/common.h#L10-L12) |
| `DEV_CONFIG_VER` | 104 (부트: 103) | [common.h:14](port/app/configuration/inc/common.h#L14) |
| `sizeof(DevConfig)` | 1596 | 계산값 |
| `DEVCONFIG_EXT_MAGIC` | `0x57495A45` ('WIZE') | [ConfigData.h:200](port/app/configuration/inc/ConfigData.h#L200) |
| `DEVCONFIG_EXT_VERSION` | 5 | [ConfigData.h:208](port/app/configuration/inc/ConfigData.h#L208) |
| `DEVCONFIG_RESERVED_EXT_SIZE` | 29 | [ConfigData.h](port/app/configuration/inc/ConfigData.h) |
| `DEVICE_COUNT` / `DEVICE_VALUE_COLS` | 64 / 3 | [sensor.h:31-32](port/app/platform_handler/inc/sensor.h#L31-L32) |
| `VALUE_LIMIT_CNT` | 4 | [ConfigData.h](port/app/configuration/inc/ConfigData.h) — 임계값을 담을 수 있는 컬럼 수 |
| `TRAP_SCAN_SEC_DEFAULT` | 5 | 〃 — `trap_scan_sec` 가 0 일 때 |
| `MODBUS_REG_COUNT` | `DEVICE_VALUE_COLS` (3) | [modbusMaster.h](port/app/platform_handler/inc/modbusMaster.h) |
| `SNMP_TRAP_QUEUE_LEN` | 32 | [snmpHandler.c](port/app/platform_handler/src/snmpHandler.c) — 셀 단위 |
| `DIN_COUNT` | 16 | [WIZnet_board.h](port/app/board/inc/WIZnet_board.h) — 이 보드만 정의 |
| `MAX_OID` / `MAX_STRING` | 12 / 64 | [snmp.h:17-18](libraries/ioLibrary_Driver/Internet/SNMP/snmp.h#L17-L18) |
| `SEG_DATA_BUF_SIZE` | 4096 | [seg.h:18](port/app/serial_to_ethernet/inc/seg.h#L18) |
| `configTOTAL_HEAP_SIZE` | 96 KB | [FreeRTOSConfig.h:75](port/app/FreeRTOS-Kernel/inc/FreeRTOSConfig.h#L75) |
| `PLL_SYS_KHZ` | 200000 (200 MHz) | [common.h:73](port/app/configuration/inc/common.h#L73) |
| HTTPS / SNMP 기본 포트 | 443 / 161 | [ConfigData.h:220-221](port/app/configuration/inc/ConfigData.h#L220-L221) |
| 계정 최대 수 | 5 | [httpsAuth.h:6](port/app/platform_handler/inc/httpsAuth.h#L6) |

### 플래시 맵 ([deviceHandler.h:30-45](port/app/platform_handler/inc/deviceHandler.h#L30-L45))

| 영역 | 오프셋 | 크기 |
|---|---|---|
| 부트로더 | 0 | 128 KB |
| App Bank0 | 0x20000 | 512 KB |
| App Bank1 | 0xA0000 | 512 KB |
| 설정 (`STORAGE_CONFIG`) | 0x120000 | — |
| RootCA / CliCA / PrivKey / MAC / Auth | +0x1000 씩 | 각 4 KB |

### 디버그 출력

USB CDC 로 나간다 (`pico_enable_stdio_usb 1`, UART stdio 는 꺼져 있음 — uart1 이 데이터 포트라서).
카테고리별 매크로: `PRT_INFO`, `PRT_ERR`, `PRT_SSL`, `PRT_SEGCP`, `PRT_SEG`, `PRT_DHCP`, `PRT_FLASH`, `PRT_HTTP`
([WIZ5XXSR-RP_Debug.h](port/app/WIZ5XXSR-RP_Debug.h))

### 관련 문서

| 문서 | 내용 |
|---|---|
| [Setup.md](Setup.md) | 개발 환경 구축, 서브모듈, 패치 적용, 보드 선택 |
| [docs/sensorUart_guide.md](docs/sensorUart_guide.md) | S/T/R 프로토콜 상세 |
| [Dev_data/SEC_UPS_PROTOCOL_HANDOVER.md](Dev_data/SEC_UPS_PROTOCOL_HANDOVER.md) | SEC UPS 프로토콜 연동 계획 |
| [SNMP_AGENT_WORK_PLAN.md](SNMP_AGENT_WORK_PLAN.md) / [SENSOR_GATEWAY_WORK_PLAN.md](SENSOR_GATEWAY_WORK_PLAN.md) | 기능 작업 계획 |
| [TODO.md](TODO.md) | 미결 과제 |
