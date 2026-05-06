# SNMP Agent 작업 계획

## 진행 상태

- [x] 작업 계획 문서 작성
- [x] SNMP용 socket 7번 예약 및 HTTPS socket 3개로 조정
- [x] ioLibrary SNMP 소스 CMake 연결
- [x] SNMP 전용 FreeRTOS task 추가
- [x] App 시작 흐름에 SNMP task 등록
- [x] 초기 Trap 비활성화
- [x] 기본 System OID 값 정리
- [x] 전체 빌드 검증
- [x] 보드 실기에서 `snmpget`/`snmpwalk` 검증

## 실기 검증 결과

검증 IP: `192.168.11.118`

검증 명령:

```bash
snmpget -v1 -c public 192.168.11.118 1.3.6.1.2.1.1.1.0
snmpwalk -v1 -c public 192.168.11.118 1.3.6.1.2.1.1
snmpget -v1 -c public 192.168.11.118 1.3.6.1.2.1.1.3.0
```

확인 결과:

- `sysDescr.0`: `"W55RP20-S2E SNMP Agent"` 응답 확인
- `system` subtree walk 정상 응답 확인
- `sysUpTime.0`: `32316`에서 3초 후 `32618`로 증가 확인
- UDP 161 기반 SNMP v1 Agent 1차 동작 검증 완료

## 목표

이 FW에서 기존 WIZnet ioLibrary의 SNMP 구현을 사용해 장비가 SNMP v1 Agent로 동작하게 한다.

1차 목표는 Manager가 장비 IP의 UDP 161 포트로 `snmpget` 또는 `snmpwalk` 요청을 보냈을 때 응답하는 것이다. Trap 송신은 Manager IP 설정이 필요하므로 1차 구현에서는 비활성화하고, Agent 응답 안정화 후 2차 작업으로 분리한다.

## 현재 구조 요약

- MCU/RTOS: RP2040 + Pico SDK + FreeRTOS
- Ethernet: W5500 계열 ioLibrary socket API 사용
- App 진입점: `main/App/App.c`
- App 포트 코드: `port/app`
- 기존 SNMP 소스:
  - `libraries/ioLibrary_Driver/Internet/SNMP/snmp.c`
  - `libraries/ioLibrary_Driver/Internet/SNMP/snmp.h`
  - `libraries/ioLibrary_Driver/Internet/SNMP/snmp_custom.c`
  - `libraries/ioLibrary_Driver/Internet/SNMP/snmp_custom.h`

## Task 설계

SNMP는 전용 FreeRTOS task 1개로 처리한다.

예정 task:

```text
SNMP_Agent_Task
  - NET_IP_UP 상태까지 대기
  - 현재 장비 IP 확인
  - SNMP Agent 초기화
  - UDP 161 요청을 snmpd_run()으로 반복 처리
  - Link down 또는 IP 재설정 시 SNMP socket close 후 재초기화 대기
```

예상 설정:

```c
#define SNMP_TASK_STACK_SIZE     2048
#define SNMP_TASK_PRIORITY       7
```

현재 주요 task와의 관계:

| Task | 역할 | Priority |
|---|---|---:|
| `Net_Status_Task` | PHY/DHCP/IP 상태 관리 | 8 |
| `http_webserver_task` | HTTPS 서버 처리 | 23 |
| `SNMP_Agent_Task` | SNMP UDP 161 처리 | 7 |
| `Heap_Monitor_Task` | Heap 출력 | 6 |
| FreeRTOS Timer Service | Software timer callback 처리 | 31 |

SNMP 라이브러리 내부가 전역 request/response buffer, socket 번호, OID table을 사용하므로 SNMP socket 소유권은 한 task에 고정한다. Trap이 나중에 필요하면 다른 task가 직접 `snmp_sendTrap()`을 호출하지 않고 SNMP task에 queue로 요청을 전달하는 구조를 사용한다.

## 소켓 배정 계획

W5500 하드웨어 socket은 0~7 총 8개다. 현재 HTTPS가 4개 socket을 점유하므로 SNMP용 socket 하나를 확보해야 한다.

1차 구현에서는 HTTPS 동시 접속 수를 4개에서 3개로 줄이고 마지막 socket을 SNMP Agent에 할당한다.

예정 배정:

| Socket | 용도 |
|---:|---|
| 0 | Serial-to-Ethernet data |
| 1 | SEGCP UDP |
| 2 | SEGCP TCP |
| 3 | DHCP/DNS/FW update 등 공용 |
| 4 | HTTPS server 1 |
| 5 | HTTPS server 2 |
| 6 | HTTPS server 3 |
| 7 | SNMP Agent UDP 161 |

## 구현 단계

1. `SNMP_AGENT_WORK_PLAN.md`를 작업 기준 문서로 유지한다.
2. `common.h`에서 `SOCK_SNMP_AGENT`를 추가하고 HTTPS socket 수를 3개로 조정한다.
3. CMake에 SNMP 소스 빌드 대상을 추가한다.
4. `snmpHandler.h`, `snmpHandler.c`를 추가한다.
5. `App.c`에서 SNMP task stack/priority define, include, `xTaskCreate()`를 추가한다.
6. SNMP Agent task에서 `NET_IP_UP` 상태를 기다리고 `snmpd_init()` 및 `snmpd_run()`을 수행한다.
7. 초기 Trap 송신은 비활성화한다.
8. `snmp_custom.c`의 기본 OID 값을 W55RP20-S2E 장비 정보에 맞게 정리한다.
9. 빌드 후 오류를 수정한다.

## 검증 계획

빌드 성공 후 장비에서 IP가 올라오면 PC에서 다음 명령으로 확인한다.

```bash
snmpget -v1 -c public <device-ip> 1.3.6.1.2.1.1.1.0
snmpwalk -v1 -c public <device-ip> 1.3.6.1.2.1.1
```

기대 동작:

- `sysDescr.0`이 W55RP20-S2E SNMP Agent 계열 문자열로 응답한다.
- `sysUpTime.0`이 증가하는 값으로 응답한다.
- 존재하지 않는 OID는 SNMP v1 오류로 응답한다.
- Link down 후 복구 시 SNMP Agent socket이 재초기화된다.

## 주의 사항

- 기존 사용자 변경분을 되돌리지 않는다.
- 1차 구현은 SNMP v1 community `public` 기준이다.
- Trap은 1차 구현에서 비활성화한다.
- SNMP v2c/v3, community 설정 저장, Trap manager 설정은 후속 작업으로 분리한다.
- `Heap_Monitor_Task`는 현재 200ms마다 출력하므로 제품 운전용에서는 추후 제거하거나 주기를 조정하는 것이 좋다.
