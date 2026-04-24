set(PROJ_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(SRCS_C "")
set(SRCS_CPP "")

set(APP_EXAMPLES_PHONE_SRC_DIR ${PROJ_SRC_DIR}/app_examples/phone)
file(GLOB_RECURSE APP_EXAMPLES_PHONE_SRCS_C ${APP_EXAMPLES_PHONE_SRC_DIR}/*.c)
file(GLOB_RECURSE APP_EXAMPLES_PHONE_SRCS_CPP ${APP_EXAMPLES_PHONE_SRC_DIR}/*.cpp)
list(APPEND SRCS_C ${APP_EXAMPLES_PHONE_SRCS_C})
list(APPEND SRCS_CPP ${APP_EXAMPLES_PHONE_SRCS_CPP})

set(ASSETS_SRC_DIR ${PROJ_SRC_DIR}/assets)
file(GLOB_RECURSE ASSETS_SRC_C ${ASSETS_SRC_DIR}/*.c)
list(APPEND SRCS_C ${ASSETS_SRC_C})

set(CORE_SRC_DIR ${PROJ_SRC_DIR}/core)
file(GLOB_RECURSE CORE_SRCS_C ${CORE_SRC_DIR}/*.c)
file(GLOB_RECURSE CORE_SRCS_CPP ${CORE_SRC_DIR}/*.cpp)
list(APPEND SRCS_C ${CORE_SRCS_C})
list(APPEND SRCS_CPP ${CORE_SRCS_CPP})

set(PHONE_SRC_DIR ${PROJ_SRC_DIR}/systems/phone)
file(GLOB_RECURSE PHONE_SRCS_C ${PHONE_SRC_DIR}/*.c)
file(GLOB_RECURSE PHONE_SRCS_CPP ${PHONE_SRC_DIR}/*.cpp)
list(APPEND SRCS_C ${PHONE_SRCS_C})
list(APPEND SRCS_CPP ${PHONE_SRCS_CPP})

set(ICON_SRC_DIR ${CMAKE_CURRENT_LIST_DIR}/../icons)
set(GENERATED_ICON_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated_icons)
set(ICON_GENERATOR ${CMAKE_CURRENT_LIST_DIR}/../../tools/png_to_lvgl_c.py)
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    idf_build_get_property(python PYTHON)
    file(MAKE_DIRECTORY ${GENERATED_ICON_DIR})

    add_custom_command(
        OUTPUT ${GENERATED_ICON_DIR}/airplane_png.c
        COMMAND ${python} ${ICON_GENERATOR}
                --input ${ICON_SRC_DIR}/airplane.png
                --output ${GENERATED_ICON_DIR}/airplane_png.c
                --symbol airplane_png
        DEPENDS ${ICON_SRC_DIR}/airplane.png ${ICON_GENERATOR}
        VERBATIM)

    add_custom_command(
        OUTPUT ${GENERATED_ICON_DIR}/sleep_png.c
        COMMAND ${python} ${ICON_GENERATOR}
                --input ${ICON_SRC_DIR}/sleep.png
                --output ${GENERATED_ICON_DIR}/sleep_png.c
                --symbol sleep_png
        DEPENDS ${ICON_SRC_DIR}/sleep.png ${ICON_GENERATOR}
        VERBATIM)

    add_custom_command(
        OUTPUT ${GENERATED_ICON_DIR}/unmuted_png.c
        COMMAND ${python} ${ICON_GENERATOR}
                --input ${ICON_SRC_DIR}/unmuted.png
                --output ${GENERATED_ICON_DIR}/unmuted_png.c
                --symbol unmuted_png
        DEPENDS ${ICON_SRC_DIR}/unmuted.png ${ICON_GENERATOR}
        VERBATIM)

    add_custom_command(
        OUTPUT ${GENERATED_ICON_DIR}/muted_png.c
        COMMAND ${python} ${ICON_GENERATOR}
                --input ${ICON_SRC_DIR}/muted.png
                --output ${GENERATED_ICON_DIR}/muted_png.c
                --symbol muted_png
        DEPENDS ${ICON_SRC_DIR}/muted.png ${ICON_GENERATOR}
        VERBATIM)

    add_custom_command(
        OUTPUT ${GENERATED_ICON_DIR}/mutedall_png.c
        COMMAND ${python} ${ICON_GENERATOR}
                --input ${ICON_SRC_DIR}/mutedall.png
                --output ${GENERATED_ICON_DIR}/mutedall_png.c
                --symbol mutedall_png
        DEPENDS ${ICON_SRC_DIR}/mutedall.png ${ICON_GENERATOR}
        VERBATIM)

    list(APPEND SRCS_C
        ${GENERATED_ICON_DIR}/airplane_png.c
        ${GENERATED_ICON_DIR}/sleep_png.c
        ${GENERATED_ICON_DIR}/unmuted_png.c
        ${GENERATED_ICON_DIR}/muted_png.c
        ${GENERATED_ICON_DIR}/mutedall_png.c)
endif()

set(WIDGETS_SRC_DIR ${PROJ_SRC_DIR}/widgets)
file(GLOB_RECURSE WIDGETS_SRCS_C ${WIDGETS_SRC_DIR}/*.c)
file(GLOB_RECURSE WIDGETS_SRCS_CPP ${WIDGETS_SRC_DIR}/*.cpp)
list(APPEND SRCS_C ${WIDGETS_SRCS_C})
list(APPEND SRCS_CPP ${WIDGETS_SRCS_CPP})

set(SQUARELINE_SRC_DIR ${PROJ_SRC_DIR}/squareline)
file(GLOB_RECURSE SQUARELINE_SRCS_C ${SQUARELINE_SRC_DIR}/*.c)
list(APPEND SRCS_C ${SQUARELINE_SRCS_C})

idf_component_register(
    SRCS ${SRCS_C} ${SRCS_CPP}
    INCLUDE_DIRS ${PROJ_SRC_DIR}
    REQUIRES esp32_p4_function_ev_board esp_hosted esp_wifi nvs_flash)

set_source_files_properties(
    ${SRCS_CPP}
    PROPERTIES
        COMPILE_FLAGS "-Wno-missing-field-initializers"
)

set_source_files_properties(
    ${APP_EXAMPLES_PHONE_SRCS_C}
    PROPERTIES
        COMPILE_FLAGS "-Wno-unused-variable"
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE -DLV_LVGL_H_INCLUDE_SIMPLE)
