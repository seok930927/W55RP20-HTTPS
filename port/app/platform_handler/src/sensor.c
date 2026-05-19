#include <string.h>
#include "sensor.h"

/*  =========================================================================
    Built-in SensorType definitions
    Adding a new type:
      1. Pick a stable type_id (add TYPE_ID_xxx in sensor.h)
      2. Define a const SensorType here
      3. Add it to type_registry[] below
    ========================================================================= */

static const SensorStateLabel STATUS_LABELS[] = {
    { 0, "Unknown"  },
    { 1, "Normal"   },
    { 2, "Warning"  },
    { 3, "Critical" },
};

static const SensorStateLabel ALARM_LABELS[] = {
    { 0, "Unknown" },
    { 1, "Normal"  },
    { 2, "Alarm"   },
};

const SensorType TYPE_TEMPERATURE = {
    .type_id       = TYPE_ID_TEMPERATURE,
    .name          = "Temperature",
    .unit          = "\xC2\xB0\x43",     /* °C in UTF-8 */
    .default_scale = -1,                  /* 256 -> 25.6 */
    .value_kind    = SENSOR_VALUE_CONTINUOUS,
};

const SensorType TYPE_HUMIDITY = {
    .type_id       = TYPE_ID_HUMIDITY,
    .name          = "Humidity",
    .unit          = "%RH",
    .default_scale = -1,
    .value_kind    = SENSOR_VALUE_CONTINUOUS,
};

const SensorType TYPE_ALARM = {
    .type_id       = TYPE_ID_ALARM,
    .name          = "Alarm",
    .unit          = "",
    .default_scale = 0,
    .value_kind    = SENSOR_VALUE_DISCRETE,
    .states        = ALARM_LABELS,
    .states_count  = sizeof(ALARM_LABELS) / sizeof(ALARM_LABELS[0]),
};

const SensorType TYPE_STATUS = {
    .type_id       = TYPE_ID_STATUS,
    .name          = "Status",
    .unit          = "",
    .default_scale = 0,
    .value_kind    = SENSOR_VALUE_DISCRETE,
    .states        = STATUS_LABELS,
    .states_count  = sizeof(STATUS_LABELS) / sizeof(STATUS_LABELS[0]),
};

const SensorType TYPE_RPM = {
    .type_id       = TYPE_ID_RPM,
    .name          = "RPM",
    .unit          = "rpm",
    .default_scale = 0,
    .value_kind    = SENSOR_VALUE_CONTINUOUS,
};

const SensorType TYPE_ILLUMINANCE = {
    .type_id       = TYPE_ID_ILLUMINANCE,
    .name          = "Illuminance",
    .unit          = "lux",
    .default_scale = 0,
    .value_kind    = SENSOR_VALUE_CONTINUOUS,
};

const SensorType TYPE_PRESSURE = {
    .type_id       = TYPE_ID_PRESSURE,
    .name          = "Pressure",
    .unit          = "kPa",
    .default_scale = -1,
    .value_kind    = SENSOR_VALUE_CONTINUOUS,
};

const SensorType TYPE_VIBRATION = {
    .type_id       = TYPE_ID_VIBRATION,
    .name          = "Vibration",
    .unit          = "mm/s",
    .default_scale = -2,
    .value_kind    = SENSOR_VALUE_CONTINUOUS,
};

const SensorType TYPE_FLOW = {
    .type_id       = TYPE_ID_FLOW,
    .name          = "Flow",
    .unit          = "L/min",
    .default_scale = -1,
    .value_kind    = SENSOR_VALUE_CONTINUOUS,
};

const SensorType TYPE_VOLTAGE = {
    .type_id       = TYPE_ID_VOLTAGE,
    .name          = "Voltage",
    .unit          = "V",
    .default_scale = -1,
    .value_kind    = SENSOR_VALUE_CONTINUOUS,
};

const SensorType TYPE_CURRENT = {
    .type_id       = TYPE_ID_CURRENT,
    .name          = "Current",
    .unit          = "A",
    .default_scale = -2,
    .value_kind    = SENSOR_VALUE_CONTINUOUS,
};

const SensorType TYPE_COUNTER = {
    .type_id       = TYPE_ID_COUNTER,
    .name          = "Counter",
    .unit          = "",
    .default_scale = 0,
    .value_kind    = SENSOR_VALUE_CONTINUOUS,
};

const SensorType TYPE_GENERIC = {
    .type_id       = TYPE_ID_GENERIC,
    .name          = "Generic",
    .unit          = "",
    .default_scale = 0,
    .value_kind    = SENSOR_VALUE_CONTINUOUS,
};

/* Indexed by type_id. Sparse — GENERIC (id=99) handled separately. */
static const SensorType *const type_registry[] = {
    [TYPE_ID_TEMPERATURE]   = &TYPE_TEMPERATURE,
    [TYPE_ID_HUMIDITY]      = &TYPE_HUMIDITY,
    [TYPE_ID_ALARM]         = &TYPE_ALARM,
    [TYPE_ID_STATUS]        = &TYPE_STATUS,
    [TYPE_ID_RPM]           = &TYPE_RPM,
    [TYPE_ID_ILLUMINANCE]   = &TYPE_ILLUMINANCE,
    [TYPE_ID_PRESSURE]      = &TYPE_PRESSURE,
    [TYPE_ID_VIBRATION]     = &TYPE_VIBRATION,
    [TYPE_ID_FLOW]          = &TYPE_FLOW,
    [TYPE_ID_VOLTAGE]       = &TYPE_VOLTAGE,
    [TYPE_ID_CURRENT]       = &TYPE_CURRENT,
    [TYPE_ID_COUNTER]       = &TYPE_COUNTER,
};

#define TYPE_REGISTRY_SIZE  (sizeof(type_registry) / sizeof(type_registry[0]))

const SensorType *sensorType_get(uint8_t type_id) {
    if (type_id == TYPE_ID_GENERIC) {
        return &TYPE_GENERIC;
    }
    if (type_id == TYPE_ID_DISABLED || type_id >= TYPE_REGISTRY_SIZE) {
        return NULL;
    }
    return type_registry[type_id];
}

const SensorType *sensorType_byName(const char *name) {
    if (name == NULL) {
        return NULL;
    }
    /* Skip index 0 — TYPE_ID_DISABLED has no entry in the registry. */
    for (uint8_t i = 1; i < TYPE_REGISTRY_SIZE; i++) {
        if (type_registry[i] && strcmp(type_registry[i]->name, name) == 0) {
            return type_registry[i];
        }
    }
    /* GENERIC sits outside the sparse registry (id 99); check separately. */
    if (strcmp(TYPE_GENERIC.name, name) == 0) {
        return &TYPE_GENERIC;
    }
    return NULL;
}

/*  =========================================================================
    Sensor instance bank
    ========================================================================= */

Sensor g_sensors[SENSOR_MAX] = {0};

void sensor_init(void) {
    memset(g_sensors, 0, sizeof(g_sensors));
}

int sensor_assign(uint8_t index, uint8_t type_id, const char *name) {
    if (index >= SENSOR_MAX) {
        return -2;
    }
    if (sensorType_get(type_id) == NULL) {
        return -3;
    }
    if (name == NULL) {
        return -1;
    }

    memset(&g_sensors[index], 0, sizeof(g_sensors[index]));
    g_sensors[index].type_id = type_id;
    g_sensors[index].enabled = 1;
    strncpy(g_sensors[index].name, name, SENSOR_NAME_MAX - 1);
    g_sensors[index].name[SENSOR_NAME_MAX - 1] = '\0';
    return 0;
}

int sensor_unassign(uint8_t index) {
    if (index >= SENSOR_MAX) {
        return -2;
    }
    memset(&g_sensors[index], 0, sizeof(g_sensors[index]));
    return 0;
}

int sensor_setName(uint8_t index, const char *name) {
    if (index >= SENSOR_MAX) {
        return -2;
    }
    if (name == NULL) {
        return -1;
    }
    strncpy(g_sensors[index].name, name, SENSOR_NAME_MAX - 1);
    g_sensors[index].name[SENSOR_NAME_MAX - 1] = '\0';
    return 0;
}

int sensor_setValue(uint8_t index, int32_t value) {
    if (index >= SENSOR_MAX) {
        return -2;
    }
    g_sensors[index].value = value;
    /* last_update_ms timestamping deferred — caller (uart task) may set if needed */
    return 0;
}

int sensor_getValue(uint8_t index, int32_t *out) {
    if (index >= SENSOR_MAX) {
        return -2;
    }
    if (out == NULL) {
        return -1;
    }
    *out = g_sensors[index].value;
    return 0;
}

const Sensor *sensor_get(uint8_t index) {
    if (index >= SENSOR_MAX) {
        return NULL;
    }
    return &g_sensors[index];
}

int sensor_effectiveScale(uint8_t index) {
    if (index >= SENSOR_MAX) {
        return 0;
    }
    if (g_sensors[index].scale_override != 0) {
        return g_sensors[index].scale_override;
    }
    const SensorType *t = sensorType_get(g_sensors[index].type_id);
    return t ? t->default_scale : 0;
}

const char *sensor_valueLabel(uint8_t index) {
    if (index >= SENSOR_MAX) {
        return NULL;
    }
    const SensorType *t = sensorType_get(g_sensors[index].type_id);
    if (t == NULL || t->value_kind != SENSOR_VALUE_DISCRETE) {
        return NULL;   /* CONTINUOUS types have no label */
    }
    int32_t v = g_sensors[index].value;
    /* Only match within uint8_t range — values >= 256 or < 0 are "Unknown". */
    if (v >= 0 && v <= 255) {
        for (uint8_t i = 0; i < t->states_count; i++) {
            if (t->states[i].value == (uint8_t)v) {
                return t->states[i].label;
            }
        }
    }
    /* DISCRETE but value doesn't match any defined state. */
    return "Unknown";
}

int sensor_findByType(uint8_t type_id, uint8_t start_index) {
    for (uint8_t s = start_index; s < SENSOR_MAX; s++) {
        if (g_sensors[s].enabled && g_sensors[s].type_id == type_id) {
            return s;
        }
    }
    return -1;
}

int sensor_findByName(const char *name) {
    if (name == NULL) {
        return -1;
    }
    for (uint8_t s = 0; s < SENSOR_MAX; s++) {
        if (g_sensors[s].enabled && strcmp(g_sensors[s].name, name) == 0) {
            return s;
        }
    }
    return -1;
}

int sensor_countByType(uint8_t type_id) {
    int n = 0;
    for (uint8_t s = 0; s < SENSOR_MAX; s++) {
        if (g_sensors[s].enabled && g_sensors[s].type_id == type_id) {
            n++;
        }
    }
    return n;
}

int sensor_countEnabled(void) {
    int n = 0;
    for (uint8_t s = 0; s < SENSOR_MAX; s++) {
        if (g_sensors[s].enabled) {
            n++;
        }
    }
    return n;
}
