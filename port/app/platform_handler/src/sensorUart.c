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
#include "uartHandler.h"      /* UART_ID, DATA0_UART_Configuration */
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

/*  ===== Line parser ======================================================
    Format: S<start_index>=<v0>[,<v1>,<v2>,...]
      - Leading whitespace tolerated
      - Both 'S' and 's' accepted
      - First value goes to start_index, each subsequent comma-separated
        value increments the index by 1
      - index range checked by sensor_setValue
    Examples:
      "S0=256"               → index 0 = 256
      "S0=256,12,23,43,23"   → indexes 0..4 = 256,12,23,43,23
      "S10=1, 2, 3"          → indexes 10,11,12 = 1,2,3
    ========================================================================= */
static void parse_line(const char *line) {
    /* Skip leading whitespace */
    while (*line == ' ' || *line == '\t') {
        line++;
    }
    if (*line == '\0') {
        return;     /* empty line */
    }

    if (*line != 'S' && *line != 's') {
        PRT_INFO("sensorUart: ignoring '%s'\r\n", line);
        return;
    }
    line++;

    if (!isdigit((unsigned char) * line)) {
        return;
    }

    int index = atoi(line);
    const char *eq = strchr(line, '=');
    if (eq == NULL) {
        return;
    }

    /* Walk comma-separated values, assigning to consecutive indexes. */
    const char *p = eq + 1;
    while (*p) {
        /* atoi tolerates leading whitespace; skip explicitly for clarity */
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
