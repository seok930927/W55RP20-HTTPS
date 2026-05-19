#include <stddef.h>
#include "snmpBuffer.h"

snmp_sensor_data_t g_snmp_sensor = {0};

/*
    Field lookup table — maps snmp_field_t to the underlying storage.
    All channel elements are 4 bytes (int32_t or uint32_t), so a single
    int32_t* view works for both. Unsigned fields are accessed via
    int32_t pointer; values are expected in [0, INT32_MAX].
*/
typedef struct {
    int32_t *array;
    uint8_t  count;
} snmp_field_info_t;

static const snmp_field_info_t field_map[SNMP_FIELD_COUNT] = {
    [SNMP_FIELD_TEMPERATURE]    = { (int32_t *)g_snmp_sensor.temperature,    SNMP_TEMP_COUNT   },
    [SNMP_FIELD_HUMIDITY]       = { (int32_t *)g_snmp_sensor.humidity,       SNMP_HUMID_COUNT  },
    [SNMP_FIELD_ALARM]          = { (int32_t *)g_snmp_sensor.alarm,          SNMP_ALARM_COUNT  },
    [SNMP_FIELD_SENSOR_STATUS]  = { (int32_t *)g_snmp_sensor.sensor_status,  SNMP_SENSOR_COUNT },
};

int snmpBuffer_set(snmp_field_t field, uint8_t idx, int32_t value) {
    if ((unsigned)field >= SNMP_FIELD_COUNT) {
        return -1;
    }
    if (idx >= field_map[field].count) {
        return -2;
    }
    field_map[field].array[idx] = value;
    return 0;
}

int snmpBuffer_get(snmp_field_t field, uint8_t idx, int32_t *out) {
    if ((unsigned)field >= SNMP_FIELD_COUNT) {
        return -1;
    }
    if (idx >= field_map[field].count) {
        return -2;
    }
    if (out == NULL) {
        return -1;
    }
    *out = field_map[field].array[idx];
    return 0;
}

int snmpBuffer_count(snmp_field_t field) {
    if ((unsigned)field >= SNMP_FIELD_COUNT) {
        return -1;
    }
    return field_map[field].count;
}

void snmpBuffer_setCommFields(uint32_t status, uint32_t recv_cs,
                              uint32_t calc_cs, uint32_t check, uint32_t flag) {
    g_snmp_sensor.comm_status    = status;
    g_snmp_sensor.recv_checksum  = recv_cs;
    g_snmp_sensor.calc_checksum  = calc_cs;
    g_snmp_sensor.comm_check     = check;
    g_snmp_sensor.comm_flag      = flag;
}

void snmpBuffer_getCommFields(uint32_t *status, uint32_t *recv_cs,
                              uint32_t *calc_cs, uint32_t *check, uint32_t *flag) {
    if (status)  *status   = g_snmp_sensor.comm_status;
    if (recv_cs) *recv_cs  = g_snmp_sensor.recv_checksum;
    if (calc_cs) *calc_cs  = g_snmp_sensor.calc_checksum;
    if (check)   *check    = g_snmp_sensor.comm_check;
    if (flag)    *flag     = g_snmp_sensor.comm_flag;
}
