#ifndef _SERIAL_PROTOCOL_H_
#define _SERIAL_PROTOCOL_H_

#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include "uartHandler.h"   /* SerialPort, enum protocol */

#ifdef __cplusplus
extern "C" {
#endif

/*  The list of application protocols a serial port can be set to.

    "This protocol exists" used to be spelled out in seven places — the enum,
    CMakeLists, the task loop in App.c, sensorUart's decision to stand aside,
    the web POST validator, the Mode dropdown in the HTML, and the regenerated
    Web_page.h. Adding one meant getting all seven right, and missing the
    sensorUart one left two handlers fighting over the same FIFO.

    Now the table below is the one place. App.c starts tasks from it,
    sensorUart asks it whether to stand aside, the POST validator takes its
    upper bound from it, and the web page builds the dropdown from the names
    it serves — so the HTML no longer lists protocols at all.

    To add one: write the .c, register it in port/app/CMakeLists.txt, and add
    a row here. See APP_DEV_GUIDE.md chapter 4.4.  */
typedef struct {
    uint8_t         id;         /* enum protocol value                       */
    const char     *name;       /* shown in the web Mode dropdown and logs   */
    TaskFunction_t  task;       /* NULL: no task, sensorUart keeps the port  */
    uint16_t        stack;      /* words, as xTaskCreate takes them          */
    UBaseType_t     priority;   /* keep at or below 31 (configMAX_PRIORITIES) */
} SerialProtocol;

extern const SerialProtocol g_serial_protocol[];
extern const uint8_t        g_serial_protocol_cnt;

/*  The row for `id`, or NULL if nothing claims that value. */
const SerialProtocol *serial_protocol_find(uint8_t id);

/*  Highest id in the table — what the web POST validator accepts. */
uint8_t serial_protocol_max_id(void);

/*  True when a handler of its own drives this protocol, which is sensorUart's
    cue to leave the port alone. */
uint8_t serial_protocol_has_handler(uint8_t id);

/*  Start the task for whatever `port` is set to. Does nothing when the
    protocol has no task of its own. Returns 1 if a task was created. */
uint8_t serial_protocol_start(SerialPort *port);

/*  Write the table to `buf` as a JSON array the Mode dropdown is built from:
    [{"id":0,"name":"Free"}, ...]. Returns the length written. */
int serial_protocol_to_json(char *buf, int len);

#ifdef __cplusplus
}
#endif

#endif /* _SERIAL_PROTOCOL_H_ */
