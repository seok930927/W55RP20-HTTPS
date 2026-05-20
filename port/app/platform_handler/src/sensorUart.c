#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/irq.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "sensorUart.h"
#include "sensor.h"
#include "snmpHandler.h"      /* snmp_notify_device */
#include "uartHandler.h"      /* UART_ID, platform_uart_puts, DATA0_UART_Configuration */
#include "bufferHandler.h"    /* data buffer ring */
#include "WIZ5XXSR-RP_Debug.h"

#define UART_LINE_BUF_SIZE  64

static SemaphoreHandle_t s_uart_sem = NULL;

/*  ===== RX ISR ============================================================
    Replaces uartHandler.c's on_uart_rx which is wired for SEG/AT gateway
    mode. We just stash bytes into the existing data_buffer ring and signal
    the parsing task.
    ========================================================================= */
static void sensorUart_rx_isr(void) {
    BaseType_t higher = pdFALSE;
    while (uart_is_readable(UART_ID)) {
        uint8_t ch = uart_getc(UART_ID);
        if (!is_data_buffer_full()) {
            put_byte_to_data_buffer(ch);
        }
        /* If buffer is full we drop the byte. */
    }
    if (s_uart_sem != NULL) {
        xSemaphoreGiveFromISR(s_uart_sem, &higher);
        portYIELD_FROM_ISR(higher);
    }
}

void sensorUart_init(void) {
    s_uart_sem = xSemaphoreCreateBinary();

    /* HW setup (baud/parity/pins) from DevConfig.serial_option */
    DATA0_UART_Configuration();

    /* Override the IRQ handler with ours — replaces uartHandler's on_uart_rx */
    int uart_irq = (UART_ID == uart0) ? UART0_IRQ : UART1_IRQ;
    irq_set_exclusive_handler(uart_irq, sensorUart_rx_isr);
    irq_set_enabled(uart_irq, true);
    uart_set_irq_enables(UART_ID, true, false);   /* RX irq only */

    PRT_INFO("sensorUart: RX ready (UART_ID=uart%d)\r\n",
             (UART_ID == uart0) ? 0 : 1);
}

/* Send a NUL-terminated string out the data UART. */
static void uart_tx_str(const char *s) {
    platform_uart_puts((uint8_t *)s, (uint16_t)strlen(s));
}

/*  ===== Write handler  (S / T commands) =================================
    Two-level value list:
        ','  → next value column within the current device
        ';'  → next device (column resets to 0)
    "S<dev>=..." starts at device <dev>; each ';' group advances to the
    next consecutive device. Values past DEVICE_VALUE_COLS / devices past
    DEVICE_COUNT are ignored.
        S1=12,23,1            device 1 = {12,23,1}
        S1=12,23,1;13,24,0    device 1 = {12,23,1}, device 2 = {13,24,0}
    send_trap != 0 (T command) → queue one SNMP trap per device touched.
    ========================================================================= */
static void parse_write(const char *p, int dev, uint8_t send_trap) {
    uint8_t col         = 0;
    int     touched_dev = -1;   /* device currently being written, -1 = none */

    while (*p) {
        if (*p == ' ' || *p == '\t') {
            p++;
            continue;
        }
        if (*p == ';') {                       /* next device */
            if (send_trap && touched_dev >= 0) {
                snmp_notify_device((uint8_t)touched_dev);
            }
            touched_dev = -1;
            p++;
            dev++;
            col = 0;
            continue;
        }
        if (*p == ',') {                       /* next value column */
            p++;
            col++;
            continue;
        }

        /* numeric token */
        int32_t value = atoi(p);
        if (dev >= 0 && dev < DEVICE_COUNT && col < DEVICE_VALUE_COLS) {
            if (device_setValue((uint8_t)dev, col, value) == 0) {
                PRT_INFO("sensorUart: dev %d col %u = %ld\r\n",
                         dev, col, (long)value);
                touched_dev = dev;
            }
        }
        /* advance past the number to the next ',' / ';' / end */
        while (*p && *p != ',' && *p != ';') {
            p++;
        }
    }

    if (send_trap && touched_dev >= 0) {
        snmp_notify_device((uint8_t)touched_dev);
    }
}

/*  ===== Request handler  (R command) ====================================
    "R<dev>"          → reply one line with the device's value columns
    "R<d1>~<d2>"      → reply one line per device in range
    Reply format:  R<dev>=<v0>,<v1>,...\r\n
    Out-of-range devices are skipped.
    ========================================================================= */
static void parse_request(const char *p) {
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (!isdigit((unsigned char) * p)) {
        return;
    }

    int start = atoi(p);
    int end   = start;

    const char *tilde = strchr(p, '~');
    if (tilde != NULL) {
        const char *q = tilde + 1;
        while (*q == ' ' || *q == '\t') {
            q++;
        }
        if (isdigit((unsigned char) * q)) {
            end = atoi(q);
        }
    }
    if (end < start) {
        int tmp = start;
        start = end;
        end = tmp;
    }
    if (start < 0) {
        start = 0;
    }
    if (start >= DEVICE_COUNT) {
        return;
    }
    if (end >= DEVICE_COUNT) {
        end = DEVICE_COUNT - 1;
    }

    for (int d = start; d <= end; d++) {
        char buf[160];
        int  n = snprintf(buf, sizeof(buf), "R%d=", d);

        for (uint8_t c = 0;
                c < DEVICE_VALUE_COLS && n > 0 && n < (int)sizeof(buf); c++) {
            int32_t v = 0;
            device_getValue((uint8_t)d, c, &v);
            n += snprintf(buf + n, sizeof(buf) - n, "%s%ld",
                          c ? "," : "", (long)v);
        }
        if (n > 0 && n < (int)sizeof(buf) - 2) {
            n += snprintf(buf + n, sizeof(buf) - n, "\r\n");
            uart_tx_str(buf);
        }
    }
}

/*  ===== Line parser ======================================================
    Commands (leading whitespace tolerated, case-insensitive):
      S<dev>=<vals>            write values; ',' = column, ';' = next device
      T<dev>=<vals>            same as S, plus an SNMP trap per device
      R<dev>                   request one device  → reply on UART TX
      R<d1>~<d2>               request a device range → reply on UART TX
    Examples (DEVICE_VALUE_COLS = 3):
      "S0=235,600,0"          → device 0 = {235, 600, 0}
      "S0=235,600,0;236,601,1"→ device 0 + device 1
      "T5=10,20,1"            → device 5 = {10, 20, 1}  + trap
      "R5"                    → "R5=235,600,0\r\n"
      "R5~9"                  → one "R<d>=...\r\n" line per device 5..9
    ========================================================================= */
static void parse_line(const char *line) {
    while (*line == ' ' || *line == '\t') {
        line++;
    }
    if (*line == '\0') {
        return;     /* empty line */
    }

    char cmd = (char)toupper((unsigned char) * line);
    const char *rest = line + 1;

    if (cmd == 'S' || cmd == 'T') {
        if (!isdigit((unsigned char) * rest)) {
            return;
        }
        int dev = atoi(rest);
        const char *eq = strchr(rest, '=');
        if (eq == NULL) {
            return;
        }
        parse_write(eq + 1, dev, (uint8_t)(cmd == 'T'));
    } else if (cmd == 'R') {
        parse_request(rest);
    } else {
        PRT_INFO("sensorUart: ignoring '%s'\r\n", line);
    }
}

/*  ===== Parsing task =====================================================
    Wakes on RX semaphore, drains the byte ring buffer, assembles lines
    terminated by '\n' (or '\r\n'), parses each line.
    ========================================================================= */
void sensorUart_task(void *argument) {
    (void)argument;

    static char line[UART_LINE_BUF_SIZE];
    uint16_t pos = 0;

    while (1) {
        xSemaphoreTake(s_uart_sem, portMAX_DELAY);

        while (!is_data_buffer_empty()) {
            int32_t ch = data_buffer_getc_nonblk();
            if (ch == RET_NOK) {
                break;
            }
            if (ch == '\r') {
                continue;       /* swallow CR */
            }
            if (ch == '\n') {
                line[pos] = '\0';
                if (pos > 0) {
                    parse_line(line);
                }
                pos = 0;
                continue;
            }
            if (pos < UART_LINE_BUF_SIZE - 1) {
                line[pos++] = (char)ch;
            } else {
                /* Line too long — discard and reset */
                PRT_INFO("sensorUart: line overflow, dropping\r\n");
                pos = 0;
            }
        }
    }
}
