#include <string.h>
#include "sensor.h"

/*  =========================================================================
    Value column catalog
    --------------------------------------------------------------------------
    EDIT THIS to change what every device exposes. The array length must
    equal DEVICE_VALUE_COLS (sensor.h). Order = SNMP value-column order
    = order of comma-separated values in the UART "S/T" commands.
    ========================================================================= */
const ValueColumn g_value_columns[DEVICE_VALUE_COLS] = {
    { "Temperature", "C",   -1 },   /* col 0 — raw 235 -> 23.5 C   */
    { "Humidity",    "%RH", -1 },   /* col 1 — raw 600 -> 60.0 %RH */
    { "Alarm",       "",     0 },   /* col 2 — 0 / 1               */
};

/*  =========================================================================
    Device bank
    ========================================================================= */

Device g_devices[DEVICE_COUNT] = {0};

/*  Rows handed out by device_bank_reserve() so far. Only ever grows, and only
    from protocol tasks as they start, so no lock is needed: App.c creates them
    one after another and each reserves before its first exchange. */
static uint8_t s_bank_next = 0;

void device_init(void) {
    memset(g_devices, 0, sizeof(g_devices));
    s_bank_next = 0;
}

/*  Give a polling protocol its own block of rows.

    A protocol that owns a fixed set of devices — a Modbus master with N
    slaves, say — used to carry a hand-picked base row per port, and two of
    them picking overlapping ranges wrote over each other's values with
    nothing to say so. Reserving instead means the ranges cannot overlap and
    nobody has to know what the other protocols chose.

    S/T/R does not reserve: its commands name a row outright ("S5=..." is
    device 5), so it addresses the whole bank by design. */
int device_bank_reserve(uint8_t count) {
    int base;

    if (count == 0) {
        return -1;
    }
    if ((int)s_bank_next + (int)count > DEVICE_COUNT) {
        return -2;                  /* bank full */
    }
    base = (int)s_bank_next;
    s_bank_next = (uint8_t)(s_bank_next + count);
    return base;
}

int device_assign(uint8_t dev, const char *name) {
    if (dev >= DEVICE_COUNT) {
        return -2;
    }
    if (name == NULL) {
        return -1;
    }
    memset(&g_devices[dev], 0, sizeof(g_devices[dev]));
    g_devices[dev].enabled = 1;
    strncpy(g_devices[dev].name, name, DEVICE_NAME_MAX - 1);
    g_devices[dev].name[DEVICE_NAME_MAX - 1] = '\0';
    return 0;
}

int device_unassign(uint8_t dev) {
    if (dev >= DEVICE_COUNT) {
        return -2;
    }
    memset(&g_devices[dev], 0, sizeof(g_devices[dev]));
    return 0;
}

int device_setName(uint8_t dev, const char *name) {
    if (dev >= DEVICE_COUNT) {
        return -2;
    }
    if (name == NULL) {
        return -1;
    }
    strncpy(g_devices[dev].name, name, DEVICE_NAME_MAX - 1);
    g_devices[dev].name[DEVICE_NAME_MAX - 1] = '\0';
    return 0;
}

int device_setValue(uint8_t dev, uint8_t col, int32_t value) {
    if (dev >= DEVICE_COUNT || col >= DEVICE_VALUE_COLS) {
        return -2;
    }
    g_devices[dev].value[col] = value;
    g_devices[dev].enabled = 1;   /* receiving data registers the device */
    return 0;
}

int device_getValue(uint8_t dev, uint8_t col, int32_t *out) {
    if (dev >= DEVICE_COUNT || col >= DEVICE_VALUE_COLS) {
        return -2;
    }
    if (out == NULL) {
        return -1;
    }
    *out = g_devices[dev].value[col];
    return 0;
}

const Device *device_get(uint8_t dev) {
    if (dev >= DEVICE_COUNT) {
        return NULL;
    }
    return &g_devices[dev];
}

const ValueColumn *valueColumn_get(uint8_t col) {
    if (col >= DEVICE_VALUE_COLS) {
        return NULL;
    }
    return &g_value_columns[col];
}

int device_columnScale(uint8_t col) {
    if (col >= DEVICE_VALUE_COLS) {
        return 0;
    }
    return g_value_columns[col].scale;
}

int device_countEnabled(void) {
    int n = 0;
    for (uint8_t d = 0; d < DEVICE_COUNT; d++) {
        if (g_devices[d].enabled) {
            n++;
        }
    }
    return n;
}
