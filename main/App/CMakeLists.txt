set(TARGET_NAME App)

add_executable(${TARGET_NAME}
        ${TARGET_NAME}.c
        )

target_link_libraries(${TARGET_NAME} PRIVATE
        APP_MCU_FILES
        APP_IOLIBRARY_FILES
        APP_BOARD_FILES
        APP_CONFIG_FILES
        APP_MBEDTLS_FILES
        APP_TIMER_FILES
        APP_PLATFORM_FILES
        APP_S2E_FILES
        )

set_target_properties(${TARGET_NAME} PROPERTIES COMPILE_FLAGS "-Wall")
# Drop the picotool VENDOR reset interface so the device enumerates as a single
# pure CDC (ACM) function. The vendor interface needs a WinUSB/MS-OS-2.0 bind that
# shows as "Unknown device / Code 10" in Windows when it fails — and it is not
# needed here (we flash via BOOTSEL/UF2, not picotool over USB).
target_compile_definitions(${TARGET_NAME} PRIVATE PICO_STDIO_USB_ENABLE_RESET_VIA_VENDOR_INTERFACE=0)
pico_enable_stdio_usb(${TARGET_NAME} 1)
# UART stdio disabled: uart1 (GPIO4/5) is the RS-232 data port. Debug goes via USB CDC.
pico_enable_stdio_uart(${TARGET_NAME} 0)
pico_add_extra_outputs(${TARGET_NAME})

set(TARGET_NAME_LINKER ${TARGET_NAME}_linker)
add_executable(${TARGET_NAME_LINKER}
        ${TARGET_NAME}.c
        )
target_link_libraries(${TARGET_NAME_LINKER} PRIVATE
        APP_MCU_FILES
        APP_IOLIBRARY_FILES
        APP_BOARD_FILES
        APP_CONFIG_FILES
        APP_MBEDTLS_FILES
        APP_TIMER_FILES
        APP_PLATFORM_FILES
        APP_S2E_FILES
        )
set_target_properties(${TARGET_NAME_LINKER} PROPERTIES COMPILE_FLAGS "-Wall")
pico_set_linker_script(${TARGET_NAME_LINKER} ${CMAKE_CURRENT_SOURCE_DIR}/${TARGET_NAME_LINKER}.ld)
target_compile_definitions(${TARGET_NAME_LINKER} PRIVATE PICO_STDIO_USB_ENABLE_RESET_VIA_VENDOR_INTERFACE=0)
pico_enable_stdio_usb(${TARGET_NAME_LINKER} 1)
# UART stdio disabled: uart1 (GPIO4/5) is the RS-232 data port. Debug goes via USB CDC.
pico_enable_stdio_uart(${TARGET_NAME_LINKER} 0)
pico_add_extra_outputs(${TARGET_NAME_LINKER})
