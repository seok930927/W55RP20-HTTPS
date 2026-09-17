#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "hvacModbus.h"
#include "modbusMaster.h"       /* modbusMaster_init, modbus_read_regs       */
#include "itemBank.h"
#include "WIZ5XXSR-RP_Debug.h"

#define HVAC_SLAVE          1       /* 캡처된 요청이 01 이다                  */
#define HVAC_FUNC           0x03    /* holding register                       */
#define HVAC_REG_BASE       0x0000
#define HVAC_REG_COUNT      87      /* 요청의 0x0057                          */
#define HVAC_RSP_LEN        (5 + 2 * HVAC_REG_COUNT)    /* 179 바이트         */
#define HVAC_POLL_MS        1000

/*  레지스터가 없는 항목. 통신 상태처럼 우리가 만들어내는 값이다. */
#define HVAC_REG_NONE       0xFF

/*  통신 상태(100번). 문서 슬라이드 19 가 0 정상 / 2 경보라고 적어두었다.
    1을 쓰지 않는 것은 문서가 그렇게 정해서다. */
#define HVAC_ITEM_COMM      100
#define HVAC_COMM_OK        0
#define HVAC_COMM_ALARM     2

/*  ── 레지스터 표 ──────────────────────────────────────────────────────────

    한 줄이 한 항목이다. 차례로 레지스터 번호, OID 번호, 이름, 배율, 단위,
    그리고 부호 있는 값인지.

    표가 하나인 이유가 있다. 등록에는 ItemDef 배열이 필요하고 읽기에는
    레지스터 번호 배열이 필요한데, 둘을 따로 적어 두면 한쪽만 고치는 날이
    반드시 온다. 아래에서 두 배열을 이 목록 하나로 펼친다.

    배율은 문서가 OID 마다 적어둔 "/10" 을 옮긴 것이다. 온도는 /10 이고 습도는
    아니다 -- 캡처에서 현재온도 00DE 는 22.2도, 현재습도 0017 은 23% 로 읽힌다.
    편차는 문서가 따로 적지 않았지만, 온도 편차는 온도와 같은 단위여야 하므로
    /10 으로 두었다(냉방편차 000A = 1.0도). 확인이 필요한 부분이다.

    부호는 음수가 될 수 있는 것에만 붙인다. 운전시간은 양수로만 커지므로 부호
    없이 읽어야 32767시간을 넘겨도 음수로 뒤집히지 않는다.             */

/*  마지막 칸은 트랩 조건이다. 그 값이 되는 순간 한 번만 나간다.

    1 이 붙은 것은 고객 문서 슬라이드 19 의 "경보상태" 표에 실제로 적혀 있는
    항목들이고, 그 표 아래 "경보상태 값이 1이면 트랩 한번 발생" 이 이 칸의
    근거다. 이름에 경보가 들어간다고 다 붙이지는 않았다 -- 문서가 열거한 것만
    이다. 접점 입력(14-30)과 종합경보(67)가 빠져 있는데, 빠뜨린 것이 아니라
    문서에 없어서 확인이 필요한 자리다.

    66번과 70번에 둘 다 붙은 것은 문서가 엇갈려서다. 바이트 표에서는 66이
    필터경보이고 70이 가습실린더경보인데, 슬라이드 19는 필터경보를 70이라고
    적었다. 어느 쪽이 맞든 둘 다 경보 항목이라 트랩이 나가도 틀린 일이 되지
    않는다. 확인되면 하나를 0으로 내린다.

       레지스터 OID  이름                    배율 단위    부호 트랩 */
#define HVAC_ITEM_LIST(X)                                           \
    X(  0,   3, "현재온도",                  -1, "℃",  1, 0)        \
    X(  2,   4, "현재습도",                   0, "%",   0, 0)        \
    X(  4,  49, "가습기 전류",                0, "A",   0, 0)        \
    X( 14,  13, "송풍기 운전",                0, "",    0, 0)        \
    X( 15,   9, "냉방 운전",                  0, "",    0, 0)        \
    X( 16,  10, "난방 운전",                  0, "",    0, 0)        \
    X( 17,  11, "가습 운전",                  0, "",    0, 0)        \
    X( 18,  12, "제습 운전",                  0, "",    0, 0)        \
    X( 19,  31, "SSR 출력",                   0, "",    0, 0)        \
    X( 20,  32, "히터1",                      0, "",    0, 0)        \
    X( 21,  33, "히터2",                      0, "",    0, 0)        \
    X( 22,  34, "히터3",                      0, "",    0, 0)        \
    X( 23,  35, "히터4",                      0, "",    0, 0)        \
    X( 24,  36, "COMP1",                      0, "",    0, 0)        \
    X( 25,  37, "COMP2",                      0, "",    0, 0)        \
    X( 26,  38, "SOL1 출력",                  0, "",    0, 0)        \
    X( 27,  39, "SOL2 출력",                  0, "",    0, 0)        \
    X( 28,  40, "팬 출력",                    0, "",    0, 0)        \
    X( 29,  41, "종합경보 출력",              0, "",    0, 0)        \
    X( 30,  42, "냉수밸브 출력",              0, "",    0, 0)        \
    X( 31,  43, "가습기2 출력",               0, "",    0, 0)        \
    X( 32,  44, "가습기1 출력",               0, "",    0, 0)        \
    X( 33,  45, "가습기 SOL 출력",            0, "",    0, 0)        \
    X( 34,  46, "COMP3 출력",                 0, "",    0, 0)        \
    X( 35,  47, "COMP3 SOL 출력",             0, "",    0, 0)        \
    X( 36,  14, "COMP1 OCR 입력",             0, "",    0, 0)        \
    X( 37,  15, "COMP1 LP 입력",              0, "",    0, 0)        \
    X( 38,  16, "COMP2 OCR 입력",             0, "",    0, 0)        \
    X( 39,  17, "COMP2 LP 입력",              0, "",    0, 0)        \
    X( 40,  18, "가습기 OT 입력",             0, "",    0, 0)        \
    X( 41,  19, "히터 OT 입력",               0, "",    0, 0)        \
    X( 42,  20, "AFS 입력",                   0, "",    0, 0)        \
    X( 43,  21, "BWR OCR 입력",               0, "",    0, 0)        \
    X( 44,  22, "REMOTE",                     0, "",    0, 0)        \
    X( 45,  23, "누수 입력",                  0, "",    0, 0)        \
    X( 46,  24, "하론 입력",                  0, "",    0, 0)        \
    X( 47,  25, "COMP3 OCR 입력",             0, "",    0, 0)        \
    X( 48,  26, "COMP3 LP 입력",              0, "",    0, 0)        \
    X( 49,  27, "필터경보 입력",              0, "",    0, 0)        \
    X( 50,  28, "W/L1 경보 입력",             0, "",    0, 0)        \
    X( 51,  29, "W/L2 경보 입력",             0, "",    0, 0)        \
    X( 52,  30, "WSEN 경보 입력",             0, "",    0, 0)        \
    X( 53,  50, "COMP1 경보",                 0, "",    0, 1)        \
    X( 54,  51, "COMP2 경보",                 0, "",    0, 1)        \
    X( 55,  52, "COMP3 경보",                 0, "",    0, 1)        \
    X( 56,  53, "COMP1 저압경보",             0, "",    0, 1)        \
    X( 57,  54, "COMP2 저압경보",             0, "",    0, 1)        \
    X( 58,  55, "COMP3 저압경보",             0, "",    0, 1)        \
    X( 59,  56, "가습히터 경보",              0, "",    0, 1)        \
    X( 60,  57, "난방히터 경보",              0, "",    0, 1)        \
    X( 61,  58, "공기흐름 경보",              0, "",    0, 1)        \
    X( 62,  59, "팬 경보",                    0, "",    0, 1)        \
    X( 63,  60, "누수 경보",                  0, "",    0, 1)        \
    X( 64,  61, "고온 경보",                  0, "",    0, 1)        \
    X( 65,  62, "저온 경보",                  0, "",    0, 1)        \
    X( 66,  63, "고습 경보",                  0, "",    0, 1)        \
    X( 67,  64, "저습 경보",                  0, "",    0, 1)        \
    X( 68,  65, "소방 경보",                  0, "",    0, 1)        \
    X( 69,  66, "필터 경보",                  0, "",    0, 1)        \
    X( 70,  67, "종합 경보",                  0, "",    0, 0)        \
    X( 71,  68, "급수 경보",                  0, "",    0, 1)        \
    X( 72,  69, "배수실패 경보",              0, "",    0, 1)        \
    X( 73,  70, "가습실린더 경보",            0, "",    0, 1)        \
    X( 74,  71, "과전류 경보",                0, "",    0, 1)        \
    X( 75,  72, "운전시간",                   0, "h",   0, 0)        \
    X( 76,  73, "압축기1 운전시간",           0, "h",   0, 0)        \
    X( 77,  74, "압축기2 운전시간",           0, "h",   0, 0)        \
    X( 78,  75, "압축기3 운전시간",           0, "h",   0, 0)        \
    X( 79,  76, "가습기 운전시간",            0, "h",   0, 0)        \
    X( 80,  48, "운전상태",                   0, "",    0, 0)        \
    X( 81,   1, "설정온도",                  -1, "℃",  1, 0)        \
    X( 82,   2, "설정습도",                   0, "%",   0, 0)        \
    X( 83,   5, "냉방편차",                  -1, "℃",  1, 0)        \
    X( 84,   8, "제습편차",                   0, "%",   0, 0)        \
    X( 85,   7, "가습편차",                   0, "%",   0, 0)        \
    X( 86,   6, "난방편차",                  -1, "℃",  1, 0)        \
    X(HVAC_REG_NONE, HVAC_ITEM_COMM, "통신 상태", 0, "", 0, HVAC_COMM_ALARM)

#define HVAC_ROW_REG(reg, num, name, scale, unit, sign, trap)   (uint8_t)(reg),
#define HVAC_ROW_SIGN(reg, num, name, scale, unit, sign, trap)  (uint8_t)(sign),
#define HVAC_ROW_DEF(reg, num, name, scale, unit, sign, trap)   { (num), (name), (scale), (unit), (trap) },

static const uint8_t HVAC_REG[]   = { HVAC_ITEM_LIST(HVAC_ROW_REG) };
static const uint8_t HVAC_SIGN[]  = { HVAC_ITEM_LIST(HVAC_ROW_SIGN) };
static const ItemDef HVAC_ITEMS[] = { HVAC_ITEM_LIST(HVAC_ROW_DEF) };

#define HVAC_ITEM_CNT   (sizeof(HVAC_ITEMS) / sizeof(HVAC_ITEMS[0]))

_Static_assert(sizeof(HVAC_REG) == HVAC_ITEM_CNT, "reg/item table out of step");
_Static_assert(sizeof(HVAC_SIGN) == HVAC_ITEM_CNT, "sign/item table out of step");

/*  한 번에 다 받는다. 179바이트 응답과 87개 레지스터를 태스크 스택에 올리면
    스택이 프레임 하나에 끌려다니므로 여기 둔다. 이 표를 읽는 태스크는 하나뿐
    이라 공유될 일이 없다. */
static uint8_t s_rsp[HVAC_RSP_LEN];
static int16_t s_reg[HVAC_REG_COUNT];

static void hvac_store(void) {
    for (uint16_t i = 0; i < HVAC_ITEM_CNT; i++) {
        uint8_t reg = HVAC_REG[i];
        int32_t v;

        if (reg == HVAC_REG_NONE || reg >= HVAC_REG_COUNT) {
            continue;
        }

        /*  부호 없는 항목은 16비트 그대로 넓힌다. 그러지 않으면 운전시간이
            32767을 넘는 순간 음수로 보인다. */
        v = HVAC_SIGN[i] ? (int32_t)s_reg[reg]
            : (int32_t)(uint16_t)s_reg[reg];

        item_set(ITEM_DEV_HVAC, HVAC_ITEMS[i].num, v);
    }
}

void hvacModbus_register(void) {
    int rc = item_bank_register(ITEM_DEV_HVAC, HVAC_ITEMS, (uint16_t)HVAC_ITEM_CNT);

    if (rc < 0) {
        PRT_INFO("hvac: item table rejected (%d)\r\n", rc);
    }
}

void hvacModbus_task(void *argument) {
    SerialPort *port = (SerialPort *)argument;

    if (port == NULL) {
        vTaskDelete(NULL);
        return;
    }

    /*  부팅 때 이미 걸렸겠지만 한 번 더. 같은 표를 다시 걸면 덮어쓴다. */
    hvacModbus_register();

    modbusMaster_init(port);

    for (;;) {
        int rc;

        if (serial_port_in_command_mode(port)) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        rc = modbus_read_regs(port, HVAC_SLAVE, HVAC_FUNC, HVAC_REG_BASE,
                              HVAC_REG_COUNT, s_rsp, (int)sizeof(s_rsp), s_reg);

        if (rc == 0) {
            hvac_store();
            item_set(ITEM_DEV_HVAC, HVAC_ITEM_COMM, HVAC_COMM_OK);
        } else {
            /*  값은 그대로 두고 통신 상태만 세운다. 읽지 못한 것과 0이 온
                것은 다르고, 여기서 0으로 밀어버리면 웹 화면이 모든 경보를
                "정상" 으로 그린다. */
            item_set(ITEM_DEV_HVAC, HVAC_ITEM_COMM, HVAC_COMM_ALARM);
            PRT_INFO("hvac: ch%d poll error %d\r\n", port->channel, rc);
        }

        vTaskDelay(pdMS_TO_TICKS(HVAC_POLL_MS));
    }
}
