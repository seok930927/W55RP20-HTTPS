#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/irq.h"
#include "hardware/gpio.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "sensorUart.h"
#include "sensor.h"
#include "snmpHandler.h"      /* snmp_notify_device */
#include "ConfigData.h"       /* get_DevConfig_pointer, struct __serial_option */
#include "uartHandler.h"      /* UART_ID, platform_uart_puts, DATA0_UART_Configuration */
#include "bufferHandler.h"    /* data buffer ring */
#include "WIZnet_board.h"     /* RS485_UART_TX_PIN, RS485_UART_RX_PIN, RS485_UART_DE_PIN */
#include "WIZ5XXSR-RP_Debug.h"

#define UART_LINE_BUF_SIZE  64

/* baud_table is defined (non-static) in uartHandler.c */
extern uint32_t baud_table[];

static SemaphoreHandle_t s_uart_sem = NULL;

volatile uint32_t dbg_rs232_isr_cnt = 0;
volatile uint32_t dbg_rs485_isr_cnt = 0;

/*  ===== [U4] RX ISR pair — byte ingestion ==================================
    USER-CUSTOMIZABLE: Change how incoming bytes are stored or routed.

    Current policy: both ISRs push bytes into the SAME shared ring buffer.
    The task cannot tell which port a byte came from.

    To track the originating port:
      Option A — add a volatile flag:
          static volatile uint8_t s_last_rx_port = 0;   // 0=232, 1=485
          Set it in each ISR before giving the semaphore.
          Read it in sensorUart_task() / parse_request() to decide reply port.

      Option B — separate ring buffers per port (more complex, avoids races
          between simultaneous traffic on both ports).

    DRIVER LAYER (do not change unless HW changes):
      RS-232  XR32330  uart1  GPIO4(TX) GPIO5(RX)              UART1_IRQ
      RS-485  SP3485EN uart0  GPIO0(TX) GPIO1(RX) GPIO3(DE)    UART0_IRQ
    ========================================================================= */
static void sensorUart_rs232_rx_isr(void) {
    dbg_rs232_isr_cnt++;
    BaseType_t higher = pdFALSE;
    while (uart_is_readable(UART_ID)) {
        uint8_t ch = uart_getc(UART_ID);
        if (!is_data_buffer_full()) {
            put_byte_to_data_buffer(ch);
        }
    }
    if (s_uart_sem != NULL) {
        xSemaphoreGiveFromISR(s_uart_sem, &higher);
        portYIELD_FROM_ISR(higher);
    }
}

/*  ===== RS-485 RX ISR (uart0, GPIO0/1) ====================================
    Handles incoming bytes from the RS-485 transceiver (SP3485EN).
    DE pin (GPIO3) is held LOW so the receiver is always enabled.
    ========================================================================= */
static void sensorUart_rs485_rx_isr(void) {
    dbg_rs485_isr_cnt++;
    gpio_xor_mask(1u << 19);   /* LED3 toggle: visual proof ISR is firing */
    BaseType_t higher = pdFALSE;
    while (uart_is_readable(uart0)) {
        uint8_t ch = uart_getc(uart0);
        if (!is_data_buffer_full()) {
            put_byte_to_data_buffer(ch);
        }
    }
    if (s_uart_sem != NULL) {
        xSemaphoreGiveFromISR(s_uart_sem, &higher);
        portYIELD_FROM_ISR(higher);
    }
}

/*  ===== RS-485 UART (uart0) initialisation =================================
    Reads baud/parity/data-bits from DevConfig.serial_option_485 — the RS-485
    port has its OWN settings, independent of RS-232 (uart1 / serial_option).
    ========================================================================= */
static void init_rs485_uart(void) {
    struct __serial_option *opt =
        (struct __serial_option *) & (get_DevConfig_pointer()->serial_option_485);

    uart_init(uart0, 115200);

    gpio_set_function(RS485_UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(RS485_UART_RX_PIN, GPIO_FUNC_UART);
    gpio_pull_up(RS485_UART_RX_PIN);

    /* Baud rate */
    uint32_t baud = 115200;
    if (opt->baud_rate < 20) {
        baud = baud_table[opt->baud_rate];
    }
    uart_set_baudrate(uart0, baud);

    /* Data bits */
    uint8_t dbits = (opt->data_bits == word_len7) ? 7 : 8;

    /* Stop bits */
    uint8_t sbits = (opt->stop_bits == stop_bit2) ? 2 : 1;

    /* Parity — includes space/mark (PL011 stick parity) */
    uart_set_format_parity(uart0, dbits, sbits, opt->parity);
    uart_set_hw_flow(uart0, false, false);
    uart_set_fifo_enabled(uart0, true);

    /*  DE / nRE pin — only driven in an RS-485 mode (half duplex needs a
        direction line). TTL/RS-232 and RS-422 are full duplex, so it is left
        alone. Idle level = receive. */
    uint8_t de_mode = uart_de_mode_for(uart0);
    if (de_mode == UART_IF_RS485 || de_mode == UART_IF_RS485_REVERSE) {
        uart_rs485_rs422_init(uart_de_pin_for(uart0), de_mode);
    }

    irq_set_exclusive_handler(UART0_IRQ, sensorUart_rs485_rx_isr);
    irq_set_enabled(UART0_IRQ, true);
    uart_set_irq_enables(uart0, true, false);   /* RX irq only */

    PRT_INFO("sensorUart: RS-485 ready (uart0, TX=GPIO%d, RX=GPIO%d, DE=GPIO%d)\r\n",
             RS485_UART_TX_PIN, RS485_UART_RX_PIN, uart_de_pin_for(uart0));
}

void sensorUart_init(void) {
    s_uart_sem = xSemaphoreCreateBinary();

    /*  ── RS-232 (uart1, GPIO4/5) ──
        Skip the S/T/R handler when RS-232 is in Modbus mode: modbusMaster owns
        uart1 and polls it synchronously (no shared RX ISR / ring buffer).
        Only takes uart1 for S/T/R in Free mode. */
    if (get_DevConfig_pointer()->serial_option.protocol == modbus_rtu) {
        PRT_INFO("sensorUart: RS-232 in Modbus mode -> handed to modbusMaster\r\n");
    } else {
        DATA0_UART_Configuration();   /* baud/parity/pins from DevConfig.serial_option */
        irq_set_exclusive_handler(UART1_IRQ, sensorUart_rs232_rx_isr);
        irq_set_enabled(UART1_IRQ, true);
        uart_set_irq_enables(UART_ID, true, false);
        PRT_INFO("sensorUart: RS-232 ready (uart1, TX=GPIO%d, RX=GPIO%d)\r\n",
                 DATA0_UART_TX_PIN, DATA0_UART_RX_PIN);
    }

    /* ── RS-485 (uart0, GPIO0/1, DE=GPIO3) — always S/T/R ── */
    init_rs485_uart();
}

/*  ===== [U3] TX helpers — reply routing =====================================
    USER-CUSTOMIZABLE: Change which port(s) a reply is sent on.

    Current policy: ALL replies go to BOTH RS-232 and RS-485 simultaneously,
    regardless of which port sent the command.

    rs485_tx_str()  — RS-485 only (DE pin managed internally)
    uart_tx_str()   — BOTH ports (calls platform_uart_puts + rs485_tx_str)

    To change: replace uart_tx_str() calls in parse_request() with individual
    port functions. See [U2] for how to track the originating port.
    ========================================================================= */
static void rs485_tx_str(const char *s) {
    uint8_t pin = uart_de_pin_for(uart0);
    uint8_t mode = uart_de_mode_for(uart0);

    uart_rs485_enable(pin, mode);
    const char *p = s;
    while (*p) {
        uart_putc_raw(uart0, (uint8_t)*p++);
    }
    uart_rs485_disable(uart0, pin, mode);
}

/* Send on RS-232 (uart1) and RS-485 (uart0) simultaneously. */
static void uart_tx_str(const char *s) {
    platform_uart_puts((uint8_t *)s, (uint16_t)strlen(s));   /* RS-232 */
    rs485_tx_str(s);                                           /* RS-485 */
}

/*  ===== [U1] Write handler  (S / T commands) ================================
    USER-CUSTOMIZABLE: This function decides what happens when S or T arrives.

    Two-level value list:
        ','  → next value column within the current device
        ';'  → next device (column resets to 0)
    "S<dev>=..." starts at device <dev>; each ';' group advances to the
    next consecutive device. Values past DEVICE_VALUE_COLS / devices past
    DEVICE_COUNT are ignored.
        S1=12,23,1            device 1 = {12,23,1}
        S1=12,23,1;13,24,0    device 1 = {12,23,1}, device 2 = {13,24,0}
    send_trap != 0 (T command) → queue one SNMP trap per device touched.

    To add: value range check, logging, duplicate-suppression, etc.
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

/*  ===== [U2] Request handler  (R command) ===================================
    USER-CUSTOMIZABLE: This function decides what to reply and where to send it.

    "R<dev>"          → reply one line with the device's value columns
    "R<d1>~<d2>"      → reply one line per device in range
    Reply format:  R<dev>=<v0>,<v1>,...\r\n
    Out-of-range devices are skipped.

    Currently: uart_tx_str() sends reply on BOTH RS-232 and RS-485.
    To reply only on the originating port, replace uart_tx_str() with:
        platform_uart_puts(...)  — RS-232 only
        rs485_tx_str(...)        — RS-485 only
    (You'll need to track which port triggered the semaphore — see [U4].)
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

/*  ===== Line parser — command dispatch ====================================
    Assembles one complete line then dispatches to [U1] or [U2].
    Add new commands here (new else-if branch on cmd letter).

    Commands (leading whitespace tolerated, case-insensitive):
      S<dev>=<vals>            write values → [U1] parse_write(send_trap=0)
      T<dev>=<vals>            write + SNMP trap → [U1] parse_write(send_trap=1)
      R<dev>                   request one device → [U2] parse_request()
      R<d1>~<d2>               request a range   → [U2] parse_request()
    Unrecognised commands: logged via PRT_INFO, otherwise ignored.
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
    Wakes on RX semaphore (from either RS-232 or RS-485 ISR), drains the
    byte ring buffer, assembles lines terminated by '\n' (or '\r\n'),
    parses each line.
    ========================================================================= */
void sensorUart_task(void *argument) {
    (void)argument;

    static char line[UART_LINE_BUF_SIZE];
    uint16_t pos = 0;
    TickType_t last_dbg = 0;

    while (1) {
        xSemaphoreTake(s_uart_sem, pdMS_TO_TICKS(5000));

        TickType_t now = xTaskGetTickCount();
        if ((now - last_dbg) >= pdMS_TO_TICKS(5000)) {
            printf("[UART] RS232 ISR=%lu  RS485 ISR=%lu\r\n",
                   dbg_rs232_isr_cnt, dbg_rs485_isr_cnt);
            last_dbg = now;
        }

        while (!is_data_buffer_empty()) {
            int32_t ch = data_buffer_getc_nonblk();
            if (ch == RET_NOK) {
                break;
            }
            /*  DIAG: dump every received byte as hex + char. If baud is right,
                sending "R0\r" shows: RX 52 'R'  RX 30 '0'  RX 0D '.' */
            printf("RX %02X '%c'\r\n", (unsigned)(ch & 0xFF),
                   (ch >= 0x20 && ch < 0x7f) ? (char)ch : '.');
            if (ch == '\r' || ch == '\n') {
                if (pos > 0) {
                    line[pos] = '\0';
                    parse_line(line);
                    pos = 0;
                }
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
