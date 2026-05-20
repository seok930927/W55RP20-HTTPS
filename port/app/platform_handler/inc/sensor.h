#ifndef _SENSOR_H_
#define _SENSOR_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*  =========================================================================
    Device-centric monitoring model
    --------------------------------------------------------------------------
    The firmware monitors a fixed bank of DEVICES. Every device exposes the
    SAME set of values (DEVICE_VALUE_COLS of them) — e.g. temperature,
    humidity, alarm — described once in g_value_columns[].

        g_devices[d].value[c]   = device d, value column c

    This maps 1:1 onto the SNMP conceptual table and the web view:
        row    = device
        column = value column

    To change what every device exposes, edit ONLY these two things:
        1. DEVICE_VALUE_COLS  (below)
        2. g_value_columns[]  (in sensor.c)
    The SNMP table build/refresh, the trap path and the web JSON all derive
    their shape from those — nothing else needs touching.
    ========================================================================= */

#define DEVICE_NAME_MAX     16      /* device name: 15 chars + NUL          */
#define DEVICE_COUNT        64      /* number of devices (= rows)   <= 127  */
#define DEVICE_VALUE_COLS   3       /* values per device (= value columns)  */

/*  Constraints:
      DEVICE_COUNT      <= 127               (SNMP cell OID stays 12 bytes)
      DEVICE_VALUE_COLS <= 125               (SNMP column id stays 1 byte)  */

/* Descriptor for one value column — shared by every device. */
typedef struct {
    const char *name;       /* "Temperature", "Humidity", "Alarm" ... */
    const char *unit;       /* "C", "%RH", "" */
    int8_t      scale;      /* displayed = raw * 10^scale  (0 = none) */
} ValueColumn;

extern const ValueColumn g_value_columns[DEVICE_VALUE_COLS];

/* One monitored device = one table row. */
typedef struct {
    char     name[DEVICE_NAME_MAX];
    uint8_t  enabled;                       /* 0 = slot never used */
    int32_t  value[DEVICE_VALUE_COLS];      /* raw values, column-indexed */
    uint32_t last_update_ms;
} Device;

extern Device g_devices[DEVICE_COUNT];

/* Clear every device. Call once at boot before any device_assign(). */
void device_init(void);

/*  Per-device ops — return code:
      0  = OK
     -1  = invalid arg (NULL pointer)
     -2  = device / column index out of range
*/
int device_assign(uint8_t dev, const char *name);          /* enable + name */
int device_unassign(uint8_t dev);
int device_setName(uint8_t dev, const char *name);
int device_setValue(uint8_t dev, uint8_t col, int32_t value);
int device_getValue(uint8_t dev, uint8_t col, int32_t *out);

/* Read-only access — returns NULL if index out of range. */
const Device      *device_get(uint8_t dev);
const ValueColumn *valueColumn_get(uint8_t col);

/* Effective scale of a value column = ValueColumn.scale. */
int device_columnScale(uint8_t col);

int device_countEnabled(void);

#ifdef __cplusplus
}
#endif

#endif /* _SENSOR_H_ */
