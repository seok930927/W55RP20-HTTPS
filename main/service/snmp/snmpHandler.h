#ifndef SNMPHANDLER_H_
#define SNMPHANDLER_H_

#include <stdint.h>

void snmp_agent_task(void *argument);
void snmp_request_reinit(void);

/*  Queue one value of one device (both 0-based) for an SNMP trap. Safe to call
    from any task; the trap is sent by snmp_agent_task on its next cycle.

    Most values do not need this. The agent watches the whole device bank
    against the limits in the web settings and reports crossings by itself, so
    a protocol that writes into the bank gets traps without asking. This is for
    a protocol whose peer says outright that something happened -- the S/T/R
    "T" command is the one in the tree -- where waiting for a sweep to notice
    would lose the point of being told. */
void snmp_notify_cell(uint8_t dev, uint8_t col);

/*  Every value column of one device at once. Same caveat as above. */
void snmp_notify_device(uint8_t dev);

/*  Queue one flat item (itemBank.h) for a trap. `dev` is an ITEM_DEV_* slot.

    Same relationship as above: the agent watches every registered item against
    its ItemDef.trap_on and reports the moment one arrives at that value, so a
    protocol that writes into the item bank gets its traps without asking. This
    is for a protocol that is told outright. */
void snmp_notify_item(uint8_t dev, uint8_t num);

#endif /* SNMPHANDLER_H_ */
