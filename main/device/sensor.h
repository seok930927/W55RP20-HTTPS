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

/*  Who publishes a row.

    A serial protocol uses its own port's channel number, so everything one
    port produces can be shown together and kept apart from the other port's.
    Sources that are not a serial port take a value above the port numbers. */
#define DEVICE_SRC_CONTACT  0x10    /* CN10/CN11 contact inputs */
#define DEVICE_SRC_NONE     0xFF    /* row never reserved */

/* One monitored device = one table row. */
typedef struct {
    char     name[DEVICE_NAME_MAX];
    uint8_t  enabled;                       /* 0 = slot never used */
    uint8_t  source;                        /* DEVICE_SRC_* / port channel */
    int32_t  value[DEVICE_VALUE_COLS];      /* raw values, column-indexed */
    uint32_t last_update_ms;
} Device;

extern Device g_devices[DEVICE_COUNT];

/* Clear every device. Call once at boot before any device_assign(). */
void device_init(void);

/*  Reserve `count` consecutive rows for one publisher and return the first
    one, or a negative code if the bank has no room (-2) or count is 0 (-1).

    Call it once, when the task starts, and keep the returned base; rows given
    out this way never overlap with another publisher's. This is what a
    protocol uses instead of a hand-picked base row -- two protocols choosing
    bases by hand is how they end up writing over each other.

    `source` is stamped on every row reserved, and survives device_assign(),
    so the web page and SNMP can group rows by where they came from. A serial
    protocol passes its port's channel number; anything else passes one of the
    DEVICE_SRC_* values.

    S/T/R does not reserve: its commands name a row outright, so it addresses
    the whole bank by design. */
int device_bank_reserve(uint8_t count, uint8_t source);

/*  Write to a reserved row by naming the publisher and its own index, rather
    than by working out a bank row.

    `idx` counts from 0 within what that source reserved: a Modbus master with
    four slaves uses 0..3 whatever rows it was actually given. The bank does
    the arithmetic, and an index past the end of the block is refused instead
    of landing on whoever owns the next row -- which is the whole point, since
    a base + offset the caller computes itself is checked by nobody.

    Return: 0 on success, -1 on a NULL name, -2 if that source reserved
    nothing or has no such index, -3 on a bad column. */
int device_bank_assign(uint8_t source, uint8_t idx, const char *name);
int device_bank_setValue(uint8_t source, uint8_t idx, uint8_t col, int32_t value);

/*  The bank row behind (source, idx), or negative if there is none. Only for
    the few places that must pass an absolute row on -- snmp_notify_device()
    takes one. Prefer the two calls above for reading and writing. */
int device_bank_row(uint8_t source, uint8_t idx);

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
