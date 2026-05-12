#ifndef _SNMP_BUFFER_H_
#define _SNMP_BUFFER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define SNMP_TEMP_COUNT     8
#define SNMP_HUMID_COUNT    8
#define SNMP_ALARM_COUNT    8
#define SNMP_SENSOR_COUNT   12

// 온도: int32_t × 10  (예: 256 = 25.6℃, -10 = -1.0℃)
// 습도: uint32_t × 10 (예: 650 = 65.0%)
// 경보/상태: 1=정상, 2=경보
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

void snmpBuffer_setTemperature(uint8_t idx, int32_t value_x10);
void snmpBuffer_setHumidity(uint8_t idx, uint32_t value_x10);
void snmpBuffer_setAlarm(uint8_t idx, uint32_t value);
void snmpBuffer_setSensorStatus(uint8_t idx, uint32_t value);
void snmpBuffer_setCommFields(uint32_t status, uint32_t recv_cs,
                              uint32_t calc_cs, uint32_t check, uint32_t flag);

#ifdef __cplusplus
}
#endif

#endif
