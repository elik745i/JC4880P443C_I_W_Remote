set(APPS_DIR ./)
file(GLOB_RECURSE APPS_C_SRCS ${APPS_DIR}/*.c)
file(GLOB_RECURSE APPS_CPP_SRCS ${APPS_DIR}/*.cpp)
set(APPS_SHARED_SRCS ${APPS_DIR}/system_ui_service.cpp)
list(REMOVE_ITEM APPS_CPP_SRCS ${APPS_SHARED_SRCS})

idf_component_register(
    SRCS ${APPS_C_SRCS} ${APPS_CPP_SRCS} ${APPS_SHARED_SRCS}
    INCLUDE_DIRS ${APPS_DIR}
                 ${APPS_DIR}/music_player
                 ${APPS_DIR}/sega_emulator/smsplus
                 ${APPS_DIR}/sega_emulator/gwenesis
                 ${APPS_DIR}/sega_emulator/gwenesis/src/bus
                 ${APPS_DIR}/sega_emulator/gwenesis/src/cpus/M68K
                 ${APPS_DIR}/sega_emulator/gwenesis/src/cpus/Z80
                 ${APPS_DIR}/sega_emulator/gwenesis/src/io
                 ${APPS_DIR}/sega_emulator/gwenesis/src/savestate
                 ${APPS_DIR}/sega_emulator/gwenesis/src/sound
                 ${APPS_DIR}/sega_emulator/gwenesis/src/vdp
    REQUIRES lvgl__lvgl esp_adc esp_event esp_wifi esp_http_client json nvs_flash esp_driver_jpeg esp_mm esp-brookesia bsp_extra esp32_p4_function_ev_board esp_video pedestrian_detect human_face_detect espressif__esp_lcd_touch_gt911 espressif__adc_battery_estimation espressif__avi_player app_update esp_partition esp-tls esp_hosted bt main)

target_compile_options(
    ${COMPONENT_LIB}
    PRIVATE
    $<$<COMPILE_LANGUAGE:C>:
        -Wno-format
        -Wno-incompatible-pointer-types
        -Wno-int-conversion
    >
    $<$<COMPILE_LANGUAGE:CXX>:
        -Wno-format
        -Wno-conversion-null
        -Wno-attributes
        -Wno-write-strings
    >
    -DLV_LVGL_H_INCLUDE_SIMPLE
)