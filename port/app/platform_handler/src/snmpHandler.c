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

/*  Ring queue of device numbers awaiting a trap. Producer: any task via
    snmp_notify_device(). Consumer: snmp_agent_task via snmp_flush_traps(). */
static volatile uint8_t snmp_trap_q[SNMP_TRAP_QUEUE_LEN];
static volatile uint8_t snmp_trap_q_head = 0;
static volatile uint8_t snmp_trap_q_tail = 0;

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

void snmp_notify_device(uint8_t dev) {
    taskENTER_CRITICAL();
    uint8_t next = (uint8_t)((snmp_trap_q_head + 1) % SNMP_TRAP_QUEUE_LEN);
    if (next != snmp_trap_q_tail) {            /* drop if queue full */
        snmp_trap_q[snmp_trap_q_head] = dev;
        snmp_trap_q_head = next;
    }
    taskEXIT_CRITICAL();
}

/*  Drain the trap queue. Runs inside snmp_agent_task so it shares the trap
    socket sequentially — no cross-task contention. One trap is emitted per
    value column of each queued device. */
static void snmp_flush_traps(void) {
    DevConfig *conf = get_DevConfig_pointer();

    while (snmp_trap_q_tail != snmp_trap_q_head) {
        taskENTER_CRITICAL();
        uint8_t dev = snmp_trap_q[snmp_trap_q_tail];
        snmp_trap_q_tail = (uint8_t)((snmp_trap_q_tail + 1) % SNMP_TRAP_QUEUE_LEN);
        taskEXIT_CRITICAL();

        for (int t = 0; t < SNMP_TRAP_IP_CNT; t++) {
            const uint8_t *mgr = conf->snmp_option.trap_ip[t];
            if ((mgr[0] | mgr[1] | mgr[2] | mgr[3]) == 0) {
                continue;                     /* trap destination not set */
            }
            for (uint8_t c = 0; c < DEVICE_VALUE_COLS; c++) {
                snmp_custom_sendValueTrap(mgr, snmp_agent_ip, dev, c);
            }
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
