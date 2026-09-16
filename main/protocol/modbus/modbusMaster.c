#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"

#include "FreeRTOS.h"
#include "task.h"

#include "modbusMaster.h"
#include "sensor.h"             /* device_assign, device_setValue            */
#include "ConfigData.h"         /* get_DevConfig_pointer, serial_option_485   */
#include "uartHandler.h"        /* baud_table, word_len/parity/stop enums     */
#include "WIZnet_board.h"       /* board pin macros (via uartHandler)           */
#include "WIZ5XXSR-RP_Debug.h"  /* PRT_INFO                                    */

/*  CRC-16 (Modbus) — defined in the bundled modbus lib (mbcrc.c), linked via
    APP_MODBUS_FILES. Declared here to avoid pulling in the whole modbus headers. */
extern uint16_t usMBCRC16(uint8_t *pucFrame, uint16_t usLen);

extern uint32_t baud_table[];

/*  Slave range to poll. Each module is a separate RS-485 device with its own
    address; module addr k maps to device bank row (k-1). The reference capture
    (온습도센서화면전송.pptx) polls slaves 1..4; spec allows up to 15.
    Raise MODBUS_SLAVE_LAST as more modules are wired. */
#define MODBUS_SLAVE_FIRST   1
#define MODBUS_SLAVE_LAST    4

/* slave, func, byte count, MODBUS_REG_COUNT registers, CRC16 */
#define MODBUS_RSP_LEN       (5 + 2 * MODBUS_REG_COUNT)

#define MODBUS_RSP_TIMEOUT   150    /* ms to wait for a full response frame      */
#define MODBUS_POLL_PERIOD   1000   /* ms between full poll cycles               */

/*  The request carries the register quantity in a byte and the reply its byte
    count in a byte, and mb_recv() fills a fixed buffer -- so the column count
    has to stay inside what both can say. */
_Static_assert(MODBUS_REG_COUNT >= 1 && MODBUS_REG_COUNT <= 125,
               "DEVICE_VALUE_COLS must be 1..125 for the Modbus master");

/*  Configure `uart` for Modbus. baud/format come from that port's serial_option
    so the web serial settings apply (sensor default is 9600 8N1 — set Baud=9600).
    No RX IRQ: the master drains the RX FIFO itself. */
void modbusMaster_init(SerialPort *port) {
    struct __serial_option *opt;

    serial_port_setup(port);
    serial_port_hw_flow_disable(port);            /* Modbus RTU never uses RTS/CTS */
    opt = port->opt;

    PRT_INFO("modbusMaster: master ready (ch%d TX=GP%u RX=GP%u, %lu-%u-%s-%u)\r\n",
             port->channel, port->tx_pin, port->rx_pin,
             (unsigned long)baud_table[opt->baud_rate < baud_max ? opt->baud_rate : baud_115200],
             (opt->data_bits == word_len7) ? 7 : 8,
             parity_table[opt->parity <= parity_mark ? opt->parity : parity_none],
             (opt->stop_bits == stop_bit2) ? 2 : 1);
}

/*  Hex into `buf`, stopping at the buffer instead of past it. Returns buf so a
    log call can use it inline. Replaces a row of fixed %02X slots that had to
    be re-counted by hand every time a frame length changed. */
static const char *hexdump(char *buf, size_t cap, const uint8_t *p, int len) {
    size_t used = 0;

    for (int i = 0; i < len && used + 4 <= cap; i++) {
        snprintf(buf + used, cap - used, "%02X ", p[i]);
        used += 3;
    }
    buf[used ? used - 1 : 0] = '\0';   /* drop the trailing space */
    return buf;
}

/* Read up to `want` bytes within `timeout_ms`. Returns the number received. */
static int mb_recv(SerialPort *port, uint8_t *buf, int want, uint32_t timeout_ms) {
    TickType_t start = xTaskGetTickCount();
    int got = 0;
    while (got < want) {
        int32_t ch;
        while (got < want && (ch = serial_port_getc(port)) != RET_NOK) {
            buf[got++] = (uint8_t)ch;
        }
        if (got >= want) {
            break;
        }
        if ((xTaskGetTickCount() - start) >= pdMS_TO_TICKS(timeout_ms)) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(2));   /* FIFO (32B) buffers while we yield */
    }
    return got;
}

int modbus_read_values(SerialPort *port, uint8_t slave, int16_t *out) {
    /*  Request: read MODBUS_REG_COUNT input registers (Func 04) from address
        0x0000. The quantity is 16-bit on the wire; the static assert above
        keeps it inside the low byte. */
    uint8_t req[8] = { slave, 0x04, 0x00, 0x00, 0x00, MODBUS_REG_COUNT, 0x00, 0x00 };
    uint16_t crc = usMBCRC16(req, 6);
    req[6] = (uint8_t)(crc & 0xFF);   /* Modbus CRC: low byte first */
    req[7] = (uint8_t)(crc >> 8);

    serial_port_flush_rx(port);

    /*  Sends the frame and drives the RS-485 direction line around it, waiting
        for the shift register to drain before releasing the bus, so the slave
        never sees a truncated frame. No-op direction handling on TTL/RS-232
        and RS-422. */
    serial_port_puts(port, req, sizeof(req));

    uint8_t rsp[MODBUS_RSP_LEN];
    int n = mb_recv(port, rsp, MODBUS_RSP_LEN, MODBUS_RSP_TIMEOUT);
    char txs[3 * sizeof(req) + 1];
    char rxs[3 * MODBUS_RSP_LEN + 1];

    /* DIAG: show what we sent and what (if anything) came back. */
    PRT_INFO("modbus ch%d TX: %s | RX %d bytes: %s\r\n",
             port->channel,
             hexdump(txs, sizeof(txs), req, (int)sizeof(req)),
             n, hexdump(rxs, sizeof(rxs), rsp, n));

    if (n != MODBUS_RSP_LEN) {
        return -1;                               /* timeout / short frame */
    }
    if (usMBCRC16(rsp, MODBUS_RSP_LEN) != 0) {
        return -2;                               /* CRC mismatch */
    }
    if (rsp[0] != slave || rsp[1] != 0x04
            || rsp[2] != (uint8_t)(2 * MODBUS_REG_COUNT)) {
        return -3;                               /* wrong addr/func/bytecount */
    }

    /* Register n -> value column n, big-endian as Modbus sends them. */
    for (uint8_t i = 0; i < MODBUS_REG_COUNT; i++) {
        out[i] = (int16_t)(((uint16_t)rsp[3 + 2 * i] << 8) | rsp[4 + 2 * i]);
    }
    return 0;
}

void modbusMaster_task(void *argument) {
    SerialPort *port = (SerialPort *)argument;
    uint8_t src;

    if (port == NULL) {
        vTaskDelete(NULL);
        return;
    }

    /*  One row per slave, asked for rather than chosen, and addressed from
        here by slave number -- the bank keeps track of where they landed. */
    src = (uint8_t)port->channel;
    if (device_bank_reserve(MODBUS_SLAVE_LAST - MODBUS_SLAVE_FIRST + 1, src) < 0) {
        PRT_INFO("modbusMaster: ch%d no room in the device bank\r\n", port->channel);
        vTaskDelete(NULL);
        return;
    }

    modbusMaster_init(port);

    /*  Pre-assign a device-bank row per slave so it shows up even before the
        first successful read. */
    for (uint8_t s = MODBUS_SLAVE_FIRST; s <= MODBUS_SLAVE_LAST; s++) {
        char name[DEVICE_NAME_MAX];
        snprintf(name, sizeof(name), "TH-%u", s);
        device_bank_assign(src, (uint8_t)(s - MODBUS_SLAVE_FIRST), name);
    }

    while (1) {
        /*  While the operator is in command mode that port belongs to the
            config handler; polling would fight for the same FIFO. */
        if (serial_port_in_command_mode(port)) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        for (uint8_t s = MODBUS_SLAVE_FIRST; s <= MODBUS_SLAVE_LAST; s++) {
            int16_t v[MODBUS_REG_COUNT];
            int r = modbus_read_values(port, s, v);

            if (r == 0) {
                uint8_t idx = (uint8_t)(s - MODBUS_SLAVE_FIRST);
                char   line[24 * MODBUS_REG_COUNT];
                size_t ln = 0;

                line[0] = '\0';

                /*  Every column, not just the ones this protocol happens to
                    care about. A column left alone keeps whatever was in it,
                    which the web page draws as a reading of 0 rather than as
                    "no value" -- an alarm column that always says normal. */
                for (uint8_t c = 0; c < MODBUS_REG_COUNT; c++) {
                    device_bank_setValue(src, idx, c, v[c]);
                }

                /*  Formatting is a second pass on purpose. Sharing the loop
                    above would tie publishing to the log buffer: a column name
                    long enough to fill `line` would stop the loop and silently
                    drop every column after it from the bank. Running out of
                    room here shortens the log line and nothing else. */
                for (uint8_t c = 0; c < MODBUS_REG_COUNT; c++) {
                    const ValueColumn *vc = valueColumn_get(c);
                    int w = snprintf(line + ln, sizeof(line) - ln, "%s%s=%d",
                                     ln ? "  " : "", vc ? vc->name : "?", v[c]);

                    if (w < 0 || (size_t)w >= sizeof(line) - ln) {
                        break;              /* keep the line as far as it got */
                    }
                    ln += (size_t)w;
                }
                PRT_INFO("modbusMaster: ch%d slave %u  %s\r\n",
                         port->channel, s, line);
            } else {
                PRT_INFO("modbusMaster: ch%d slave %u poll error %d\r\n",
                         port->channel, s, r);
            }
            vTaskDelay(pdMS_TO_TICKS(50));   /* small gap between slaves */
        }
        vTaskDelay(pdMS_TO_TICKS(MODBUS_POLL_PERIOD));
    }
}
