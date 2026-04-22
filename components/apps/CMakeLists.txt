set(APPS_DIR ${CMAKE_CURRENT_LIST_DIR})
set(APPS_SRCS ${APPS_DIR}/system_ui_service.cpp)

function(jc4880_append_app_sources out_var dir_path)
    file(GLOB_RECURSE APP_C_SRCS ${dir_path}/*.c)
    file(GLOB_RECURSE APP_CPP_SRCS ${dir_path}/*.cpp)
    set(${out_var} ${${out_var}} ${APP_C_SRCS} ${APP_CPP_SRCS} PARENT_SCOPE)
endfunction()

if(CONFIG_JC4880_APP_SETTINGS)
    jc4880_append_app_sources(APPS_SRCS ${APPS_DIR}/setting)
endif()

if(CONFIG_JC4880_APP_CALCULATOR)
    jc4880_append_app_sources(APPS_SRCS ${APPS_DIR}/calculator)
endif()

if(CONFIG_JC4880_APP_SEGA_EMULATOR)
    jc4880_append_app_sources(APPS_SRCS ${APPS_DIR}/sega_emulator)
endif()

if(CONFIG_JC4880_APP_IMAGE_VIEWER)
    jc4880_append_app_sources(APPS_SRCS ${APPS_DIR}/image_display)
endif()

if(CONFIG_JC4880_APP_FILE_MANAGER)
    jc4880_append_app_sources(APPS_SRCS ${APPS_DIR}/file_manager)
endif()

if(CONFIG_JC4880_APP_MUSIC_PLAYER)
    jc4880_append_app_sources(APPS_SRCS ${APPS_DIR}/music_player)
endif()

if(CONFIG_JC4880_APP_INTERNET_RADIO)
    jc4880_append_app_sources(APPS_SRCS ${APPS_DIR}/internet_radio)
endif()

idf_component_register(
    SRCS ${APPS_SRCS}
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
    REQUIRES lvgl__lvgl esp_adc esp_event esp_wifi esp_http_client json nvs_flash esp_driver_jpeg esp_mm esp-brookesia bsp_extra esp32_p4_function_ev_board espressif__esp_lcd_touch_gt911 espressif__adc_battery_estimation espressif__avi_player app_update esp_partition esp-tls esp_hosted bt main)

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