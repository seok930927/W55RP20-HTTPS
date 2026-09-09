/*

    @file   wiznet_board.h
    @brief
*/

#ifndef __WIZNET_BOARD_H__
#define __WIZNET_BOARD_H__

#include <stdint.h>
#include "common.h"

////////////////////////////////
// Product Configurations     //
////////////////////////////////

#define WIZ5XXSR_RP 0
#define W55RP20_S2E 1
#define W232N       2
#define IP20        3
#define PLATYPUS_S2E 4

typedef enum {RESET = 0, SET = !RESET} FlagStatus, ITStatus;

#if ((DEVICE_BOARD_NAME == WIZ5XXSR_RP) || DEVICE_BOARD_NAME == W55RP20_S2E || DEVICE_BOARD_NAME == W232N || DEVICE_BOARD_NAME == IP20 || DEVICE_BOARD_NAME == PLATYPUS_S2E) // Chip product
//#define __USE_DHCP_INFINITE_LOOP__          // When this option is enabled, if DHCP IP allocation failed, process_dhcp() function will try to DHCP steps again.
//#define __USE_DNS_INFINITE_LOOP__           // When this option is enabled, if DNS query failed, process_dns() function will try to DNS steps again.
/*  Off on this board: the pin it watches, GP18, is CN11 terminal 1, so a field
    contact held closed there would read as the reset button being held.

    It could not fire as it stood -- the flag it waits on is only ever set from
    a GPIO interrupt callback that nothing installs -- but the pin is wired to
    a terminal block now, and leaving the watcher in place means whoever
    connects that callback later gets a device that factory-resets itself when
    a sensor reports normal. There is no reset button on this board; SW4 and
    SW5 drive RP_BOOT and RSTn directly. */
//#define __USE_HW_FACTORY_RESET__          // Use Factory reset pin
#define __USE_SAFE_SAVE__                   // When this option is enabled, data verify is additionally performed in the flash save of config-data.
#define __USE_WATCHDOG__                  // WDT timeout 30 Second
#define __USE_S2E_OVER_TLS__                // Use S2E TCP client over SSL/TLS mode
#define __USE_UART_485_422__
//#define __USE_USERS_GPIO__
#if (DEVICE_BOARD_NAME == WIZ5XXSR_RP)
#define DEVICE_ID_DEFAULT                   "WIZ5XXSR-RP"//"S2E_SSL-MB" // Device name
#elif (DEVICE_BOARD_NAME == W55RP20_S2E || DEVICE_BOARD_NAME == PLATYPUS_S2E)
#define __USE_UART_IF_SELECTOR__            // Use Serial interface port selector pin
#define DEVICE_ID_DEFAULT                   "W55RP20-S2E"//"S2E_SSL-MB" // Device name
#define __USE_UART_SPI_IF_SELECTOR__        // Use UART or SPI interface port selector pin
#elif (DEVICE_BOARD_NAME == W232N)
#define DEVICE_ID_DEFAULT                   "W232N"//"S2E_SSL-MB" // Device name
#elif (DEVICE_BOARD_NAME == IP20)
#define DEVICE_ID_DEFAULT                   "IP20"//"S2E_SSL-MB" // Device name
#endif
#define DEVICE_CLOCK_SELECT                 CLOCK_SOURCE_EXTERNAL // or CLOCK_SOURCE_INTERNAL
#define DEVICE_UART_CNT                     (1)
#define DEVICE_SETTING_PASSWORD_DEFAULT     "00000000"
#define DEVICE_GROUP_DEFAULT                "WORKGROUP" // Device group
#define DEVICE_TARGET_SYSTEM_CLOCK   PLL_SYS_KHZ
#endif

/* PHY Link check  */
#define PHYLINK_CHECK_CYCLE_MSEC  1000

/* Factory Reset period  */
#define FACTORY_RESET_TIME_MS   5000

////////////////////////////////
// Pin definitions        //
////////////////////////////////

#if (DEVICE_BOARD_NAME == WIZ5XXSR_RP)
#define DTR_PIN                 8
#define DSR_PIN                 9

#define STATUS_PHYLINK_PIN      10
#define STATUS_TCPCONNECT_PIN   11

// UART1
#define DATA0_UART_TX_PIN      4
#define DATA0_UART_RX_PIN      5
#define DATA0_UART_CTS_PIN     6
#define DATA0_UART_RTS_PIN     7

#define WIZCHIP_PIN_SCK 18
#define WIZCHIP_PIN_MOSI 19
#define WIZCHIP_PIN_MISO 16
#define WIZCHIP_PIN_CS 17
#define WIZCHIP_PIN_RST 20
#define WIZCHIP_PIN_IRQ 21

#define BOOT_MODE_PIN          13
#define FAC_RSTn_PIN           28
#define HW_TRIG_PIN            29
#define DATA0_UART_PORTNUM          (1)

#define LED1_PIN      STATUS_PHYLINK_PIN        //STATUS_PHYLINK
#define LED2_PIN      STATUS_TCPCONNECT_PIN    //STATUS_TCP_PIN
#define LED3_PIN      12    //Blink
#define LEDn    3

#elif ((DEVICE_BOARD_NAME == W55RP20_S2E) || (DEVICE_BOARD_NAME == W232N) || (DEVICE_BOARD_NAME == IP20) || (DEVICE_BOARD_NAME == PLATYPUS_S2E))
#define UART_IF_SEL_PIN        12   //High : 485/422, Low or NC : TTL/232
#define UART_SPI_IF_SEL_PIN    13   //High : SPI, Low or NC : UART

#define DTR_PIN                 8
#define DSR_PIN                 9

#if (DEVICE_BOARD_NAME == PLATYPUS_S2E)
#define STATUS_PHYLINK_PIN      11
#define STATUS_TCPCONNECT_PIN   10
#else
#define STATUS_PHYLINK_PIN      10
#define STATUS_TCPCONNECT_PIN   11
#endif

// RS-232 (XR32330) — RP2040 uart1, schematic "UART0"
#define DATA0_UART_TX_PIN      4
#define DATA0_UART_RX_PIN      5
#define DATA0_UART_CTS_PIN     6
#define DATA0_UART_RTS_PIN     7

// RS-485 (SP3485EN) — RP2040 uart0, schematic "UART1"
#define RS485_UART_TX_PIN      0
#define RS485_UART_RX_PIN      1
#define RS485_UART_DE_PIN      3    // DE / nRE: HIGH = transmit, LOW = receive

// SPI0
#define DATA0_SPI_SCK_PIN      2
#define DATA0_SPI_TX_PIN       3
#define DATA0_SPI_RX_PIN       4
#define DATA0_SPI_CSn_PIN      5
#define DATA0_SPI_INT_PIN      26

#define WIZCHIP_PIN_SCK        21
#define WIZCHIP_PIN_MOSI       23
#define WIZCHIP_PIN_MISO       22
#define WIZCHIP_PIN_CS         20
#define WIZCHIP_PIN_RST        25
#define WIZCHIP_PIN_IRQ        24

#define BOOT_MODE_PIN          15    //When this pin is Low during a device reset, it enters AT Command Mode
#define FAC_RSTn_PIN           18    //Holding Low for more than 5 seconds triggers a factory reset
#define HW_TRIG_PIN            14    //When this pin is Low during a device reset, it enters AT Command Mode
#define DATA0_UART_PORTNUM          (1)

/*  ── CN10 / CN11 digital inputs ─────────────────────────────────────────
    Sixteen contact inputs, one per terminal, read as the health of whatever
    sensor is wired to it: closed (low) = normal, open (high) = alarm.
    They reach the web page and SNMP through the device bank, so nothing
    downstream needs to know they came from GPIO rather than a serial bus.

    DIN_PINS is J2 and J3 on SCH-JSiCT-FMS-V100, in terminal order. Several
    of the names further down this file still claim some of these pins for
    status LEDs, DTR/DSR, interface straps and an SPI slave port; those come
    from the W55RP20-S2E reference board and none of them exist here. This
    board drives its Ethernet LEDs from the chip's own LINKLED/ACTLED pins and
    has no reset or strap buttons -- SW4 and SW5 drive RP_BOOT and RSTn.
    Nothing re-configures a pin direction after boot, so the leftover code
    writes to pins that are inputs by then and has no effect.

    All sixteen are read. GP29 is also SW1 position 4, which shorts that line
    to ground when it is closed, so I/O_13 reads as a closed contact whenever
    that switch is on whatever the field wiring says -- leave it off to use
    the terminal.

    Define DIN_PINS_IN_USE with a list of pins to have digitalInput leave them
    alone, for a board that still needs one of these lines for something else. */
#define DIN_COUNT              16
#define DIN_PINS   { 10, 11, 12, 13, 14, 15, 16,  2, \
                     18, 19, 26, 27, 28, 29,  8,  9 }

//#define DIN_PINS_IN_USE  { 29 }

#ifdef UART_PIO_DEBUG
#define DEBUG_UART_TX_PIN      0
#endif
#define LED1_PIN      STATUS_PHYLINK_PIN        //STATUS_PHYLINK
#define LED2_PIN      STATUS_TCPCONNECT_PIN    //STATUS_TCP_PIN
#define LED3_PIN      19    //Blink
#define LEDn          3
#endif

#ifdef __USE_UART_SPI_IF_SELECTOR__
typedef enum {
    UART_IF = 0,
    SPI_IF
} if_TypeDef;
#endif


typedef enum {
    LED1 = 0, // PHY link status
    LED2 = 1, // TCP connection status
    LED3 = 2  // blink
} Led_TypeDef;

extern volatile uint16_t phylink_check_time_msec;
extern uint8_t flag_check_phylink;

void RP2040_Board_Init(void);
void init_hw_trig_pin(void);
uint8_t get_hw_trig_pin(void);


void init_uart_spi_if_sel_pin(void);
uint8_t get_uart_spi_if(void);

void init_uart_if_sel_pin(void);
uint8_t get_uart_if_sel_pin(void);
void init_factory_reset_pin(void);
uint8_t get_phylink(void);
uint8_t get_factory_reset_pin(void);

#ifdef __USE_BOOT_ENTRY__
void init_boot_entry_pin(void);
uint8_t get_boot_entry_pin(void);
#endif

void LED_Init(Led_TypeDef Led);
void LED_On(Led_TypeDef Led);
void LED_Off(Led_TypeDef Led);
void LED_Toggle(Led_TypeDef Led);
uint8_t get_LED_Status(Led_TypeDef Led);

#endif
