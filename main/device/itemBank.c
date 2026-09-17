#include <string.h>

#include "itemBank.h"

/*  OID 의 <장비> 자리. 슬롯 순서와 같게 둔다.

    항온항습기가 1 인 것은 고객 문서 슬라이드 19 에 숫자로 끝까지 적힌 예가
    서른 개 넘게 있어서 확정이고, UPS 의 12082 는 그 장비의 고유 번호다.

    오름차순이어야 한다. SNMP 표를 만드는 쪽이 이 순서대로 OID 를 쌓는다. */
static const uint32_t s_oid_dev[ITEM_DEV_CNT] = {
    1,          /* ITEM_DEV_HVAC */
    12082,      /* ITEM_DEV_UPS  */
};

static int32_t s_val[ITEM_DEV_CNT][ITEM_NUM_MAX];

/*  한 장비가 걸 수 있는 표의 수.

    항온항습기는 둘이다 -- 프로토콜의 데이터 항목과 접점 입력. 넷이면 나중에
    SET 제어 같은 것이 하나 더 붙어도 여유가 있다. */
#define ITEM_TABLE_MAX      4

static const ItemDef *s_defs[ITEM_DEV_CNT][ITEM_TABLE_MAX];
static uint16_t s_cnt[ITEM_DEV_CNT][ITEM_TABLE_MAX];
static uint8_t s_tabs[ITEM_DEV_CNT];

/*  그 번호가 등록됐나. 표를 처음부터 훑지 않으려고 둔다 -- 스윕 한 번에 값을
    수백 번 넣고 빼는데, 그때마다 표를 뒤지면 번호 하나가 표 길이만큼의 일이
    된다. 장비당 140바이트다.

    HAS_ITEM 는 등록됨, HAS_OID 는 그중 SNMP 로도 나가는 것. */
#define HAS_ITEM        0x01
#define HAS_OID         0x02

static uint8_t s_has[ITEM_DEV_CNT][ITEM_NUM_MAX];

/*  등록이 바뀐 횟수. 0 에서 시작하지 않는 이유는, 표를 만드는 쪽이 "아직 한
    번도 안 만들었음" 을 0 으로 표시할 수 있게 하기 위해서다. */
static uint32_t s_generation = 1;

/*  s_has 를 등록된 표 전부로부터 다시 만든다. 표 하나가 덮이면 그 표에만
    있던 번호는 없어져야 하는데, 지우면서 맞추는 것보다 다시 세는 편이
    틀릴 구석이 없다. */
static void rebuild_index(uint8_t dev) {
    memset(s_has[dev], 0, ITEM_NUM_MAX);

    for (uint8_t t = 0; t < s_tabs[dev]; t++) {
        for (uint16_t i = 0; i < s_cnt[dev][t]; i++) {
            const ItemDef *d = &s_defs[dev][t][i];

            s_has[dev][d->num] = (uint8_t)(HAS_ITEM |
                                           (d->web_only ? 0 : HAS_OID));
        }
    }
}

void item_bank_init(void) {
    memset(s_val, 0, sizeof(s_val));
    memset(s_defs, 0, sizeof(s_defs));
    memset(s_cnt, 0, sizeof(s_cnt));
    memset(s_tabs, 0, sizeof(s_tabs));
    memset(s_has, 0, sizeof(s_has));
    s_generation++;
}

int item_bank_register(uint8_t dev, const ItemDef *defs, uint16_t count) {
    uint8_t slot;

    if (dev >= ITEM_DEV_CNT || defs == NULL || count == 0) {
        return -1;
    }

    /*  같은 표를 다시 걸면 그 자리를 쓴다. 없으면 새 자리. */
    for (slot = 0; slot < s_tabs[dev]; slot++) {
        if (s_defs[dev][slot] == defs) {
            break;
        }
    }
    if (slot == s_tabs[dev] && slot >= ITEM_TABLE_MAX) {
        return -4;
    }

    for (uint16_t i = 0; i < count; i++) {
        uint8_t num = defs[i].num;

        if (num == 0 || num >= ITEM_NUM_MAX) {
            return -2;          /* 표가 쓸 수 없는 번호를 들고 있다 */
        }

        /*  다른 표가 이미 쓰는 번호면 거부한다. 받아주면 두 장치가 같은 OID
            로 서로의 값을 덮어쓰는데, 값이 그럴듯해서 안 드러난다.
            자기 자신을 다시 거는 경우는 덮는 것이므로 센다. */
        if (s_has[dev][num]) {
            uint8_t mine = 0;

            if (slot < s_tabs[dev]) {
                for (uint16_t j = 0; j < s_cnt[dev][slot]; j++) {
                    if (s_defs[dev][slot][j].num == num) {
                        mine = 1;
                        break;
                    }
                }
            }
            if (!mine) {
                return -3;
            }
        }
    }

    s_defs[dev][slot] = defs;
    s_cnt[dev][slot] = count;
    if (slot == s_tabs[dev]) {
        s_tabs[dev]++;
    }
    rebuild_index(dev);
    s_generation++;
    return 0;
}

uint32_t item_bank_generation(void) {
    return s_generation;
}

int item_set(uint8_t dev, uint8_t num, int32_t value) {
    if (dev >= ITEM_DEV_CNT || num >= ITEM_NUM_MAX) {
        return -1;
    }
    if (!s_has[dev][num]) {
        return -2;              /* 표에 없는 번호 */
    }
    s_val[dev][num] = value;
    return 0;
}

int item_get(uint8_t dev, uint8_t num, int32_t *out) {
    if (dev >= ITEM_DEV_CNT || num >= ITEM_NUM_MAX || out == NULL) {
        return -1;
    }
    if (!s_has[dev][num]) {
        return -2;
    }
    *out = s_val[dev][num];
    return 0;
}

uint8_t item_is_registered(uint8_t dev, uint8_t num) {
    if (dev >= ITEM_DEV_CNT || num >= ITEM_NUM_MAX) {
        return 0;
    }
    return (s_has[dev][num] & HAS_ITEM) ? 1u : 0u;
}

uint8_t item_has_oid(uint8_t dev, uint8_t num) {
    if (dev >= ITEM_DEV_CNT || num >= ITEM_NUM_MAX) {
        return 0;
    }
    return (s_has[dev][num] & HAS_OID) ? 1u : 0u;
}

uint32_t item_oid_device(uint8_t dev) {
    return (dev < ITEM_DEV_CNT) ? s_oid_dev[dev] : 0;
}

uint16_t item_count(uint8_t dev) {
    uint16_t n = 0;

    if (dev >= ITEM_DEV_CNT) {
        return 0;
    }
    for (uint8_t t = 0; t < s_tabs[dev]; t++) {
        n = (uint16_t)(n + s_cnt[dev][t]);
    }
    return n;
}

/*  등록된 순서대로 하나씩. 표가 여러 개면 걸린 순서로 이어 붙여 센다 --
    번호 순은 아니다. 번호 순이 필요한 쪽(SNMP 표)은 번호로 훑으면서
    item_is_registered() 를 묻는다. */
const ItemDef *item_at(uint8_t dev, uint16_t idx) {
    if (dev >= ITEM_DEV_CNT) {
        return NULL;
    }
    for (uint8_t t = 0; t < s_tabs[dev]; t++) {
        if (idx < s_cnt[dev][t]) {
            return &s_defs[dev][t][idx];
        }
        idx = (uint16_t)(idx - s_cnt[dev][t]);
    }
    return NULL;
}
