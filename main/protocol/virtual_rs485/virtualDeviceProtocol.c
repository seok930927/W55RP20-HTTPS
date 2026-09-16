#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "FreeRTOS.h"
#include "task.h"
#include "virtualDeviceProtocol.h"
#include "sensor.h"
#include "snmpHandler.h"
#include "WIZ5XXSR-RP_Debug.h"

/* Example: @TEMP=25.4;HUM=60.2;ALARM=0*XX\r\n, XX = XOR through '*'. */
#define FRAME_MAX 96
#define FRAME_TIMEOUT_MS 500

typedef enum {
    VIRTUAL_VALUE_TEMPERATURE = 0,
    VIRTUAL_VALUE_HUMIDITY,
    VIRTUAL_VALUE_ALARM,
    VIRTUAL_VALUE_COUNT
} VirtualValueIndex;

typedef struct {
    int32_t value[VIRTUAL_VALUE_COUNT];
} VirtualDeviceValues;

static int parse_frame(char *f, VirtualDeviceValues *values)
{
    char *star = strchr(f, '*');
    unsigned got;
    unsigned xorv = 0;
    size_t i;
    float temperature;
    float humidity;
    long alarm;

    if (!star || strncmp(f, "@TEMP=", 6) != 0 ||
            sscanf(star + 1, "%2x", &got) != 1) {
        return -1;
    }

    for (i = 0; i < (size_t)(star - f); ++i) {
        xorv ^= (unsigned char)f[i];
    }

    if ((xorv & 255u) != (got & 255u)) {
        return -1;
    }

    *star = '\0';

    if (sscanf(f, "@TEMP=%f;HUM=%f;ALARM=%ld",
               &temperature, &humidity, &alarm) != 3)
    {
        return -1;
    }
    if (alarm != 0 && alarm != 1)
    {
        return -1;
    }

    values->value[VIRTUAL_VALUE_TEMPERATURE] =
        (int32_t)(temperature * 10.0f +
                  (temperature < 0 ? -0.5f : 0.5f));
    values->value[VIRTUAL_VALUE_HUMIDITY] =
        (int32_t)(humidity * 10.0f +
                  (humidity < 0 ? -0.5f : 0.5f));
    values->value[VIRTUAL_VALUE_ALARM] = (int32_t)alarm;

    return 0;
}

void virtual_device_task(void *argument)
{
    SerialPort *port = (SerialPort *)argument;
    uint8_t src;
    char frame[FRAME_MAX];
    size_t frame_length = 0;
    TickType_t frame_started = 0;
    VirtualDeviceValues values;

    if (!port) {
        vTaskDelete(NULL);
        return;
    }

    src = (uint8_t)port->channel;
    if (device_bank_reserve(1, src) < 0) {
        vTaskDelete(NULL);
        return;
    }

    serial_port_setup(port);
    serial_port_hw_flow_disable(port);
    device_bank_assign(src, 0, "VIRTUAL-232");

    for (;;)
    {
        int32_t c;

        if (frame_length > 0 &&
                (xTaskGetTickCount() - frame_started) >=
                pdMS_TO_TICKS(FRAME_TIMEOUT_MS))
        {
            PRT_INFO("virtual protocol: frame timeout\r\n");
            frame_length = 0;
        }

        if (serial_port_in_command_mode(port)) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        while ((c = serial_port_getc(port)) != RET_NOK)
        {
            if (frame_length == 0 && c != '@') {
                continue;
            }

            if (frame_length == 0) {
                frame_started = xTaskGetTickCount();
            }

            if (frame_length < FRAME_MAX - 1) {
                frame[frame_length++] = (char)c;
            } else {
                frame_length = 0;
                continue;
            }

            if (c == '\n')
            {
                frame[frame_length] = '\0';

                if (!parse_frame(frame, &values))
                {
                    for (uint8_t column = 0;
                            column < VIRTUAL_VALUE_COUNT;
                            column++)
                    {
                        device_bank_setValue(src, 0, column,
                                              values.value[column]);
                    }

                    snmp_notify_device((uint8_t)device_bank_row(src, 0));
                } else {
                    PRT_INFO("virtual protocol: invalid frame\r\n");
                }

                frame_length = 0;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}
