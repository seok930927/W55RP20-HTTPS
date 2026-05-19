#ifndef _SENSOR_UART_H_
#define _SENSOR_UART_H_

#ifdef __cplusplus
extern "C" {
#endif

/*
    UART → Sensor bank ingestion.

    Wire format (ASCII, newline-terminated):
        S<start_index>=<v0>[,<v1>,<v2>,...]\n

    First value goes to start_index. Each subsequent comma-separated value
    is written to the next consecutive index (start_index+1, +2, ...).

    Examples:
        S0=256                    → index 0 = 256
        S0=256,12,23,43,23        → indexes 0..4 = 256, 12, 23, 43, 23
        S10=1, 2, 3               → indexes 10,11,12 = 1, 2, 3
        S8=1800                   → index 8 = 1800

    Lines that don't match the format are silently ignored.
    Index must be < SENSOR_MAX (50). Value is parsed as int32_t.

    Baud / parity / flow control are taken from DevConfig.serial_option
    (default 115200 8N1) — the existing DATA0_UART_Configuration() does
    the HW setup; sensorUart only owns the RX IRQ + parsing task.
*/

/* Initialize uart1 HW + register our RX IRQ handler. Call once at boot. */
void sensorUart_init(void);

/* FreeRTOS task — parses lines from the UART buffer and updates sensors. */
void sensorUart_task(void *argument);

#ifdef __cplusplus
}
#endif

#endif /* _SENSOR_UART_H_ */
