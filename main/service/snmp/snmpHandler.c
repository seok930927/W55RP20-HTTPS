#include <FreeRTOS.h>
#include <task.h>

#include <stdint.h>
#include <string.h>

#include "common.h"
#include "deviceHandler.h"
#include "netHandler.h"
#include "sensor.h"
#include "snmp.h"
#include "snmp_custom.h"
#include "snmpHandler.h"
#include "socket.h"
#include "wizchip_conf.h"
#include "WIZ5XXSR-RP_Debug.h"
#include "ConfigData.h"

#define SNMP_AGENT_POLL_MS      10
#define SNMP_NET_WAIT_MS        200
#define SNMP_RETRY_WAIT_MS      1000

#define SNMP_TRAP_QUEUE_LEN     32

static uint8_t snmp_initialized = FALSE;
static uint8_t snmp_agent_ip[4] = {0, };

/*  Ring queue of cells awaiting a trap: a device and one of its value columns.

    It held device numbers before, and the flush sent every column of each, so
    one contact changing state put three traps on the wire and two of them
    carried a value nobody had asked about. A limit crossing is about one
    column, so the queue holds one.

    Producers: any task via snmp_notify_cell()/snmp_notify_device(), and the
    limit sweep below. Consumer: snmp_agent_task via snmp_flush_traps(). */
typedef struct {
    uint8_t dev;
    uint8_t col;
} TrapCell;

static volatile TrapCell snmp_trap_q[SNMP_TRAP_QUEUE_LEN];
static volatile uint8_t snmp_trap_q_head = 0;
static volatile uint8_t snmp_trap_q_tail = 0;

/*  Which cells were outside their limits on the previous sweep, one bit per
    value column. 64 bytes for the whole bank, where a timestamp per cell would
    be 768 -- and it does not need one: the repeat interval is a single setting
    for the unit, so everything still outside re-reports on the same beat. */
static uint8_t s_limit_out[DEVICE_COUNT];
static TickType_t s_limit_next_scan = 0;
static TickType_t s_limit_next_repeat = 0;

static void snmp_agent_close(void) {
    if (getSn_SR(SOCK_SNMP_AGENT) != SOCK_CLOSED) {
        close(SOCK_SNMP_AGENT);
    }

    snmp_initialized = FALSE;
    memset(snmp_agent_ip, 0x00, sizeof(snmp_agent_ip));
}

static void snmp_agent_init(void) {
    wiz_NetInfo netinfo;

    ctlnetwork(CN_GET_NETINFO, (void *)&netinfo);
    memcpy(snmp_agent_ip, netinfo.ip, sizeof(snmp_agent_ip));

    if (getSn_SR(SOCK_SNMP_AGENT) != SOCK_CLOSED) {
        close(SOCK_SNMP_AGENT);
    }

    DevConfig *conf = get_DevConfig_pointer();
    uint16_t agent_port = conf->snmp_agent_port ? conf->snmp_agent_port : SNMP_AGENT_PORT_DEFAULT;
    snmp_set_allowed_ips((const uint8_t (*)[4])conf->snmp_option.allowed_ip);  /* 4 slots */
    snmp_set_agent_port(agent_port);
    /*  Access control (community / permission / trap community) — injected here so
        the SNMP core never depends on DevConfig. Empty strings resolve to "public". */
    snmp_set_community(conf->snmp_community);
    snmp_set_permission(conf->snmp_perm);
    snmp_set_trap_community(conf->trap_community);
    snmpd_init(NULL, snmp_agent_ip, SOCK_SNMP_AGENT, SOCK_SNMP_TRAP);
    snmp_initialized = TRUE;

    PRT_INFO("SNMP Agent ready: udp://%d.%d.%d.%d:%d\r\n",
             snmp_agent_ip[0],
             snmp_agent_ip[1],
             snmp_agent_ip[2],
             snmp_agent_ip[3],
             agent_port);
}

void snmp_request_reinit(void) {
    snmp_agent_close();
}

void snmp_notify_cell(uint8_t dev, uint8_t col) {
    if (dev >= DEVICE_COUNT || col >= DEVICE_VALUE_COLS) {
        return;
    }
    taskENTER_CRITICAL();
    uint8_t next = (uint8_t)((snmp_trap_q_head + 1) % SNMP_TRAP_QUEUE_LEN);
    if (next != snmp_trap_q_tail) {            /* drop if queue full */
        snmp_trap_q[snmp_trap_q_head].dev = dev;
        snmp_trap_q[snmp_trap_q_head].col = col;
        snmp_trap_q_head = next;
    }
    taskEXIT_CRITICAL();
}

void snmp_notify_device(uint8_t dev) {
    for (uint8_t c = 0; c < DEVICE_VALUE_COLS; c++) {
        snmp_notify_cell(dev, c);
    }
}

/*  Is `v` outside the rule for value column `col`?

    A column with neither bit set is not watched, so it can never be out --
    which is what an untouched configuration holds, and why a unit upgraded
    into this firmware sends nothing new until somebody sets a limit. */
static int limit_breached(const DevConfig *conf, uint8_t col, int32_t v) {
    const struct __value_limit *lim;

    if (col >= VALUE_LIMIT_CNT) {
        return 0;               /* more value columns than the config holds */
    }
    lim = &conf->value_limit[col];

    if ((lim->use & VALUE_LIMIT_USE_LO) && v < value_limit_get(lim->lo)) {
        return 1;
    }
    if ((lim->use & VALUE_LIMIT_USE_HI) && v > value_limit_get(lim->hi)) {
        return 1;
    }
    return 0;
}

/*  Sweep the bank and queue a trap for every cell that has just gone outside
    its limits -- and again every trap_repeat_sec for as long as it stays
    outside, when that is set.

    Coming back inside is silent on purpose: the bit is cleared so the next
    crossing reports, and nothing goes on the wire. A manager that wants to
    know when a value recovers reads it back with a GET.

    This runs inside snmp_agent_task rather than in a task of its own. A task
    costs its stack out of the same 96 KB heap the TLS sessions come from, and
    the sweep is a walk over 64 rows on a period measured in seconds -- there
    is nothing here to keep a stack warm for. */
static void snmp_limit_scan(void) {
    DevConfig *conf = get_DevConfig_pointer();
    TickType_t now = xTaskGetTickCount();
    uint8_t scan_sec = conf->trap_scan_sec ? conf->trap_scan_sec
                       : TRAP_SCAN_SEC_DEFAULT;
    int repeat_due = 0;

    /*  Signed difference, so the comparison keeps working across the tick
        counter's wrap rather than stalling the sweep for 49 days. */
    if ((int32_t)(now - s_limit_next_scan) < 0) {
        return;
    }
    s_limit_next_scan = now + pdMS_TO_TICKS((uint32_t)scan_sec * 1000u);

    if (conf->trap_repeat_sec && (int32_t)(now - s_limit_next_repeat) >= 0) {
        repeat_due = 1;
        s_limit_next_repeat =
            now + pdMS_TO_TICKS((uint32_t)conf->trap_repeat_sec * 1000u);
    }

    for (uint8_t d = 0; d < DEVICE_COUNT; d++) {
        const Device *dev = device_get(d);

        if (dev == NULL || !dev->enabled) {
            s_limit_out[d] = 0;      /* a row that went away starts clean */
            continue;
        }
        /*  One bit per column, so eight columns is what this can track. The
            config holds VALUE_LIMIT_CNT of them, which is well under that. */
        for (uint8_t c = 0; c < DEVICE_VALUE_COLS && c < 8; c++) {
            uint8_t bit = (uint8_t)(1u << c);
            int out = limit_breached(conf, c, dev->value[c]);
            int was = (s_limit_out[d] & bit) != 0;

            if (!out) {
                s_limit_out[d] &= (uint8_t)~bit;
                continue;
            }
            if (!was || repeat_due) {
                snmp_notify_cell(d, c);
            }
            s_limit_out[d] |= bit;
        }
    }
}

/*  Drain the trap queue. Runs inside snmp_agent_task so it shares the trap
    socket sequentially — no cross-task contention. One trap is emitted per
    value column of each queued device. */
static void snmp_flush_traps(void) {
    DevConfig *conf = get_DevConfig_pointer();

    /* Trap Accept = NO: drain the queue without sending (keeps producers happy). */
    if (conf->trap_disable) {
        taskENTER_CRITICAL();
        snmp_trap_q_tail = snmp_trap_q_head;
        taskEXIT_CRITICAL();
        return;
    }

    while (snmp_trap_q_tail != snmp_trap_q_head) {
        uint8_t dev, col;

        taskENTER_CRITICAL();
        dev = snmp_trap_q[snmp_trap_q_tail].dev;
        col = snmp_trap_q[snmp_trap_q_tail].col;
        snmp_trap_q_tail = (uint8_t)((snmp_trap_q_tail + 1) % SNMP_TRAP_QUEUE_LEN);
        taskEXIT_CRITICAL();

        for (int t = 0; t < SNMP_TRAP_IP_CNT; t++) {
            const uint8_t *mgr = conf->snmp_option.trap_ip[t];
            if ((mgr[0] | mgr[1] | mgr[2] | mgr[3]) == 0) {
                continue;                     /* trap destination not set */
            }
            snmp_custom_sendValueTrap(mgr, snmp_agent_ip, dev, col);
        }
    }
}

void snmp_agent_task(void *argument) {
    (void)argument;

    while (1) {
#ifdef __USE_WATCHDOG__
        device_wdt_reset();
#endif

        if (get_net_status() != NET_IP_UP) {
            if (snmp_initialized) {
                snmp_agent_close();
            }

            vTaskDelay(pdMS_TO_TICKS(SNMP_NET_WAIT_MS));
            continue;
        }

        if (!snmp_initialized) {
            snmp_agent_init();
        }

        /*  Decide what is worth reporting before draining the queue, so a
            crossing found on this cycle goes out on this cycle. */
        snmp_limit_scan();

        /*  Send any queued traps (reuses the agent socket; snmpd_run below
            reopens it from SOCK_CLOSED on the same cycle). */
        snmp_flush_traps();

        /* Sync sensor bank → SNMP table before serving any request */
        snmp_custom_refresh();

        if (snmpd_run() < 0) {
            PRT_ERR("SNMP Agent run failed, retry after socket reinit\r\n");
            snmp_agent_close();
            vTaskDelay(pdMS_TO_TICKS(SNMP_RETRY_WAIT_MS));
            continue;
        }

        vTaskDelay(pdMS_TO_TICKS(SNMP_AGENT_POLL_MS));
    }
}
