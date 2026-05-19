#ifndef _SENSOR_H_
#define _SENSOR_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*  =========================================================================
    Sensor object model
    --------------------------------------------------------------------------
    SensorType  : const class-like descriptor (Temperature / Humidity / RPM ...)
    Sensor      : runtime instance bound to a slot, with user-given name
                  + reference (type_id) to a SensorType
    Adding a new sensor type = add one SensorType definition in sensor.c.
    ========================================================================= */

/*
    Fixed-size sensor table.
    SENSOR_NAME_MAX = 11 → user names limited to 10 ASCII chars + null.
    Per Sensor struct = 11 + 1 + 1 + 1 + 4 + 4 = 22 B, padded to 24 B.
    Total g_sensors[] = 50 × 24 = 1200 B static RAM.
    Worst-case JSON per slot ≈ 130 B → 50 slots ≈ 6500 B (fits body[8192]).
*/
#define SENSOR_NAME_MAX     11
#define SENSOR_MAX          50

/* ===== SensorType (const class-like descriptor) ======================= */

typedef enum {
    SENSOR_VALUE_CONTINUOUS = 0,   /* Temperature / Humidity / RPM ... */
    SENSOR_VALUE_DISCRETE   = 1,   /* Status / Alarm — labelled states */
} SensorValueKind;

typedef struct {
    uint8_t     value;             /* enum value (0, 1, 2, ...) */
    const char *label;             /* "Normal", "Warning", "Critical" ... */
} SensorStateLabel;

typedef struct {
    uint8_t                  type_id;        /* stable id (flash/SNMP wire) */
    const char              *name;           /* "Temperature", "Status" ... */
    const char              *unit;           /* "°C", "%RH", "" */
    int8_t                   default_scale;  /* displayed = raw * 10^scale */
    SensorValueKind          value_kind;
    const SensorStateLabel  *states;         /* DISCRETE only */
    uint8_t                  states_count;
} SensorType;

/* Stable type IDs — do NOT change once assigned (persisted in flash, sent over SNMP). */
#define TYPE_ID_DISABLED       0
#define TYPE_ID_TEMPERATURE    1
#define TYPE_ID_HUMIDITY       2
#define TYPE_ID_ALARM          3
#define TYPE_ID_STATUS         4
#define TYPE_ID_RPM            5
#define TYPE_ID_ILLUMINANCE    6
#define TYPE_ID_PRESSURE       7
#define TYPE_ID_VIBRATION      8
#define TYPE_ID_FLOW           9
#define TYPE_ID_VOLTAGE        10
#define TYPE_ID_CURRENT        11
#define TYPE_ID_COUNTER        12
#define TYPE_ID_GENERIC        99

/* Built-in SensorType objects (defined in sensor.c). */
extern const SensorType TYPE_TEMPERATURE;
extern const SensorType TYPE_HUMIDITY;
extern const SensorType TYPE_ALARM;
extern const SensorType TYPE_STATUS;
extern const SensorType TYPE_RPM;
extern const SensorType TYPE_ILLUMINANCE;
extern const SensorType TYPE_PRESSURE;
extern const SensorType TYPE_VIBRATION;
extern const SensorType TYPE_FLOW;
extern const SensorType TYPE_VOLTAGE;
extern const SensorType TYPE_CURRENT;
extern const SensorType TYPE_COUNTER;
extern const SensorType TYPE_GENERIC;

/* Type catalog lookup — returns NULL if unknown. */
const SensorType *sensorType_get(uint8_t type_id);
const SensorType *sensorType_byName(const char *name);

/* ===== Sensor (runtime instance) ====================================== */

typedef struct {
    char     name[SENSOR_NAME_MAX];  /* user-given, e.g. "Chamber A Temp" */
    uint8_t  type_id;                /* maps to one SensorType */
    uint8_t  enabled;                /* 0 = slot unused */
    int8_t   scale_override;         /* 0 = use SensorType.default_scale */

    int32_t  value;                  /* current raw value */
    uint32_t last_update_ms;         /* timestamp of last setValue */
} Sensor;

extern Sensor g_sensors[SENSOR_MAX];

/* Clear all slots to disabled. Call once at boot before any sensor_assign. */
void sensor_init(void);

/*
    Instance ops — return code:
      0  = OK
     -1  = invalid arg (NULL pointer)
     -2  = slot out of range
     -3  = unknown type_id (sensor_assign only)
*/
int sensor_assign(uint8_t slot, uint8_t type_id, const char *name);
int sensor_unassign(uint8_t slot);
int sensor_setName(uint8_t slot, const char *name);
int sensor_setValue(uint8_t slot, int32_t value);
int sensor_getValue(uint8_t slot, int32_t *out);

/* Read-only access (returns NULL if slot out of range). */
const Sensor *sensor_get(uint8_t slot);

/* Effective scale = scale_override if non-zero, else SensorType.default_scale. */
int sensor_effectiveScale(uint8_t slot);

/*  DISCRETE sensors only — returns the label matching the current value,
    or NULL if the slot is not discrete or no label matches. */
const char *sensor_valueLabel(uint8_t slot);

/* Iteration — returns slot index or -1 if not found. */
int sensor_findByType(uint8_t type_id, uint8_t start_slot);
int sensor_findByName(const char *name);
int sensor_countByType(uint8_t type_id);
int sensor_countEnabled(void);

#ifdef __cplusplus
}
#endif

#endif /* _SENSOR_H_ */
