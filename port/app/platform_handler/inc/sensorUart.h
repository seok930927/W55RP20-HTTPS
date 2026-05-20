#ifndef _SENSOR_UART_H_
#define _SENSOR_UART_H_

#ifdef __cplusplus
extern "C" {
#endif

/*
    UART <-> Sensor bank protocol  (ASCII, newline-terminated, case-insensitive).

    Commands (RX, host -> device):
        S<index>=<v0>[,<v1>,...]\n   Write values to consecutive indexes.
        T<index>=<v0>[,<v1>,...]\n   Same as S, plus an SNMP trap per index.
        R<index>\n                   Request one value.
        R<start>~<end>\n             Request an index range.

    For S/T: the first value goes to <index>, each subsequent comma-separated
    value to the next consecutive index (<index>+1, +2, ...).

    Response (TX, device -> host) — emitted only for R:
        R<index>=<value>\r\n         One line per requested index.
                                     Out-of-range indexes are skipped.

    Examples:
        S0=256,12,23      -> indexes 0,1,2 = 256,12,23
        T5=10,20          -> indexes 5,6 = 10,20  + traps for 5,6
        R5                -> "R5=999111\r\n"
        R5~14             -> "R5=...\r\n" ... "R14=...\r\n"

    Lines that don't match are silently ignored. Index must be < SENSOR_MAX.
    Values are parsed as int32_t. Traps go to DevConfig.snmp_option.trap_ip
    and are sent by the SNMP agent task (queued via snmp_notify_sensor()).

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
