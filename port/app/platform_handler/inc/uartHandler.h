#ifndef UARTHANDLER_H_
#define UARTHANDLER_H_

#include <stdint.h>
#include "port_common.h"
#include "common.h"
#include "bufferHandler.h"

//#define _UART_DEBUG_

#ifndef DATA_BUF_SIZE
#define DATA_BUF_SIZE 2048
#endif

// XON/XOFF: Transmitter On / Off, Software flow control
#define UART_XON                        0x11 // 17
#define UART_XOFF                       0x13 // 19
#define UART_ON_THRESHOLD               (uint16_t)(SEG_DATA_BUF_SIZE / 10)
#define UART_OFF_THRESHOLD              (uint16_t)(SEG_DATA_BUF_SIZE - UART_ON_THRESHOLD)

// UART interface selector, RS-232/TTL or RS-422/485
#define UART_IF_RS232_TTL               0
#define UART_IF_RS422                   1
#define UART_IF_RS485                   2
#define UART_IF_RS485_REVERSE           3
#define SPI_IF_SLAVE                    4

#define UART_IF_STR_RS232_TTL          "TTL/RS-232"
//#define UART_IF_STR_RS232               "RS-232"
#define UART_IF_STR_RS422               "RS-422"
#define UART_IF_STR_RS485               "RS-485"
#define SPI_IF_STR_SLAVE                "SPI_SLAVE"

// If the define '__USE_UART_IF_SELECTOR__' disabled, default UART interface is selected to be 'UART_IF_DEFAULT'
//#define UART_IF_DEFAULT                 UART_IF_RS485
#define UART_IF_DEFAULT               UART_IF_RS232_TTL

// UART RTS/CTS pins
// RTS: output, CTS: input
#define UART_RTS_HIGH                   1
#define UART_RTS_LOW                    0

#define UART_CTS_HIGH                   1
#define UART_CTS_LOW                    0

#define UART_ID uart1
enum baud {
    baud_300 = 0,
    baud_600 = 1,
    baud_1200 = 2,
    baud_1800 = 3,
    baud_2400 = 4,
    baud_4800 = 5,
    baud_9600 = 6,
    baud_14400 = 7,
    baud_19200 = 8,
    baud_28800 = 9,
    baud_38400 = 10,
    baud_57600 = 11,
    baud_115200 = 12,
    baud_230400 = 13,
    baud_460800 = 14,
    baud_921600 = 15,
    baud_1M = 16,
    baud_2M = 17,
    baud_4M = 18,
    baud_8M = 19,
    baud_max = 20
};

enum word_len {
    word_len7 = 0,
    word_len8 = 1,
    word_len9 = 2
};

enum stop_bit {
    stop_bit1 = 0,
    stop_bit2 = 1
};

enum parity {
    parity_none = 0,
    parity_odd = 1,
    parity_even = 2,
    parity_space = 3,   // stick parity, bit always 0
    parity_mark = 4     // stick parity, bit always 1
};

enum flow_ctrl {
    flow_none = 0,
    flow_xon_xoff = 1,
    flow_rts_cts = 2,
    flow_rtsonly = 3,  // RTS_ONLY
    flow_reverserts = 4 // Reverse RTS
};

enum protocol {
    protocol_none = 0,
    modbus_rtu = 1,
    modbus_ascii = 2,
    sec_ups = 3
};

/*  A serial port, as this firmware uses one.

    Board wiring (uart, irq, channel, pins) is fixed in the g_serial_port table.
    serial_port_setup() fills in the rest from that port's settings block, so
    everything a caller needs about a port is reachable from one pointer and no
    call has to ask "which port is this" again. */
#define SERIAL_PORT_CNT     2
#define SERIAL_PIN_NONE     0xFF

typedef struct __serial_port {
    /* board wiring — constant */
    uart_inst_t            *uart;         /* uart0 / uart1                    */
    uint8_t                 irq;          /* UART0_IRQ / UART1_IRQ            */
    int                     channel;      /* SEG_DATA0_CH / SEG_DATA1_CH      */
    uint8_t                 tx_pin, rx_pin;
    uint8_t                 cts_pin, rts_pin;   /* SERIAL_PIN_NONE if absent  */
    uint8_t                 de_pin_board; /* DE/nRE this board routes         */
    /* resolved by serial_port_setup() — from the stored settings */
    uint8_t                 de_pin;       /* configured DE, else de_pin_board */
    uint8_t                 intf;         /* TTL / RS-422 / RS-485 / reverse  */
    uint8_t                 protocol;     /* enum protocol                    */
    struct __serial_option *opt;          /* that port's settings block       */
} SerialPort;

extern SerialPort g_serial_port[SERIAL_PORT_CNT];

/*  Bring the port up from its settings and fill in the resolved fields.
    Safe to call more than once. */
void serial_port_setup(SerialPort *port);

/*  Bring every port up once, before anything transmits. Until a port has been
    through setup its opt pointer is still NULL, so this has to run before the
    first serial_port_puts(). */
void serial_port_init_all(void);

/*  Send on that port. serial_port_puts() holds DE for the whole frame and
    masks each byte to the port's word length; it writes raw, so binary
    protocol frames pass through untouched. */
int32_t serial_port_putc(SerialPort *port, uint16_t ch);
int32_t serial_port_puts(SerialPort *port, const uint8_t *buf, uint16_t bytes);

/*  Direction line around a frame the caller writes itself. No-ops unless the
    port runs an RS-485 mode; tx_disable() waits for the shift register first. */
void serial_port_tx_enable(SerialPort *port);
void serial_port_tx_disable(SerialPort *port);

extern uint32_t baud_table[];
extern uint8_t word_len_table[];
extern uint8_t stop_bit_table[];
extern uint8_t * parity_table[];
extern uint8_t * flow_ctrl_table[];
extern uint8_t * uart_if_table[];

/*  Set data/stop/parity on `uart`. Unlike pico-sdk's uart_set_format(), this
    handles parity_space / parity_mark via the PL011 stick-parity bit.
    `parity_sel` is an `enum parity` value, not a uart_parity_t. */
void uart_set_format_parity(uart_inst_t *uart, uint8_t data_bits, uint8_t stop_bits, uint8_t parity_sel);

void on_uart_rx(void);
void DEBUG_UART_Configuration(void);
void DATA0_UART_Configuration(void);
void DATA0_UART_Deinit(void);
void DATA0_UART_Interrupt_Enable(void);
void DATA1_UART_Configuration(void);

// XON/XOFF Software flow control: Check the Buffer usage and Send the start/stop commands
void check_uart_flow_control(uint8_t flow_ctrl);

// Hardware flow control by GPIOs (RTS/CTS)
#ifdef __USE_GPIO_HARDWARE_FLOWCONTROL__
uint8_t get_uart_cts_pin(void);
void set_uart_rts_pin_high(void);
void set_uart_rts_pin_low(void);
#endif

int32_t platform_uart_putc(uint16_t ch);                    // User Buffer -> UART
int32_t platform_uart_getc(void);                                 // Ring Buffer -> User
int32_t platform_uart_getc_nonblk(void);
int32_t platform_uart_puts(uint8_t* buf, uint16_t bytes);
int32_t platform_uart_gets(uint8_t* buf, uint16_t bytes);
uint8_t get_byte_from_uart(void);                        // UART Port -> User
void get_byte_from_uart_it(void);                        // UART Port -> User (global variable for IRQ handler)
/*  Ring buffer API lives in bufferHandler.h, which this header includes. */
uint8_t get_uart_rs485_sel(void);

#endif /* UARTHANDLER_H_ */
