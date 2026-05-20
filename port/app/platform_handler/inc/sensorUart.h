#ifndef _SENSOR_UART_H_
#define _SENSOR_UART_H_

#ifdef __cplusplus
extern "C" {
#endif

/*
    UART <-> Device bank protocol  (ASCII, newline-terminated, case-insensitive).

    A "device" owns DEVICE_VALUE_COLS value columns (see sensor.h). The
    value list uses two delimiters:
        ','  -> next value column within the current device
        ';'  -> next device (consecutive; column resets to 0)

    Commands (RX, host -> device):
        S<dev>=<vals>\n   Write values starting at device <dev>.
        T<dev>=<vals>\n   Same as S, plus an SNMP trap per device touched.
        R<dev>\n          Request one device.
        R<d1>~<d2>\n      Request a device range.

    Response (TX, device -> host) — emitted only for R:
        R<dev>=<v0>,<v1>,...\r\n   One line per requested device, all of its
                                   value columns. Out-of-range devices skipped.

    Examples (DEVICE_VALUE_COLS = 3):
        S0=235,600,0             -> device 0 = {235, 600, 0}
        S0=235,600,0;236,601,1   -> device 0 + device 1
        T5=10,20,1               -> device 5 = {10, 20, 1}  + trap
        R5                       -> "R5=235,600,0\r\n"
        R5~9                     -> one "R<d>=...\r\n" line per device 5..9

    Lines that don't match are silently ignored. Device index must be
    < DEVICE_COUNT. Values are parsed as int32_t. Traps go to
    DevConfig.snmp_option.trap_ip and are sent by the SNMP agent task
    (queued via snmp_notify_device()).

    Baud / parity / flow control come from DevConfig.serial_option
    (default 115200 8N1) — DATA0_UART_Configuration() does the HW setup;
    sensorUart owns the RX IRQ + parsing task and uses platform_uart_puts()
    for R responses.
*/

/* Initialize uart1 HW + register our RX IRQ handler. Call once at boot. */
void sensorUart_init(void);

/* FreeRTOS task — parses lines from the UART buffer and updates sensors. */
void sensorUart_task(void *argument);

#ifdef __cplusplus
}
#endif

#endif /* _SENSOR_UART_H_ */
