# ioLibrary SNMP 패치 적용 방법

## 패치 파일

`ioLibrary_snmp_patch.patch`

## 변경 내용

| 파일 | 내용 |
|---|---|
| `Internet/SNMP/snmp.c` | 소켓 범위 체크 버그 수정 (`>` → `>=`), `initial_Trap()` 조건부 호출 가드 추가 |
| `Internet/SNMP/snmp_custom.c` | System MIB OID 값 W55RP20-S2E 장비 정보로 수정 |

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
```

### 3. 패치 적용

```bash
git apply ../../ioLibrary_snmp_patch.patch
```

### 4. 적용 확인

```bash
git diff --stat
```

아래와 같이 출력되면 정상이다.

```
Internet/SNMP/snmp.c       | 11 +++++++++--
Internet/SNMP/snmp_custom.c |  8 ++++----
```

---

## 문제 해결

### 패치 적용 실패 시

베이스 커밋이 다를 경우 아래 옵션으로 강제 적용을 시도할 수 있다.

```bash
git apply --reject ../../ioLibrary_snmp_patch.patch
```

`.rej` 파일이 생성되며 충돌 부분을 수동으로 병합해야 한다.

### 적용 전 미리 확인

```bash
git apply --check ../../ioLibrary_snmp_patch.patch
```

오류 없이 완료되면 실제로 적용해도 안전하다.
