#include "snmpBuffer.h"

snmp_sensor_data_t g_snmp_sensor = {0};

void snmpBuffer_setTemperature(uint8_t idx, int32_t value_x10) {
    if (idx < SNMP_TEMP_COUNT) {
        g_snmp_sensor.temperature[idx] = value_x10;
    }
}

void snmpBuffer_setHumidity(uint8_t idx, uint32_t value_x10) {
    if (idx < SNMP_HUMID_COUNT) {
        g_snmp_sensor.humidity[idx] = value_x10;
    }
}

void snmpBuffer_setAlarm(uint8_t idx, uint32_t value) {
    if (idx < SNMP_ALARM_COUNT) {
        g_snmp_sensor.alarm[idx] = value;
    }
}

void snmpBuffer_setSensorStatus(uint8_t idx, uint32_t value) {
    if (idx < SNMP_SENSOR_COUNT) {
        g_snmp_sensor.sensor_status[idx] = value;
    }
}

void snmpBuffer_setCommFields(uint32_t status, uint32_t recv_cs,
                              uint32_t calc_cs, uint32_t check, uint32_t flag) {
    g_snmp_sensor.comm_status    = status;
    g_snmp_sensor.recv_checksum  = recv_cs;
    g_snmp_sensor.calc_checksum  = calc_cs;
    g_snmp_sensor.comm_check     = check;
    g_snmp_sensor.comm_flag      = flag;
}
