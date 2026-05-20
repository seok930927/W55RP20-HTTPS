#ifndef SNMPHANDLER_H_
#define SNMPHANDLER_H_

#include <stdint.h>

void snmp_agent_task(void *argument);
void snmp_request_reinit(void);

/*  Queue a sensor index (0-based) for an SNMP trap. Safe to call from any
    task; the trap is actually sent by snmp_agent_task on its next cycle. */
void snmp_notify_sensor(uint8_t index);

#endif /* SNMPHANDLER_H_ */
