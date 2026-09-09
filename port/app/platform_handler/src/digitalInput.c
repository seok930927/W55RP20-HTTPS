/*  CN10 / CN11 contact inputs — see digitalInput.h.

    Reads sixteen GPIO lines and publishes each as one device-bank row, which
    is all it takes for a value to reach the web page and SNMP.  */

#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"

#include "FreeRTOS.h"
#include "task.h"

#include "digitalInput.h"
#include "sensor.h"             /* device_bank_reserve, device_setValue      */
#include "snmpHandler.h"        /* snmp_notify_device                        */
#include "WIZnet_board.h"       /* DIN_COUNT, DIN_PINS, DIN_PINS_IN_USE      */
#include "WIZ5XXSR-RP_Debug.h"  /* PRT_INFO                                  */

/*  Only the boards that route CN10/CN11 define a pin map. On the others this
    whole module compiles down to a task that exits, so App.c does not need a
    board test around the xTaskCreate(). */
#ifndef DIN_COUNT

int digitalInput_init(void) {
    return -1;
}

int digitalInput_poll(int base) {
    (void)base;
    return 0;
}

void digitalInput_task(void *argument) {
    (void)argument;
    vTaskDelete(NULL);
}

#else

/*  Which value column the state goes in. The bank's columns are shared by
    every device (see sensor.h), so this rides in the alarm column rather
    than getting one of its own -- a column costs every row, not just these. */
#define DIN_VALUE_COL       2

/*  What the panel shows, not what the pin reads: a closed contact is a
    healthy sensor. */
#define DIN_STATE_NORMAL    1
#define DIN_STATE_ALARM     2

#define DIN_POLL_PERIOD     200     /* ms between sweeps                     */

static const uint8_t s_pins[DIN_COUNT] = DIN_PINS;
static const uint8_t s_in_use[] = DIN_PINS_IN_USE;

/*  Last published state per input, so a sweep only sends a trap for a line
    that actually moved. 0 means "nothing published yet". */
static uint8_t s_last[DIN_COUNT];

/*  True while some other function on this board still owns the pin. Those are
    skipped rather than taken over, so the LEDs and the factory-reset button
    keep working on a build where the connector is not wired up yet. */
static int din_pin_taken(uint8_t pin) {
    for (unsigned i = 0; i < sizeof(s_in_use) / sizeof(s_in_use[0]); i++) {
        if (s_in_use[i] == pin) {
            return 1;
        }
    }
    return 0;
}

int digitalInput_init(void) {
    int base = device_bank_reserve(DIN_COUNT);
    int skipped = 0;

    if (base < 0) {
        PRT_INFO("digitalInput: no room in the device bank\r\n");
        return base;
    }

    for (int i = 0; i < DIN_COUNT; i++) {
        char name[DEVICE_NAME_MAX];

        /*  The row is claimed either way, so the numbering stays put whether
            or not a given pin is available yet -- freeing a pin later must
            not shift every input after it. */
        snprintf(name, sizeof(name), "IN-%d", i + 1);
        device_assign((uint8_t)(base + i), name);
        s_last[i] = 0;

        if (din_pin_taken(s_pins[i])) {
            skipped++;
            continue;
        }
        gpio_init(s_pins[i]);
        gpio_set_dir(s_pins[i], GPIO_IN);
        gpio_pull_up(s_pins[i]);        /* open terminal reads high = alarm */
    }

    PRT_INFO("digitalInput: %d inputs at rows %d..%d (%d pin(s) still in use "
             "elsewhere, skipped)\r\n",
             DIN_COUNT, base, base + DIN_COUNT - 1, skipped);
    return base;
}

int digitalInput_poll(int base) {
    int changed = 0;

    for (int i = 0; i < DIN_COUNT; i++) {
        uint8_t state;

        if (din_pin_taken(s_pins[i])) {
            continue;               /* not ours to read yet */
        }
        state = gpio_get(s_pins[i]) ? DIN_STATE_ALARM : DIN_STATE_NORMAL;

        if (state == s_last[i]) {
            continue;
        }
        s_last[i] = state;
        device_setValue((uint8_t)(base + i), DIN_VALUE_COL, (int32_t)state);
        snmp_notify_device((uint8_t)(base + i));
        changed++;

        PRT_INFO("digitalInput: IN-%d (GP%u) -> %s\r\n",
                 i + 1, s_pins[i],
                 (state == DIN_STATE_ALARM) ? "alarm" : "normal");
    }
    return changed;
}

void digitalInput_task(void *argument) {
    int base;

    (void)argument;

    base = digitalInput_init();
    if (base < 0) {
        vTaskDelete(NULL);
        return;
    }

    while (1) {
        digitalInput_poll(base);
        vTaskDelay(pdMS_TO_TICKS(DIN_POLL_PERIOD));
    }
}

#endif /* DIN_COUNT */
