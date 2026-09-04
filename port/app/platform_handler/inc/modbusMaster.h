#ifndef _MODBUS_MASTER_H_
#define _MODBUS_MASTER_H_

#include <stdint.h>
#include "hardware/uart.h"
#include "uartHandler.h"   /* SerialPort */

#ifdef __cplusplus
extern "C" {
#endif

/*  Modbus-RTU MASTER for the temperature/humidity sensor module(s) on RS-232 (uart1).
    The bundled "modbus" code (mb.c) is a TCP<->serial gateway, NOT a master, so
    this is a small purpose-built poller for the fixed sensor frame:

        request  : [slave][0x04][0x0000][0x0002][CRC16]              (8 bytes)
        response : [slave][0x04][0x04][temp:int16][hum:int16][CRC16] (9 bytes)

    Reg 30001 = temperature, reg 30002 = humidity, both signed int16, x0.1
    (raw -100 => -10.0 C). Values are pushed into the device bank
    (device_setValue) so the existing SNMP / web path serves them unchanged.

    RS-232 is full-duplex (separate TX/RX) so there is NO DE direction pin to
    manage. Active only when DevConfig.serial_option.protocol == modbus_rtu
    (the web RS-232 "Mode" selector); otherwise uart1 keeps the S/T/R role. */

/*  Configure `uart` for Modbus: baud/format from the matching serial_option.
    No DE pin, no RX IRQ. The master polls synchronously. */
void modbusMaster_init(SerialPort *port);

/*  Poll one slave for temperature + humidity.
    Returns 0 on success (fills *temp/*hum), negative on error:
      -1 timeout / short frame, -2 CRC error, -3 unexpected header. */
int modbus_read_th(SerialPort *port, uint8_t slave, int16_t *temp, int16_t *hum);

/*  FreeRTOS task: periodically polls the configured slave range and writes
    results into the device bank. Pass pvParameters = the SerialPort * to poll. */
void modbusMaster_task(void *argument);

#ifdef __cplusplus
}
#endif

#endif /* _MODBUS_MASTER_H_ */
