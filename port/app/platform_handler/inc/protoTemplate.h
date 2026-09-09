#ifndef _PROTO_TEMPLATE_H_
#define _PROTO_TEMPLATE_H_

#include <stdint.h>
#include "uartHandler.h"   /* SerialPort */

#ifdef __cplusplus
extern "C" {
#endif

/*  Starting point for a serial protocol of your own.

    Copy protoTemplate.[ch] to a name of your own, register the .c in
    port/app/CMakeLists.txt, and fill in the two TODOs in protoTemplate_poll().
    Everything around them — bringing the port up, driving the RS-485 direction
    line, standing down while the config port is in command mode, reserving
    device-bank rows and publishing into them — is already wired, and none of
    it needs the port number: the SerialPort * carries what differs.

    Selecting it: this file has no row in the protocol registry, so it does not
    appear in the web Mode dropdown. That list holds what the firmware
    implements, and a starting point is not an implementation. Once yours does
    something, add a row for it under its own name and it becomes selectable.

    A worked example already exists — S/T/R in sensorUart.c. It is the one
    custom protocol this firmware ships, and it is worth reading before
    starting here.

    See APP_DEV_GUIDE.md chapter 4.4 for the full checklist.  */

/*  Bring the port up. Called by the task; call it yourself only if you drive
    the protocol from somewhere other than protoTemplate_task(). */
void protoTemplate_init(SerialPort *port);

/*  One request/response exchange. `base` is the first device-bank row this
    port owns, as returned by device_bank_reserve(). Returns 0 on success,
    negative on error. */
int protoTemplate_poll(SerialPort *port, uint8_t base);

/*  FreeRTOS task. Pass pvParameters = the SerialPort * to drive. */
void protoTemplate_task(void *argument);

#ifdef __cplusplus
}
#endif

#endif /* _PROTO_TEMPLATE_H_ */
