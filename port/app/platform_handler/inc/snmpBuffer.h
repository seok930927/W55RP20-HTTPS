#ifndef _SNMP_BUFFER_H_
#define _SNMP_BUFFER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/*
    Legacy SNMP sensor data backing store.

    The new design lives in sensor.h / sensor.c (SensorType + Sensor +
    g_sensors[]). This file remains because snmp_custom.c still reads
    g_snmp_sensor directly via OID-callback macros. Once snmp_custom.c
    is migrated, snmp_sensor_data_t and g_snmp_sensor can be removed.
*/

#define SNMP_TEMP_COUNT     8
#define SNMP_HUMID_COUNT    8
#define SNMP_ALARM_COUNT    8
#define SNMP_SENSOR_COUNT   12

/*
    Storage layout (per channel):
      temperature   : int32_t × 10   (256 = 25.6°C)
      humidity      : uint32_t × 10  (650 = 65.0%)
      alarm         : 1 = normal, 2 = alarm
      sensor_status : 1 = normal, 2 = alarm
*/
typedef struct {
    int32_t  temperature[SNMP_TEMP_COUNT];
    uint32_t humidity[SNMP_HUMID_COUNT];
    uint32_t alarm[SNMP_ALARM_COUNT];
    uint32_t sensor_status[SNMP_SENSOR_COUNT];
    uint32_t comm_status;
    uint32_t recv_checksum;
    uint32_t calc_checksum;
    uint32_t comm_check;
    uint32_t comm_flag;
} snmp_sensor_data_t;

extern snmp_sensor_data_t g_snmp_sensor;

/* ----- Unified field access (used as compat layer over legacy struct) - */

typedef enum {
    SNMP_FIELD_TEMPERATURE   = 0,   /* 8 channels, int32_t  ×10 */
    SNMP_FIELD_HUMIDITY      = 1,   /* 8 channels, uint32_t ×10 */
    SNMP_FIELD_ALARM         = 2,   /* 8 channels, uint32_t     */
    SNMP_FIELD_SENSOR_STATUS = 3,   /* 12 channels, uint32_t    */
    SNMP_FIELD_COUNT                /* keep last */
} snmp_field_t;

/*
    Return: 0 = OK, -1 = invalid field/arg, -2 = idx out of range.
    All channel values are 4 bytes; unsigned fields share the int32_t
    interface — caller keeps values within [0, INT32_MAX].
*/
int snmpBuffer_set(snmp_field_t field, uint8_t idx, int32_t value);
int snmpBuffer_get(snmp_field_t field, uint8_t idx, int32_t *out);
int snmpBuffer_count(snmp_field_t field);

/* ----- Comm meta (standalone, not arrayed) ---------------------------- */
void snmpBuffer_setCommFields(uint32_t status, uint32_t recv_cs,
                              uint32_t calc_cs, uint32_t check, uint32_t flag);
void snmpBuffer_getCommFields(uint32_t *status, uint32_t *recv_cs,
                              uint32_t *calc_cs, uint32_t *check, uint32_t *flag);

#ifdef __cplusplus
}
#endif

#endif /* _SNMP_BUFFER_H_ */
