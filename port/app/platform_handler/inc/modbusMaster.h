#ifndef _MODBUS_MASTER_H_
#define _MODBUS_MASTER_H_

#include <stdint.h>
#include "hardware/uart.h"
#include "uartHandler.h"   /* SerialPort */
#include "sensor.h"        /* DEVICE_VALUE_COLS, ValueColumn */

#ifdef __cplusplus
extern "C" {
#endif

/*  Modbus-RTU MASTER for the temperature/humidity sensor module(s).
    The bundled "modbus" code (mb.c) is a TCP<->serial gateway, NOT a master, so
    this is a small purpose-built poller:

        request  : [slave][0x04][0x0000][qty:uint16][CRC16]        (8 bytes)
        response : [slave][0x04][2*qty][reg:int16 x qty][CRC16]    (5 + 2*qty)

    qty is MODBUS_REG_COUNT below, and input register 30001 + n is device-bank
    value column n. With the columns sensor.c ships that reads 30001 =
    temperature, 30002 = humidity, 30003 = alarm; the first two are signed
    int16 x0.1 (raw -100 => -10.0 C). What any of them MEAN is
    g_value_columns[]'s business, not this file's -- the master just moves
    registers into columns, one for one.

    Values go into the device bank, so the existing SNMP and web paths serve
    them with nothing else to wire up.

    Runs on either serial port. serial_port_puts() drives the RS-485 direction
    line when the port is set to RS-485 and does nothing on TTL/RS-232 and
    RS-422, so this file does not care which one it got. Active on a port whose
    web "Mode" selector is set to Modbus RTU; otherwise that port keeps the
    S/T/R role. */

/*  Configure `uart` for Modbus: baud/format from the matching serial_option.
    No DE pin, no RX IRQ. The master polls synchronously. */
void modbusMaster_init(SerialPort *port);

/*  How many input registers a slave holds: one per device-bank value column.

    The bank's columns are the schema everything downstream follows -- the web
    table, the SNMP table and the OIDs all come from g_value_columns[] in
    sensor.c -- so the master asks for exactly as many registers as there are
    columns and fills all of them. Add a column there and this asks for one
    more register with no edit here.

    A slave holding FEWER registers than that answers with an exception rather
    than data, which surfaces as a poll error instead of a plausible-looking
    wrong reading. If the sensor really has fewer, bring DEVICE_VALUE_COLS down
    to match rather than patching a count in here. */
#define MODBUS_REG_COUNT    DEVICE_VALUE_COLS

/*  Poll one slave. On success fills out[0 .. MODBUS_REG_COUNT-1] with the raw
    register values in device-bank column order. Negative on error:
      -1 timeout / short frame, -2 CRC error, -3 unexpected header. */
int modbus_read_values(SerialPort *port, uint8_t slave, int16_t *out);

/*  FreeRTOS task: periodically polls the configured slave range and writes
    results into the device bank. Pass pvParameters = the SerialPort * to poll. */
void modbusMaster_task(void *argument);

#ifdef __cplusplus
}
#endif

#endif /* _MODBUS_MASTER_H_ */
