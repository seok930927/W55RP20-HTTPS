#include <stdio.h>

#include "serialProtocol.h"
#include "seg.h"                /* SEG_DATA0_CH                              */
#include "modbusMaster.h"       /* modbusMaster_task                         */
#include "WIZ5XXSR-RP_Debug.h"  /* PRT_INFO                                  */

/*  One row per protocol. Order does not matter; the id is what binds a row to
    the value stored in serial_option.protocol.

    This table is the whole dropdown: what is here is what an operator can pick,
    and nothing else survives the config POST. So it lists what this firmware
    actually implements, and nothing else.

    A row is not a promise to build something -- it is a report that something
    is built. modbus_ascii and sec_ups have ids in enum protocol but no rows,
    because neither has a handler; listing them would let an operator select a
    protocol that silently does not run. protocol_custom has no row for the
    same kind of reason: it means something to whoever extends the firmware,
    not to whoever operates the device.

    A NULL task means sensorUart keeps the port and parses S/T/R on it, which
    is what protocol_none is -- S/T/R is implemented, so its row stays.

    Adding a protocol: write it, register the .c, add a row here under its own
    name. See APP_DEV_GUIDE.md chapter 4.4.  */
const SerialProtocol g_serial_protocol[] = {
    { protocol_none,   "Free",         NULL,               0,    0 },
    { modbus_rtu,      "Modbus RTU",   modbusMaster_task,  1024, 9 },
};

const uint8_t g_serial_protocol_cnt =
    (uint8_t)(sizeof(g_serial_protocol) / sizeof(g_serial_protocol[0]));

const SerialProtocol *serial_protocol_find(uint8_t id) {
    for (uint8_t i = 0; i < g_serial_protocol_cnt; i++) {
        if (g_serial_protocol[i].id == id) {
            return &g_serial_protocol[i];
        }
    }
    return NULL;
}

uint8_t serial_protocol_max_id(void) {
    uint8_t max = 0;

    for (uint8_t i = 0; i < g_serial_protocol_cnt; i++) {
        if (g_serial_protocol[i].id > max) {
            max = g_serial_protocol[i].id;
        }
    }
    return max;
}

uint8_t serial_protocol_has_handler(uint8_t id) {
    const SerialProtocol *p = serial_protocol_find(id);

    return (p != NULL && p->task != NULL) ? 1u : 0u;
}

uint8_t serial_protocol_start(SerialPort *port) {
    const SerialProtocol *p = serial_protocol_find(port->protocol);
    char task_name[configMAX_TASK_NAME_LEN];

    if (p == NULL || p->task == NULL) {
        return 0;
    }

    /*  Task names are per port so both channels can run the same protocol and
        still be told apart in a task dump. */
    snprintf(task_name, sizeof(task_name), "%.8s_ch%d", p->name, port->channel);
    xTaskCreate(p->task, task_name, p->stack, port, p->priority, NULL);

    PRT_INFO("serialProtocol: ch%d -> %s\r\n", port->channel, p->name);
    return 1;
}

int serial_protocol_to_json(char *buf, int len) {
    int n = 0;

    n += snprintf(buf + n, (size_t)(len - n), "[");
    for (uint8_t i = 0; i < g_serial_protocol_cnt && n > 0 && n < len; i++) {
        n += snprintf(buf + n, (size_t)(len - n), "%s{\"id\":%u,\"name\":\"%s\"}",
                      i ? "," : "", g_serial_protocol[i].id, g_serial_protocol[i].name);
    }
    if (n > 0 && n < len) {
        n += snprintf(buf + n, (size_t)(len - n), "]");
    }
    return (n < len) ? n : len - 1;
}
