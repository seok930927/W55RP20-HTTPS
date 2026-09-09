#ifndef _DIGITAL_INPUT_H_
#define _DIGITAL_INPUT_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*  CN10 / CN11 contact inputs.

    Sixteen terminals, one per sensor, each telling whether the sensor wired
    to it is healthy. The line is read straight off a GPIO -- closed (low) is
    normal, open (high) is alarm -- and published into the device bank, so the
    web page and SNMP pick it up the same way they pick up a serial protocol's
    readings. Nothing downstream knows the difference.

    Where the values land: one device-bank row per input, reserved at startup
    with device_bank_reserve(), written into the alarm column as 1 (normal) or
    2 (alarm) to match what the panel expects. The other columns of those rows
    stay 0 -- the bank gives every device the same columns, so a contact input
    carries a temperature and a humidity it has no use for.

    Pin map lives in WIZnet_board.h (DIN_PINS). Pins that board build still
    assigns elsewhere are listed in DIN_PINS_IN_USE and skipped here, so the
    LEDs and the factory-reset button keep working until the board frees them.

    This is a worked example of publishing into the bank from something that
    is not a serial protocol: it has no SerialPort, no entry in the protocol
    registry, and no place in the web Mode dropdown. It is just a task.  */

/*  Set the input pins up and reserve the bank rows. Called by the task; call
    it yourself only if you drive the polling from somewhere else. Returns the
    first reserved row, or negative if the bank had no room. */
int digitalInput_init(void);

/*  Read every input once and publish what changed. `base` is the row returned
    by digitalInput_init(). Returns how many inputs changed state. */
int digitalInput_poll(int base);

/* FreeRTOS task. Takes no parameter. */
void digitalInput_task(void *argument);

#ifdef __cplusplus
}
#endif

#endif /* _DIGITAL_INPUT_H_ */
