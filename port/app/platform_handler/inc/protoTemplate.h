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
    line, standing down while the config port is in command mode, publishing
    into the device bank — is already wired.

    Selecting it: this template is bound to protocol value 4 ("Custom" in the
    web Mode dropdown). App.c starts the task for whichever ports carry that
    value, so both ports can run it at once.

    See APP_DEV_GUIDE.md chapter 4.4 for the full checklist.  */

/*  Bring the port up. Called by the task; call it yourself only if you drive
    the protocol from somewhere other than protoTemplate_task(). */
void protoTemplate_init(SerialPort *port);

/*  One request/response exchange. Returns 0 on success, negative on error. */
int protoTemplate_poll(SerialPort *port);

/*  FreeRTOS task. Pass pvParameters = the SerialPort * to drive. */
void protoTemplate_task(void *argument);

#ifdef __cplusplus
}
#endif

#endif /* _PROTO_TEMPLATE_H_ */
