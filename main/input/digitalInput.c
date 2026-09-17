/*  CN10 / CN11 contact inputs — see digitalInput.h.

    Reads sixteen GPIO lines and publishes each as one item bank number, which
    is all it takes for a value to reach the web page and SNMP.

    The numbers are 111 to 126 and they are the customer's, not ours: their
    document says "접점 상태값은 111 부터 16개". That puts the contacts in the
    same flat number space as the HVAC unit's own items (1-76, 100), under
    1.3.6.1.4.1.22210.2.1.<번호>.0, which is what they asked for.  */

#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"

#include "FreeRTOS.h"
#include "task.h"

#include "digitalInput.h"
#include "itemBank.h"           /* item_bank_register, item_set              */
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

/*  Terminal 0 is number 111, terminal 1 is 112, and so on to 126. */
#define DIN_ITEM_BASE       111

/*  What the panel shows, not what the pin reads: a closed contact is a
    healthy sensor. */
#define DIN_STATE_NORMAL    1
#define DIN_STATE_ALARM     2

#define DIN_POLL_PERIOD     200     /* ms between sweeps                     */

static const uint8_t s_pins[DIN_COUNT] = DIN_PINS;

/*  The item table is filled in at init rather than written out as a literal,
    because each name carries the pin behind the terminal and the pin map is a
    board header. It has to outlive init: item_bank_register() keeps the
    pointer and never copies, so neither the table nor the names can be local
    to a function. */
static char    s_name[DIN_COUNT][16];
static ItemDef s_item[DIN_COUNT];

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
    int skipped = 0;
    int rc;

    for (int i = 0; i < DIN_COUNT; i++) {
        /*  The name carries the terminal number and the pin behind it, so the
            web page can lay the inputs out the way the connector is wired
            without keeping its own copy of the pin map. Terminal numbering
            starts at zero to match the board drawing.

            The number is claimed either way, so the numbering stays put
            whether or not a given pin is available yet -- freeing a pin later
            must not shift every input after it. */
        snprintf(s_name[i], sizeof(s_name[i]), "IO%d GP%u", i, s_pins[i]);

        s_item[i].num     = (uint8_t)(DIN_ITEM_BASE + i);
        s_item[i].name    = s_name[i];
        s_item[i].scale   = 0;
        s_item[i].unit    = "";
        s_item[i].trap_on = DIN_STATE_ALARM;
        s_last[i] = 0;

        if (din_pin_taken(s_pins[i])) {
            skipped++;
            continue;
        }
        gpio_init(s_pins[i]);
        gpio_set_dir(s_pins[i], GPIO_IN);
        gpio_pull_up(s_pins[i]);        /* open terminal reads high = alarm */
    }

    /*  A second table on the HVAC device, alongside whatever the protocol on
        that port registered. The numbers do not overlap, which the bank
        checks rather than trusts. */
    rc = item_bank_register(ITEM_DEV_HVAC, s_item, DIN_COUNT);
    if (rc < 0) {
        PRT_INFO("digitalInput: item table rejected (%d)\r\n", rc);
        return rc;
    }

    PRT_INFO("digitalInput: %d inputs at numbers %d..%d (%d pin(s) still in "
             "use elsewhere, skipped)\r\n",
             DIN_COUNT, DIN_ITEM_BASE, DIN_ITEM_BASE + DIN_COUNT - 1, skipped);
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

        /*  No trap call here. The agent watches every item against its
            ItemDef.trap_on and fires once on the way in, so a line going to
            alarm reports itself -- and a line coming back does not, which is
            what "트랩 한번 발생" asks for. */
        item_set(ITEM_DEV_HVAC, (uint8_t)(DIN_ITEM_BASE + i), (int32_t)state);
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
