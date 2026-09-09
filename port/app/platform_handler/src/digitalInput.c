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

int digitalInput_poll(void) {
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

#ifdef DIN_PINS_IN_USE
static const uint8_t s_in_use[] = DIN_PINS_IN_USE;
#endif

/*  Last published state per input, so a sweep only sends a trap for a line
    that actually moved. 0 means "nothing published yet". */
static uint8_t s_last[DIN_COUNT];

/*  True while some other function on this board still owns the pin, so that a
    board which has not freed one of these lines yet keeps whatever is using
    it. Without DIN_PINS_IN_USE nothing is held back and every terminal reads. */
static int din_pin_taken(uint8_t pin) {
#ifdef DIN_PINS_IN_USE
    for (unsigned i = 0; i < sizeof(s_in_use) / sizeof(s_in_use[0]); i++) {
        if (s_in_use[i] == pin) {
            return 1;
        }
    }
#else
    (void)pin;
#endif
    return 0;
}

int digitalInput_init(void) {
    int base = device_bank_reserve(DIN_COUNT, DEVICE_SRC_CONTACT);
    int skipped = 0;

    if (base < 0) {
        PRT_INFO("digitalInput: no room in the device bank\r\n");
        return base;
    }
    /*  Terminal number is the index into the block from here on -- the base is
        only used for the log line below. */

    for (int i = 0; i < DIN_COUNT; i++) {
        char name[DEVICE_NAME_MAX];

        /*  The name carries the terminal number and the pin behind it, so the
            web page can lay the inputs out the way the connector is wired
            without keeping its own copy of the pin map. Terminal numbering
            starts at zero to match the board drawing.

            The row is claimed either way, so the numbering stays put whether
            or not a given pin is available yet -- freeing a pin later must
            not shift every input after it. */
        snprintf(name, sizeof(name), "IO%d GP%u", i, s_pins[i]);
        device_bank_assign(DEVICE_SRC_CONTACT, (uint8_t)i, name);
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
    return 0;
}

int digitalInput_poll(void) {
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
        device_bank_setValue(DEVICE_SRC_CONTACT, (uint8_t)i,
                             DIN_VALUE_COL, (int32_t)state);
        snmp_notify_device((uint8_t)device_bank_row(DEVICE_SRC_CONTACT, (uint8_t)i));
        changed++;

        PRT_INFO("digitalInput: IN-%d (GP%u) -> %s\r\n",
                 i + 1, s_pins[i],
                 (state == DIN_STATE_ALARM) ? "alarm" : "normal");
    }
    return changed;
}

void digitalInput_task(void *argument) {
    (void)argument;

    if (digitalInput_init() < 0) {
        vTaskDelete(NULL);
        return;
    }

    while (1) {
        digitalInput_poll();
        vTaskDelay(pdMS_TO_TICKS(DIN_POLL_PERIOD));
    }
}

#endif /* DIN_COUNT */
