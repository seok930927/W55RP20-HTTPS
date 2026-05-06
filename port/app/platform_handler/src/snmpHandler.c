#include <FreeRTOS.h>
#include <task.h>

#include <stdint.h>
#include <string.h>

#include "common.h"
#include "deviceHandler.h"
#include "netHandler.h"
#include "snmp.h"
#include "snmpHandler.h"
#include "socket.h"
#include "wizchip_conf.h"
#include "WIZ5XXSR-RP_Debug.h"

#define SNMP_AGENT_POLL_MS      10
#define SNMP_NET_WAIT_MS        200
#define SNMP_RETRY_WAIT_MS      1000

static uint8_t snmp_initialized = FALSE;
static uint8_t snmp_agent_ip[4] = {0, };

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

    snmpd_init(NULL, snmp_agent_ip, SOCK_SNMP_AGENT, SOCK_SNMP_AGENT);
    snmp_initialized = TRUE;

    PRT_INFO("SNMP Agent ready: udp://%d.%d.%d.%d:%d\r\n",
             snmp_agent_ip[0],
             snmp_agent_ip[1],
             snmp_agent_ip[2],
             snmp_agent_ip[3],
             PORT_SNMP_AGENT);
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

        if (snmpd_run() < 0) {
            PRT_ERR("SNMP Agent run failed, retry after socket reinit\r\n");
            snmp_agent_close();
            vTaskDelay(pdMS_TO_TICKS(SNMP_RETRY_WAIT_MS));
            continue;
        }

        vTaskDelay(pdMS_TO_TICKS(SNMP_AGENT_POLL_MS));
    }
}
