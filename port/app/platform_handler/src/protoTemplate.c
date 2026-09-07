/*  Starting point for a serial protocol of your own — see protoTemplate.h.

    As shipped it sends nothing and writes nothing; it exists so that the parts
    that are easy to get wrong are already right, and the only code you add is
    the part only you can write: the bytes your device speaks.  */

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "FreeRTOS.h"
#include "task.h"

#include "protoTemplate.h"
#include "seg.h"                /* SEG_DATA0_CH, opmode, DEVICE_AT_MODE      */
#include "sensor.h"             /* device_assign, device_setValue            */
#include "snmpHandler.h"        /* snmp_notify_device                        */
#include "WIZ5XXSR-RP_Debug.h"  /* PRT_INFO                                  */

/*  Which device-bank row this port publishes into. Two ports running this at
    once must not share rows, so they are kept apart the way modbusMaster.c
    keeps its own apart. */
#define PROTO_BANK_BASE_CH0   16
#define PROTO_BANK_BASE_CH1   48

#define PROTO_RSP_TIMEOUT     200   /* ms to wait for a full response frame  */
#define PROTO_POLL_PERIOD    1000   /* ms between exchanges                  */
#define PROTO_RSP_MAX          64   /* longest response this expects         */

void protoTemplate_init(SerialPort *port) {
    /*  Applies that port's stored baud/parity/data bits, assigns its pins and
        puts the RS-485 direction line in its receive state. Read the settings
        afterwards through port->opt if you need them. */
    serial_port_setup(port);

    /*  Say so if your device uses RTS/CTS; most half-duplex buses do not. */
    uart_set_hw_flow(port->uart, false, false);

    PRT_INFO("protoTemplate: ready (ch%d, DE=GPIO%u)\r\n",
             port->channel, port->de_pin);
}

/*  Read a response into `buf`, up to `want` bytes or until `timeout_ms`.
    Returns how many bytes arrived. */
static int proto_recv(SerialPort *port, uint8_t *buf, int want, uint32_t timeout_ms) {
    TickType_t start = xTaskGetTickCount();
    int got = 0;

    while (got < want) {
        int32_t ch;
        /*  serial_port_getc() also watches for the +++ escape on the config
            port. Reading with uart_getc() instead would leave this port unable
            to reach command mode. */
        while (got < want && (ch = serial_port_getc(port)) != RET_NOK) {
            buf[got++] = (uint8_t)ch;
        }
        if (got >= want) {
            break;
        }
        if ((xTaskGetTickCount() - start) >= pdMS_TO_TICKS(timeout_ms)) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(2));   /* the 32-byte FIFO holds while we yield */
    }
    return got;
}

int protoTemplate_poll(SerialPort *port) {
    uint8_t rsp[PROTO_RSP_MAX];
    int n;

    /*  Drop anything left over from a previous exchange. */
    while (uart_is_readable(port->uart)) {
        (void)serial_port_getc(port);
    }

    /*  ── TODO 1: send your request ──────────────────────────────────────
        serial_port_puts() holds the RS-485 direction line for the whole frame
        and waits for the shift register to drain before releasing the bus, so
        the far end never sees a truncated frame.

            static const uint8_t req[] = { 0x01, 0x02, 0x03 };
            serial_port_puts(port, req, sizeof(req));
        ------------------------------------------------------------------ */

    n = proto_recv(port, rsp, sizeof(rsp), PROTO_RSP_TIMEOUT);
    if (n <= 0) {
        return -1;                      /* timeout */
    }

    /*  ── TODO 2: parse `rsp` and publish ────────────────────────────────
        Values reach the web page and SNMP by going into the device bank; the
        column names, units and decimal places come from g_value_columns[] in
        sensor.c, and the SNMP OIDs follow from it. Nothing else to wire up.

            uint8_t row = (port->channel == SEG_DATA0_CH)
                          ? PROTO_BANK_BASE_CH0 : PROTO_BANK_BASE_CH1;
            device_setValue(row, 0, value_from(rsp));
            snmp_notify_device(row);    // only if a trap should go out
        ------------------------------------------------------------------ */
    (void)rsp;

    return 0;
}

void protoTemplate_task(void *argument) {
    SerialPort *port = (SerialPort *)argument;
    uint8_t base;
    char name[DEVICE_NAME_MAX];

    if (port == NULL) {
        vTaskDelete(NULL);
        return;
    }
    base = (port->channel == SEG_DATA0_CH) ? PROTO_BANK_BASE_CH0
                                           : PROTO_BANK_BASE_CH1;

    protoTemplate_init(port);

    /*  Claim the row now so the device shows up in the web table before the
        first successful exchange, rather than appearing out of nowhere. */
    snprintf(name, sizeof(name), "CUSTOM-%d", port->channel);
    device_assign(base, name);

    while (1) {
        /*  Command mode owns the config port while it lasts. Polling through
            it would fight segcp for the same FIFO. */
        if ((port->channel == SEG_DATA0_CH) && (opmode == DEVICE_AT_MODE)) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (protoTemplate_poll(port) != 0) {
            PRT_INFO("protoTemplate: ch%d no response\r\n", port->channel);
        }

        vTaskDelay(pdMS_TO_TICKS(PROTO_POLL_PERIOD));
    }
}
