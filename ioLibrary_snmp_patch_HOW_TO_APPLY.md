# ioLibrary SNMP 패치 적용 방법

## 패치 파일

`ioLibrary_snmp_patch.patch`

## 변경 내용

| 파일 | 내용 |
|---|---|
| `Internet/SNMP/snmp.c` | 소켓 범위 체크 버그 수정, `initial_Trap()` 가드, **SNMP 접근허용 IP 슬롯 2→4 (`s_allowed_ip[4][4]`, `snmp_set_allowed_ips`)**, **agent 포트 런타임화 (`s_agent_port`, `snmp_set_agent_port`)** |
| `Internet/SNMP/snmp.h` | `snmp_set_allowed_ips([4][4])`, `snmp_set_agent_port()` 선언 |
| `Internet/SNMP/snmp_custom.c` | System MIB OID 값 W55RP20-S2E 장비 정보로 수정, 센서/디바이스 OID |
| `Internet/SNMP/snmp_custom.h` | 커스텀 OID/community 등 정의 |

> 최종 갱신: 2026-05-27 (TODO #2 SNMP IP 슬롯 4개, #8 agent 포트 설정 반영)

## 베이스 커밋

```
b981401  (ioLibrary_Driver upstream "Update README.md")
```

패치는 이 커밋 위에서 만들어졌다. 다른 커밋에 적용하면 실패할 수 있다.

---

## 적용 절차

### 1. 프로젝트 클론

```bash
git clone --recurse-submodules <repo-url>
cd W55RP20-S2E_SNMP_HTTPS_TEST
```

### 2. ioLibrary 베이스 커밋으로 이동

```bash
cd libraries/ioLibrary_Driver
git checkout b981401
cd ..
```

### 3. 패치 적용

`libraries/` 디렉토리에서 실행한다.

```bash
git apply ../ioLibrary_snmp_patch.patch
```

### 4. 적용 확인

```bash
git diff --stat
```

아래와 같이 4개 파일이 출력되면 정상이다.

```
Internet/SNMP/snmp.c        | 383 +++++++++++++++++++++++++++++++++++--
Internet/SNMP/snmp.h        |  33 +++-
Internet/SNMP/snmp_custom.c | 303 ++++++++++++++++++--------------
Internet/SNMP/snmp_custom.h |  10 +
4 files changed, 582 insertions(+), 147 deletions(-)
```

---

## 문제 해결

### 패치 적용 실패 시

베이스 커밋이 다를 경우 아래 옵션으로 강제 적용을 시도할 수 있다.

```bash
git apply --reject ../ioLibrary_snmp_patch.patch
```

`.rej` 파일이 생성되며 충돌 부분을 수동으로 병합해야 한다.

### 적용 전 미리 확인

```bash
git apply --check ../ioLibrary_snmp_patch.patch
```

오류 없이 완료되면 실제로 적용해도 안전하다.
