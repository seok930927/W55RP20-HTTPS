#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "FreeRTOS.h"
#include "task.h"
#include "secUps.h"
#include "itemBank.h"
#include "WIZ5XXSR-RP_Debug.h"

/*  ── 한 번의 주고받기 ──────────────────────────────────────────────────── */

#define UPS_HDR_LEN         5       /* '^' 'D' 와 길이 3자리                  */
#define UPS_DATA_MAX        128     /* 가장 긴 응답이 67바이트다              */
#define UPS_REPLY_MS        500     /* 질의 하나를 기다리는 시간              */
#define UPS_GAP_MS          50      /* 질의 사이 간격                         */
#define UPS_POLL_MS         1000    /* 한 바퀴 돌고 쉬는 시간                 */

/*  내부 항목 번호는 30-99 다. 번호로 바로 꽂아 넣으려고 100칸을 잡는다. */
#define UPS_UID_MAX         100

/*  ── SNMP 로 나가는 번호 ──────────────────────────────────────────────── */

#define UPS_ITEM_VIN_RS     1
#define UPS_ITEM_VIN_ST     2
#define UPS_ITEM_VIN_TR     3
#define UPS_ITEM_VOUT       4
#define UPS_ITEM_IOUT       5
#define UPS_ITEM_BAT_V      6
#define UPS_ITEM_BAT_I      7
#define UPS_ITEM_CHARGE     8
#define UPS_ITEM_FOUT       9
#define UPS_ITEM_SOURCE     10
#define UPS_ITEM_BYPASS     21
#define UPS_ITEM_BYPASS_OK  22
#define UPS_ITEM_CONV_ALARM 23
#define UPS_ITEM_BLACKOUT   25
#define UPS_ITEM_ETC_ALARM  27
#define UPS_ITEM_COMM       28

/*  통신 상태(28번)가 갖는 값. 다 알아들었으면 정상, 일부만 틀어졌으면 데이터
    오류, 한 마디도 못 받았으면 통신 경보. 트랩은 통신 경보에서만 나간다 --
    한 번 틀어진 응답까지 트랩을 쏘면 실제로 끊긴 것과 구분이 안 된다. */
#define COMM_OK             0
#define COMM_DATA_ERROR     1
#define COMM_LOST           2

/*  ── 질의 다섯 가지 ───────────────────────────────────────────────────────

    표가 들고 있는 것은 항목의 순서뿐이다. 응답의 n번째 값이 내부 항목 몇 번
    인가, 그것만 적혀 있다.

    자리로 세지 않는 이유가 있다. 값의 자릿수가 고정이 아니다 -- 같은 항목이
    1 로 올 때도 있고 1111 로 올 때도 있어서, 시작 자리를 표에 박아두면 짧은
    값이 하나만 와도 그 뒤가 전부 밀린다. 한 번 밀리면 출력전압 자리에 주파수
    가 앉는데 값이 그럴듯해서 아무도 눈치채지 못한다.

    그래서 쉼표로 나눈다. 빈 항목은 빈 칸으로 오고(ST1 응답 앞부분 ",,3,,,,"
    이 그렇다) 쉼표는 그대로 찍혀 오므로, 세다가 어긋날 일이 없다.         */

static const uint8_t ST1_UID[] = { 55, 56, 57, 58, 59, 60, 61, 62, 63 };
static const uint8_t ST2_UID[] = { 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97 };
static const uint8_t ST3_UID[] = { 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54 };
static const uint8_t ST4_UID[] = { 98, 99, 30, 31, 32, 33, 34, 35, 36, 37, 38 };
static const uint8_t ST5_UID[] = { 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78 };

typedef struct {
    const char    *req;         /* 보낼 문자열                                */
    const uint8_t *uid;         /* 응답에 실려오는 순서대로의 내부 항목 번호  */
    uint8_t        count;
} UpsQuery;

static const UpsQuery UPS_QUERY[] = {
    { "^P003ST1", ST1_UID, (uint8_t)sizeof(ST1_UID) },  /* 축전지   */
    { "^P003ST2", ST2_UID, (uint8_t)sizeof(ST2_UID) },  /* 입력     */
    { "^P003ST3", ST3_UID, (uint8_t)sizeof(ST3_UID) },  /* 출력     */
    { "^P003ST4", ST4_UID, (uint8_t)sizeof(ST4_UID) },  /* 바이패스 */
    { "^P003ST5", ST5_UID, (uint8_t)sizeof(ST5_UID) },  /* 경보     */
};
#define UPS_QUERY_CNT   (sizeof(UPS_QUERY) / sizeof(UPS_QUERY[0]))

/*  ── 밖으로 내보내는 항목 ─────────────────────────────────────────────────

    scale 은 저장된 값이 아니라 그 값을 읽는 방법이다. 입력전압은 3588 로
    저장하고 scale -1 이 붙어 358.8 V 로 읽힌다 -- 프로토콜에서 나누지 않는
    것은, 나누는 순간 소수점 아래가 사라지고 되돌릴 수 없기 때문이다.

    마지막 칸은 트랩 조건이다. 그 값이 되는 순간 한 번만 나가고, 서 있는 동안
    은 조용하다. 측정값에는 안 붙인다 -- 358.8 V 가 트랩을 쏠 일은 없다.    */
static const ItemDef UPS_ITEMS[] = {
    { UPS_ITEM_VIN_RS,     "R-S상 입력전압", -1, "V",  0,         0 },
    { UPS_ITEM_VIN_ST,     "S-T상 입력전압", -1, "V",  0,         0 },
    { UPS_ITEM_VIN_TR,     "T-R상 입력전압", -1, "V",  0,         0 },
    { UPS_ITEM_VOUT,       "출력 전압",      -1, "V",  0,         0 },
    { UPS_ITEM_IOUT,       "출력 전류",      -1, "A",  0,         0 },
    { UPS_ITEM_BAT_V,      "축전지 전압",    -1, "V",  0,         0 },
    { UPS_ITEM_BAT_I,      "축전지 전류",    -1, "A",  0,         0 },
    { UPS_ITEM_CHARGE,     "충전모드",        0, "",   0,         0 },
    { UPS_ITEM_FOUT,       "출력 주파수",    -1, "Hz", 0,         0 },
    { UPS_ITEM_SOURCE,     "전원 출력원",     0, "",   0,         0 },
    { UPS_ITEM_BYPASS,     "바이패스 전원",   0, "",   1,         0 },
    { UPS_ITEM_BYPASS_OK,  "바이패스 복구",   0, "",   1,         0 },
    { UPS_ITEM_CONV_ALARM, "변환부 경보",     0, "",   1,         0 },
    { UPS_ITEM_BLACKOUT,   "입력전원 경보",   0, "",   1,         0 },
    { UPS_ITEM_ETC_ALARM,  "기타장비 경보",   0, "",   1,         0 },
    { UPS_ITEM_COMM,       "통신 상태",       0, "",   COMM_LOST, 0 },
};
#define UPS_ITEM_CNT    (sizeof(UPS_ITEMS) / sizeof(UPS_ITEMS[0]))

/*  ── 내부 번호도 웹에는 보여준다 ──────────────────────────────────────────

    고객 화면 슬라이드 10, 11 은 이 프로토콜의 내부 번호 30-99 를 그대로
    늘어놓는다. SNMP 로는 위의 1-28 만 나가므로 여기는 전부 web_only 다.

    이름과 단위는 그 두 장에서 그대로 옮겼다. 캡처와도 맞는다 -- ST1 응답의
    57번이 3(방전), 61번이 2741(274.1 VDC), 62번이 -034(-3.4 A), 63번이
    27(27도)로 읽힌다.

    마지막 칸이 1 인 것이 web_only 표시다.                                  */
static const ItemDef UPS_RAW_ITEMS[] = {
    /*  3상 입/출력 (슬라이드 10) -- 한 항목이 R/S/T 세 번호로 나뉜다 */
    { 84, "입력상태 R (Input Bad)",  0, "",   0, 1 },
    { 85, "입력상태 S (입력 상수)",  0, "",   0, 1 },
    { 86, "입력주파수 R",           -1, "Hz", 0, 1 },
    { 90, "입력주파수 S",           -1, "Hz", 0, 1 },
    { 94, "입력주파수 T",           -1, "Hz", 0, 1 },
    { 87, "입력전압 R",             -1, "V",  0, 1 },
    { 91, "입력전압 S",             -1, "V",  0, 1 },
    { 95, "입력전압 T",             -1, "V",  0, 1 },
    { 88, "입력전류 R",             -1, "A",  0, 1 },
    { 92, "입력전류 S",             -1, "A",  0, 1 },
    { 96, "입력전류 T",             -1, "A",  0, 1 },
    { 89, "입력전력 R",              0, "W",  0, 1 },
    { 93, "입력전력 S",              0, "W",  0, 1 },
    { 97, "입력전력 T",              0, "W",  0, 1 },
    { 98, "바이패스 주파수",        -1, "Hz", 0, 1 },
    { 99, "바이패스 입력 상수",      0, "",   0, 1 },
    { 30, "바이패스 전압 R",        -1, "V",  0, 1 },
    { 33, "바이패스 전압 S",        -1, "V",  0, 1 },
    { 36, "바이패스 전압 T",        -1, "V",  0, 1 },
    { 31, "바이패스 전류 R",        -1, "A",  0, 1 },
    { 34, "바이패스 전류 S",        -1, "A",  0, 1 },
    { 37, "바이패스 전류 T",        -1, "A",  0, 1 },
    { 32, "바이패스 전력 R",         0, "W",  0, 1 },
    { 35, "바이패스 전력 S",         0, "W",  0, 1 },
    { 38, "바이패스 전력 T",         0, "W",  0, 1 },
    { 40, "출력원",                  0, "",   0, 1 },
    { 41, "출력 주파수",            -1, "Hz", 0, 1 },
    { 42, "출력 상수",               0, "",   0, 1 },
    { 43, "출력전압 R",             -1, "V",  0, 1 },
    { 47, "출력전압 S",             -1, "V",  0, 1 },
    { 51, "출력전압 T",             -1, "V",  0, 1 },
    { 44, "출력전류 R",             -1, "A",  0, 1 },
    { 48, "출력전류 S",             -1, "A",  0, 1 },
    { 52, "출력전류 T",             -1, "A",  0, 1 },
    { 45, "출력전력 R",              0, "W",  0, 1 },
    { 49, "출력전력 S",              0, "W",  0, 1 },
    { 53, "출력전력 T",              0, "W",  0, 1 },
    { 46, "출력부하율 R",            0, "%",  0, 1 },
    { 50, "출력부하율 S",            0, "%",  0, 1 },
    { 54, "출력부하율 T",            0, "%",  0, 1 },

    /*  축전지 상태 (슬라이드 11) */
    { 55, "축전지 조건",             0, "",   0, 1 },
    { 56, "축전지 상태",             0, "",   0, 1 },
    { 57, "충전모드",                0, "",   0, 1 },
    { 58, "방전시간",                0, "초", 0, 1 },
    { 59, "축전지 잔량",             0, "분", 0, 1 },
    { 60, "축전지 충전율",           0, "%",  0, 1 },
    { 61, "밧데리 전압",            -1, "V",  0, 1 },
    { 62, "밧데리 전류",            -1, "A",  0, 1 },
    { 63, "밧데리 온도",             0, "℃", 0, 1 },

    /*  UPS 경보내용 (슬라이드 11) -- 0 정상 / 1 경보.

        트랩은 여기서 안 쏜다. 이 열다섯 개를 묶어 23번(변환부)과 27번(기타
        장비)으로 내보내고 트랩은 그 둘이 맡는다 -- 고객 문서가 SNMP 로
        내보낼 경보를 그 둘로 정했기 때문이다. */
    { 64, "온도과열 경보",           0, "",   0, 1 },
    { 65, "입력 경보",               0, "",   0, 1 },
    { 66, "출력 고장",               0, "",   0, 1 },
    { 67, "과부하 경보",             0, "",   0, 1 },
    { 68, "바이패스 고장",           0, "",   0, 1 },
    { 69, "출력off 경보",            0, "",   0, 1 },
    { 70, "ups shutdown",            0, "",   0, 1 },
    { 71, "충전 경보",               0, "",   0, 1 },
    { 72, "시스템 off",              0, "",   0, 1 },
    { 73, "팬 경보",                 0, "",   0, 1 },
    { 74, "fuse 경보",               0, "",   0, 1 },
    { 75, "일반적 경보",             0, "",   0, 1 },
    { 76, "Awaiting power",          0, "",   0, 1 },
    { 77, "shutdown pending",        0, "",   0, 1 },
    { 78, "shutdown imminent",       0, "",   0, 1 },
};
#define UPS_RAW_CNT     (sizeof(UPS_RAW_ITEMS) / sizeof(UPS_RAW_ITEMS[0]))

/*  ── 내부 항목 번호로 본 값 ───────────────────────────────────────────── */

#define UID_OUT_SOURCE      40      /* 0 상용 / 1 축전지 / 2 바이패스         */
#define UID_OUT_FREQ        41
#define UID_OUT_VOLT        43
#define UID_OUT_CURR        44
#define UID_BAT_VOLT        61
#define UID_BAT_CURR        62
#define UID_CHARGE_MODE     57      /* 0 부동 / 1 충전 / 2 대기 / 3 방전      */
/*  입력전압 세 개.

    고객 화면은 이 셋을 "R-S상 / S-T상 / T-R상" 이라고 부르는데, 선간전압이
    아니라 그냥 R상, S상, T상이다. 고객 확인을 받았다 -- "입력전압1은 R상,
    2는 S상, 3은 T상. R-S상은 R상, S-T상은 S상, T-R상은 T상."

    이름만 보면 두 상 사이의 전압 같아서 반대로 짚기 쉬우므로, 여기 이름은
    실제 상으로 둔다. 화면에 찍히는 글자는 고객 문서 그대로 간다 -- 그쪽이
    관리 프로그램에 등록해 둔 이름이라 우리가 고칠 것이 아니다. */
#define UID_IN_VOLT_R       87
#define UID_IN_VOLT_S       91
#define UID_IN_VOLT_T       95

/*  전원 출력원(40) 이 갖는 값. 21번 트랩의 조건이기도 하다. */
#define OUT_SOURCE_LINE     0
#define OUT_SOURCE_BATTERY  1
#define OUT_SOURCE_BYPASS   2

/*  입력정전 판정선. 문서는 "입력전원 R-S상 값이 140보다 작을 때" 라고 적었고,
    그 값은 0.1 V 단위로 들어오므로 140.0 V = 1400 이다. */
#define UPS_BLACKOUT_RAW    1400

/*  ST5 가 실어오는 경보 열다섯 개를 둘로 묶어 내보낸다. 하나라도 서면 그 묶음
    이 선다.

    23번(변환부)의 목록은 고객 문서에 있는 그대로다. 27번(기타장비)은 그 나머지
    전부인데, 문서가 27번이 무엇인지는 적어두지 않았다 -- 같은 표의 경보를 둘로
    나눈다면 남는 쪽이 27번이라고 본 것이라, 확인이 필요한 자리다. */
static const uint8_t CONV_ALARM_UID[] = { 65, 66, 68, 69, 70, 72, 77, 78 };
#define CONV_ALARM_CNT  (sizeof(CONV_ALARM_UID) / sizeof(CONV_ALARM_UID[0]))

static const uint8_t ETC_ALARM_UID[] = { 64, 67, 71, 73, 74, 75, 76 };
#define ETC_ALARM_CNT   (sizeof(ETC_ALARM_UID) / sizeof(ETC_ALARM_UID[0]))

static int32_t s_raw[UPS_UID_MAX];
static uint8_t s_raw_ok[UPS_UID_MAX];   /* 이번 바퀴에 값이 들어왔나 */

/*  22번(바이패스 복구)은 "직전에 21번이 서 있었는데 지금 내려갔다" 일 때 한 번
    만 선다. 그 직전 상태가 여기 남는다. */
static uint8_t s_bypass_prev;

/*  ── 한 항목 읽어내기 ─────────────────────────────────────────────────── */

/*  data[from] 부터 data[to] 직전까지를 정수로. 빈 자리와 숫자가 아닌 것은
    실패로 돌려주고 호출자가 "값 없음" 으로 처리한다 -- 0 으로 적어 두면
    진짜 0 과 구분이 안 된다. */
static int field_value(const char *data, uint8_t from, uint8_t to, int32_t *out) {
    char tmp[12];
    uint8_t n = 0;
    char *end;
    long v;

    for (uint8_t i = from; i < to && n < (uint8_t)(sizeof(tmp) - 1); i++) {
        if (data[i] != ' ') {
            tmp[n++] = data[i];
        }
    }
    tmp[n] = '\0';

    if (n == 0) {
        return -1;
    }
    v = strtol(tmp, &end, 10);
    if (*end != '\0') {
        return -1;
    }
    *out = (int32_t)v;
    return 0;
}

/*  이번 질의가 실어오기로 한 항목들을 값 없음으로 되돌린다.

    응답을 못 받았을 때 부른다. 그러지 않으면 지난 바퀴의 값이 계속 "있는 값"
    으로 남아서, 통신이 끊긴 뒤에도 입력정전 판정 같은 것이 옛날 전압으로
    계산된다. */
static void ups_invalidate(const UpsQuery *q) {
    for (uint8_t i = 0; i < q->count; i++) {
        s_raw_ok[q->uid[i]] = 0;
    }
}

/*  쉼표로 나눠 순서대로 꽂는다. 0 정상, -1 항목 수가 표와 다르다.

    수가 안 맞으면 호출자가 응답 전체를 버린다. 하나가 빠진 채로 받아들이면
    그 뒤가 한 칸씩 밀려서, 틀린 값이 맞는 항목 번호를 달고 올라간다. */
static int ups_store(const UpsQuery *q, const char *data, uint8_t len) {
    uint8_t from = 0;
    uint8_t i = 0;

    for (uint8_t pos = 0; pos <= len; pos++) {
        int32_t v;

        if (pos < len && data[pos] != ',') {
            continue;
        }

        /*  [from, pos) 가 한 항목. 마지막 항목은 쉼표가 아니라 데이터 끝에서
            닫히므로 pos == len 도 한 번 돌린다. */
        if (i >= q->count) {
            return -1;                  /* 표보다 항목이 많다 */
        }
        if (field_value(data, from, pos, &v) == 0) {
            s_raw[q->uid[i]] = v;
            s_raw_ok[q->uid[i]] = 1;
        } else {
            s_raw_ok[q->uid[i]] = 0;    /* 빈 칸이거나 숫자가 아니다 */
        }
        i++;
        from = (uint8_t)(pos + 1);
    }

    return (i == q->count) ? 0 : -1;
}

/*  ── 질의 한 번 ───────────────────────────────────────────────────────────

    0 정상, -1 응답 없음, -2 응답은 왔는데 모양이 아니다. 이 셋을 구분하는
    이유는 28번 통신 상태가 "통신 경보" 와 "데이터 오류" 를 다르게 세기
    때문이다.                                                               */
static int ups_exchange(SerialPort *port, const UpsQuery *q, uint8_t *data) {
    uint8_t hdr[UPS_HDR_LEN];
    int len;
    int got;

    /*  지난번에 늦게 온 응답이 남아 있으면 이번 질의의 답으로 읽힌다. */
    serial_port_flush_rx(port);
    serial_port_puts(port, (const uint8_t *)q->req, (uint16_t)strlen(q->req));

    if (serial_port_read_exact(port, hdr, UPS_HDR_LEN, UPS_REPLY_MS) != UPS_HDR_LEN) {
        return -1;
    }
    if (hdr[0] != '^' || hdr[1] != 'D') {
        return -2;
    }
    for (uint8_t i = 2; i < UPS_HDR_LEN; i++) {
        if (hdr[i] < '0' || hdr[i] > '9') {
            return -2;
        }
    }

    len = (hdr[2] - '0') * 100 + (hdr[3] - '0') * 10 + (hdr[4] - '0');
    if (len <= 0 || len > UPS_DATA_MAX - 1) {
        return -2;
    }

    got = serial_port_read_exact(port, data, len, UPS_REPLY_MS);
    if (got != len) {
        return -2;
    }
    data[len] = '\0';

    /*  앞뒤 공백은 여기서 손대지 않는다. 캡처에 ST2 만 데이터 앞에 한 칸을
        달고 오는 것이 남아 있는데, 항목을 잘라낸 뒤 field_value() 가 공백을
        버리므로 그대로 두어도 값은 같다. */
    if (ups_store(q, (const char *)data, (uint8_t)len) < 0) {
        return -2;              /* 항목 수가 표와 다르다 */
    }
    return 0;
}

/*  ── 내부 값 -> 밖으로 나가는 번호 ─────────────────────────────────────── */

static void pub_raw(uint8_t num, uint8_t uid) {
    if (s_raw_ok[uid]) {
        item_set(ITEM_DEV_UPS, num, s_raw[uid]);
    }
}

/*  하나라도 서 있으면 1. 값이 안 들어온 번호는 세지 않는다. */
static int32_t any_alarm(const uint8_t *uid, uint8_t cnt) {
    for (uint8_t i = 0; i < cnt; i++) {
        if (s_raw_ok[uid[i]] && s_raw[uid[i]] != 0) {
            return 1;
        }
    }
    return 0;
}

static void ups_publish(uint8_t comm) {
    uint8_t bypass = 0;

    /*  내부 번호를 그대로 웹에 넘긴다. 슬라이드 10, 11 화면이 이 번호로
        그려지고, SNMP 는 web_only 라서 건너뛴다. */
    for (uint16_t i = 0; i < UPS_RAW_CNT; i++) {
        uint8_t uid = UPS_RAW_ITEMS[i].num;

        if (s_raw_ok[uid]) {
            item_set(ITEM_DEV_UPS, uid, s_raw[uid]);
        }
    }

    pub_raw(UPS_ITEM_VIN_RS, UID_IN_VOLT_R);
    pub_raw(UPS_ITEM_VIN_ST, UID_IN_VOLT_S);
    pub_raw(UPS_ITEM_VIN_TR, UID_IN_VOLT_T);
    pub_raw(UPS_ITEM_VOUT,   UID_OUT_VOLT);
    pub_raw(UPS_ITEM_IOUT,   UID_OUT_CURR);
    pub_raw(UPS_ITEM_BAT_V,  UID_BAT_VOLT);
    pub_raw(UPS_ITEM_BAT_I,  UID_BAT_CURR);
    pub_raw(UPS_ITEM_FOUT,   UID_OUT_FREQ);
    pub_raw(UPS_ITEM_CHARGE, UID_CHARGE_MODE);

    /*  10번은 40번을 그대로 내보내지 않는다. 우리 쪽 번호가 따로 정해져 있다.
        상용 0 -> 3, 축전지 1 -> 5, 바이패스 2 -> 4. */
    if (s_raw_ok[UID_OUT_SOURCE]) {
        int32_t out;
        switch (s_raw[UID_OUT_SOURCE]) {
        case OUT_SOURCE_LINE:    out = 3; break;
        case OUT_SOURCE_BATTERY: out = 5; break;
        case OUT_SOURCE_BYPASS:  out = 4; break;
        default:                 out = 0; break;
        }
        item_set(ITEM_DEV_UPS, UPS_ITEM_SOURCE, out);

        bypass = (s_raw[UID_OUT_SOURCE] == OUT_SOURCE_BYPASS) ? 1 : 0;
        item_set(ITEM_DEV_UPS, UPS_ITEM_BYPASS, bypass);

        /*  복구는 한 바퀴만 서 있다가 내려간다. 트랩을 한 번만 내보내기
            위해서다 -- 계속 서 있으면 매 바퀴 같은 트랩이 나간다. */
        item_set(ITEM_DEV_UPS, UPS_ITEM_BYPASS_OK,
                 (s_bypass_prev && !bypass) ? 1 : 0);
        s_bypass_prev = bypass;
    }

    item_set(ITEM_DEV_UPS, UPS_ITEM_CONV_ALARM,
             any_alarm(CONV_ALARM_UID, CONV_ALARM_CNT));
    item_set(ITEM_DEV_UPS, UPS_ITEM_ETC_ALARM,
             any_alarm(ETC_ALARM_UID, ETC_ALARM_CNT));

    if (s_raw_ok[UID_IN_VOLT_R]) {
        item_set(ITEM_DEV_UPS, UPS_ITEM_BLACKOUT,
                 (s_raw[UID_IN_VOLT_R] < UPS_BLACKOUT_RAW) ? 1 : 0);
    }

    item_set(ITEM_DEV_UPS, UPS_ITEM_COMM, comm);
}

/*  ── 태스크 ───────────────────────────────────────────────────────────── */

void secUps_register(void) {
    int rc = item_bank_register(ITEM_DEV_UPS, UPS_ITEMS, (uint16_t)UPS_ITEM_CNT);

    if (rc < 0) {
        PRT_INFO("sec ups: item table rejected (%d)\r\n", rc);
    }

    /*  두 번째 표. SNMP 로 나가는 1-28 과 웹에만 쓰는 30-99 는 성격이 달라서
        표를 나눴다 -- 한 표에 섞어두면 어느 쪽이 OID 를 갖는지가 줄마다
        달라진다. */
    rc = item_bank_register(ITEM_DEV_UPS, UPS_RAW_ITEMS, (uint16_t)UPS_RAW_CNT);
    if (rc < 0) {
        PRT_INFO("sec ups: raw item table rejected (%d)\r\n", rc);
    }
}

void secUps_task(void *argument) {
    SerialPort *port = (SerialPort *)argument;
    uint8_t data[UPS_DATA_MAX];

    if (!port) {
        vTaskDelete(NULL);
        return;
    }

    serial_port_setup(port);
    serial_port_hw_flow_disable(port);

    /*  부팅 때 이미 걸렸겠지만 한 번 더 부른다. 같은 표를 다시 걸면 그 자리를
        덮으므로 두 벌이 되지 않고, 이 태스크가 어디서 시작되든 자기 항목이
        있다는 것을 스스로 보장한다. */
    secUps_register();

    for (;;) {
        uint8_t replied = 0;
        uint8_t good = 0;
        uint8_t comm;

        if (serial_port_in_command_mode(port)) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        for (uint8_t q = 0; q < UPS_QUERY_CNT; q++) {
            int rc = ups_exchange(port, &UPS_QUERY[q], data);

            if (rc == 0) {
                replied++;
                good++;
            } else {
                ups_invalidate(&UPS_QUERY[q]);
                if (rc == -2) {
                    replied++;  /* 말은 했는데 알아들을 수 없었다 */
                }
            }
            vTaskDelay(pdMS_TO_TICKS(UPS_GAP_MS));
        }

        /*  다 알아들었으면 정상, 일부만 틀어졌으면 데이터 오류, 한 마디도
            못 받았으면 통신 경보. 28번이 이 셋을 그대로 싣는다. */
        if (good == UPS_QUERY_CNT) {
            comm = COMM_OK;
        } else if (replied > 0) {
            comm = COMM_DATA_ERROR;
        } else {
            comm = COMM_LOST;
        }

        ups_publish(comm);
        vTaskDelay(pdMS_TO_TICKS(UPS_POLL_MS));
    }
}
