#ifndef _SENSOR_UART_H_
#define _SENSOR_UART_H_

#ifdef __cplusplus
extern "C" {
#endif

/*
    ============================================================================
    SERIAL UART PROTOCOL  —  sensorUart
    ============================================================================

    HARDWARE WIRING
    ---------------
    RS-232  XR32330  RP2040 uart1  TX=GPIO4  RX=GPIO5
    RS-485  SP3485EN RP2040 uart0  TX=GPIO0  RX=GPIO1  DE/nRE=GPIO3
                                  DE LOW = receive,  DE HIGH = transmit

    Both ports share ONE ring buffer and ONE semaphore.
    Both ports receive the same commands.
    Reply (R command) is sent on BOTH ports simultaneously.

    DATA FLOW
    ---------

    ISR: sensorUart_rs232_rx_isr()  ─┐
    ISR: sensorUart_rs485_rx_isr()  ─┤─► put_byte_to_data_buffer()
                                    │   (shared ring buffer)
                                    └─► xSemaphoreGiveFromISR()
                                               │
                                               ▼
                              sensorUart_task()  [FreeRTOS task]
                                               │
                                          parse_line()
                                          │         │
                                     S / T cmd    R cmd
                                          │         │
                                    parse_write() parse_request()
                                          │         │
                              device_setValue()  device_getValue()
                              snmp_notify_device()  │
                              (T only)              │
                                               uart_tx_str()
                                               ├── platform_uart_puts()  RS-232
                                               └── rs485_tx_str()        RS-485


    COMMAND REFERENCE  (ASCII, newline-terminated, case-insensitive)
    -----------------------------------------------------------------

    WRITE  (host → device):
     S<dev>=<v0>[,<v1>,...][;<v0>,...]\n

         Write values to device <dev>. Comma separates value columns.
         Semicolon advances to the next consecutive device.
         Silently ignores out-of-range device / column indices.

     T<dev>=<v0>[,<v1>,...][;<v0>,...]\n

         Same as S, but additionally queues one SNMP trap per device
         that was written (via snmp_notify_device()).

    REQUEST  (host → device):
     R<dev>\n           Query one device.
     R<d1>~<d2>\n       Query a device range (inclusive, either order).

    RESPONSE  (device → host):
     R<dev>=<v0>,<v1>,...\r\n

         One line per requested device, all DEVICE_VALUE_COLS columns.
         Sent on BOTH RS-232 and RS-485 simultaneously.
         Out-of-range devices are skipped.

    EXAMPLES  (DEVICE_VALUE_COLS = 3, DEVICE_COUNT = 64):
     "S0=235,600,0"            → device 0 = {235, 600, 0}
     "S0=235,600,0;236,601,1"  → device 0 = {235,600,0}, device 1 = {236,601,1}
     "T5=10,20,1"              → device 5 = {10, 20, 1} + SNMP trap
     "R5"                      → "R5=235,600,0\r\n"
     "R5~9"                    → "R5=...\r\n" "R6=...\r\n" ... "R9=...\r\n"


    DEVICE / VALUE MODEL  (defined in sensor.h / sensor.c)
    -------------------------------------------------------
    g_devices[DEVICE_COUNT]              — device table (RAM, resets on boot)
    g_value_columns[DEVICE_VALUE_COLS]   — column descriptors (name / unit / scale)

    To change what values each device exposes:
     1. Edit DEVICE_VALUE_COLS in sensor.h
     2. Edit g_value_columns[] in sensor.c
    SNMP table, web JSON, and UART protocol all derive their shape from these.


    USER-CUSTOMIZABLE POINTS  (see sensorUart.c)
    ---------------------------------------------
    [U1] parse_write()
        What happens when S or T arrives.
        Default: write to g_devices[] + optional SNMP trap.
        → Change here to add logging, filtering, range validation, etc.

    [U2] parse_request()
        What to reply and where to send it on R command.
        Default: read from g_devices[], send on BOTH ports.
        → Change uart_tx_str() to port-specific call to split reply routing.
           e.g.  send only on the port that sent the R command.

    [U3] uart_tx_str() / rs485_tx_str()
        Reply routing logic.
        Default: always both RS-232 and RS-485.
        → To send only on the originating port, you need to track which ISR
           last signalled the semaphore (add a global volatile flag per port).

    [U4] sensorUart_rs232_rx_isr() / sensorUart_rs485_rx_isr()
        RX interrupt handlers.
        Default: push bytes into shared ring buffer → wake task.
        → Add per-port ring buffers here if you need to separate traffic.


    BAUD / FORMAT
    -------------
    Both ports read their settings from DevConfig.serial_option at boot:
     baud_rate, data_bits, stop_bits, parity
    (DevConfig default = 115200 8N1, loaded from flash by load_DevConfig_from_storage())
    To change at runtime, update DevConfig and call sensorUart_init() again,
    or call init_rs485_uart() / DATA0_UART_Configuration() directly.

    ============================================================================
*/

/*  Initialize RS-232 (uart1) and RS-485 (uart0) hardware, register RX IRQs.
    Call once at boot before starting sensorUart_task. */
void sensorUart_init(void);

/*  FreeRTOS task — drains the ring buffer, assembles lines, dispatches
    parse_line(). Pass as pvParameters = NULL. */
void sensorUart_task(void *argument);

#ifdef __cplusplus
}
#endif

#endif /* _SENSOR_UART_H_ */
