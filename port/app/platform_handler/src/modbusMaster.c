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

#define MODBUS_RSP_LEN       9      /* slave,func,bytecount,temp(2),hum(2),crc(2) */
#define MODBUS_RSP_TIMEOUT   150    /* ms to wait for a full response frame      */
#define MODBUS_POLL_PERIOD   1000   /* ms between full poll cycles               */

/*  Configure `uart` for Modbus. baud/format come from that port's serial_option
    so the web serial settings apply (sensor default is 9600 8N1 — set Baud=9600).
    No RX IRQ: the master drains the RX FIFO itself. */
void modbusMaster_init(SerialPort *port) {
    struct __serial_option *opt;

    serial_port_setup(port);
    serial_port_hw_flow_disable(port);            /* Modbus RTU never uses RTS/CTS */
    opt = port->opt;

    PRT_INFO("modbusMaster: master ready (ch%d, %lu-%u-%s-%u)\r\n",
             port->channel,
             (unsigned long)baud_table[opt->baud_rate < baud_max ? opt->baud_rate : baud_115200],
             (opt->data_bits == word_len7) ? 7 : 8,
             parity_table[opt->parity <= parity_mark ? opt->parity : parity_none],
             (opt->stop_bits == stop_bit2) ? 2 : 1);
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

int modbus_read_th(SerialPort *port, uint8_t slave, int16_t *temp, int16_t *hum) {
    /* Request: read 2 input registers (Func 04) from address 0x0000. */
    uint8_t req[8] = { slave, 0x04, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00 };
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
    /* DIAG: show what we sent and what (if anything) came back. */
    PRT_INFO("modbus TX: %02X %02X %02X %02X %02X %02X %02X %02X | RX %d bytes: "
             "%02X %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
             req[0], req[1], req[2], req[3], req[4], req[5], req[6], req[7], n,
             n > 0 ? rsp[0] : 0, n > 1 ? rsp[1] : 0, n > 2 ? rsp[2] : 0,
             n > 3 ? rsp[3] : 0, n > 4 ? rsp[4] : 0, n > 5 ? rsp[5] : 0,
             n > 6 ? rsp[6] : 0, n > 7 ? rsp[7] : 0, n > 8 ? rsp[8] : 0);
    if (n != MODBUS_RSP_LEN) {
        return -1;                               /* timeout / short frame */
    }
    if (usMBCRC16(rsp, MODBUS_RSP_LEN) != 0) {
        return -2;                               /* CRC mismatch */
    }
    if (rsp[0] != slave || rsp[1] != 0x04 || rsp[2] != 0x04) {
        return -3;                               /* wrong addr/func/bytecount */
    }

    *temp = (int16_t)(((uint16_t)rsp[3] << 8) | rsp[4]);   /* reg 30001 */
    *hum  = (int16_t)(((uint16_t)rsp[5] << 8) | rsp[6]);   /* reg 30002 */
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
            int16_t t = 0, h = 0;
            int r = modbus_read_th(port, s, &t, &h);
            if (r == 0) {
                uint8_t idx = (uint8_t)(s - MODBUS_SLAVE_FIRST);
                device_bank_setValue(src, idx, 0, t);   /* temperature */
                device_bank_setValue(src, idx, 1, h);   /* humidity    */
                PRT_INFO("modbusMaster: slave %u  T=%d (%.1fC)  H=%d (%.1f%%)\r\n",
                         s, t, t / 10.0, h, h / 10.0);
            } else {
                PRT_INFO("modbusMaster: slave %u poll error %d\r\n", s, r);
            }
            vTaskDelay(pdMS_TO_TICKS(50));   /* small gap between slaves */
        }
        vTaskDelay(pdMS_TO_TICKS(MODBUS_POLL_PERIOD));
    }
}
