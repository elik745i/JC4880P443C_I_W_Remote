set(APPS_DIR ${CMAKE_CURRENT_LIST_DIR})
set(APPS_SRCS ${APPS_DIR}/system_ui_service.cpp)
list(APPEND APPS_SRCS ${APPS_DIR}/lvgl_input_helper.c)
list(APPEND APPS_SRCS ${APPS_DIR}/joypad_runtime.cpp)
list(APPEND APPS_SRCS ${APPS_DIR}/joypad_transport.cpp)
list(APPEND APPS_SRCS ${APPS_DIR}/neopixel_runtime.cpp)

list(APPEND APPS_SRCS ${APPS_DIR}/hardware_history_service.cpp)

if(CONFIG_JC4880_FEATURE_BATTERY)
    list(APPEND APPS_SRCS ${APPS_DIR}/battery_history_service.cpp)
endif()

function(jc4880_append_app_sources out_var dir_path)
    file(GLOB_RECURSE APP_C_SRCS CONFIGURE_DEPENDS ${dir_path}/*.c)
    file(GLOB_RECURSE APP_CPP_SRCS CONFIGURE_DEPENDS ${dir_path}/*.cpp)
    set(${out_var} ${${out_var}} ${APP_C_SRCS} ${APP_CPP_SRCS} PARENT_SCOPE)
endfunction()

if(CONFIG_JC4880_APP_SETTINGS)
    jc4880_append_app_sources(APPS_SRCS ${APPS_DIR}/setting)

    if(NOT CONFIG_JC4880_FEATURE_LEGACY_BLE_MENU)
        list(REMOVE_ITEM APPS_SRCS
             ${APPS_DIR}/setting/ui/screens/ui_ScreenSettingBLE.c
             ${APPS_DIR}/setting/ui/images/ui_img_bluetooth_png.c)
    endif()
endif()

if(CONFIG_JC4880_APP_CALCULATOR)
    jc4880_append_app_sources(APPS_SRCS ${APPS_DIR}/calculator)
endif()

if(CONFIG_JC4880_APP_SEGA_EMULATOR)
    jc4880_append_app_sources(APPS_SRCS ${APPS_DIR}/sega_emulator)
endif()

if(CONFIG_JC4880_APP_IMAGE_VIEWER)
    jc4880_append_app_sources(APPS_SRCS ${APPS_DIR}/image_display)
    list(APPEND APPS_SRCS ${PROJECT_DIR}/managed_components/lvgl__lvgl/src/extra/libs/sjpg/tjpgd.c)
endif()

if(CONFIG_JC4880_APP_FILE_MANAGER)
    jc4880_append_app_sources(APPS_SRCS ${APPS_DIR}/file_manager)
endif()

if(CONFIG_JC4880_APP_WEB_SERVER)
    jc4880_append_app_sources(APPS_SRCS ${APPS_DIR}/web_server)
endif()

if(CONFIG_JC4880_APP_MUSIC_PLAYER)
    jc4880_append_app_sources(APPS_SRCS ${APPS_DIR}/music_player)
endif()

if(CONFIG_JC4880_APP_RECORDER)
    jc4880_append_app_sources(APPS_SRCS ${APPS_DIR}/recorder)
endif()

if(CONFIG_JC4880_APP_RS485)
    jc4880_append_app_sources(APPS_SRCS ${APPS_DIR}/rs485)
endif()

if(CONFIG_JC4880_APP_INTERNET_RADIO)
    jc4880_append_app_sources(APPS_SRCS ${APPS_DIR}/internet_radio)
endif()

if(CONFIG_JC4880_APP_EREADER)
    jc4880_append_app_sources(APPS_SRCS ${APPS_DIR}/e_reader)
endif()

if(CONFIG_JC4880_APP_MQTT)
    jc4880_append_app_sources(APPS_SRCS ${APPS_DIR}/mqtt_app)
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
    REQUIRES lvgl__lvgl esp_adc esp_event esp_wifi esp_http_client esp_http_server json nvs_flash esp_driver_jpeg esp_mm esp-brookesia bsp_extra esp32_p4_function_ev_board espressif__esp_lcd_touch_gt911 espressif__adc_battery_estimation espressif__avi_player espressif__libjpeg-turbo espressif__esp_audio_codec app_update esp_partition esp-tls esp_hosted bt mdns mqtt driver led_strip main)

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

if(CONFIG_JC4880_APP_IMAGE_VIEWER)
    target_compile_definitions(
        ${COMPONENT_LIB}
        PRIVATE
        CONFIG_LV_USE_SJPG=1
        LV_USE_SJPG=1
    )
endif()