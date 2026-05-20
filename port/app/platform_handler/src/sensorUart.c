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
#include "snmpHandler.h"      /* snmp_notify_sensor */
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
    Walk comma-separated values, assigning to consecutive indexes.
    send_trap != 0 → also queue an SNMP trap for each index written (T).
    ========================================================================= */
static void parse_write(const char *p, int index, uint8_t send_trap) {
    while (*p) {
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '\0' || *p == ',') {
            /* empty token between commas → skip but still advance index */
            if (*p == ',') {
                p++;
                index++;
                continue;
            }
            break;
        }

        int32_t value = atoi(p);
        int ret = sensor_setValue((uint8_t)index, value);
        if (ret == 0) {
            PRT_INFO("sensorUart: index %d = %ld\r\n", index, (long)value);
            if (send_trap) {
                snmp_notify_sensor((uint8_t)index);
            }
        } else {
            PRT_INFO("sensorUart: setValue(%d, %ld) -> %d\r\n",
                     index, (long)value, ret);
        }

        /* Advance to next comma (or end) */
        while (*p && *p != ',') {
            p++;
        }
        if (*p == ',') {
            p++;
            index++;
        }
    }
}

/*  ===== Request handler  (R command) ====================================
    "R<index>"          → reply one line
    "R<start>~<end>"    → reply one line per index in range
    Reply format (per index):  R<index>=<value>\r\n
    Out-of-range indexes are skipped.
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
    if (start >= SENSOR_MAX) {
        return;
    }
    if (end >= SENSOR_MAX) {
        end = SENSOR_MAX - 1;
    }

    for (int i = start; i <= end; i++) {
        int32_t value;
        char buf[32];
        if (sensor_getValue((uint8_t)i, &value) != 0) {
            continue;
        }
        snprintf(buf, sizeof(buf), "R%d=%ld\r\n", i, (long)value);
        uart_tx_str(buf);
    }
}

/*  ===== Line parser ======================================================
    Commands (leading whitespace tolerated, case-insensitive):
      S<index>=<v0>[,<v1>,...]   write values to consecutive indexes
      T<index>=<v0>[,<v1>,...]   same as S, plus an SNMP trap per index
      R<index>                   request one value  → reply on UART TX
      R<start>~<end>             request a range    → reply on UART TX
    Examples:
      "S0=256,12,23"   → indexes 0,1,2 = 256,12,23
      "T5=10,20"       → indexes 5,6 = 10,20  + traps for 5,6
      "R5"             → "R5=999111\r\n"
      "R5~14"          → "R5=...\r\n" ... "R14=...\r\n"
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
        int index = atoi(rest);
        const char *eq = strchr(rest, '=');
        if (eq == NULL) {
            return;
        }
        parse_write(eq + 1, index, (uint8_t)(cmd == 'T'));
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
