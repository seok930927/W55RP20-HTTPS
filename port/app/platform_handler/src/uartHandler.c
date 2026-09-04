#include <string.h>
#include "common.h"
#include "ConfigData.h"
#include "deviceHandler.h"
#include "uartHandler.h"
#include "spiHandler.h"
#include "gpioHandler.h"
#include "seg.h"
#include "port_common.h"
#include "WIZnet_board.h"
#ifdef UART_PIO_DEBUG
#include "uart_tx.pio.h"
#endif

/* Private typedef -----------------------------------------------------------*/

/* Private define ------------------------------------------------------------*/


/* Private functions prototypes ----------------------------------------------*/
static void serial_port_de_idle(SerialPort *port);

/* Private functions ---------------------------------------------------------*/

/* Private macro -------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/

#if (DEVICE_BOARD_NAME == W232N)
uint32_t baud_table[] = {300, 600, 1200, 1800, 2400, 4800, 9600, 14400, 19200, 28800, 38400, 57600, 115200, 230400};
#else
uint32_t baud_table[] = {300, 600, 1200, 1800, 2400, 4800, 9600, 14400, 19200, 28800, 38400, 57600, 115200, 230400, 460800, 921600, 1000000, 2000000, 4000000, 8000000};
#endif
uint8_t word_len_table[] = {7, 8, 9};
uint8_t * parity_table[] = {(uint8_t *)"N", (uint8_t *)"ODD", (uint8_t *)"EVEN", (uint8_t *)"SPACE", (uint8_t *)"MARK"};
uint8_t stop_bit_table[] = {1, 2};
uint8_t * flow_ctrl_table[] = {(uint8_t *)"NONE", (uint8_t *)"XON/XOFF", (uint8_t *)"RTS/CTS", (uint8_t *)"RTS Only", (uint8_t *)"RTS Only Reverse"};
uint8_t * uart_if_table[] = {(uint8_t *)UART_IF_STR_RS232_TTL, (uint8_t *)UART_IF_STR_RS422, (uint8_t *)UART_IF_STR_RS485, (uint8_t *)UART_IF_STR_RS485, (uint8_t *)SPI_IF_STR_SLAVE};

// XON/XOFF Status;
static uint8_t xonoff_status = UART_XON;

// RTS Status; __USE_GPIO_HARDWARE_FLOWCONTROL__ defined
#ifdef __USE_GPIO_HARDWARE_FLOWCONTROL__
static uint8_t rts_status = UART_RTS_LOW;
#endif

// UART Interface selector; RS-422 or RS-485 use only
//static uint8_t uart_if_mode = UART_IF_RS422;
// 외부 매개변수에 의존한다
/*  The board's serial ports. Only the wiring is fixed here; serial_port_setup()
    fills the resolved fields from the stored settings.
    Channel numbering follows the DATA0_UART_* macros, not the RP2040 instance
    numbers: DATA0 is the original data port, which is RP2040 uart1. */
SerialPort g_serial_port[SERIAL_PORT_CNT] = {
    [SEG_DATA0_CH] = {
        .uart = uart1, .irq = UART1_IRQ, .channel = SEG_DATA0_CH,
        .tx_pin = DATA0_UART_TX_PIN, .rx_pin = DATA0_UART_RX_PIN,
        .cts_pin = DATA0_UART_CTS_PIN, .rts_pin = DATA0_UART_RTS_PIN,
        .de_pin_board = DATA0_UART_RTS_PIN,
    },
    [SEG_DATA1_CH] = {
        .uart = uart0, .irq = UART0_IRQ, .channel = SEG_DATA1_CH,
        .tx_pin = RS485_UART_TX_PIN, .rx_pin = RS485_UART_RX_PIN,
        .cts_pin = SERIAL_PIN_NONE, .rts_pin = SERIAL_PIN_NONE,
        .de_pin_board = RS485_UART_DE_PIN,
    },
};

extern xSemaphoreHandle seg_u2e_sem;
extern xSemaphoreHandle segcp_uart_sem;


/* Public functions ----------------------------------------------------------*/

////////////////////////////////////////////////////////////////////////////////
// Data UART Configuration
////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////
// Data UART Configuration & IRQ handler
////////////////////////////////////////////////////////////////////////////////

// RX interrupt handler
void on_uart_rx(void) {
    //uartRxByte: // 1-byte character variable for UART Interrupt request handler
    uint8_t ch = 0, input_flag = 0;
    signed portBASE_TYPE xHigherPriorityTaskWoken = pdFALSE;

    while (uart_is_readable(UART_ID)) {
        ch = uart_getc(UART_ID);

        if (!(check_modeswitch_trigger(ch))) { // ret: [0] data / [!0] trigger code
            if (is_data_buffer_full(SEG_DATA0_CH) == TRUE) {
                data_buffer_flush(SEG_DATA0_CH);
            }

            if (check_serial_store_permitted(ch)) { // ret: [0] not permitted / [1] permitted
                put_byte_to_data_buffer(ch, SEG_DATA0_CH);
                input_flag = 1;
            }
        }
    }

    if (input_flag) {
        init_time_delimiter_timer();
        if (opmode == DEVICE_GW_MODE) {
            xSemaphoreGiveFromISR(seg_u2e_sem, &xHigherPriorityTaskWoken);
        } else if (opmode == DEVICE_AT_MODE) {
            xSemaphoreGiveFromISR(segcp_uart_sem, &xHigherPriorityTaskWoken);
        }
        portEND_SWITCHING_ISR(xHigherPriorityTaskWoken);
    }
}

/*  Apply data/stop/parity, including the space/mark modes.

    pico-sdk's uart_set_format() only knows none/even/odd — space and mark need
    the PL011 stick-parity bit (LCR_H.SPS, bit 7), which it neither sets nor
    clears (SPS is absent from its write mask), so SPS survives across calls and
    we have to drive it explicitly both ways:

        space  PEN=1, EPS=1, SPS=1   parity bit always 0
        mark   PEN=1, EPS=0, SPS=1   parity bit always 1

    SPS must be patched in *after* uart_set_format(), which supplies PEN/EPS. */
void uart_set_format_parity(uart_inst_t *uart, uint8_t data_bits, uint8_t stop_bits, uint8_t parity_sel) {
    uart_parity_t par;
    uint8_t stick = 0;

    switch (parity_sel) {
    case parity_odd:
        par = UART_PARITY_ODD;
        break;
    case parity_even:
        par = UART_PARITY_EVEN;
        break;
    case parity_space:
        par = UART_PARITY_EVEN;
        stick = 1;
        break;
    case parity_mark:
        par = UART_PARITY_ODD;
        stick = 1;
        break;
    default:
        par = UART_PARITY_NONE;
        break;
    }

    uart_set_format(uart, data_bits, stop_bits, par);

    if (stick) {
        hw_set_bits(&uart_get_hw(uart)->lcr_h, UART_UARTLCR_H_SPS_BITS);
    } else {
        hw_clear_bits(&uart_get_hw(uart)->lcr_h, UART_UARTLCR_H_SPS_BITS);
    }
}

/*  Resolve the settings-driven fields, then bring the hardware up. This is the
    one place that knows how a channel maps onto a settings block, so nothing
    downstream has to ask which port it is holding. */
void serial_port_init_all(void) {
    for (uint8_t p = 0; p < SERIAL_PORT_CNT; p++) {
        serial_port_setup(&g_serial_port[p]);
    }
}

void serial_port_setup(SerialPort *port) {
    DevConfig *dev_config = get_DevConfig_pointer();
    uart_inst_t *uart = port->uart;
    uint8_t cfg_de, cfg_intf;
    uint8_t valid_arg = 0;
    uint8_t temp_data_bits, temp_stop_bits, temp_parity;

    if (port->channel == SEG_DATA0_CH) {
        port->opt = (struct __serial_option *) & (dev_config->serial_option);
        cfg_de = dev_config->serial_de_pin;
        cfg_intf = dev_config->serial_intf_sel;
    } else {
        port->opt = (struct __serial_option *) & (dev_config->serial_option_485);
        cfg_de = dev_config->serial485_de_pin;
        cfg_intf = dev_config->serial485_intf_sel;
    }
    struct __serial_option *serial_option = port->opt;

    /*  0 means unset, and GPIO0 is a UART TX pin so it can never be DE. */
    port->de_pin = (cfg_de != 0 && cfg_de <= 29) ? cfg_de : port->de_pin_board;
    port->intf = (cfg_intf > UART_IF_RS485_REVERSE) ? UART_IF_RS232_TTL : cfg_intf;
    port->protocol = (serial_option->protocol > sec_ups)
                     ? protocol_none : serial_option->protocol;
    uint8_t intf = port->intf;
    uint8_t tx_pin = port->tx_pin;
    uint8_t rx_pin = port->rx_pin;

    uart_deinit(uart);

    // Set up our UART with a basic baud rate.
    uart_init(uart, 2400);

    // Set the TX and RX pins by using the function select on the GPIO
    // Set datasheet for more information on function select
    gpio_init(tx_pin);
    gpio_init(rx_pin);
    /* Not every port has the handshake pair routed out. */
    if (port->cts_pin != SERIAL_PIN_NONE) {
        gpio_init(port->cts_pin);
        gpio_init(port->rts_pin);
    }

    gpio_set_function(tx_pin, GPIO_FUNC_UART);
    gpio_set_function(rx_pin, GPIO_FUNC_UART);
    if (port->cts_pin != SERIAL_PIN_NONE) {
        gpio_set_function(port->cts_pin, GPIO_FUNC_UART);
        gpio_set_function(port->rts_pin, GPIO_FUNC_UART);
    }
    gpio_pull_up(rx_pin);

    /* Set Baud Rate */
    if (serial_option->baud_rate < (sizeof(baud_table) / sizeof(baud_table[0]))) {
        PRT_INFO("Real baudrate = %d\r\n", uart_set_baudrate(uart, baud_table[serial_option->baud_rate]));
        valid_arg = 1;
    }

    if (!valid_arg) {
        PRT_INFO("Real baudrate = %d\r\n", uart_set_baudrate(uart, baud_table[baud_115200]));
    }

    /* Set Data Bits */
    switch (serial_option->data_bits) {
    case word_len7:
        temp_data_bits = 7;
        break;
    case word_len8:
        temp_data_bits = 8;
        break;
    case word_len9:
        temp_data_bits = 9;
        break;
    default:
        temp_data_bits = 8;
        serial_option->data_bits = word_len8;
        break;
    }

    /*  PL011 supports 5..8 bits only. uart_set_format() masks the field, so 9
        would silently come out as 5 — clamp instead. */
    if (temp_data_bits > 8) {
        temp_data_bits = 8;
    }

    /* Set Stop Bits */
    switch (serial_option->stop_bits) {
    case stop_bit1:
        temp_stop_bits = 1;
        break;
    case stop_bit2:
        temp_stop_bits = 2;
        break;
    default:
        temp_stop_bits = 1;
        serial_option->stop_bits = stop_bit1;
        break;
    }

    /*  Set Parity Bits — uart_set_format_parity() maps the enum, including the
        space/mark stick-parity modes. Only the range check lives here. */
    if (serial_option->parity > parity_mark) {
        serial_option->parity = parity_none;
    }
    temp_parity = serial_option->parity;

    /* Flow Control */
    if (intf == UART_IF_RS232_TTL) {
        // RS232 Hardware Flow Control
        //7     RTS     Request To Send     Output
        //8     CTS     Clear To Send       Input
        switch (serial_option->flow_control) {
        case flow_none:
            uart_set_hw_flow(uart, false, false);
            break;
        case flow_rts_cts:
#ifdef __USE_GPIO_HARDWARE_FLOWCONTROL__
            uart_set_hw_flow(uart, false, false);
            set_uart_rts_pin_low(uartNum);
#else
            uart_set_hw_flow(uart, true, true);
#endif
            break;
        case flow_xon_xoff:
            uart_set_hw_flow(uart, false, false);
            break;
        default:
            uart_set_hw_flow(uart, false, false);
            serial_option->flow_control = flow_none;
            break;
        }
    }

#ifdef __USE_UART_485_422__
    else { // UART_IF_RS422 || UART_IF_RS485
        uart_set_hw_flow(uart, false, false);

        /*  RTS pin becomes the 485SEL / DE direction line here, so Handshake is
            ignored in this mode. uart_interface was already resolved from
            serial_intf_sel in set_minimal_runtime_config(), so it names the
            variant directly; the legacy flow/strap sources stay as fallbacks. */
        uint8_t uart_if_mode;
        if ((intf == UART_IF_RS422) ||
                (intf == UART_IF_RS485) ||
                (intf == UART_IF_RS485_REVERSE)) {
            uart_if_mode = intf;
        } else if (serial_option->flow_control == flow_rtsonly) {
            uart_if_mode = UART_IF_RS485;
        } else if (serial_option->flow_control == flow_reverserts) {
            uart_if_mode = UART_IF_RS485_REVERSE;
        } else if (port->channel == SEG_DATA0_CH) {
            uart_if_mode = get_uart_rs485_sel();
        } else {
            /* No interface strap pin on this port; RS-485 is the board wiring. */
            uart_if_mode = UART_IF_RS485;
        }
        port->intf = uart_if_mode;
        serial_port_de_idle(port);
        serial_option->uart_interface = uart_if_mode;
    }
#endif

    // Set our data format
    uart_set_format_parity(uart, temp_data_bits, temp_stop_bits, temp_parity);
    uart_set_fifo_enabled(uart, true);

    PRT_INFO("serial_option->flow_control = %d\r\n", serial_option->flow_control);
    PRT_INFO("data_bits = %d, stop_bits = %d, parity = %s\r\n", temp_data_bits, temp_stop_bits, parity_table[temp_parity]);
    PRT_INFO("baud = %d\r\n", baud_table[serial_option->baud_rate]);
}

void DATA0_UART_Configuration(void) {
    serial_port_setup(&g_serial_port[SEG_DATA0_CH]);
}

void DATA0_UART_Deinit(void) {
    uart_deinit(UART_ID);
}


void DATA0_UART_Interrupt_Enable(void) {
    // And set up and enable the interrupt handlers
    int UART_IRQ = UART_ID == uart0 ? UART0_IRQ : UART1_IRQ;

    irq_set_exclusive_handler(UART_IRQ, on_uart_rx);
    irq_set_enabled(UART_IRQ, true);

    // Set up a RX interrupt
    // We need to set up the handler first
    // Select correct interrupt for the UART we are using

    // Now enable the UART to send interrupts - RX only
    uart_set_irq_enables(UART_ID, true, false);


}

void check_uart_flow_control(uint8_t flow_ctrl) {
    if (flow_ctrl == flow_xon_xoff) {
        if ((xonoff_status == UART_XON) && (get_data_buffer_usedsize(SEG_DATA0_CH) > UART_OFF_THRESHOLD)) { // Send the transmit stop command to peer - go XOFF
            platform_uart_putc(UART_XOFF);
            xonoff_status = UART_XOFF;
#ifdef _UART_DEBUG_
            printf(" >> SEND XOFF [%d / %d]\r\n", get_data_buffer_usedsize(SEG_DATA0_CH), SEG_DATA_BUF_SIZE);
#endif
        } else if ((xonoff_status == UART_XOFF) && (get_data_buffer_usedsize(SEG_DATA0_CH) < UART_ON_THRESHOLD)) { // Send the transmit start command to peer. -go XON
            platform_uart_putc(UART_XON);
            xonoff_status = UART_XON;
#ifdef _UART_DEBUG_
            printf(" >> SEND XON [%d / %d]\r\n", get_data_buffer_usedsize(SEG_DATA0_CH), SEG_DATA_BUF_SIZE);
#endif
        }
    }
#ifdef __USE_GPIO_HARDWARE_FLOWCONTROL__
    else if (flow_ctrl == flow_rts_cts) { // RTS pin control
        // Buffer full occurred
        if ((rts_status == UART_RTS_LOW) && (get_data_buffer_usedsize(SEG_DATA0_CH) > UART_OFF_THRESHOLD)) {
            set_uart_rts_pin_high(uartNum);
            rts_status = UART_RTS_HIGH;
#ifdef _UART_DEBUG_
            printf(" >> UART_RTS_HIGH [%d / %d]\r\n", get_data_buffer_usedsize(SEG_DATA0_CH), SEG_DATA_BUF_SIZE);
#endif
        }

        // Clear the buffer full event
        if ((rts_status == UART_RTS_HIGH) && (get_data_buffer_usedsize(SEG_DATA0_CH) <= UART_OFF_THRESHOLD)) {
            set_uart_rts_pin_low(uartNum);
            rts_status = UART_RTS_LOW;
#ifdef _UART_DEBUG_
            printf(" >> UART_RTS_LOW [%d / %d]\r\n", get_data_buffer_usedsize(SEG_DATA0_CH), SEG_DATA_BUF_SIZE);
#endif
        }
    }
#endif
}


int32_t serial_port_putc(SerialPort *port, uint16_t ch) {
    uint8_t mask = (port->opt->data_bits == word_len7) ? 0x7F : 0xFF;

    device_wdt_reset();
    uart_putc_raw(port->uart, (char)(ch & mask));

    return RET_OK;
}

int32_t platform_uart_putc(uint16_t ch) {
    return serial_port_putc(&g_serial_port[SEG_DATA0_CH], ch);
}

/*  Send `bytes` on `uart`, holding that port's DE line for the whole frame and
    masking each byte to the word length that port is configured for.

    Raw output: uart_putc() would insert a CR ahead of any byte matching the
    port's line-feed setting, which corrupts binary protocols. */
int32_t serial_port_puts(SerialPort *port, const uint8_t *buf, uint16_t bytes) {
    uint8_t mask = (port->opt->data_bits == word_len7) ? 0x7F : 0xFF;
    uint16_t i;

    serial_port_tx_enable(port);
    for (i = 0; i < bytes; i++) {
        device_wdt_reset();
        uart_putc_raw(port->uart, (char)(buf[i] & mask));
    }
    serial_port_tx_disable(port);

    return bytes;
}

int32_t platform_uart_puts(uint8_t* buf, uint16_t bytes) {
    return serial_port_puts(&g_serial_port[SEG_DATA0_CH], buf, bytes);
}

#ifdef __USE_UART_485_422__
uint8_t get_uart_rs485_sel(void) {
    GPIO_Configuration(DATA0_UART_RTS_PIN, GPIO_IN, IO_PULLUP);// UART0 RTS pin: GPIO / Input
    if (GPIO_Input_Read(DATA0_UART_RTS_PIN) == IO_LOW) {
        return UART_IF_RS422;
    }
    return UART_IF_RS485;
}

/*  Put the DE line in its receive state. Called from setup once the interface
    is known; the idle level is inverted in the reverse variant. */
static void serial_port_de_idle(SerialPort *port) {
    GPIO_Configuration(port->de_pin, GPIO_OUT, IO_NOPULL);
    if (port->intf == UART_IF_RS485) {
        GPIO_Output_Reset(port->de_pin);
    } else {
        GPIO_Output_Set(port->de_pin);
    }
}

void serial_port_tx_enable(SerialPort *port) {
    if (port->intf == UART_IF_RS485) {
        GPIO_Output_Set(port->de_pin);
    } else if (port->intf == UART_IF_RS485_REVERSE) {
        GPIO_Output_Reset(port->de_pin);
    }    //UART_IF_RS422: None
}

void serial_port_tx_disable(SerialPort *port) {
    if (port->intf == UART_IF_RS485) {
        uart_tx_wait_blocking(port->uart);
        GPIO_Output_Reset(port->de_pin);

    } else if (port->intf == UART_IF_RS485_REVERSE) {
        uart_tx_wait_blocking(port->uart);
        GPIO_Output_Set(port->de_pin);
    }
    //UART_IF_RS422: None
}
#endif

#ifdef __USE_GPIO_HARDWARE_FLOWCONTROL__

uint8_t get_uart_cts_pin(void) {
    uint8_t cts_pin = UART_CTS_HIGH;

#ifdef _UART_DEBUG_
    static uint8_t prev_cts_pin;
#endif
    cts_pin = GPIO_Input_Read(DATA0_UART_CTS_PIN);


#ifdef _UART_DEBUG_
    if (cts_pin != prev_cts_pin) {
        printf(" >> UART_CTS_%s\r\n", cts_pin ? "HIGH" : "LOW");
        prev_cts_pin = cts_pin;
    }
#endif

    return cts_pin;
}

void set_uart_rts_pin_high(void) {
    GPIO_Output_Set(DATA0_UART_RTS_PIN);
}

void set_uart_rts_pin_low(void) {
    GPIO_Output_Reset(DATA0_UART_RTS_PIN);
}

#endif

#ifdef UART_PIO_DEBUG
static void debug_uart_init(void) {
    gpio_init(DEBUG_UART_TX_PIN);
    gpio_set_dir(DEBUG_UART_TX_PIN, GPIO_OUT);

    uint offset = pio_add_program(pio0, &uart_tx_program);
    uart_tx_program_init(pio0, 0, offset, DEBUG_UART_TX_PIN, PICO_DEFAULT_UART_BAUD_RATE);
}

static void debug_uart_puts(const char *buf, int len) {
    for (int i = 0; i < len; i++) {
        uart_tx_program_putc(pio0, 0, buf[i]);
    }
}

static struct stdio_driver debug_driver = {
    .out_chars = debug_uart_puts,
    .in_chars = NULL,
};

void debug_uart_enable(void) {
    debug_uart_init();
    stdio_set_driver_enabled(&debug_driver, true);
}
#endif