#ifndef SNMPHANDLER_H_
#define SNMPHANDLER_H_

#include <stdint.h>

void snmp_agent_task(void *argument);
void snmp_request_reinit(void);

/*  Queue a device (0-based) for an SNMP trap. Safe to call from any task;
    the trap is sent by snmp_agent_task on its next cycle (one trap per
    value column of the device). */
void snmp_notify_device(uint8_t dev);

#endif /* SNMPHANDLER_H_ */
