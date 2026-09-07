/**
    Copyright (c) 2021 WIZnet Co.,Ltd

    SPDX-License-Identifier: BSD-3-Clause
*/

/**
    ----------------------------------------------------------------------------------------------------
    Includes
    ----------------------------------------------------------------------------------------------------
*/
#include <stdio.h>

#include "pico/stdlib.h"

#include "timer.h"

/**
    ----------------------------------------------------------------------------------------------------
    Variables
    ----------------------------------------------------------------------------------------------------
*/
/* Timer */
static struct repeating_timer g_timer;
void (*callback_ptr)(void);

/**
    ----------------------------------------------------------------------------------------------------
    Functions
    ----------------------------------------------------------------------------------------------------
*/
/* Timer */
void wizchip_1ms_timer_initialize(void (*callback)(void)) {
    callback_ptr = callback;
    add_repeating_timer_us(-1000, wizchip_1ms_timer_callback, NULL, &g_timer);
}

bool wizchip_1ms_timer_callback(struct repeating_timer *t) {
    if (callback_ptr != NULL) {
        callback_ptr();
    }
    /*  The SDK stops a repeating timer when its callback returns false, so
        falling off the end here would leave the 1 ms tick alive only for as
        long as r0 happened to be non-zero. */
    return true;
}

/* Delay */
void wizchip_delay_ms(uint32_t ms) {
    sleep_ms(ms);
}
