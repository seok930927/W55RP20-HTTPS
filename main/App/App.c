/**
    Copyright (c) 2022 WIZnet Co.,Ltd

    SPDX-License-Identifier: BSD-3-Clause
*/

/**
    ----------------------------------------------------------------------------------------------------
    Includes
    ----------------------------------------------------------------------------------------------------
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "WIZnet_board.h"
#include "port_common.h"
#include "ConfigData.h"
#include "uartHandler.h"
#include "timerHandler.h"
#include "deviceHandler.h"
#include "flashHandler.h"
#include "httpHandler.h"
#include "wizchip_conf.h"
#include "netHandler.h"
#include "snmpHandler.h"
#include "segcp.h"
#include "sensor.h"
#include "sensorUart.h"
#include "modbusMaster.h"

#include "w5x00_spi.h"

/**
    ----------------------------------------------------------------------------------------------------
    Macros
    ----------------------------------------------------------------------------------------------------
*/
/* Task */

#define NET_TASK_STACK_SIZE 1024
#define NET_TASK_PRIORITY 8

#define HTTP_WEBSERVER_TASK_STACK_SIZE 2048
#define HTTP_WEBSERVER_TASK_PRIORITY 23

#define SNMP_TASK_STACK_SIZE 2048
#define SNMP_TASK_PRIORITY 7

#define SEGCP_UDP_TASK_STACK_SIZE 1024
#define SEGCP_UDP_TASK_PRIORITY 52

#define SEGCP_TCP_TASK_STACK_SIZE 1024
#define SEGCP_TCP_TASK_PRIORITY 51

#define SENSOR_UART_TASK_STACK_SIZE 1024
#define SENSOR_UART_TASK_PRIORITY 9

#define MODBUS_MASTER_TASK_STACK_SIZE 1024
#define MODBUS_MASTER_TASK_PRIORITY 9

#define HEAP_MONITOR_TASK_STACK_SIZE 1024
#define HEAP_MONITOR_TASK_PRIORITY 6

#define START_TASK_STACK_SIZE 512
#define START_TASK_PRIORITY 65

/**
    ----------------------------------------------------------------------------------------------------
    Variables
    ----------------------------------------------------------------------------------------------------
*/
xSemaphoreHandle net_segcp_udp_sem = NULL;
xSemaphoreHandle net_segcp_tcp_sem = NULL;
xSemaphoreHandle net_http_webserver_sem = NULL;
xSemaphoreHandle net_seg_sem = NULL;
xSemaphoreHandle net_seg_u2e_sem = NULL;
xSemaphoreHandle eth_interrupt_sem = NULL;
xSemaphoreHandle segcp_udp_sem = NULL;
xSemaphoreHandle segcp_tcp_sem = NULL;
xSemaphoreHandle segcp_uart_sem = NULL;
xSemaphoreHandle seg_u2e_sem = NULL;
xSemaphoreHandle seg_e2u_sem = NULL;
xSemaphoreHandle seg_spi_pending_sem = NULL;
xSemaphoreHandle seg_sem = NULL;
xSemaphoreHandle seg_critical_sem = NULL;
xSemaphoreHandle seg_timer_sem = NULL;
xSemaphoreHandle wizchip_critical_sem = NULL;
xSemaphoreHandle flash_critical_sem = NULL;

TimerHandle_t seg_inactivity_timer = NULL;
TimerHandle_t seg_keepalive_timer = NULL;
TimerHandle_t seg_auth_timer = NULL;
TimerHandle_t spi_reset_timer = NULL;
TimerHandle_t reset_timer = NULL;

TaskHandle_t seg_mqtt_yield_task_handle = NULL;

/**
    ----------------------------------------------------------------------------------------------------
    Functions
    ----------------------------------------------------------------------------------------------------
*/
static void RP2040_Init(void);
static void RP2040_W5X00_Init(void);
static void set_W5X00_NetTimeout(void);
static void set_minimal_runtime_config(void);
void start_task(void *argument);
void heap_monitor_task(void *argument);

/**
    ----------------------------------------------------------------------------------------------------
    Main
    ----------------------------------------------------------------------------------------------------
*/
int main() {
    xTaskCreate(start_task, "Start_Task", START_TASK_STACK_SIZE, NULL, START_TASK_PRIORITY, NULL);
    vTaskStartScheduler();

    while (1) {
        ;
    }
}

/**
    ----------------------------------------------------------------------------------------------------
    Functions
    ----------------------------------------------------------------------------------------------------
*/
/* Task */

static void RP2040_Init(void) {
#if 0
    set_sys_clock_khz(PLL_SYS_KHZ, true);

    clock_configure(
        clk_peri,
        0,                                                // No glitchless mux
        CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS, // System PLL on AUX mux
        PLL_SYS_KHZ * 1000,                               // Input frequency
        PLL_SYS_KHZ * 1000                                // Output (must be same as no divider)
    );
#endif
    //SystemCoreClockUpdate();
    flash_critical_section_init();
    sleep_ms(10);
}

static void RP2040_W5X00_Init(void) {
    wizchip_spi_initialize((PLL_SYS_KHZ * 1000 / 4)); //33.25Mhz
    wizchip_cris_initialize();

    wizchip_reset();
    wizchip_initialize();
    wizchip_check();
}

static void set_W5X00_NetTimeout(void) {
    DevConfig *dev_config = get_DevConfig_pointer();
    wiz_NetTimeout net_timeout;

    net_timeout.retry_cnt = dev_config->network_option.tcp_rcr_val;
    net_timeout.time_100us = 2000;
    wizchip_settimeout(&net_timeout);

    wizchip_gettimeout(&net_timeout); // TCP timeout settings
    PRT_INFO(" - Network Timeout Settings - RCR: %d, RTR: %d\r\n", net_timeout.retry_cnt, net_timeout.time_100us);
}

static void set_minimal_runtime_config(void) {
    DevConfig *dev_config = get_DevConfig_pointer();

    dev_config->network_connection.working_mode = TCP_SERVER_MODE;
    dev_config->network_connection.dns_use = DISABLE;
    /*  serial_option.uart_interface is a per-boot scratch value, NOT storage: the
        bootloader owns that same flash byte under a different enum and rewrites
        it after a firmware update. The real setting lives in serial_intf_sel
        (extension section, which the bootloader cannot reach) and is copied in
        here on every boot, so whatever boot wrote is always discarded. */
    uint8_t intf = dev_config->serial_intf_sel;
    if (intf > UART_IF_RS485_REVERSE) {
        intf = UART_IF_RS232_TTL;
    }
    dev_config->serial_option.uart_interface = intf;
    /*  NOTE: flow_control is no longer forced here — it comes from stored
        serial_option so the web "Handshake" setting (#11) persists across reboot.
        baud_rate / data_bits / parity / protocol were never forced (stored). */
}

void heap_monitor_task(void *argument) {
    (void)argument;

    while (1) {
        printf("Free heap: %d\n", xPortGetFreeHeapSize());
        printf("Min free heap: %d\n", xPortGetMinimumEverFreeHeapSize());
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

void start_task(void *argument) {
    (void)argument;

    /*  Pin this task to Core 0. stdio_init_all() -> stdio_usb_init() asserts that
        it runs on the default alarm pool core (Core 0); under FreeRTOS SMP this
        task could otherwise be scheduled on Core 1 and trip that assert. Pinning
        also keeps USBCTRL_IRQ and its mutex-coordinated tud_task() worker on Core 0. */
    vTaskCoreAffinitySet(NULL, 1U << 0);

    /*  stdio_init_all() here (after the scheduler is running), not in main().
        pico_stdio_usb services tud_task() from its own IRQ worker, serialized with
        printf output via an internal mutex — do NOT add a separate tud_task() task,
        that races the worker and stalls output. */
    stdio_init_all();

    RP2040_Init();
    RP2040_W5X00_Init();

    // 현재 시스템 클럭(Hz) 가져오기
    uint32_t current_hz = clock_get_hz(clk_sys);

    // MHz 단위로 변환해서 출력
    printf("Current System Clock: %lu Hz (%lu MHz)\n", current_hz, current_hz / 1000000);

    load_DevConfig_from_storage();
    RP2040_Board_Init();
    set_minimal_runtime_config();
    serial_port_init_all();   /* both ports ready before check_mac_address() */
    check_mac_address();

    Net_Conf();
    display_Dev_Info_main();
    display_Net_Info();

    set_W5X00_NetTimeout();
    Timer_Configuration();

    /* ── Device bank ──────────────────────────────────────────────────── */
    device_init();

    /*  Serial ports, in bank order. Each one carries its own protocol setting;
        the Modbus poller takes the ports set to Modbus RTU and sensorUart
        claims the rest. */
    for (uint8_t p = 0; p < SERIAL_PORT_CNT; p++) {
        printf("=== ch%u protocol=%u (1=Modbus poller ON) ===\r\n",
               p, g_serial_port[p].protocol);
    }

    /*  Demo entries matching the reference capture (온습도센서화면전송.pptx).
        Set unconditionally (both modes) as initial values:
          slave 1: temp 0x00FA=250 (25.0C), hum 0x0208=520 (52.0%)
          slave 2: temp 0x010F=271 (27.1C), hum 0x01FC=508 (50.8%)
          slave 3: temp 0x0116=278 (27.8C), hum 0x01F6=502 (50.2%)
          slave 4: temp 0x0114=276 (27.6C), hum 0x0200=512 (51.2%)
        In Modbus mode these are overwritten by modbusMaster_task once real
        responses arrive (and remain visible for any slave that never replies). */
    {
        static const struct {
            int16_t temp;
            int16_t hum;
        } demo[4] = {
            { 250, 520 }, { 271, 508 }, { 278, 502 }, { 276, 512 },
        };
        for (uint8_t d = 0; d < 4; d++) {
            char name[DEVICE_NAME_MAX];
            snprintf(name, sizeof(name), "TH-%d", d + 1);
            device_assign(d, name);
            device_setValue(d, 0, demo[d].temp);   /* col 0 — temperature */
            device_setValue(d, 1, demo[d].hum);    /* col 1 — humidity    */
            device_setValue(d, 2, 0);              /* col 2 — alarm       */
        }
    }

    /* ── UART RX → sensor bank ingestion ─────────────────────────────── */
    sensorUart_init();

    net_http_webserver_sem = xSemaphoreCreateCounting((unsigned portBASE_TYPE)0x7fffffff, (unsigned portBASE_TYPE)0);
    net_segcp_udp_sem = xSemaphoreCreateCounting((unsigned portBASE_TYPE)0x7fffffff, (unsigned portBASE_TYPE)0);
    net_segcp_tcp_sem = xSemaphoreCreateCounting((unsigned portBASE_TYPE)0x7fffffff, (unsigned portBASE_TYPE)0);
    segcp_udp_sem = xSemaphoreCreateCounting((unsigned portBASE_TYPE)0x7fffffff, (unsigned portBASE_TYPE)0);
    segcp_tcp_sem = xSemaphoreCreateCounting((unsigned portBASE_TYPE)0x7fffffff, (unsigned portBASE_TYPE)0);

#if defined(MBEDTLS_PLATFORM_C) && defined(MBEDTLS_PLATFORM_MEMORY)
    mbedtls_platform_set_calloc_free(pvPortCalloc, vPortFree);
#endif
    reset_timer = xTimerCreate("reset_timer", pdMS_TO_TICKS(5000), pdFALSE, 0, reset_timer_callback);
    xTaskCreate(net_status_task, "Net_Status_Task", NET_TASK_STACK_SIZE, NULL, NET_TASK_PRIORITY, NULL);
    xTaskCreate(http_webserver_task, "http_webserver_task", HTTP_WEBSERVER_TASK_STACK_SIZE, NULL, HTTP_WEBSERVER_TASK_PRIORITY, NULL);
    xTaskCreate(snmp_agent_task, "SNMP_Agent_Task", SNMP_TASK_STACK_SIZE, NULL, SNMP_TASK_PRIORITY, NULL);
    xTaskCreate(segcp_udp_task, "SEGCP_udp_Task", SEGCP_UDP_TASK_STACK_SIZE, NULL, SEGCP_UDP_TASK_PRIORITY, NULL);
    xTaskCreate(segcp_tcp_task, "SEGCP_tcp_Task", SEGCP_TCP_TASK_STACK_SIZE, NULL, SEGCP_TCP_TASK_PRIORITY, NULL);
    xTaskCreate(sensorUart_task, "Sensor_UART_Task", SENSOR_UART_TASK_STACK_SIZE, NULL, SENSOR_UART_TASK_PRIORITY, NULL);
    for (uint8_t p = 0; p < SERIAL_PORT_CNT; p++) {
        if (g_serial_port[p].protocol != modbus_rtu) {
            continue;
        }
        char task_name[configMAX_TASK_NAME_LEN];
        snprintf(task_name, sizeof(task_name), "Modbus_ch%u", p);
        xTaskCreate(modbusMaster_task, task_name, MODBUS_MASTER_TASK_STACK_SIZE,
                    &g_serial_port[p], MODBUS_MASTER_TASK_PRIORITY, NULL);
    }
    // xTaskCreate(heap_monitor_task, "Heap_Monitor_Task", HEAP_MONITOR_TASK_STACK_SIZE, NULL, HEAP_MONITOR_TASK_PRIORITY, NULL);
#ifdef __USE_WATCHDOG__
    watchdog_enable(8388, 0);
#endif
    vTaskDelete(NULL);
}

void vApplicationPassiveIdleHook(void) {
#ifdef __USE_WATCHDOG__
    static uint8_t core_num = 0;
    uint8_t core_num_tmp = get_core_num();

    if (core_num != core_num_tmp) {
        device_wdt_reset();
        core_num = core_num_tmp;
    }
#endif

}

void vApplicationStackOverflowHook(TaskHandle_t pxTask, char *pcTaskName) {
    (void) pcTaskName;
    (void) pxTask;

    /*  Run time stack overflow checking is performed if
        configCHECK_FOR_STACK_OVERFLOW is defined to 1 or 2.  This hook
        function is called if a stack overflow is detected. */

    /* Force an assert. */
    printf("vApplicationStackOverflowHook [%s]\r\n", pcTaskName);
    configASSERT((volatile void *) NULL);
}
