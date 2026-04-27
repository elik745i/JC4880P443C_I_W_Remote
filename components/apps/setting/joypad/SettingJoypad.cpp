#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <inttypes.h>
#include <string>

#include "../Setting.hpp"
#include "./SettingJoypadLayout.hpp"
#include "../wifi/SettingWifiPrivate.hpp"
#include "../ui/ui.h"

namespace {

constexpr int8_t kDefaultResistiveAxisPins[2] = {50, 51};
constexpr int8_t kDefaultMcpI2cPins[2] = {30, 31};
constexpr int8_t kDefaultMcpControlPins[JC4880_JOYPAD_SPI_CONTROL_COUNT] = {
    -1,
    -1,
    -1,
    -1,
    12,
    11,
    15,
    14,
    10,
    9,
    8,
    12,
};

constexpr uint32_t kSettingScreenAnimTimeMs = 220;

constexpr int32_t kJoypadManualModeOptions[] = {
    JC4880_JOYPAD_MANUAL_MODE_SPI,
    JC4880_JOYPAD_MANUAL_MODE_RESISTIVE,
    JC4880_JOYPAD_MANUAL_MODE_MCP23017,
};

constexpr int32_t kJoypadMapOptions[] = {
    JC4880_JOYPAD_MAP_NONE,
    JC4880_JOYPAD_MAP_UP,
    JC4880_JOYPAD_MAP_DOWN,
    JC4880_JOYPAD_MAP_LEFT,
    JC4880_JOYPAD_MAP_RIGHT,
    JC4880_JOYPAD_MAP_BUTTON_A,
    JC4880_JOYPAD_MAP_BUTTON_B,
    JC4880_JOYPAD_MAP_BUTTON_C,
    JC4880_JOYPAD_MAP_START,
    JC4880_JOYPAD_MAP_EXIT,
    JC4880_JOYPAD_MAP_SAVE,
    JC4880_JOYPAD_MAP_LOAD,
};

constexpr int32_t kJoypadGpioOptions[] = {
    -1,
    28,
    29,
    30,
    31,
    32,
    33,
    34,
    35,
    49,
    50,
    51,
    52,
};

constexpr int32_t kJoypadAnalogGpioOptions[] = {
    -1,
    50,
    51,
};

constexpr int32_t kJoypadMcpPinOptions[] = {
    -1,
    0,
    1,
    2,
    3,
    4,
    5,
    6,
    7,
    8,
    9,
    10,
    11,
    12,
    13,
    14,
    15,
};

constexpr int32_t kJoypadResistiveBindingOptions[] = {
    -1,
    0,
    1,
    2,
    3,
    4,
    5,
    6,
    7,
    8,
    9,
    10,
    11,
    12,
    13,
    14,
    15,
};

constexpr const char *kJoypadManualModeOptionsText = "Direct GPIO (Legacy)\nResistive Keyboard\nMCP23017";
constexpr const char *kJoypadMapOptionsText = "None\nUp\nDown\nLeft\nRight\nA\nB\nC\nStart\nExit\nSave\nLoad";
constexpr const char *kJoypadGpioOptionsText = "Disabled\nGPIO 28\nGPIO 29\nGPIO 30\nGPIO 31\nGPIO 32\nGPIO 33\nGPIO 34\nGPIO 35\nGPIO 49\nGPIO 50\nGPIO 51\nGPIO 52";
constexpr const char *kJoypadAnalogGpioOptionsText = "Disabled\nGPIO 50\nGPIO 51";
constexpr const char *kJoypadMcpPinOptionsText = "Disabled\nA0\nA1\nA2\nA3\nA4\nA5\nA6\nA7\nB0\nB1\nB2\nB3\nB4\nB5\nB6\nB7";
constexpr const char *kJoypadResistiveBindingOptionsText = "Disabled\nL1 330 ohm\nL1 680 ohm\nL1 1 kohm\nL1 2.2 kohm\nL1 3.3 kohm\nL1 4.7 kohm\nL1 6.8 kohm\nL1 10 kohm\nL2 330 ohm\nL2 680 ohm\nL2 1 kohm\nL2 2.2 kohm\nL2 3.3 kohm\nL2 4.7 kohm\nL2 6.8 kohm\nL2 10 kohm";

constexpr const char *kJoypadBleRemapLabels[JC4880_JOYPAD_BLE_CONTROL_COUNT] = {
    "D-pad Up",
    "D-pad Down",
    "D-pad Left",
    "D-pad Right",
    "A Button",
    "B Button",
    "C Button",
    "Start Button",
    "Y Button",
    "Left Shoulder",
    "Right Shoulder",
    "Left Trigger",
    "Right Trigger",
    "Select Button",
    "System Button",
    "Capture / Turbo",
    "Left Stick Click",
    "Right Stick Click",
    "Left Stick Up",
    "Left Stick Down",
    "Left Stick Left",
    "Left Stick Right",
    "Right Stick Up",
    "Right Stick Down",
    "Right Stick Left",
    "Right Stick Right",
};

constexpr const char *kJoypadSpiLabels[JC4880_JOYPAD_SPI_CONTROL_COUNT] = {
    "SPI D-pad Up",
    "SPI D-pad Down",
    "SPI D-pad Left",
    "SPI D-pad Right",
    "Start Button",
    "Exit Button",
    "Save Button",
    "Load Button",
    "A Button",
    "B Button",
    "C Button",
    "Key Button",
};

constexpr const char *kJoypadResistiveLabels[2] = {
    "Resistive Ladder 1 GPIO",
    "Resistive Ladder 2 GPIO",
};

constexpr const char *kJoypadAxisLabels[2] = {
    "Y Axis GPIO",
    "X Axis GPIO",
};

constexpr const char *kJoypadMcpLabels[2] = {
    "MCP SDA GPIO",
    "MCP SCL GPIO",
};

constexpr const char *kJoypadMcpControlLabels[JC4880_JOYPAD_SPI_CONTROL_COUNT] = {
    "D-pad Up",
    "D-pad Down",
    "D-pad Left",
    "D-pad Right",
    "Start Button",
    "Exit Button",
    "Save Button",
    "Load Button",
    "A Button",
    "B Button",
    "C Button",
    "Key Button",
};

constexpr int32_t kJoypadPeripheralGpioOptions[] = {
    -1,
    28,
    29,
    30,
    31,
    32,
    33,
    34,
    35,
    49,
    50,
    51,
    52,
};

constexpr int32_t kJoypadHapticLevelOptions[] = {
    0,
    1,
    2,
    3,
};

constexpr int32_t kJoypadNeopixelPaletteOptions[] = {
    0, 1, 2, 3, 4, 5,
};

constexpr int32_t kJoypadNeopixelEffectOptions[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
    10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
};

constexpr const char *kJoypadPeripheralGpioOptionsText = "Disabled\nGPIO 28\nGPIO 29\nGPIO 30\nGPIO 31\nGPIO 32\nGPIO 33\nGPIO 34\nGPIO 35\nGPIO 49\nGPIO 50\nGPIO 51\nGPIO 52";
constexpr const char *kJoypadHapticLevelOptionsText = "None\nLow\nMedium\nHigh";
constexpr const char *kJoypadNeopixelPaletteOptionsText = "Rainbow\nOcean\nSunset\nForest\nIce\nArcade";
constexpr const char *kJoypadNeopixelEffectOptionsText =
    "Solid\nBreathing\nColor Wipe\nTheater Chase\nRainbow\nRainbow Cycle\nSparkle\nPulse\nComet\nScanner\nTwinkle\nFire\nLava\nAurora\nPolice\nMeteor\nConfetti\nPlasma\nWave\nParty";

constexpr const char *kNvsKeyAudioHapticGpio = "haptic_gpio";
constexpr const char *kNvsKeyAudioHapticLevel = "haptic_lvl";
constexpr const char *kNvsKeyNeopixelPower = "neo_en";
constexpr const char *kNvsKeyNeopixelGpio = "neo_gpio";
constexpr const char *kNvsKeyNeopixelBrightness = "neo_bri";
constexpr const char *kNvsKeyNeopixelPalette = "neo_pal";
constexpr const char *kNvsKeyNeopixelEffect = "neo_fx";

constexpr const char *kJoypadButtonLabels[JC4880_JOYPAD_BUTTON_CONTROL_COUNT] = {
    "Start Button",
    "Exit Button",
    "Save Button",
    "Load Button",
    "A Button",
    "B Button",
    "C Button",
    "Key Button",
};

lv_color_t parseJoypadColor(const char *hex, lv_color_t fallback)
{
    if ((hex == nullptr) || (*hex == '\0')) {
        return fallback;
    }

    const char *value = (*hex == '#') ? (hex + 1) : hex;
    char *end = nullptr;
    const unsigned long parsed = std::strtoul(value, &end, 16);
    if ((end == value) || (parsed > 0xFFFFFFUL)) {
        return fallback;
    }

    return lv_color_hex(static_cast<uint32_t>(parsed));
}

const lv_font_t *joypadFontForSize(int text_size)
{
    if (text_size >= 20) {
        return &lv_font_montserrat_20;
    }
    if (text_size >= 16) {
        return &lv_font_montserrat_16;
    }

    return &lv_font_montserrat_14;
}

bool joypadShapeMatches(const char *shape_type, const char *expected)
{
    return (shape_type != nullptr) && (std::strcmp(shape_type, expected) == 0);
}

bool joypadFunctionMatches(const char *function_type, const char *expected)
{
    return (function_type != nullptr) && (std::strcmp(function_type, expected) == 0);
}

lv_coord_t scaleJoypadCoordinate(int coordinate, lv_coord_t stage_size, lv_coord_t source_size)
{
    if (source_size <= 0) {
        return 0;
    }

    return static_cast<lv_coord_t>(std::lround((static_cast<double>(coordinate) * static_cast<double>(stage_size)) /
                                               static_cast<double>(source_size)));
}

struct JoypadPreviewDrawState {
    const jc4880::joypad_layout::Visual *visual;
    uint16_t analog_value;
};

uint16_t joypadPresetPoints(const char *shape_type, lv_point_t *points, lv_coord_t width, lv_coord_t height)
{
    const auto scalePoint = [width, height](int x_percent, int y_percent) {
        return lv_point_t{
            static_cast<lv_coord_t>((x_percent * std::max<lv_coord_t>(1, width - 1)) / 100),
            static_cast<lv_coord_t>((y_percent * std::max<lv_coord_t>(1, height - 1)) / 100),
        };
    };

    if (joypadShapeMatches(shape_type, "triangle")) {
        points[0] = scalePoint(50, 6);
        points[1] = scalePoint(92, 84);
        points[2] = scalePoint(8, 84);
        return 3;
    }

    if (joypadShapeMatches(shape_type, "star")) {
        points[0] = scalePoint(50, 3);
        points[1] = scalePoint(61, 34);
        points[2] = scalePoint(95, 35);
        points[3] = scalePoint(68, 55);
        points[4] = scalePoint(79, 89);
        points[5] = scalePoint(50, 69);
        points[6] = scalePoint(21, 89);
        points[7] = scalePoint(32, 55);
        points[8] = scalePoint(5, 35);
        points[9] = scalePoint(39, 34);
        return 10;
    }

    if (joypadShapeMatches(shape_type, "pentagon")) {
        points[0] = scalePoint(50, 4);
        points[1] = scalePoint(92, 35);
        points[2] = scalePoint(76, 88);
        points[3] = scalePoint(24, 88);
        points[4] = scalePoint(8, 35);
        return 5;
    }

    return 0;
}

uint16_t buildJoypadVisualPoints(const jc4880::joypad_layout::Visual &visual, lv_coord_t width, lv_coord_t height, lv_point_t *points)
{
    uint16_t point_count = 0;
    if (joypadShapeMatches(visual.shape.type, "custom") && (visual.shape.point_count > 0)) {
        point_count = static_cast<uint16_t>(std::min(visual.shape.point_count, 16));
        for (uint16_t index = 0; index < point_count; ++index) {
            points[index].x = scaleJoypadCoordinate(visual.shape.points[index].x, std::max<lv_coord_t>(1, width - 1), 100);
            points[index].y = scaleJoypadCoordinate(visual.shape.points[index].y, std::max<lv_coord_t>(1, height - 1), 100);
        }
    } else {
        point_count = joypadPresetPoints(visual.shape.type, points, width, height);
    }

    if ((point_count == 0) || (visual.shape.rotation_degrees == 0)) {
        return point_count;
    }

    const double radians = (static_cast<double>(visual.shape.rotation_degrees) * M_PI) / 180.0;
    const double sin_theta = std::sin(radians);
    const double cos_theta = std::cos(radians);
    const double pivot_x = (static_cast<double>(width) - 1.0) * 0.5;
    const double pivot_y = (static_cast<double>(height) - 1.0) * 0.5;
    for (uint16_t index = 0; index < point_count; ++index) {
        const double translated_x = static_cast<double>(points[index].x) - pivot_x;
        const double translated_y = static_cast<double>(points[index].y) - pivot_y;
        const double rotated_x = (translated_x * cos_theta) - (translated_y * sin_theta);
        const double rotated_y = (translated_x * sin_theta) + (translated_y * cos_theta);
        points[index].x = static_cast<lv_coord_t>(std::lround(pivot_x + rotated_x));
        points[index].y = static_cast<lv_coord_t>(std::lround(pivot_y + rotated_y));
    }

    return point_count;
}

uint16_t clipJoypadPolygonToMaxX(const lv_point_t *input, uint16_t input_count, lv_coord_t max_x, lv_point_t *output, uint16_t output_capacity)
{
    if ((input == nullptr) || (output == nullptr) || (input_count < 3) || (output_capacity == 0)) {
        return 0;
    }

    const auto inside = [max_x](const lv_point_t &point) {
        return point.x <= max_x;
    };
    const auto intersection = [max_x](const lv_point_t &start, const lv_point_t &end) {
        if (start.x == end.x) {
            return lv_point_t{max_x, end.y};
        }

        const double ratio = static_cast<double>(max_x - start.x) / static_cast<double>(end.x - start.x);
        return lv_point_t{
            max_x,
            static_cast<lv_coord_t>(std::lround(static_cast<double>(start.y) +
                                                (static_cast<double>(end.y - start.y) * ratio))),
        };
    };

    uint16_t output_count = 0;
    lv_point_t previous = input[input_count - 1];
    bool previous_inside = inside(previous);
    for (uint16_t index = 0; index < input_count; ++index) {
        const lv_point_t current = input[index];
        const bool current_inside = inside(current);
        if (current_inside) {
            if (!previous_inside && (output_count < output_capacity)) {
                output[output_count++] = intersection(previous, current);
            }
            if (output_count < output_capacity) {
                output[output_count++] = current;
            }
        } else if (previous_inside && (output_count < output_capacity)) {
            output[output_count++] = intersection(previous, current);
        }
        previous = current;
        previous_inside = current_inside;
    }

    return output_count;
}

void applyRequestedLocalDefaults(jc4880_joypad_config_t &config)
{
    if (config.manual_mode == JC4880_JOYPAD_MANUAL_MODE_RESISTIVE) {
        for (size_t index = 0; index < 2; ++index) {
            if (config.manual_resistive_gpio[index] < 0) {
                config.manual_resistive_gpio[index] = kDefaultResistiveAxisPins[index];
            }
        }
    }

    if (config.manual_mode != JC4880_JOYPAD_MANUAL_MODE_MCP23017) {
        return;
    }

    for (size_t index = 0; index < 2; ++index) {
        if (config.manual_mcp_i2c_gpio[index] < 0) {
            config.manual_mcp_i2c_gpio[index] = kDefaultMcpI2cPins[index];
        }
    }

    for (size_t index = 0; index < JC4880_JOYPAD_SPI_CONTROL_COUNT; ++index) {
        if ((config.manual_mcp_button_pin[index] < 0) && (kDefaultMcpControlPins[index] >= 0)) {
            config.manual_mcp_button_pin[index] = kDefaultMcpControlPins[index];
        }
    }
}

void translateJoypadPointsToObject(const lv_area_t &coords, lv_point_t *points, uint16_t point_count)
{
    for (uint16_t index = 0; index < point_count; ++index) {
        points[index].x = static_cast<lv_coord_t>(coords.x1 + points[index].x);
        points[index].y = static_cast<lv_coord_t>(coords.y1 + points[index].y);
    }
}

void cleanupJoypadPreviewDrawStateEvent(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_DELETE) {
        return;
    }

    auto *state = static_cast<JoypadPreviewDrawState *>(lv_event_get_user_data(e));
    delete state;
}

void updateJoypadPreviewAnalogLabel(lv_obj_t *object, uint16_t value)
{
    if (!lv_obj_ready(object)) {
        return;
    }

    lv_obj_t *shape = lv_obj_get_child(object, 0);
    lv_obj_t *label = lv_obj_get_child(object, 1);
    if ((shape == nullptr) || (label == nullptr)) {
        return;
    }

    const auto *state = static_cast<const JoypadPreviewDrawState *>(lv_obj_get_user_data(shape));
    if ((state == nullptr) || (state->visual == nullptr) || !joypadFunctionMatches(state->visual->function_type, "analog")) {
        return;
    }

    char text[48];
    const unsigned percent = static_cast<unsigned>((static_cast<uint32_t>(std::min<uint16_t>(value, 1024)) * 100U + 512U) / 1024U);
    std::snprintf(text, sizeof(text), "%s\n%u%%", state->visual->label, percent);
    lv_label_set_text(label, text);
}

void centerJoypadPreviewKnob(lv_obj_t *base, lv_obj_t *knob)
{
    if (!lv_obj_ready(base) || !lv_obj_ready(knob)) {
        return;
    }

    const lv_coord_t base_width = lv_obj_get_width(base);
    const lv_coord_t base_height = lv_obj_get_height(base);
    const lv_coord_t knob_width = lv_obj_get_width(knob);
    const lv_coord_t knob_height = lv_obj_get_height(knob);
    const lv_coord_t center_offset_x = knob_width;
    const lv_coord_t center_offset_y = knob_height;
    lv_obj_set_pos(knob,
                   std::max<lv_coord_t>(0, ((base_width - knob_width) / 2) - center_offset_x),
                   std::max<lv_coord_t>(0, ((base_height - knob_height) / 2) - center_offset_y));
}

void updateJoypadPreviewStickKnob(lv_obj_t *base, lv_obj_t *knob, int16_t axis_x, int16_t axis_y)
{
    if (!lv_obj_ready(base) || !lv_obj_ready(knob)) {
        return;
    }

    constexpr int16_t kPreviewDeadzone = 18;
    const auto normalizeAxis = [](int16_t axis_value) {
        return static_cast<int16_t>(std::max<int32_t>(-512, std::min<int32_t>(512, axis_value)));
    };
    const auto applyDeadzone = [](int16_t axis_value) {
        return (std::abs(axis_value) <= kPreviewDeadzone) ? static_cast<int16_t>(0) : axis_value;
    };

    const lv_coord_t base_width = lv_obj_get_width(base);
    const lv_coord_t base_height = lv_obj_get_height(base);
    const lv_coord_t knob_width = lv_obj_get_width(knob);
    const lv_coord_t knob_height = lv_obj_get_height(knob);
    const int16_t normalized_axis_x = applyDeadzone(normalizeAxis(axis_x));
    const int16_t normalized_axis_y = applyDeadzone(normalizeAxis(axis_y));
    const float travel_radius_x = static_cast<float>(base_width - knob_width) * 0.5f;
    const float travel_radius_y = static_cast<float>(base_height - knob_height) * 0.5f;
    const float offset_x = (static_cast<float>(normalized_axis_x) / 512.0f) * travel_radius_x;
    const float offset_y = (static_cast<float>(normalized_axis_y) / 512.0f) * travel_radius_y;

    const float center_x = (static_cast<float>(base_width) - static_cast<float>(knob_width)) * 0.5f;
    const float center_y = (static_cast<float>(base_height) - static_cast<float>(knob_height)) * 0.5f;
    const float center_offset_x = static_cast<float>(knob_width);
    const float center_offset_y = static_cast<float>(knob_height);
    const lv_coord_t knob_x = static_cast<lv_coord_t>(std::lround((center_x + offset_x) - center_offset_x));
    const lv_coord_t knob_y = static_cast<lv_coord_t>(std::lround((center_y + offset_y) - center_offset_y));
    const lv_coord_t min_knob_x = static_cast<lv_coord_t>(-std::lround(center_offset_x));
    const lv_coord_t max_knob_x = static_cast<lv_coord_t>(std::lround(static_cast<float>(base_width - knob_width) - center_offset_x));
    const lv_coord_t min_knob_y = static_cast<lv_coord_t>(-std::lround(center_offset_y));
    const lv_coord_t max_knob_y = static_cast<lv_coord_t>(std::lround(static_cast<float>(base_height - knob_height) - center_offset_y));
    lv_obj_set_pos(knob,
                   std::max<lv_coord_t>(min_knob_x, std::min<lv_coord_t>(max_knob_x, knob_x)),
                   std::max<lv_coord_t>(min_knob_y, std::min<lv_coord_t>(max_knob_y, knob_y)));
}

std::string formatJoypadCalibrationSlotSummary(const jc4880_joypad_ble_calibration_slot_t &slot)
{
    std::string info;
    info += "Saved ranges:\n";
    info += "LX " + std::to_string(slot.axis_min[0]) + " / " + std::to_string(slot.axis_center[0]) + " / " + std::to_string(slot.axis_max[0]) + "\n";
    info += "LY " + std::to_string(slot.axis_min[1]) + " / " + std::to_string(slot.axis_center[1]) + " / " + std::to_string(slot.axis_max[1]) + "\n";
    info += "RX " + std::to_string(slot.axis_min[2]) + " / " + std::to_string(slot.axis_center[2]) + " / " + std::to_string(slot.axis_max[2]) + "\n";
    info += "RY " + std::to_string(slot.axis_min[3]) + " / " + std::to_string(slot.axis_center[3]) + " / " + std::to_string(slot.axis_max[3]) + "\n";
    info += "L2 " + std::to_string(slot.pedal_min[0]) + " / " + std::to_string(slot.pedal_max[0]) + "\n";
    info += "R2 " + std::to_string(slot.pedal_min[1]) + " / " + std::to_string(slot.pedal_max[1]);
    return info;
}

void drawJoypadPolygonEvent(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_DRAW_POST) {
        return;
    }

    const auto *state = static_cast<const JoypadPreviewDrawState *>(lv_event_get_user_data(e));
    if ((state == nullptr) || (state->visual == nullptr)) {
        return;
    }
    const auto *visual = state->visual;

    lv_obj_t *target = lv_event_get_target(e);
    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(e);
    if ((target == nullptr) || (draw_ctx == nullptr)) {
        return;
    }

    lv_draw_rect_dsc_t draw_dsc;
    lv_draw_rect_dsc_init(&draw_dsc);
    const lv_coord_t width = lv_obj_get_width(target);
    const lv_coord_t height = lv_obj_get_height(target);
    lv_point_t points[16] = {};
    const uint16_t point_count = buildJoypadVisualPoints(*visual, width, height, points);

    if (point_count == 0) {
        return;
    }

    const lv_color_t fill_color = parseJoypadColor(visual->fill_color, lv_color_hex(0xE2E8F0));
    const lv_color_t border_color = parseJoypadColor(visual->border_color, lv_color_hex(0x94A3B8));
    const lv_coord_t border_width = static_cast<lv_coord_t>((visual->border_width > 0) ? std::max(1, visual->border_width) : 0);
    const bool analog_shape = joypadFunctionMatches(visual->function_type, "analog");

    if (analog_shape) {
        lv_draw_rect_dsc_t base_dsc;
        lv_draw_rect_dsc_init(&base_dsc);
        base_dsc.bg_opa = LV_OPA_COVER;
        base_dsc.bg_color = lv_color_mix(fill_color, lv_color_white(), 115);
        base_dsc.border_opa = LV_OPA_TRANSP;

        lv_point_t base_points[16] = {};
        std::copy(points, points + point_count, base_points);
        translateJoypadPointsToObject(target->coords, base_points, point_count);
        lv_draw_polygon(draw_ctx, &base_dsc, base_points, point_count);

        const lv_coord_t fill_limit = static_cast<lv_coord_t>((static_cast<int32_t>(std::min<uint16_t>(state->analog_value, 1024)) * width) / 1024);
        if (fill_limit > 0) {
            lv_point_t clipped_points[32] = {};
            const uint16_t clipped_count = clipJoypadPolygonToMaxX(points, point_count,
                                                                   static_cast<lv_coord_t>(fill_limit - 1),
                                                                   clipped_points,
                                                                   static_cast<uint16_t>(std::size(clipped_points)));
            if (clipped_count >= 3) {
                translateJoypadPointsToObject(target->coords, clipped_points, clipped_count);
                lv_draw_rect_dsc_t fill_dsc;
                lv_draw_rect_dsc_init(&fill_dsc);
                fill_dsc.bg_opa = static_cast<lv_opa_t>(LV_OPA_90 + ((LV_OPA_COVER - LV_OPA_90) * std::min<uint16_t>(state->analog_value, 1024)) / 1024);
                fill_dsc.bg_color = fill_color;
                fill_dsc.border_opa = LV_OPA_TRANSP;
                lv_draw_polygon(draw_ctx, &fill_dsc, clipped_points, clipped_count);
            }
        }

        if (border_width > 0) {
            lv_draw_rect_dsc_t border_dsc;
            lv_draw_rect_dsc_init(&border_dsc);
            border_dsc.bg_opa = LV_OPA_TRANSP;
            border_dsc.border_opa = LV_OPA_COVER;
            border_dsc.border_width = border_width;
            border_dsc.border_color = border_color;
            lv_point_t border_points[16] = {};
            std::copy(points, points + point_count, border_points);
            translateJoypadPointsToObject(target->coords, border_points, point_count);
            lv_draw_polygon(draw_ctx, &border_dsc, border_points, point_count);
        }
        return;
    }

    draw_dsc.bg_opa = LV_OPA_COVER;
    draw_dsc.bg_color = fill_color;
    draw_dsc.border_opa = (border_width > 0) ? LV_OPA_COVER : LV_OPA_TRANSP;
    draw_dsc.border_width = border_width;
    draw_dsc.border_color = border_color;

    translateJoypadPointsToObject(target->coords, points, point_count);
    lv_draw_polygon(draw_ctx, &draw_dsc, points, point_count);
}

void applyJoypadVisualShape(lv_obj_t *shape, const jc4880::joypad_layout::Visual &visual)
{
    const lv_color_t fill_color = parseJoypadColor(visual.fill_color, lv_color_hex(0xE2E8F0));
    const lv_color_t border_color = parseJoypadColor(visual.border_color, lv_color_hex(0x94A3B8));
    const bool polygon_shape = joypadShapeMatches(visual.shape.type, "custom") ||
                               joypadShapeMatches(visual.shape.type, "triangle") ||
                               joypadShapeMatches(visual.shape.type, "star") ||
                               joypadShapeMatches(visual.shape.type, "pentagon");

    lv_obj_clear_flag(shape, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(shape, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_all(shape, 0, 0);
    lv_obj_set_style_shadow_width(shape, 0, 0);
    lv_obj_set_style_outline_width(shape, 0, 0);

    if (polygon_shape) {
        lv_obj_set_style_bg_opa(shape, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(shape, 0, 0);
        lv_obj_set_style_transform_angle(shape, 0, 0);
        auto *draw_state = new JoypadPreviewDrawState{
            .visual = &visual,
            .analog_value = static_cast<uint16_t>(std::min(1024, std::max(0, (visual.preview_analog_level * 1024) / 100))),
        };
        lv_obj_set_user_data(shape, draw_state);
        lv_obj_add_event_cb(shape, drawJoypadPolygonEvent, LV_EVENT_DRAW_POST, draw_state);
        lv_obj_add_event_cb(shape, cleanupJoypadPreviewDrawStateEvent, LV_EVENT_DELETE, draw_state);
    } else {
        lv_obj_set_style_bg_color(shape, fill_color, 0);
        lv_obj_set_style_bg_opa(shape, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(shape, border_color, 0);
        lv_obj_set_style_border_width(shape, visual.border_width, 0);

        if (joypadShapeMatches(visual.shape.type, "circle") || joypadShapeMatches(visual.shape.type, "capsule")) {
            lv_obj_set_style_radius(shape, LV_RADIUS_CIRCLE, 0);
        } else {
            lv_obj_set_style_radius(shape, std::max(0, visual.shape.corner_radius), 0);
        }
    }

    if (!polygon_shape && (visual.shape.rotation_degrees != 0)) {
        lv_obj_set_style_transform_angle(shape, static_cast<lv_coord_t>(visual.shape.rotation_degrees * 10), 0);
        lv_obj_set_style_transform_pivot_x(shape, static_cast<lv_coord_t>(lv_obj_get_width(shape) / 2), 0);
        lv_obj_set_style_transform_pivot_y(shape, static_cast<lv_coord_t>(lv_obj_get_height(shape) / 2), 0);
    }
}

lv_obj_t *createJoypadPreviewVisual(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t width, lv_coord_t height,
                                    const jc4880::joypad_layout::Visual &visual)
{
    lv_obj_t *container = lv_obj_create(parent);
    lv_obj_set_size(container, width, height);
    lv_obj_set_pos(container, x, y);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_set_style_shadow_width(container, 0, 0);
    lv_obj_set_style_outline_width(container, 0, 0);

    lv_obj_t *shape = lv_obj_create(container);
    lv_obj_set_size(shape, width, height);
    lv_obj_set_pos(shape, 0, 0);
    applyJoypadVisualShape(shape, visual);

    lv_obj_t *label = lv_label_create(container);
    lv_label_set_text(label, visual.label);
    lv_obj_set_width(label, lv_pct(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(label, joypadFontForSize(visual.text_size), 0);
    lv_obj_set_style_text_color(label, parseJoypadColor(visual.text_color, lv_color_hex(0x0F172A)), 0);
    lv_obj_center(label);
    if (joypadFunctionMatches(visual.function_type, "analog")) {
        updateJoypadPreviewAnalogLabel(container,
                                       static_cast<uint16_t>(std::min(1024, std::max(0, (visual.preview_analog_level * 1024) / 100))));
    }
    return container;
}

void setJoypadPreviewActivity(lv_obj_t *object, bool active, lv_color_t accent_color)
{
    if (!lv_obj_ready(object)) {
        return;
    }

    lv_obj_set_style_outline_width(object, active ? 2 : 0, 0);
    lv_obj_set_style_outline_pad(object, active ? 2 : 0, 0);
    lv_obj_set_style_outline_opa(object, active ? LV_OPA_60 : LV_OPA_TRANSP, 0);
    lv_obj_set_style_outline_color(object, accent_color, 0);
    lv_obj_set_style_shadow_width(object, active ? 18 : 0, 0);
    lv_obj_set_style_shadow_spread(object, active ? 1 : 0, 0);
    lv_obj_set_style_shadow_opa(object, active ? LV_OPA_40 : LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_color(object, accent_color, 0);
}

void setJoypadPreviewAnalogActivity(lv_obj_t *object, uint16_t value, lv_color_t accent_color)
{
    if (!lv_obj_ready(object)) {
        return;
    }

    const uint16_t clamped_value = std::min<uint16_t>(value, 1024);
    const uint8_t outline_opa = static_cast<uint8_t>(LV_OPA_10 + ((LV_OPA_70 - LV_OPA_10) * clamped_value) / 1024);
    const uint8_t shadow_opa = static_cast<uint8_t>((LV_OPA_50 * clamped_value) / 1024);
    const lv_coord_t shadow_width = static_cast<lv_coord_t>(4 + ((18 * clamped_value) / 1024));

    lv_obj_set_style_outline_width(object, 2, 0);
    lv_obj_set_style_outline_pad(object, 2, 0);
    lv_obj_set_style_outline_color(object, accent_color, 0);
    lv_obj_set_style_outline_opa(object, outline_opa, 0);
    lv_obj_set_style_shadow_width(object, shadow_width, 0);
    lv_obj_set_style_shadow_spread(object, 1, 0);
    lv_obj_set_style_shadow_color(object, accent_color, 0);
    lv_obj_set_style_shadow_opa(object, shadow_opa, 0);

    lv_obj_t *shape = lv_obj_get_child(object, 0);
    if (shape != nullptr) {
        auto *state = static_cast<JoypadPreviewDrawState *>(lv_obj_get_user_data(shape));
        if ((state != nullptr) && (state->analog_value != clamped_value)) {
            state->analog_value = clamped_value;
            lv_obj_invalidate(shape);
        }
    }

    updateJoypadPreviewAnalogLabel(object, clamped_value);
}

[[maybe_unused]] void appendJoypadLayoutPreview(lv_obj_t *section, const jc4880::joypad_layout::Layout &layout, const lv_img_dsc_t *controller_asset = &ui_img_controller_png)
{
    lv_obj_t *controllerPad = lv_obj_create(section);
    lv_obj_set_width(controllerPad, lv_pct(100));
    lv_obj_set_height(controllerPad, LV_SIZE_CONTENT);
    lv_obj_clear_flag(controllerPad, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(controllerPad, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(controllerPad, 22, 0);
    lv_obj_set_style_border_width(controllerPad, 0, 0);
    lv_obj_set_style_bg_color(controllerPad, lv_color_hex(0xEFF6FF), 0);
    lv_obj_set_style_bg_opa(controllerPad, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(controllerPad, 12, 0);
    lv_obj_set_style_pad_row(controllerPad, 0, 0);
    lv_obj_set_flex_flow(controllerPad, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(controllerPad, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    const auto scaleX = [&layout](int coordinate) {
        return scaleJoypadCoordinate(coordinate,
                                     static_cast<lv_coord_t>(layout.preview_frame.width),
                                     static_cast<lv_coord_t>(layout.controller_source_width));
    };
    const auto scaleY = [&layout](int coordinate) {
        return scaleJoypadCoordinate(coordinate,
                                     static_cast<lv_coord_t>(layout.preview_frame.height),
                                     static_cast<lv_coord_t>(layout.controller_source_height));
    };

    lv_obj_t *controllerStage = lv_obj_create(controllerPad);
    lv_obj_set_size(controllerStage, static_cast<lv_coord_t>(layout.preview_frame.width), static_cast<lv_coord_t>(layout.preview_frame.height));
    lv_obj_clear_flag(controllerStage, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(controllerStage, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(controllerStage, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(controllerStage, 0, 0);
    lv_obj_set_style_pad_all(controllerStage, 0, 0);

    lv_obj_t *controllerImage = lv_img_create(controllerStage);
    lv_img_set_src(controllerImage, controller_asset);
    lv_img_set_size_mode(controllerImage, LV_IMG_SIZE_MODE_REAL);
    lv_img_set_zoom(controllerImage, static_cast<uint16_t>((256 * layout.preview_frame.width) / layout.controller_source_width));
    lv_obj_center(controllerImage);

    for (size_t index = 0; index < 2; ++index) {
        if (layout.trigger_bar_visuals[index].enabled) {
            createJoypadPreviewVisual(controllerStage,
                                      scaleX(layout.trigger_bars[index].x),
                                      scaleY(layout.trigger_bars[index].y),
                                      scaleX(layout.trigger_bars[index].width),
                                      scaleY(layout.trigger_bars[index].height),
                                      layout.trigger_bar_visuals[index]);
        }
        if (layout.shoulder_visuals[index].enabled) {
            createJoypadPreviewVisual(controllerStage,
                                      scaleX(layout.shoulder_indicators[index].x),
                                      scaleY(layout.shoulder_indicators[index].y),
                                      scaleX(layout.shoulder_indicators[index].width),
                                      scaleY(layout.shoulder_indicators[index].height),
                                      layout.shoulder_visuals[index]);
        }
        if (layout.stick_visuals[index].enabled) {
            createJoypadPreviewVisual(controllerStage,
                                      scaleX(layout.stick_bases[index].x),
                                      scaleY(layout.stick_bases[index].y),
                                      scaleX(layout.stick_bases[index].width),
                                      scaleY(layout.stick_bases[index].height),
                                      layout.stick_visuals[index]);
        }
    }

    for (size_t index = 0; index < 4; ++index) {
        if (layout.dpad_visuals[index].enabled) {
            const lv_coord_t size = scaleX(layout.dpad_buttons[index].size);
            createJoypadPreviewVisual(controllerStage,
                                      static_cast<lv_coord_t>(scaleX(layout.dpad_buttons[index].center_x) - (size / 2)),
                                      static_cast<lv_coord_t>(scaleY(layout.dpad_buttons[index].center_y) - (size / 2)),
                                      size,
                                      size,
                                      layout.dpad_visuals[index]);
        }
        if (layout.face_visuals[index].enabled) {
            const lv_coord_t size = scaleX(layout.face_buttons[index].size);
            createJoypadPreviewVisual(controllerStage,
                                      static_cast<lv_coord_t>(scaleX(layout.face_buttons[index].center_x) - (size / 2)),
                                      static_cast<lv_coord_t>(scaleY(layout.face_buttons[index].center_y) - (size / 2)),
                                      size,
                                      size,
                                      layout.face_visuals[index]);
        }
    }
}

lv_obj_t *createJoypadSettingsToggleRow(lv_obj_t *parent, const char *title)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, lv_pct(100), 72);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(row, 18, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(row, 18, 0);
    lv_obj_set_style_pad_right(row, 18, 0);
    lv_obj_set_style_pad_top(row, 10, 0);
    lv_obj_set_style_pad_bottom(row, 10, 0);

    lv_obj_t *label = lv_label_create(row);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0x111827), 0);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);

    return row;
}

uint16_t findDropdownIndexForValue(const int32_t *values, size_t value_count, int32_t value)
{
    for (size_t index = 0; index < value_count; ++index) {
        if (values[index] == value) {
            return static_cast<uint16_t>(index);
        }
    }

    return 0;
}

int32_t getDropdownValueForIndex(const int32_t *values, size_t value_count, uint16_t index)
{
    if (index >= value_count) {
        return values[0];
    }

    return values[index];
}

std::string describeJoypadMask(uint32_t raw_mask)
{
    std::string pressed;
    const struct {
        uint32_t mask;
        const char *label;
    } mask_names[] = {
        {JC4880_JOYPAD_MASK_UP, "Up"},
        {JC4880_JOYPAD_MASK_DOWN, "Down"},
        {JC4880_JOYPAD_MASK_LEFT, "Left"},
        {JC4880_JOYPAD_MASK_RIGHT, "Right"},
        {JC4880_JOYPAD_MASK_BUTTON_A, "A"},
        {JC4880_JOYPAD_MASK_BUTTON_B, "B"},
        {JC4880_JOYPAD_MASK_BUTTON_C, "C"},
        {JC4880_JOYPAD_MASK_START, "Start"},
        {JC4880_JOYPAD_MASK_BUTTON_Y, "Y"},
        {JC4880_JOYPAD_MASK_SHOULDER_L, "L1"},
        {JC4880_JOYPAD_MASK_SHOULDER_R, "R1"},
        {JC4880_JOYPAD_MASK_TRIGGER_L, "L2"},
        {JC4880_JOYPAD_MASK_TRIGGER_R, "R2"},
        {JC4880_JOYPAD_MASK_SELECT, "Select"},
        {JC4880_JOYPAD_MASK_SYSTEM, "System"},
        {JC4880_JOYPAD_MASK_CAPTURE, "Capture"},
        {JC4880_JOYPAD_MASK_THUMB_L, "L3"},
        {JC4880_JOYPAD_MASK_THUMB_R, "R3"},
        {JC4880_JOYPAD_MASK_STICK_L_UP, "LS Up"},
        {JC4880_JOYPAD_MASK_STICK_L_DOWN, "LS Down"},
        {JC4880_JOYPAD_MASK_STICK_L_LEFT, "LS Left"},
        {JC4880_JOYPAD_MASK_STICK_L_RIGHT, "LS Right"},
        {JC4880_JOYPAD_MASK_STICK_R_UP, "RS Up"},
        {JC4880_JOYPAD_MASK_STICK_R_DOWN, "RS Down"},
        {JC4880_JOYPAD_MASK_STICK_R_LEFT, "RS Left"},
        {JC4880_JOYPAD_MASK_STICK_R_RIGHT, "RS Right"},
    };

    for (const auto &entry : mask_names) {
        if ((raw_mask & entry.mask) == 0) {
            continue;
        }
        if (!pressed.empty()) {
            pressed += ", ";
        }
        pressed += entry.label;
    }

    return pressed.empty() ? std::string("None") : pressed;
}

} // namespace

void AppSettings::ensureJoypadScreen(void)
{
    if ((_joypadScreen != nullptr) && lv_obj_ready(_joypadScreen)) {
        return;
    }

    auto createJoypadHeader = [](lv_obj_t *screen, const char *title, const char *badge_text, lv_color_t badge_color) {
        lv_obj_t *titleLabel = lv_label_create(screen);
        lv_label_set_text(titleLabel, title);
        lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(titleLabel, lv_color_hex(0x0F172A), 0);
        lv_obj_align(titleLabel, LV_ALIGN_TOP_MID, 0, 74);

        lv_obj_t *badge = lv_obj_create(screen);
        lv_obj_set_size(badge, 38, 38);
        lv_obj_align_to(badge, titleLabel, LV_ALIGN_OUT_LEFT_MID, -16, 0);
        lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(badge, 0, 0);
        lv_obj_set_style_bg_color(badge, badge_color, 0);
        lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);

        lv_obj_t *badgeLabel = lv_label_create(badge);
        lv_label_set_text(badgeLabel, badge_text);
        lv_obj_set_style_text_font(badgeLabel, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(badgeLabel, lv_color_hex(0xFFFFFF), 0);
        lv_obj_center(badgeLabel);
    };

    auto createJoypadPanel = [](lv_obj_t *screen, lv_coord_t height) {
        lv_obj_t *panel = lv_obj_create(screen);
        lv_obj_set_size(panel, lv_pct(92), height);
        lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, 132);
        lv_obj_set_style_radius(panel, 20, 0);
        lv_obj_set_style_border_width(panel, 0, 0);
        lv_obj_set_style_bg_color(panel, lv_color_hex(0xF8FAFC), 0);
        lv_obj_set_style_pad_all(panel, 14, 0);
        lv_obj_set_style_pad_row(panel, 12, 0);
        lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_scroll_dir(panel, LV_DIR_VER);
        return panel;
    };

    auto createSection = [](lv_obj_t *parent, const char *title, const char *hint) {
        lv_obj_t *section = lv_obj_create(parent);
        lv_obj_set_width(section, lv_pct(100));
        lv_obj_set_height(section, LV_SIZE_CONTENT);
        lv_obj_clear_flag(section, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(section, 18, 0);
        lv_obj_set_style_border_width(section, 0, 0);
        lv_obj_set_style_bg_color(section, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_pad_all(section, 14, 0);
        lv_obj_set_style_pad_row(section, 10, 0);
        lv_obj_set_flex_flow(section, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(section, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        lv_obj_t *sectionTitle = lv_label_create(section);
        lv_label_set_text(sectionTitle, title);
        lv_obj_set_style_text_font(sectionTitle, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(sectionTitle, lv_color_hex(0x0F172A), 0);

        lv_obj_t *sectionHint = lv_label_create(section);
        lv_obj_set_width(sectionHint, lv_pct(100));
        lv_label_set_long_mode(sectionHint, LV_LABEL_LONG_WRAP);
        lv_label_set_text(sectionHint, hint);
        lv_obj_set_style_text_font(sectionHint, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(sectionHint, lv_color_hex(0x475569), 0);
        return section;
    };

    auto createDropdownRow = [this](lv_obj_t *parent, const char *title, const char *options, lv_event_cb_t callback, lv_obj_t **out_dropdown) {
        lv_obj_t *row = createJoypadSettingsToggleRow(parent, title);
        lv_obj_t *dropdown = lv_dropdown_create(row);
        lv_dropdown_set_options_static(dropdown, options);
        lv_obj_set_width(dropdown, 180);
        lv_obj_align(dropdown, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_event_cb(dropdown, callback, LV_EVENT_VALUE_CHANGED, this);
        if (out_dropdown != nullptr) {
            *out_dropdown = dropdown;
        }
    };

    auto createIndicatorButton = [](lv_obj_t *parent, const char *label, lv_coord_t size) {
        lv_obj_t *button = lv_obj_create(parent);
        lv_obj_set_size(button, size, size);
        lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(button, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(button, 2, 0);
        lv_obj_set_style_border_color(button, lv_color_hex(0x94A3B8), 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(0xE2E8F0), 0);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(button, 0, 0);

        lv_obj_t *buttonLabel = lv_label_create(button);
        lv_label_set_text(buttonLabel, label);
        lv_obj_set_style_text_font(buttonLabel, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(buttonLabel, lv_color_hex(0x0F172A), 0);
        lv_obj_center(buttonLabel);
        return button;
    };

    auto createShoulderIndicator = [](lv_obj_t *parent, const char *label) {
        lv_obj_t *indicator = lv_obj_create(parent);
        lv_obj_set_size(indicator, 46, 30);
        lv_obj_clear_flag(indicator, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(indicator, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(indicator, 15, 0);
        lv_obj_set_style_border_width(indicator, 2, 0);
        lv_obj_set_style_border_color(indicator, lv_color_hex(0x94A3B8), 0);
        lv_obj_set_style_bg_color(indicator, lv_color_hex(0xE2E8F0), 0);
        lv_obj_set_style_bg_opa(indicator, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(indicator, 0, 0);

        lv_obj_t *indicatorLabel = lv_label_create(indicator);
        lv_label_set_text(indicatorLabel, label);
        lv_obj_set_style_text_font(indicatorLabel, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(indicatorLabel, lv_color_hex(0x0F172A), 0);
        lv_obj_center(indicatorLabel);
        return indicator;
    };

    auto createStickOverlay = [](lv_obj_t *parent, lv_coord_t width, lv_coord_t height, lv_obj_t **out_base, lv_obj_t **out_knob) {
        lv_obj_t *base = lv_obj_create(parent);
        lv_obj_set_size(base, width, height);
        lv_obj_clear_flag(base, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(base, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(base, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(base, 2, 0);
        lv_obj_set_style_border_color(base, lv_color_hex(0x94A3B8), 0);
        lv_obj_set_style_bg_color(base, lv_color_hex(0xDBEAFE), 0);
        lv_obj_set_style_bg_opa(base, LV_OPA_40, 0);
        lv_obj_set_style_pad_all(base, 0, 0);
        lv_obj_set_style_shadow_width(base, 0, 0);
        lv_obj_set_style_outline_width(base, 0, 0);

        lv_obj_t *crossH = lv_obj_create(base);
    lv_obj_set_size(crossH, static_cast<lv_coord_t>((width * 3) / 5), 2);
        lv_obj_center(crossH);
        lv_obj_clear_flag(crossH, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_border_width(crossH, 0, 0);
        lv_obj_set_style_bg_color(crossH, lv_color_hex(0x60A5FA), 0);

        lv_obj_t *crossV = lv_obj_create(base);
    lv_obj_set_size(crossV, 2, static_cast<lv_coord_t>((height * 3) / 5));
        lv_obj_center(crossV);
        lv_obj_clear_flag(crossV, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_border_width(crossV, 0, 0);
        lv_obj_set_style_bg_color(crossV, lv_color_hex(0x60A5FA), 0);

        lv_obj_t *knob = lv_obj_create(base);
    const lv_coord_t knob_size = static_cast<lv_coord_t>(std::max<int>(18, std::min<int>(width, height) / 3));
        lv_obj_set_size(knob, knob_size, knob_size);
        lv_obj_clear_flag(knob, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(knob, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(knob, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(knob, 2, 0);
        lv_obj_set_style_border_color(knob, lv_color_hex(0x0284C7), 0);
        lv_obj_set_style_bg_color(knob, lv_color_hex(0x38BDF8), 0);
        lv_obj_set_style_bg_opa(knob, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(knob, 0, 0);
        lv_obj_set_style_shadow_width(knob, 0, 0);
        lv_obj_set_style_outline_width(knob, 0, 0);
        lv_obj_center(knob);

        if (out_base != nullptr) {
            *out_base = base;
        }
        if (out_knob != nullptr) {
            *out_knob = knob;
        }
        return base;
    };

    auto addRefreshOnLoad = [this](lv_obj_t *screen) {
        lv_obj_add_event_cb(screen, [](lv_event_t *e) {
            AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
            if (app != nullptr) {
                app->refreshJoypadUi();
            }
        }, LV_EVENT_SCREEN_LOADED, this);
    };

    auto createHubItem = [](lv_obj_t *parent, const char *title, const char *subtitle, const char *badge_text, lv_color_t badge_color) {
        lv_obj_t *item = lv_obj_create(parent);
        lv_obj_set_width(item, lv_pct(100));
        lv_obj_set_height(item, 96);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(item, 18, 0);
        lv_obj_set_style_border_width(item, 0, 0);
        lv_obj_set_style_bg_color(item, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(item, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(item, lv_color_hex(0xDCEEFF), LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(item, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_pad_all(item, 0, 0);

        lv_obj_t *badge = lv_obj_create(item);
        lv_obj_set_size(badge, 46, 46);
        lv_obj_align(badge, LV_ALIGN_LEFT_MID, 18, 0);
        lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(badge, 0, 0);
        lv_obj_set_style_bg_color(badge, badge_color, 0);
        lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);

        lv_obj_t *badgeLabel = lv_label_create(badge);
        lv_label_set_text(badgeLabel, badge_text);
        lv_obj_set_style_text_font(badgeLabel, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(badgeLabel, lv_color_hex(0xFFFFFF), 0);
        lv_obj_center(badgeLabel);

        lv_obj_t *titleLabel = lv_label_create(item);
        lv_label_set_text(titleLabel, title);
        lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_22, 0);
        lv_obj_set_style_text_color(titleLabel, lv_color_hex(0x0F172A), 0);
        lv_obj_align_to(titleLabel, badge, LV_ALIGN_OUT_RIGHT_TOP, 18, -2);

        lv_obj_t *subtitleLabel = lv_label_create(item);
        lv_obj_set_width(subtitleLabel, lv_pct(58));
        lv_label_set_long_mode(subtitleLabel, LV_LABEL_LONG_WRAP);
        lv_label_set_text(subtitleLabel, subtitle);
        lv_obj_set_style_text_font(subtitleLabel, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(subtitleLabel, lv_color_hex(0x475569), 0);
        lv_obj_align_to(subtitleLabel, titleLabel, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);

        lv_obj_t *arrow = lv_img_create(item);
        lv_img_set_src(arrow, &ui_img_arrow_png);
        lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -22, 0);
        return item;
    };

    if ((_joypadScreen == nullptr) || !lv_obj_ready(_joypadScreen)) {
        _joypadScreen = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(_joypadScreen, lv_color_hex(0xE5F3FF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(_joypadScreen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_clear_flag(_joypadScreen, LV_OBJ_FLAG_SCROLLABLE);

        createJoypadHeader(_joypadScreen, "Joypad", "JP", lv_color_hex(0x0F766E));

        lv_obj_t *joypadPanel = createJoypadPanel(_joypadScreen, 320);
        lv_obj_t *introLabel = lv_label_create(joypadPanel);
        lv_obj_set_width(introLabel, lv_pct(100));
        lv_label_set_long_mode(introLabel, LV_LABEL_LONG_WRAP);
        lv_label_set_text(introLabel, "Choose which joypad path you want to configure. Swipe back like the other settings pages.");
        lv_obj_set_style_text_font(introLabel, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(introLabel, lv_color_hex(0x475569), 0);

        _joypadBleMenuItem = createHubItem(joypadPanel,
                                           "BLE Controller",
                                           "Pair through the ESP32-C6 radio, store one controller, and inspect live button activity.",
                                           "BT",
                                           lv_color_hex(0x2563EB));
        _joypadLocalMenuItem = createHubItem(joypadPanel,
                                             "Local Controller",
                                             "Configure manual GPIO-backed controls for SPI or resistive local input.",
                                             "LC",
                                             lv_color_hex(0x0F766E));

        auto onJoypadHubItemClicked = [](lv_event_t *e) {
            AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
            if ((app == nullptr) || (lv_event_get_code(e) != LV_EVENT_CLICKED)) {
                return;
            }

            lv_obj_t *target = lv_event_get_target(e);
            if (target == app->_joypadBleMenuItem) {
                app->ensureJoypadBleScreen();
                app->refreshJoypadUi();
            } else if (target == app->_joypadLocalMenuItem) {
                app->ensureJoypadLocalScreen();
                app->refreshJoypadUi();
            }

            if ((target == app->_joypadBleMenuItem) && lv_obj_ready(app->_joypadBleScreen)) {
                lv_scr_load_anim(app->_joypadBleScreen, LV_SCR_LOAD_ANIM_MOVE_LEFT, kSettingScreenAnimTimeMs, 0, false);
            } else if ((target == app->_joypadLocalMenuItem) && lv_obj_ready(app->_joypadLocalScreen)) {
                lv_scr_load_anim(app->_joypadLocalScreen, LV_SCR_LOAD_ANIM_MOVE_LEFT, kSettingScreenAnimTimeMs, 0, false);
            }
        };
        lv_obj_add_event_cb(_joypadBleMenuItem, onJoypadHubItemClicked, LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(_joypadLocalMenuItem, onJoypadHubItemClicked, LV_EVENT_CLICKED, this);

        _screen_list[UI_JOYPAD_SETTING_INDEX] = _joypadScreen;
        lv_obj_add_event_cb(_joypadScreen, onScreenLoadEventCallback, LV_EVENT_SCREEN_LOADED, this);
    }
}

void AppSettings::ensureJoypadBleScreen(void)
{
    if ((_joypadBleScreen != nullptr) && lv_obj_ready(_joypadBleScreen)) {
        return;
    }

    auto createJoypadHeader = [](lv_obj_t *screen, const char *title, const char *badge_text, lv_color_t badge_color) {
        lv_obj_t *titleLabel = lv_label_create(screen);
        lv_label_set_text(titleLabel, title);
        lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(titleLabel, lv_color_hex(0x0F172A), 0);
        lv_obj_align(titleLabel, LV_ALIGN_TOP_MID, 0, 74);

        lv_obj_t *badge = lv_obj_create(screen);
        lv_obj_set_size(badge, 38, 38);
        lv_obj_align_to(badge, titleLabel, LV_ALIGN_OUT_LEFT_MID, -16, 0);
        lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(badge, 0, 0);
        lv_obj_set_style_bg_color(badge, badge_color, 0);
        lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);

        lv_obj_t *badgeLabel = lv_label_create(badge);
        lv_label_set_text(badgeLabel, badge_text);
        lv_obj_set_style_text_font(badgeLabel, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(badgeLabel, lv_color_hex(0xFFFFFF), 0);
        lv_obj_center(badgeLabel);
    };

    auto createJoypadPanel = [](lv_obj_t *screen, lv_coord_t height) {
        lv_obj_t *panel = lv_obj_create(screen);
        lv_obj_set_size(panel, lv_pct(92), height);
        lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, 132);
        lv_obj_set_style_radius(panel, 20, 0);
        lv_obj_set_style_border_width(panel, 0, 0);
        lv_obj_set_style_bg_color(panel, lv_color_hex(0xF8FAFC), 0);
        lv_obj_set_style_pad_all(panel, 14, 0);
        lv_obj_set_style_pad_row(panel, 12, 0);
        lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_scroll_dir(panel, LV_DIR_VER);
        return panel;
    };

    auto createSection = [](lv_obj_t *parent, const char *title, const char *hint) {
        lv_obj_t *section = lv_obj_create(parent);
        lv_obj_set_width(section, lv_pct(100));
        lv_obj_set_height(section, LV_SIZE_CONTENT);
        lv_obj_clear_flag(section, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(section, 18, 0);
        lv_obj_set_style_border_width(section, 0, 0);
        lv_obj_set_style_bg_color(section, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_pad_all(section, 14, 0);
        lv_obj_set_style_pad_row(section, 10, 0);
        lv_obj_set_flex_flow(section, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(section, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        lv_obj_t *sectionTitle = lv_label_create(section);
        lv_label_set_text(sectionTitle, title);
        lv_obj_set_style_text_font(sectionTitle, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(sectionTitle, lv_color_hex(0x0F172A), 0);

        lv_obj_t *sectionHint = lv_label_create(section);
        lv_obj_set_width(sectionHint, lv_pct(100));
        lv_label_set_long_mode(sectionHint, LV_LABEL_LONG_WRAP);
        lv_label_set_text(sectionHint, hint);
        lv_obj_set_style_text_font(sectionHint, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(sectionHint, lv_color_hex(0x475569), 0);
        return section;
    };

    auto createJoypadSettingsToggleRow = [](lv_obj_t *parent, const char *title) {
        lv_obj_t *row = lv_obj_create(parent);
        lv_obj_set_size(row, lv_pct(100), 72);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(row, 18, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_left(row, 18, 0);
        lv_obj_set_style_pad_right(row, 18, 0);
        lv_obj_set_style_pad_top(row, 10, 0);
        lv_obj_set_style_pad_bottom(row, 10, 0);

        lv_obj_t *label = lv_label_create(row);
        lv_label_set_text(label, title);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0x111827), 0);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);

        return row;
    };

    auto createDropdownRow = [this, &createJoypadSettingsToggleRow](lv_obj_t *parent, const char *title, const char *options, lv_event_cb_t callback, lv_obj_t **out_dropdown) {
        lv_obj_t *row = createJoypadSettingsToggleRow(parent, title);
        lv_obj_t *dropdown = lv_dropdown_create(row);
        lv_dropdown_set_options_static(dropdown, options);
        lv_obj_set_width(dropdown, 180);
        lv_obj_align(dropdown, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_event_cb(dropdown, callback, LV_EVENT_VALUE_CHANGED, this);
        if (out_dropdown != nullptr) {
            *out_dropdown = dropdown;
        }
    };

    auto createSliderRow = [this, &createJoypadSettingsToggleRow](lv_obj_t *parent, const char *title, int32_t min_value, int32_t max_value, lv_event_cb_t callback, lv_obj_t **out_slider) {
        lv_obj_t *row = createJoypadSettingsToggleRow(parent, title);
        lv_obj_t *slider = lv_slider_create(row);
        lv_slider_set_range(slider, min_value, max_value);
        lv_obj_set_width(slider, 210);
        lv_obj_align(slider, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_event_cb(slider, callback, LV_EVENT_VALUE_CHANGED, this);
        if (out_slider != nullptr) {
            *out_slider = slider;
        }
    };

    auto createIndicatorButton = [](lv_obj_t *parent, const char *label, lv_coord_t size) {
        lv_obj_t *button = lv_obj_create(parent);
        lv_obj_set_size(button, size, size);
        lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(button, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(button, 2, 0);
        lv_obj_set_style_border_color(button, lv_color_hex(0x94A3B8), 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(0xE2E8F0), 0);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(button, 0, 0);

        lv_obj_t *buttonLabel = lv_label_create(button);
        lv_label_set_text(buttonLabel, label);
        lv_obj_set_style_text_font(buttonLabel, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(buttonLabel, lv_color_hex(0x0F172A), 0);
        lv_obj_center(buttonLabel);
        return button;
    };

    auto createShoulderIndicator = [](lv_obj_t *parent, const char *label) {
        lv_obj_t *indicator = lv_obj_create(parent);
        lv_obj_set_size(indicator, 46, 30);
        lv_obj_clear_flag(indicator, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(indicator, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(indicator, 15, 0);
        lv_obj_set_style_border_width(indicator, 2, 0);
        lv_obj_set_style_border_color(indicator, lv_color_hex(0x94A3B8), 0);
        lv_obj_set_style_bg_color(indicator, lv_color_hex(0xE2E8F0), 0);
        lv_obj_set_style_bg_opa(indicator, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(indicator, 0, 0);

        lv_obj_t *indicatorLabel = lv_label_create(indicator);
        lv_label_set_text(indicatorLabel, label);
        lv_obj_set_style_text_font(indicatorLabel, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(indicatorLabel, lv_color_hex(0x0F172A), 0);
        lv_obj_center(indicatorLabel);
        return indicator;
    };

    auto createStickOverlay = [](lv_obj_t *parent, lv_coord_t width, lv_coord_t height, lv_obj_t **out_base, lv_obj_t **out_knob) {
        lv_obj_t *base = lv_obj_create(parent);
        lv_obj_set_size(base, width, height);
        lv_obj_clear_flag(base, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(base, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(base, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(base, 2, 0);
        lv_obj_set_style_border_color(base, lv_color_hex(0x94A3B8), 0);
        lv_obj_set_style_bg_color(base, lv_color_hex(0xDBEAFE), 0);
        lv_obj_set_style_bg_opa(base, LV_OPA_40, 0);
        lv_obj_set_style_pad_all(base, 0, 0);
        lv_obj_set_style_shadow_width(base, 0, 0);
        lv_obj_set_style_outline_width(base, 0, 0);

        lv_obj_t *crossH = lv_obj_create(base);
        lv_obj_set_size(crossH, static_cast<lv_coord_t>((width * 3) / 5), 2);
        lv_obj_center(crossH);
        lv_obj_clear_flag(crossH, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_border_width(crossH, 0, 0);
        lv_obj_set_style_bg_color(crossH, lv_color_hex(0x60A5FA), 0);

        lv_obj_t *crossV = lv_obj_create(base);
        lv_obj_set_size(crossV, 2, static_cast<lv_coord_t>((height * 3) / 5));
        lv_obj_center(crossV);
        lv_obj_clear_flag(crossV, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_border_width(crossV, 0, 0);
        lv_obj_set_style_bg_color(crossV, lv_color_hex(0x60A5FA), 0);

        lv_obj_t *knob = lv_obj_create(base);
        const lv_coord_t knob_size = static_cast<lv_coord_t>(std::max<int>(18, std::min<int>(width, height) / 3));
        lv_obj_set_size(knob, knob_size, knob_size);
        lv_obj_clear_flag(knob, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(knob, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(knob, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(knob, 2, 0);
        lv_obj_set_style_border_color(knob, lv_color_hex(0x0284C7), 0);
        lv_obj_set_style_bg_color(knob, lv_color_hex(0x38BDF8), 0);
        lv_obj_set_style_bg_opa(knob, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(knob, 0, 0);
        lv_obj_set_style_shadow_width(knob, 0, 0);
        lv_obj_set_style_outline_width(knob, 0, 0);
        lv_obj_center(knob);

        if (out_base != nullptr) {
            *out_base = base;
        }
        if (out_knob != nullptr) {
            *out_knob = knob;
        }
        return base;
    };

    auto addRefreshOnLoad = [this](lv_obj_t *screen) {
        lv_obj_add_event_cb(screen, [](lv_event_t *e) {
            AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
            if (app != nullptr) {
                app->refreshJoypadUi();
            }
        }, LV_EVENT_SCREEN_LOADED, this);
    };

    {
        _joypadBleScreen = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(_joypadBleScreen, lv_color_hex(0xE5F3FF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(_joypadBleScreen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_clear_flag(_joypadBleScreen, LV_OBJ_FLAG_SCROLLABLE);
        createJoypadHeader(_joypadBleScreen, "BLE Controller", "BT", lv_color_hex(0x2563EB));
        lv_obj_t *joypadPanel = createJoypadPanel(_joypadBleScreen, 1230);

        lv_obj_t *connectionSection = createSection(joypadPanel,
                                                    "Connection",
                                                    "Use the ESP32-C6 radio for controller pairing and reconnects. The status block updates with live button activity.");

        lv_obj_t *bleActiveRow = createJoypadSettingsToggleRow(connectionSection, "Use BLE Controller");
        _joypadBleActiveSwitch = lv_switch_create(bleActiveRow);
        lv_obj_align(_joypadBleActiveSwitch, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_event_cb(_joypadBleActiveSwitch, onJoypadConfigChangedEventCallback, LV_EVENT_VALUE_CHANGED, this);

        lv_obj_t *bleEnableRow = createJoypadSettingsToggleRow(connectionSection, "Enable BLE Joypad");
        _joypadBleEnableSwitch = lv_switch_create(bleEnableRow);
        lv_obj_align(_joypadBleEnableSwitch, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_event_cb(_joypadBleEnableSwitch, onJoypadConfigChangedEventCallback, LV_EVENT_VALUE_CHANGED, this);

        lv_obj_t *bleDiscoveryRow = createJoypadSettingsToggleRow(connectionSection, "Allow Discovery / Pairing");
        _joypadBleDiscoverySwitch = lv_switch_create(bleDiscoveryRow);
        lv_obj_align(_joypadBleDiscoverySwitch, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_event_cb(_joypadBleDiscoverySwitch, onJoypadConfigChangedEventCallback, LV_EVENT_VALUE_CHANGED, this);

        createDropdownRow(connectionSection, "Stored Controller", "No controller selected", onJoypadConfigChangedEventCallback, &_joypadBleDeviceDropdown);

        _joypadBleStatusLabel = lv_label_create(connectionSection);
        lv_obj_set_width(_joypadBleStatusLabel, lv_pct(100));
        lv_label_set_long_mode(_joypadBleStatusLabel, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(_joypadBleStatusLabel, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(_joypadBleStatusLabel, lv_color_hex(0x334155), 0);

        constexpr auto &kLayout = jc4880::joypad_layout::kBleCalibrationLayout;
        lv_obj_t *previewSection = createSection(joypadPanel,
                     "Live Preview",
                     "This BLE controller representation now follows Joypad Layout Configuration and reacts to live buttons, sticks, and triggers from the connected controller.");

        lv_obj_t *controllerPad = lv_obj_create(previewSection);
        lv_obj_set_width(controllerPad, lv_pct(100));
        lv_obj_set_height(controllerPad, LV_SIZE_CONTENT);
        lv_obj_clear_flag(controllerPad, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(controllerPad, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(controllerPad, 22, 0);
        lv_obj_set_style_border_width(controllerPad, 0, 0);
        lv_obj_set_style_bg_color(controllerPad, lv_color_hex(0xEFF6FF), 0);
        lv_obj_set_style_bg_opa(controllerPad, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(controllerPad, 12, 0);
        lv_obj_set_style_pad_row(controllerPad, 0, 0);
        lv_obj_set_flex_flow(controllerPad, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(controllerPad, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        constexpr lv_coord_t kControllerStageWidth = static_cast<lv_coord_t>(kLayout.preview_frame.width);
        constexpr lv_coord_t kControllerStageHeight = static_cast<lv_coord_t>(kLayout.preview_frame.height);
        constexpr lv_coord_t kControllerSourceWidth = static_cast<lv_coord_t>(kLayout.controller_source_width);
        constexpr lv_coord_t kControllerSourceHeight = static_cast<lv_coord_t>(kLayout.controller_source_height);
        auto scaleX = [kControllerStageWidth, kControllerSourceWidth](int coordinate) {
            return scaleJoypadCoordinate(coordinate, kControllerStageWidth, kControllerSourceWidth);
        };
        auto scaleY = [kControllerStageHeight, kControllerSourceHeight](int coordinate) {
            return scaleJoypadCoordinate(coordinate, kControllerStageHeight, kControllerSourceHeight);
        };
        auto positionIndicatorAtSourceCenter = [&scaleX, &scaleY](lv_obj_t *indicator, int source_center_x, int source_center_y) {
            lv_obj_set_pos(indicator,
                           static_cast<lv_coord_t>(scaleX(source_center_x) - (lv_obj_get_width(indicator) / 2)),
                           static_cast<lv_coord_t>(scaleY(source_center_y) - (lv_obj_get_height(indicator) / 2)));
        };

        lv_obj_t *controllerStage = lv_obj_create(controllerPad);
        lv_obj_set_size(controllerStage, kControllerStageWidth, kControllerStageHeight);
        lv_obj_clear_flag(controllerStage, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(controllerStage, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_opa(controllerStage, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(controllerStage, 0, 0);
        lv_obj_set_style_pad_all(controllerStage, 0, 0);

        lv_obj_t *controllerImage = lv_img_create(controllerStage);
        lv_img_set_src(controllerImage, &ui_img_controller_png);
        lv_img_set_size_mode(controllerImage, LV_IMG_SIZE_MODE_REAL);
        lv_img_set_zoom(controllerImage, static_cast<uint16_t>((256 * kControllerStageWidth) / kControllerSourceWidth));
        lv_obj_center(controllerImage);

        for (size_t index = 0; index < 2; ++index) {
            _joypadBleTriggerBars[index] = nullptr;
            if (!kLayout.trigger_bar_visuals[index].enabled) {
                continue;
            }
            _joypadBleTriggerBars[index] = createJoypadPreviewVisual(controllerStage,
                                                                     scaleX(kLayout.trigger_bars[index].x),
                                                                     scaleY(kLayout.trigger_bars[index].y),
                                                                     scaleX(kLayout.trigger_bars[index].width),
                                                                     scaleY(kLayout.trigger_bars[index].height),
                                                                     kLayout.trigger_bar_visuals[index]);
        }

        for (size_t index = 0; index < 2; ++index) {
            _joypadBleShoulderIndicators[index] = nullptr;
            if (!kLayout.shoulder_visuals[index].enabled) {
                continue;
            }
            _joypadBleShoulderIndicators[index] = createJoypadPreviewVisual(controllerStage,
                                                                            scaleX(kLayout.shoulder_indicators[index].x),
                                                                            scaleY(kLayout.shoulder_indicators[index].y),
                                                                            scaleX(kLayout.shoulder_indicators[index].width),
                                                                            scaleY(kLayout.shoulder_indicators[index].height),
                                                                            kLayout.shoulder_visuals[index]);
        }

        for (size_t index = 0; index < 2; ++index) {
            _joypadBleStickBases[index] = nullptr;
            _joypadBleStickKnobs[index] = nullptr;
            if (!kLayout.stick_visuals[index].enabled) {
                continue;
            }
            _joypadBleStickBases[index] = createJoypadPreviewVisual(controllerStage,
                                                                    scaleX(kLayout.stick_bases[index].x),
                                                                    scaleY(kLayout.stick_bases[index].y),
                                                                    scaleX(kLayout.stick_bases[index].width),
                                                                    scaleY(kLayout.stick_bases[index].height),
                                                                    kLayout.stick_visuals[index]);
            _joypadBleStickKnobs[index] = lv_obj_create(_joypadBleStickBases[index]);
            const lv_coord_t knob_size = static_cast<lv_coord_t>(std::max<int>(18,
                std::min<lv_coord_t>(lv_obj_get_width(_joypadBleStickBases[index]), lv_obj_get_height(_joypadBleStickBases[index])) / 3));
            lv_obj_set_size(_joypadBleStickKnobs[index], knob_size, knob_size);
            lv_obj_clear_flag(_joypadBleStickKnobs[index], LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_clear_flag(_joypadBleStickKnobs[index], LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_radius(_joypadBleStickKnobs[index], LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_border_width(_joypadBleStickKnobs[index], 2, 0);
            lv_obj_set_style_border_color(_joypadBleStickKnobs[index], lv_color_hex(0x0284C7), 0);
            lv_obj_set_style_bg_color(_joypadBleStickKnobs[index], lv_color_hex(0x38BDF8), 0);
            lv_obj_set_style_bg_opa(_joypadBleStickKnobs[index], LV_OPA_COVER, 0);
            lv_obj_set_style_pad_all(_joypadBleStickKnobs[index], 0, 0);
            lv_obj_set_style_shadow_width(_joypadBleStickKnobs[index], 0, 0);
            lv_obj_set_style_outline_width(_joypadBleStickKnobs[index], 0, 0);
            lv_obj_center(_joypadBleStickKnobs[index]);
        }

        for (size_t index = 0; index < 4; ++index) {
            _joypadBleDpadIndicators[index] = nullptr;
            if (!kLayout.dpad_visuals[index].enabled) {
                continue;
            }
            const lv_coord_t size = scaleX(kLayout.dpad_buttons[index].size);
            _joypadBleDpadIndicators[index] = createJoypadPreviewVisual(controllerStage,
                                                                        static_cast<lv_coord_t>(scaleX(kLayout.dpad_buttons[index].center_x) - (size / 2)),
                                                                        static_cast<lv_coord_t>(scaleY(kLayout.dpad_buttons[index].center_y) - (size / 2)),
                                                                        size,
                                                                        size,
                                                                        kLayout.dpad_visuals[index]);
            positionIndicatorAtSourceCenter(_joypadBleDpadIndicators[index],
                                            kLayout.dpad_buttons[index].center_x,
                                            kLayout.dpad_buttons[index].center_y);
        }

        for (size_t index = 0; index < 4; ++index) {
            _joypadBleFaceIndicators[index] = nullptr;
            if (!kLayout.face_visuals[index].enabled) {
                continue;
            }
            const lv_coord_t size = scaleX(kLayout.face_buttons[index].size);
            _joypadBleFaceIndicators[index] = createJoypadPreviewVisual(controllerStage,
                                                                        static_cast<lv_coord_t>(scaleX(kLayout.face_buttons[index].center_x) - (size / 2)),
                                                                        static_cast<lv_coord_t>(scaleY(kLayout.face_buttons[index].center_y) - (size / 2)),
                                                                        size,
                                                                        size,
                                                                        kLayout.face_visuals[index]);
            positionIndicatorAtSourceCenter(_joypadBleFaceIndicators[index],
                                            kLayout.face_buttons[index].center_x,
                                            kLayout.face_buttons[index].center_y);
        }

        lv_obj_t *calibrationSection = createSection(joypadPanel,
                                 "Recalibration",
                                 "Use the live preview above to center both sticks, sweep through the full range, and finish calibration for the connected controller.");

        _joypadBleCalibrationInfoLabel = lv_label_create(calibrationSection);
        lv_obj_set_width(_joypadBleCalibrationInfoLabel, lv_pct(100));
        lv_label_set_long_mode(_joypadBleCalibrationInfoLabel, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(_joypadBleCalibrationInfoLabel, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(_joypadBleCalibrationInfoLabel, lv_color_hex(0x334155), 0);

        _joypadBleCalibrationButton = lv_btn_create(calibrationSection);
        lv_obj_set_size(_joypadBleCalibrationButton, 190, 48);
        lv_obj_set_style_radius(_joypadBleCalibrationButton, 16, 0);
        lv_obj_set_style_border_width(_joypadBleCalibrationButton, 0, 0);
        lv_obj_set_style_bg_color(_joypadBleCalibrationButton, lv_color_hex(0x0F766E), 0);
        lv_obj_set_style_bg_opa(_joypadBleCalibrationButton, LV_OPA_COVER, 0);
        lv_obj_add_event_cb(_joypadBleCalibrationButton, onJoypadCalibrationClickedEventCallback, LV_EVENT_CLICKED, this);

        _joypadBleCalibrationButtonLabel = lv_label_create(_joypadBleCalibrationButton);
        lv_label_set_text(_joypadBleCalibrationButtonLabel, "Start Calibration");
        lv_obj_set_style_text_font(_joypadBleCalibrationButtonLabel, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(_joypadBleCalibrationButtonLabel, lv_color_hex(0xFFFFFF), 0);
        lv_obj_center(_joypadBleCalibrationButtonLabel);

        lv_obj_t *remapSection = createSection(joypadPanel,
                                               "Remap",
                                               "Choose how each incoming BLE control maps into the Sega input mask and joypad actions.");
        for (size_t index = 0; index < _joypadBleRemapDropdowns.size(); ++index) {
            createDropdownRow(remapSection,
                              kJoypadBleRemapLabels[index],
                              kJoypadMapOptionsText,
                              onJoypadConfigChangedEventCallback,
                              &_joypadBleRemapDropdowns[index]);
        }

        addRefreshOnLoad(_joypadBleScreen);
    }
}

void AppSettings::ensureJoypadLocalScreen(void)
{
    if ((_joypadLocalScreen != nullptr) && lv_obj_ready(_joypadLocalScreen)) {
        return;
    }

    auto createJoypadHeader = [](lv_obj_t *screen, const char *title, const char *badge_text, lv_color_t badge_color) {
        lv_obj_t *titleLabel = lv_label_create(screen);
        lv_label_set_text(titleLabel, title);
        lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(titleLabel, lv_color_hex(0x0F172A), 0);
        lv_obj_align(titleLabel, LV_ALIGN_TOP_MID, 0, 74);

        lv_obj_t *badge = lv_obj_create(screen);
        lv_obj_set_size(badge, 38, 38);
        lv_obj_align_to(badge, titleLabel, LV_ALIGN_OUT_LEFT_MID, -16, 0);
        lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(badge, 0, 0);
        lv_obj_set_style_bg_color(badge, badge_color, 0);
        lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);

        lv_obj_t *badgeLabel = lv_label_create(badge);
        lv_label_set_text(badgeLabel, badge_text);
        lv_obj_set_style_text_font(badgeLabel, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(badgeLabel, lv_color_hex(0xFFFFFF), 0);
        lv_obj_center(badgeLabel);
    };

    auto createJoypadPanel = [](lv_obj_t *screen, lv_coord_t height) {
        lv_obj_t *panel = lv_obj_create(screen);
        lv_obj_set_size(panel, lv_pct(92), height);
        lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, 132);
        lv_obj_set_style_radius(panel, 20, 0);
        lv_obj_set_style_border_width(panel, 0, 0);
        lv_obj_set_style_bg_color(panel, lv_color_hex(0xF8FAFC), 0);
        lv_obj_set_style_pad_all(panel, 14, 0);
        lv_obj_set_style_pad_row(panel, 12, 0);
        lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_scroll_dir(panel, LV_DIR_VER);
        return panel;
    };

    auto createSection = [](lv_obj_t *parent, const char *title, const char *hint) {
        lv_obj_t *section = lv_obj_create(parent);
        lv_obj_set_width(section, lv_pct(100));
        lv_obj_set_height(section, LV_SIZE_CONTENT);
        lv_obj_clear_flag(section, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(section, 18, 0);
        lv_obj_set_style_border_width(section, 0, 0);
        lv_obj_set_style_bg_color(section, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_pad_all(section, 14, 0);
        lv_obj_set_style_pad_row(section, 10, 0);
        lv_obj_set_flex_flow(section, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(section, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        lv_obj_t *sectionTitle = lv_label_create(section);
        lv_label_set_text(sectionTitle, title);
        lv_obj_set_style_text_font(sectionTitle, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(sectionTitle, lv_color_hex(0x0F172A), 0);

        lv_obj_t *sectionHint = lv_label_create(section);
        lv_obj_set_width(sectionHint, lv_pct(100));
        lv_label_set_long_mode(sectionHint, LV_LABEL_LONG_WRAP);
        lv_label_set_text(sectionHint, hint);
        lv_obj_set_style_text_font(sectionHint, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(sectionHint, lv_color_hex(0x475569), 0);
        return section;
    };

    auto createJoypadSettingsToggleRow = [](lv_obj_t *parent, const char *title) {
        lv_obj_t *row = lv_obj_create(parent);
        lv_obj_set_size(row, lv_pct(100), 72);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(row, 18, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_left(row, 18, 0);
        lv_obj_set_style_pad_right(row, 18, 0);
        lv_obj_set_style_pad_top(row, 10, 0);
        lv_obj_set_style_pad_bottom(row, 10, 0);

        lv_obj_t *label = lv_label_create(row);
        lv_label_set_text(label, title);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0x111827), 0);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);

        return row;
    };

    auto createDropdownRow = [this, &createJoypadSettingsToggleRow](lv_obj_t *parent, const char *title, const char *options, lv_event_cb_t callback, lv_obj_t **out_dropdown) {
        lv_obj_t *row = createJoypadSettingsToggleRow(parent, title);
        lv_obj_t *dropdown = lv_dropdown_create(row);
        lv_dropdown_set_options_static(dropdown, options);
        lv_obj_set_width(dropdown, 180);
        lv_obj_align(dropdown, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_event_cb(dropdown, callback, LV_EVENT_VALUE_CHANGED, this);
        if (out_dropdown != nullptr) {
            *out_dropdown = dropdown;
        }
    };

    auto createSliderRow = [this, &createJoypadSettingsToggleRow](lv_obj_t *parent, const char *title, int32_t min_value, int32_t max_value, lv_event_cb_t callback, lv_obj_t **out_slider) {
        lv_obj_t *row = createJoypadSettingsToggleRow(parent, title);
        lv_obj_t *slider = lv_slider_create(row);
        lv_slider_set_range(slider, min_value, max_value);
        lv_obj_set_width(slider, 210);
        lv_obj_align(slider, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_event_cb(slider, callback, LV_EVENT_VALUE_CHANGED, this);
        if (out_slider != nullptr) {
            *out_slider = slider;
        }
    };

    auto addRefreshOnLoad = [this](lv_obj_t *screen) {
        lv_obj_add_event_cb(screen, [](lv_event_t *e) {
            AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
            if (app != nullptr) {
                app->refreshJoypadUi();
            }
        }, LV_EVENT_SCREEN_LOADED, this);
    };

    {
        _joypadLocalScreen = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(_joypadLocalScreen, lv_color_hex(0xE5F3FF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(_joypadLocalScreen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_clear_flag(_joypadLocalScreen, LV_OBJ_FLAG_SCROLLABLE);
        createJoypadHeader(_joypadLocalScreen, "Local Controller", "LC", lv_color_hex(0x0F766E));
        lv_obj_t *joypadPanel = createJoypadPanel(_joypadLocalScreen, 680);

        lv_obj_t *generalSection = createSection(joypadPanel,
                                                 "Mode",
                                                 "Choose which local controller wiring profile the P4 should use without BLE. Legacy direct GPIO remains available, alongside Resistive Keyboard and MCP23017-backed setups.");

        lv_obj_t *manualActiveRow = createJoypadSettingsToggleRow(generalSection, "Use Local Controller");
        _joypadManualActiveSwitch = lv_switch_create(manualActiveRow);
        lv_obj_align(_joypadManualActiveSwitch, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_event_cb(_joypadManualActiveSwitch, onJoypadConfigChangedEventCallback, LV_EVENT_VALUE_CHANGED, this);

        createDropdownRow(generalSection, "Local Interface", kJoypadManualModeOptionsText, onJoypadConfigChangedEventCallback, &_joypadManualModeDropdown);

        constexpr auto &kLocalLayout = jc4880::joypad_layout::kLocalControllerLayout;
        lv_obj_t *previewSection = createSection(joypadPanel,
                                                 "Layout Preview",
                                                 "This preview is built from Joypad Layout Configuration and should match the configured Local canvas in firmware.");
        {
            lv_obj_t *controllerPad = lv_obj_create(previewSection);
            lv_obj_set_width(controllerPad, lv_pct(100));
            lv_obj_set_height(controllerPad, LV_SIZE_CONTENT);
            lv_obj_clear_flag(controllerPad, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_clear_flag(controllerPad, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_radius(controllerPad, 22, 0);
            lv_obj_set_style_border_width(controllerPad, 0, 0);
            lv_obj_set_style_bg_color(controllerPad, lv_color_hex(0xEFF6FF), 0);
            lv_obj_set_style_bg_opa(controllerPad, LV_OPA_COVER, 0);
            lv_obj_set_style_pad_all(controllerPad, 12, 0);
            lv_obj_set_style_pad_row(controllerPad, 0, 0);
            lv_obj_set_flex_flow(controllerPad, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(controllerPad, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

            const auto scaleX = [&kLocalLayout](int coordinate) {
                return scaleJoypadCoordinate(coordinate,
                                             static_cast<lv_coord_t>(kLocalLayout.preview_frame.width),
                                             static_cast<lv_coord_t>(kLocalLayout.controller_source_width));
            };
            const auto scaleY = [&kLocalLayout](int coordinate) {
                return scaleJoypadCoordinate(coordinate,
                                             static_cast<lv_coord_t>(kLocalLayout.preview_frame.height),
                                             static_cast<lv_coord_t>(kLocalLayout.controller_source_height));
            };

            lv_obj_t *controllerStage = lv_obj_create(controllerPad);
            lv_obj_set_size(controllerStage,
                            static_cast<lv_coord_t>(kLocalLayout.preview_frame.width),
                            static_cast<lv_coord_t>(kLocalLayout.preview_frame.height));
            lv_obj_clear_flag(controllerStage, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_clear_flag(controllerStage, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_bg_opa(controllerStage, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(controllerStage, 0, 0);
            lv_obj_set_style_pad_all(controllerStage, 0, 0);

            lv_obj_t *controllerImage = lv_img_create(controllerStage);
            lv_img_set_src(controllerImage, &ui_img_local_controller_png);
            lv_img_set_size_mode(controllerImage, LV_IMG_SIZE_MODE_REAL);
            lv_img_set_zoom(controllerImage, static_cast<uint16_t>((256 * kLocalLayout.preview_frame.width) / kLocalLayout.controller_source_width));
            lv_obj_center(controllerImage);

            for (size_t index = 0; index < 2; ++index) {
                _joypadLocalTriggerBars[index] = nullptr;
                if (kLocalLayout.trigger_bar_visuals[index].enabled) {
                    _joypadLocalTriggerBars[index] = createJoypadPreviewVisual(controllerStage,
                                                                               scaleX(kLocalLayout.trigger_bars[index].x),
                                                                               scaleY(kLocalLayout.trigger_bars[index].y),
                                                                               scaleX(kLocalLayout.trigger_bars[index].width),
                                                                               scaleY(kLocalLayout.trigger_bars[index].height),
                                                                               kLocalLayout.trigger_bar_visuals[index]);
                }

                _joypadLocalShoulderIndicators[index] = nullptr;
                if (kLocalLayout.shoulder_visuals[index].enabled) {
                    _joypadLocalShoulderIndicators[index] = createJoypadPreviewVisual(controllerStage,
                                                                                      scaleX(kLocalLayout.shoulder_indicators[index].x),
                                                                                      scaleY(kLocalLayout.shoulder_indicators[index].y),
                                                                                      scaleX(kLocalLayout.shoulder_indicators[index].width),
                                                                                      scaleY(kLocalLayout.shoulder_indicators[index].height),
                                                                                      kLocalLayout.shoulder_visuals[index]);
                }

                _joypadLocalStickBases[index] = nullptr;
                _joypadLocalStickKnobs[index] = nullptr;
                if (!kLocalLayout.stick_visuals[index].enabled) {
                    continue;
                }

                _joypadLocalStickBases[index] = createJoypadPreviewVisual(controllerStage,
                                                                          scaleX(kLocalLayout.stick_bases[index].x),
                                                                          scaleY(kLocalLayout.stick_bases[index].y),
                                                                          scaleX(kLocalLayout.stick_bases[index].width),
                                                                          scaleY(kLocalLayout.stick_bases[index].height),
                                                                          kLocalLayout.stick_visuals[index]);
                _joypadLocalStickKnobs[index] = lv_obj_create(_joypadLocalStickBases[index]);
                const lv_coord_t knob_size = static_cast<lv_coord_t>(std::max<int>(18,
                    std::min<lv_coord_t>(lv_obj_get_width(_joypadLocalStickBases[index]), lv_obj_get_height(_joypadLocalStickBases[index])) / 3));
                lv_obj_set_size(_joypadLocalStickKnobs[index], knob_size, knob_size);
                lv_obj_clear_flag(_joypadLocalStickKnobs[index], LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_clear_flag(_joypadLocalStickKnobs[index], LV_OBJ_FLAG_CLICKABLE);
                lv_obj_set_style_radius(_joypadLocalStickKnobs[index], LV_RADIUS_CIRCLE, 0);
                lv_obj_set_style_border_width(_joypadLocalStickKnobs[index], 2, 0);
                lv_obj_set_style_border_color(_joypadLocalStickKnobs[index], lv_color_hex(0x0284C7), 0);
                lv_obj_set_style_bg_color(_joypadLocalStickKnobs[index], lv_color_hex(0x38BDF8), 0);
                lv_obj_set_style_bg_opa(_joypadLocalStickKnobs[index], LV_OPA_COVER, 0);
                lv_obj_set_style_pad_all(_joypadLocalStickKnobs[index], 0, 0);
                lv_obj_set_style_shadow_width(_joypadLocalStickKnobs[index], 0, 0);
                lv_obj_set_style_outline_width(_joypadLocalStickKnobs[index], 0, 0);
                lv_obj_center(_joypadLocalStickKnobs[index]);
            }

            for (size_t index = 0; index < 4; ++index) {
                _joypadLocalDpadIndicators[index] = nullptr;

                _joypadLocalFaceIndicators[index] = nullptr;
                if (kLocalLayout.face_visuals[index].enabled) {
                    const lv_coord_t size = scaleX(kLocalLayout.face_buttons[index].size);
                    _joypadLocalFaceIndicators[index] = createJoypadPreviewVisual(controllerStage,
                                                                                  static_cast<lv_coord_t>(scaleX(kLocalLayout.face_buttons[index].center_x) - (size / 2)),
                                                                                  static_cast<lv_coord_t>(scaleY(kLocalLayout.face_buttons[index].center_y) - (size / 2)),
                                                                                  size,
                                                                                  size,
                                                                                  kLocalLayout.face_visuals[index]);
                }
            }
        }

        lv_obj_t *manualSection = createSection(joypadPanel,
                                                "Pin Mapping",
                                                "Direct GPIO stores per-signal pins, Resistive Keyboard stores ladder GPIO plus resistor assignments, and MCP23017 stores SDA/SCL plus analog X/Y axis GPIOs on the P4 and button pins on the expander.");
        for (size_t index = 0; index < _joypadManualSpiDropdowns.size(); ++index) {
            createDropdownRow(manualSection,
                              kJoypadSpiLabels[index],
                              kJoypadGpioOptionsText,
                              onJoypadConfigChangedEventCallback,
                              &_joypadManualSpiDropdowns[index]);
        }
        for (size_t index = 0; index < _joypadManualResistiveDropdowns.size(); ++index) {
            createDropdownRow(manualSection,
                              kJoypadResistiveLabels[index],
                              kJoypadAnalogGpioOptionsText,
                              onJoypadConfigChangedEventCallback,
                              &_joypadManualResistiveDropdowns[index]);
        }
        for (size_t index = 0; index < _joypadManualResistiveButtonDropdowns.size(); ++index) {
            createDropdownRow(manualSection,
                              kJoypadButtonLabels[index],
                              kJoypadResistiveBindingOptionsText,
                              onJoypadConfigChangedEventCallback,
                              &_joypadManualResistiveButtonDropdowns[index]);
        }
        for (size_t index = 0; index < _joypadManualMcpDropdowns.size(); ++index) {
            createDropdownRow(manualSection,
                              kJoypadMcpLabels[index],
                              kJoypadGpioOptionsText,
                              onJoypadConfigChangedEventCallback,
                              &_joypadManualMcpDropdowns[index]);
        }
        for (size_t index = 0; index < _joypadManualMcpButtonDropdowns.size(); ++index) {
            createDropdownRow(manualSection,
                              kJoypadMcpControlLabels[index],
                              kJoypadMcpPinOptionsText,
                              onJoypadConfigChangedEventCallback,
                              &_joypadManualMcpButtonDropdowns[index]);
        }

        lv_obj_t *peripheralSection = createSection(joypadPanel,
                                                    "Local Peripherals",
                                                    "These controls stay with the Local Controller profile so wiring, lighting, and haptics can be configured from the same screen.");

        createDropdownRow(peripheralSection,
                          "Haptics GPIO",
                          kJoypadPeripheralGpioOptionsText,
                          onDropdownJoypadLocalHapticGpioValueChangeEventCallback,
                          &_joypadLocalHapticGpioDropdown);

        createDropdownRow(peripheralSection,
                  "Haptics Strength",
                  kJoypadHapticLevelOptionsText,
                  onDropdownJoypadLocalHapticLevelValueChangeEventCallback,
                  &_joypadLocalHapticLevelDropdown);

        lv_obj_t *neopixelPowerRow = createJoypadSettingsToggleRow(peripheralSection, "Neopixel Power");
        _joypadLocalNeopixelPowerSwitch = lv_switch_create(neopixelPowerRow);
        lv_obj_align(_joypadLocalNeopixelPowerSwitch, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_event_cb(_joypadLocalNeopixelPowerSwitch,
                            onSwitchPanelScreenSettingNeopixelPowerValueChangeEventCallback,
                            LV_EVENT_VALUE_CHANGED,
                            this);

        createDropdownRow(peripheralSection,
                          "Neopixel GPIO",
                          kJoypadPeripheralGpioOptionsText,
                          onDropdownPanelScreenSettingNeopixelGpioValueChangeEventCallback,
                          &_joypadLocalNeopixelGpioDropdown);
        createDropdownRow(peripheralSection,
                          "Neopixel Palette",
                          kJoypadNeopixelPaletteOptionsText,
                          onDropdownPanelScreenSettingNeopixelPaletteValueChangeEventCallback,
                          &_joypadLocalNeopixelPaletteDropdown);
        createDropdownRow(peripheralSection,
                          "Neopixel Effect",
                          kJoypadNeopixelEffectOptionsText,
                          onDropdownPanelScreenSettingNeopixelEffectValueChangeEventCallback,
                          &_joypadLocalNeopixelEffectDropdown);
        createSliderRow(peripheralSection,
                        "Neopixel Brightness",
                        0,
                        255,
                        onSliderPanelScreenSettingNeopixelBrightnessValueChangeEventCallback,
                        &_joypadLocalNeopixelBrightnessSlider);

        _joypadLocalNeopixelInfoLabel = lv_label_create(peripheralSection);
        lv_obj_set_width(_joypadLocalNeopixelInfoLabel, lv_pct(100));
        lv_label_set_long_mode(_joypadLocalNeopixelInfoLabel, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(_joypadLocalNeopixelInfoLabel, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(_joypadLocalNeopixelInfoLabel, lv_color_hex(0x475569), 0);

        _joypadInfoLabel = lv_label_create(joypadPanel);
        lv_obj_set_width(_joypadInfoLabel, lv_pct(100));
        lv_label_set_long_mode(_joypadInfoLabel, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(_joypadInfoLabel, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(_joypadInfoLabel, lv_color_hex(0x475569), 0);

        addRefreshOnLoad(_joypadLocalScreen);
    }
}

void AppSettings::refreshJoypadUi(void)
{
    if (!isUiActive()) {
        return;
    }

    jc4880_joypad_config_t config = {};
    if (!jc4880_joypad_get_config(&config)) {
        return;
    }

    jc4880_joypad_ble_report_state_t report = {};
    jc4880_joypad_get_ble_report_state(&report);
    jc4880_joypad_manual_report_state_t manual_report = {};
    jc4880_joypad_get_manual_report_state(&manual_report);

    if (lv_obj_ready(_joypadManualModeDropdown)) {
        lv_dropdown_set_selected(_joypadManualModeDropdown,
                                 findDropdownIndexForValue(kJoypadManualModeOptions,
                                                           sizeof(kJoypadManualModeOptions) / sizeof(kJoypadManualModeOptions[0]),
                                                           config.manual_mode));
    }

    if (lv_obj_ready(_joypadBleActiveSwitch)) {
        if (config.backend == JC4880_JOYPAD_BACKEND_BLE) {
            lv_obj_add_state(_joypadBleActiveSwitch, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(_joypadBleActiveSwitch, LV_STATE_CHECKED);
        }
    }

    if (lv_obj_ready(_joypadManualActiveSwitch)) {
        if (config.backend == JC4880_JOYPAD_BACKEND_MANUAL) {
            lv_obj_add_state(_joypadManualActiveSwitch, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(_joypadManualActiveSwitch, LV_STATE_CHECKED);
        }
    }

    if (lv_obj_ready(_joypadBleEnableSwitch)) {
        if (config.ble_enabled != 0) {
            lv_obj_add_state(_joypadBleEnableSwitch, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(_joypadBleEnableSwitch, LV_STATE_CHECKED);
        }
    }

    if (lv_obj_ready(_joypadBleDiscoverySwitch)) {
        if (config.ble_discovery_enabled != 0) {
            lv_obj_add_state(_joypadBleDiscoverySwitch, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(_joypadBleDiscoverySwitch, LV_STATE_CHECKED);
        }
    }

    _joypadBleDeviceOptions.clear();
    std::string deviceOptionsText;
    if (config.ble_device_addr[0] != '\0') {
        _joypadBleDeviceOptions.emplace_back(config.ble_device_addr);
        deviceOptionsText += std::string("Saved: ") + config.ble_device_addr;
    }
    if ((report.device_addr[0] != '\0') &&
        (std::find(_joypadBleDeviceOptions.begin(), _joypadBleDeviceOptions.end(), report.device_addr) == _joypadBleDeviceOptions.end())) {
        if (!deviceOptionsText.empty()) {
            deviceOptionsText += "\n";
        }
        deviceOptionsText += (report.device_name[0] != '\0')
                                 ? (std::string("Connected: ") + report.device_name + " (" + report.device_addr + ")")
                                 : (std::string("Connected: ") + report.device_addr);
        _joypadBleDeviceOptions.emplace_back(report.device_addr);
    }
    if (deviceOptionsText.empty()) {
        deviceOptionsText = "No controller selected";
    }

    if (lv_obj_ready(_joypadBleDeviceDropdown)) {
        lv_dropdown_set_options(_joypadBleDeviceDropdown, deviceOptionsText.c_str());
        uint16_t selectedIndex = 0;
        for (size_t index = 0; index < _joypadBleDeviceOptions.size(); ++index) {
            if (_joypadBleDeviceOptions[index] == config.ble_device_addr) {
                selectedIndex = static_cast<uint16_t>(index);
                break;
            }
        }
        lv_dropdown_set_selected(_joypadBleDeviceDropdown, selectedIndex);
    }

    for (size_t index = 0; index < _joypadBleRemapDropdowns.size(); ++index) {
        if (lv_obj_ready(_joypadBleRemapDropdowns[index])) {
            lv_dropdown_set_selected(_joypadBleRemapDropdowns[index],
                                     findDropdownIndexForValue(kJoypadMapOptions,
                                                               sizeof(kJoypadMapOptions) / sizeof(kJoypadMapOptions[0]),
                                                               config.ble_remap[index]));
        }
    }

    for (size_t index = 0; index < _joypadManualSpiDropdowns.size(); ++index) {
        if (lv_obj_ready(_joypadManualSpiDropdowns[index])) {
            lv_dropdown_set_selected(_joypadManualSpiDropdowns[index],
                                     findDropdownIndexForValue(kJoypadGpioOptions,
                                                               sizeof(kJoypadGpioOptions) / sizeof(kJoypadGpioOptions[0]),
                                                               config.manual_spi_gpio[index]));
        }
    }

    for (size_t index = 0; index < _joypadManualResistiveDropdowns.size(); ++index) {
        if (lv_obj_ready(_joypadManualResistiveDropdowns[index])) {
            lv_dropdown_set_selected(_joypadManualResistiveDropdowns[index],
                                     findDropdownIndexForValue(kJoypadAnalogGpioOptions,
                                                               sizeof(kJoypadAnalogGpioOptions) / sizeof(kJoypadAnalogGpioOptions[0]),
                                                               config.manual_resistive_gpio[index]));
        }
    }

    for (size_t index = 0; index < _joypadManualResistiveButtonDropdowns.size(); ++index) {
        if (lv_obj_ready(_joypadManualResistiveButtonDropdowns[index])) {
            lv_dropdown_set_selected(_joypadManualResistiveButtonDropdowns[index],
                                     findDropdownIndexForValue(kJoypadResistiveBindingOptions,
                                                               sizeof(kJoypadResistiveBindingOptions) / sizeof(kJoypadResistiveBindingOptions[0]),
                                                               config.manual_resistive_button_binding[index]));
        }
    }

    for (size_t index = 0; index < _joypadManualMcpDropdowns.size(); ++index) {
        if (lv_obj_ready(_joypadManualMcpDropdowns[index])) {
            lv_dropdown_set_selected(_joypadManualMcpDropdowns[index],
                                     findDropdownIndexForValue(kJoypadGpioOptions,
                                                               sizeof(kJoypadGpioOptions) / sizeof(kJoypadGpioOptions[0]),
                                                               config.manual_mcp_i2c_gpio[index]));
        }
    }

    for (size_t index = 0; index < _joypadManualMcpButtonDropdowns.size(); ++index) {
        if (lv_obj_ready(_joypadManualMcpButtonDropdowns[index])) {
            lv_dropdown_set_selected(_joypadManualMcpButtonDropdowns[index],
                                     findDropdownIndexForValue(kJoypadMcpPinOptions,
                                                               sizeof(kJoypadMcpPinOptions) / sizeof(kJoypadMcpPinOptions[0]),
                                                               config.manual_mcp_button_pin[index]));
        }
    }

    if (lv_obj_ready(_joypadLocalHapticGpioDropdown)) {
        lv_dropdown_set_selected(_joypadLocalHapticGpioDropdown,
                                 findDropdownIndexForValue(kJoypadPeripheralGpioOptions,
                                                           sizeof(kJoypadPeripheralGpioOptions) / sizeof(kJoypadPeripheralGpioOptions[0]),
                                                           _nvs_param_map[kNvsKeyAudioHapticGpio]));
    }

    if (lv_obj_ready(_joypadLocalHapticLevelDropdown)) {
        lv_dropdown_set_selected(_joypadLocalHapticLevelDropdown,
                                 findDropdownIndexForValue(kJoypadHapticLevelOptions,
                                                           sizeof(kJoypadHapticLevelOptions) / sizeof(kJoypadHapticLevelOptions[0]),
                                                           _nvs_param_map[kNvsKeyAudioHapticLevel]));
    }

    if (lv_obj_ready(_joypadLocalNeopixelPowerSwitch)) {
        if (_nvs_param_map[kNvsKeyNeopixelPower] != 0) {
            lv_obj_add_state(_joypadLocalNeopixelPowerSwitch, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(_joypadLocalNeopixelPowerSwitch, LV_STATE_CHECKED);
        }
    }

    if (lv_obj_ready(_joypadLocalNeopixelGpioDropdown)) {
        lv_dropdown_set_selected(_joypadLocalNeopixelGpioDropdown,
                                 findDropdownIndexForValue(kJoypadPeripheralGpioOptions,
                                                           sizeof(kJoypadPeripheralGpioOptions) / sizeof(kJoypadPeripheralGpioOptions[0]),
                                                           _nvs_param_map[kNvsKeyNeopixelGpio]));
    }

    if (lv_obj_ready(_joypadLocalNeopixelPaletteDropdown)) {
        lv_dropdown_set_selected(_joypadLocalNeopixelPaletteDropdown,
                                 findDropdownIndexForValue(kJoypadNeopixelPaletteOptions,
                                                           sizeof(kJoypadNeopixelPaletteOptions) / sizeof(kJoypadNeopixelPaletteOptions[0]),
                                                           _nvs_param_map[kNvsKeyNeopixelPalette]));
    }

    if (lv_obj_ready(_joypadLocalNeopixelEffectDropdown)) {
        lv_dropdown_set_selected(_joypadLocalNeopixelEffectDropdown,
                                 findDropdownIndexForValue(kJoypadNeopixelEffectOptions,
                                                           sizeof(kJoypadNeopixelEffectOptions) / sizeof(kJoypadNeopixelEffectOptions[0]),
                                                           _nvs_param_map[kNvsKeyNeopixelEffect]));
    }

    if (lv_obj_ready(_joypadLocalNeopixelBrightnessSlider)) {
        lv_slider_set_value(_joypadLocalNeopixelBrightnessSlider, _nvs_param_map[kNvsKeyNeopixelBrightness], LV_ANIM_OFF);
    }

    const bool spi_mode = config.manual_mode == JC4880_JOYPAD_MANUAL_MODE_SPI;
    const bool resistive_mode = config.manual_mode == JC4880_JOYPAD_MANUAL_MODE_RESISTIVE;
    const bool mcp_mode = config.manual_mode == JC4880_JOYPAD_MANUAL_MODE_MCP23017;
    auto setRowHidden = [](lv_obj_t *control, bool hidden) {
        if (!lv_obj_ready(control)) {
            return;
        }
        lv_obj_t *row = lv_obj_get_parent(control);
        if (!lv_obj_ready(row)) {
            return;
        }
        if (hidden) {
            lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(row, LV_OBJ_FLAG_HIDDEN);
        }
    };
    auto setRowLabel = [](lv_obj_t *control, const char *text) {
        if (!lv_obj_ready(control)) {
            return;
        }
        lv_obj_t *row = lv_obj_get_parent(control);
        if (!lv_obj_ready(row) || (lv_obj_get_child_cnt(row) == 0)) {
            return;
        }
        lv_obj_t *label = lv_obj_get_child(row, 0);
        if (lv_obj_ready(label)) {
            lv_label_set_text(label, text);
        }
    };
    for (lv_obj_t *dropdown : _joypadManualSpiDropdowns) {
        setRowHidden(dropdown, !spi_mode);
    }
    for (size_t index = 0; index < _joypadManualResistiveDropdowns.size(); ++index) {
        lv_obj_t *dropdown = _joypadManualResistiveDropdowns[index];
        setRowLabel(dropdown, mcp_mode ? kJoypadAxisLabels[index] : kJoypadResistiveLabels[index]);
        setRowHidden(dropdown, !(resistive_mode || mcp_mode));
    }
    for (lv_obj_t *dropdown : _joypadManualResistiveButtonDropdowns) {
        setRowHidden(dropdown, !resistive_mode);
    }
    for (lv_obj_t *dropdown : _joypadManualMcpDropdowns) {
        setRowHidden(dropdown, !mcp_mode);
    }
    for (size_t index = 0; index < _joypadManualMcpButtonDropdowns.size(); ++index) {
        setRowHidden(_joypadManualMcpButtonDropdowns[index], !mcp_mode || (index < 4));
    }

    if (lv_obj_ready(_joypadBleStatusLabel)) {
        const char *active_backend = "Disabled";
        if (config.backend == JC4880_JOYPAD_BACKEND_BLE) {
            active_backend = "BLE Controller";
        } else if (config.backend == JC4880_JOYPAD_BACKEND_MANUAL) {
            active_backend = "Local Controller";
        }

        std::string status;
        if (report.connected != 0) {
            status = "Status: Connected\n";
            status += std::string("Controller: ") + ((report.device_name[0] != '\0') ? report.device_name : "Unnamed controller") + "\n";
            status += std::string("Address: ") + ((report.device_addr[0] != '\0') ? report.device_addr : config.ble_device_addr) + "\n";
        } else if ((config.ble_enabled != 0) && (report.scanning != 0)) {
            status = "Status: Pairing / scanning\n";
            status += "Put the controller into pairing mode and watch for a connected report.\n";
        } else if (config.ble_enabled != 0) {
            status = "Status: Idle\n";
            status += (config.ble_device_addr[0] != '\0')
                          ? (std::string("Waiting for saved controller: ") + config.ble_device_addr + "\n")
                          : std::string("No controller stored yet.\n");
        } else {
            status = "Status: Disabled\nEnable BLE Joypad to let the ESP32-C6 pair or reconnect a controller.\n";
        }
        status += std::string("Active Input: ") + active_backend + "\n";
        status += std::string("Discovery: ") + ((config.ble_discovery_enabled != 0) ? "On" : "Off") + "\n";
        char raw_mask_text[16] = {};
        snprintf(raw_mask_text, sizeof(raw_mask_text), "%08" PRIX32, report.raw_mask);
        status += std::string("Pressed: ") + describeJoypadMask(report.raw_mask) + "\n";
        status += std::string("Raw mask: 0x") + raw_mask_text + "\n";
        status += "Axes: (" + std::to_string(report.axis_x) + ", " + std::to_string(report.axis_y) + ", ";
        status += std::to_string(report.axis_rx) + ", " + std::to_string(report.axis_ry) + ")\n";
        status += "Triggers: (" + std::to_string(report.brake) + ", " + std::to_string(report.throttle) + ")";
        lv_label_set_text(_joypadBleStatusLabel, status.c_str());
    }

    refreshJoypadCalibrationUi(report);

    const auto localPreviewAxis = [](uint16_t raw_value) {
        const int32_t centered = static_cast<int32_t>(raw_value) - 2048;
        return static_cast<int16_t>(std::max<int32_t>(-512, std::min<int32_t>(512, centered / 4)));
    };
    const bool localAnalogActive = (manual_report.active != 0) &&
                                   (manual_report.manual_mode == JC4880_JOYPAD_MANUAL_MODE_MCP23017);
    const int16_t localAxisX = localAnalogActive ? localPreviewAxis(manual_report.axis_x_raw) : 0;
    const int16_t localAxisY = localAnalogActive ? localPreviewAxis(manual_report.axis_y_raw) : 0;
    updateJoypadPreviewStickKnob(_joypadLocalStickBases[0], _joypadLocalStickKnobs[0], localAxisX, localAxisY);
    setJoypadPreviewActivity(_joypadLocalStickBases[0], (localAxisX != 0) || (localAxisY != 0), lv_color_hex(0x0284C7));

    setJoypadPreviewActivity(_joypadLocalTriggerBars[0], (manual_report.action_mask & JC4880_JOYPAD_ACTION_SAVE) != 0, lv_color_hex(0x0F766E));
    setJoypadPreviewActivity(_joypadLocalTriggerBars[1], (manual_report.action_mask & JC4880_JOYPAD_ACTION_LOAD) != 0, lv_color_hex(0x0F766E));
    setJoypadPreviewActivity(_joypadLocalShoulderIndicators[0], (manual_report.gameplay_mask & JC4880_JOYPAD_MASK_START) != 0, lv_color_hex(0x2563EB));
    setJoypadPreviewActivity(_joypadLocalShoulderIndicators[1], (manual_report.action_mask & JC4880_JOYPAD_ACTION_EXIT) != 0, lv_color_hex(0x2563EB));

    setJoypadPreviewActivity(_joypadLocalFaceIndicators[0], (manual_report.gameplay_mask & JC4880_JOYPAD_MASK_BUTTON_Y) != 0, lv_color_hex(0x3B82F6));
    setJoypadPreviewActivity(_joypadLocalFaceIndicators[1], (manual_report.gameplay_mask & JC4880_JOYPAD_MASK_BUTTON_C) != 0, lv_color_hex(0xF59E0B));
    setJoypadPreviewActivity(_joypadLocalFaceIndicators[2], (manual_report.gameplay_mask & JC4880_JOYPAD_MASK_BUTTON_B) != 0, lv_color_hex(0xEF4444));
    setJoypadPreviewActivity(_joypadLocalFaceIndicators[3], (manual_report.gameplay_mask & JC4880_JOYPAD_MASK_BUTTON_A) != 0, lv_color_hex(0x22C55E));

    if (lv_obj_ready(_joypadInfoLabel)) {
        std::string info = (config.backend == JC4880_JOYPAD_BACKEND_MANUAL)
                               ? std::string("Local controller is the active Sega input source.\n")
                               : std::string("Local controller is configured but not currently selected as the Sega input source.\n");
        if (config.manual_mode == JC4880_JOYPAD_MANUAL_MODE_SPI) {
            info += "Mode: Direct GPIO (legacy) inputs are live now.\n";
        } else if (config.manual_mode == JC4880_JOYPAD_MANUAL_MODE_RESISTIVE) {
            info += "Mode: Resistive Keyboard reads GPIO 50/51 ladder inputs and matches configured resistor assignments using the built-in 10 kohm pull-up model.\n";
        } else {
            info += "Mode: MCP23017 scans addresses 0x20-0x27 on the selected SDA/SCL wiring, reads Y/X axis potentiometers from the configured P4 GPIOs, and treats configured MCP pins as active-low button inputs with pull-ups enabled.\n";
        }
        info += "Swipe back when you finish configuring local controls.";
        lv_label_set_text(_joypadInfoLabel, info.c_str());
    }

    if (lv_obj_ready(_joypadLocalNeopixelInfoLabel)) {
        std::string info = std::string("Neopixel GPIO: ") + std::to_string(_nvs_param_map[kNvsKeyNeopixelGpio]);
        info += " | Brightness: " + std::to_string(_nvs_param_map[kNvsKeyNeopixelBrightness]);
        info += (_nvs_param_map[kNvsKeyNeopixelPower] != 0) ? " | Power: On" : " | Power: Off";
        info += "\nHaptics GPIO: ";
        if (_nvs_param_map[kNvsKeyAudioHapticGpio] < 0) {
            info += "Disabled";
        } else {
            info += std::to_string(_nvs_param_map[kNvsKeyAudioHapticGpio]);
        }
        static constexpr const char *kHapticLevelNames[] = {"None", "Low", "Medium", "High"};
        const int32_t haptic_level = std::max<int32_t>(0, std::min<int32_t>(3, _nvs_param_map[kNvsKeyAudioHapticLevel]));
        info += " | Haptics Strength: ";
        info += kHapticLevelNames[haptic_level];
        lv_label_set_text(_joypadLocalNeopixelInfoLabel, info.c_str());
    }
}

void AppSettings::refreshJoypadCalibrationUi(const jc4880_joypad_ble_report_state_t &report)
{
    if ((report.connected == 0) || (report.device_addr[0] == '\0')) {
        _joypadBlePreviewCenterValid = false;
        _joypadBlePreviewDeviceAddr.fill('\0');
        _joypadBlePreviewCenterAxes.fill(0);
    } else if (!_joypadBlePreviewCenterValid ||
               (std::strncmp(_joypadBlePreviewDeviceAddr.data(), report.device_addr, _joypadBlePreviewDeviceAddr.size()) != 0)) {
        _joypadBlePreviewCenterValid = true;
        snprintf(_joypadBlePreviewDeviceAddr.data(), _joypadBlePreviewDeviceAddr.size(), "%s", report.device_addr);
        _joypadBlePreviewCenterAxes[0] = report.raw_axis_x;
        _joypadBlePreviewCenterAxes[1] = report.raw_axis_y;
        _joypadBlePreviewCenterAxes[2] = report.raw_axis_rx;
        _joypadBlePreviewCenterAxes[3] = report.raw_axis_ry;
    }

    const auto previewAxis = [this](int16_t raw_value, size_t index) {
        const int32_t centered = static_cast<int32_t>(raw_value) - static_cast<int32_t>(_joypadBlePreviewCenterAxes[index]);
        return static_cast<int16_t>(std::max<int32_t>(-512, std::min<int32_t>(512, centered)));
    };

    const int16_t display_axis_x = (report.calibration_active != 0) ? report.axis_x : previewAxis(report.raw_axis_x, 0);
    const int16_t display_axis_y = (report.calibration_active != 0) ? report.axis_y : previewAxis(report.raw_axis_y, 1);
    const int16_t display_axis_rx = (report.calibration_active != 0) ? report.axis_rx : previewAxis(report.raw_axis_rx, 2);
    const int16_t display_axis_ry = (report.calibration_active != 0) ? report.axis_ry : previewAxis(report.raw_axis_ry, 3);
    const uint16_t display_brake = (report.calibration_active != 0) ? report.brake : report.raw_brake;
    const uint16_t display_throttle = (report.calibration_active != 0) ? report.throttle : report.raw_throttle;

    setJoypadPreviewAnalogActivity(_joypadBleTriggerBars[0], display_brake, lv_color_hex(0x2563EB));
    setJoypadPreviewAnalogActivity(_joypadBleTriggerBars[1], display_throttle, lv_color_hex(0x2563EB));

    setJoypadPreviewActivity(_joypadBleShoulderIndicators[0], (report.raw_mask & JC4880_JOYPAD_MASK_SHOULDER_L) != 0, lv_color_hex(0x2563EB));
    setJoypadPreviewActivity(_joypadBleShoulderIndicators[1], (report.raw_mask & JC4880_JOYPAD_MASK_SHOULDER_R) != 0, lv_color_hex(0x2563EB));

    updateJoypadPreviewStickKnob(_joypadBleStickBases[0], _joypadBleStickKnobs[0], display_axis_x, display_axis_y);
    updateJoypadPreviewStickKnob(_joypadBleStickBases[1], _joypadBleStickKnobs[1], display_axis_rx, display_axis_ry);
    setJoypadPreviewActivity(_joypadBleStickBases[0], (display_axis_x != 0) || (display_axis_y != 0), lv_color_hex(0x0284C7));
    setJoypadPreviewActivity(_joypadBleStickBases[1], (display_axis_rx != 0) || (display_axis_ry != 0), lv_color_hex(0x0284C7));

    setJoypadPreviewActivity(_joypadBleDpadIndicators[0], (report.raw_mask & JC4880_JOYPAD_MASK_UP) != 0, lv_color_hex(0x0F766E));
    setJoypadPreviewActivity(_joypadBleDpadIndicators[1], (report.raw_mask & JC4880_JOYPAD_MASK_LEFT) != 0, lv_color_hex(0x0F766E));
    setJoypadPreviewActivity(_joypadBleDpadIndicators[2], (report.raw_mask & JC4880_JOYPAD_MASK_RIGHT) != 0, lv_color_hex(0x0F766E));
    setJoypadPreviewActivity(_joypadBleDpadIndicators[3], (report.raw_mask & JC4880_JOYPAD_MASK_DOWN) != 0, lv_color_hex(0x0F766E));

    setJoypadPreviewActivity(_joypadBleFaceIndicators[0], (report.raw_mask & JC4880_JOYPAD_MASK_BUTTON_C) != 0, lv_color_hex(0x3B82F6));
    setJoypadPreviewActivity(_joypadBleFaceIndicators[1], (report.raw_mask & JC4880_JOYPAD_MASK_BUTTON_Y) != 0, lv_color_hex(0xF59E0B));
    setJoypadPreviewActivity(_joypadBleFaceIndicators[2], (report.raw_mask & JC4880_JOYPAD_MASK_BUTTON_B) != 0, lv_color_hex(0xEF4444));
    setJoypadPreviewActivity(_joypadBleFaceIndicators[3], (report.raw_mask & JC4880_JOYPAD_MASK_BUTTON_A) != 0, lv_color_hex(0x22C55E));

    if (lv_obj_ready(_joypadBleCalibrationButton)) {
        const bool can_calibrate = (report.connected != 0) && (report.device_addr[0] != '\0');
        if (can_calibrate) {
            lv_obj_clear_state(_joypadBleCalibrationButton, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(_joypadBleCalibrationButton, LV_STATE_DISABLED);
        }
    }

    if (lv_obj_ready(_joypadBleCalibrationButtonLabel)) {
        lv_label_set_text(_joypadBleCalibrationButtonLabel,
                          (report.calibration_active != 0) ? "Finish Calibration" : "Start Calibration");
    }

    if (lv_obj_ready(_joypadBleCalibrationInfoLabel)) {
        std::string info;
        if (report.calibration_active != 0) {
            info = "Calibration is running. Both stick knobs were re-centered for this pass. Sweep both sticks in full circles and press both triggers through the full range, then tap Finish Calibration.";
        } else if ((report.connected != 0) && (report.calibration_available != 0)) {
            info = std::string("Calibration profile loaded for ") +
                   ((report.device_name[0] != '\0') ? report.device_name : report.device_addr) +
                   ". Up to 20 controller profiles are stored by BLE device address.";
            jc4880_joypad_ble_calibration_slot_t slot = {};
            if (jc4880_joypad_get_ble_calibration_slot(report.device_addr, &slot)) {
                info += "\n" + formatJoypadCalibrationSlotSummary(slot);
            }
        } else if (report.connected != 0) {
            info = "No calibration profile saved for this controller yet. Start calibration to create one for this device.";
        } else {
            info = "Connect a BLE controller to preview the live layout or create a saved calibration profile.";
        }
        lv_label_set_text(_joypadBleCalibrationInfoLabel, info.c_str());
    }
}

bool AppSettings::persistJoypadConfigFromUi(void)
{
    jc4880_joypad_config_t config = {};
    if (!jc4880_joypad_get_config(&config)) {
        return false;
    }

    const bool ble_active = lv_obj_ready(_joypadBleActiveSwitch) && lv_obj_has_state(_joypadBleActiveSwitch, LV_STATE_CHECKED);
    const bool manual_active = lv_obj_ready(_joypadManualActiveSwitch) && lv_obj_has_state(_joypadManualActiveSwitch, LV_STATE_CHECKED);
    if (ble_active) {
        config.backend = JC4880_JOYPAD_BACKEND_BLE;
    } else if (manual_active) {
        config.backend = JC4880_JOYPAD_BACKEND_MANUAL;
    } else {
        config.backend = JC4880_JOYPAD_BACKEND_DISABLED;
    }

    if (lv_obj_ready(_joypadManualModeDropdown)) {
        config.manual_mode = static_cast<uint8_t>(getDropdownValueForIndex(kJoypadManualModeOptions,
                                                                           sizeof(kJoypadManualModeOptions) / sizeof(kJoypadManualModeOptions[0]),
                                                                           lv_dropdown_get_selected(_joypadManualModeDropdown)));
        applyRequestedLocalDefaults(config);
    }

    config.ble_enabled = (lv_obj_ready(_joypadBleEnableSwitch) && lv_obj_has_state(_joypadBleEnableSwitch, LV_STATE_CHECKED)) ? 1 : 0;
    config.ble_discovery_enabled = (lv_obj_ready(_joypadBleDiscoverySwitch) && lv_obj_has_state(_joypadBleDiscoverySwitch, LV_STATE_CHECKED)) ? 1 : 0;

    config.ble_device_addr[0] = '\0';
    if (lv_obj_ready(_joypadBleDeviceDropdown) && !_joypadBleDeviceOptions.empty()) {
        const uint16_t selected = lv_dropdown_get_selected(_joypadBleDeviceDropdown);
        if (selected < _joypadBleDeviceOptions.size()) {
            strlcpy(config.ble_device_addr, _joypadBleDeviceOptions[selected].c_str(), sizeof(config.ble_device_addr));
        }
    }

    for (size_t index = 0; index < _joypadBleRemapDropdowns.size(); ++index) {
        if (lv_obj_ready(_joypadBleRemapDropdowns[index])) {
            config.ble_remap[index] = static_cast<uint8_t>(getDropdownValueForIndex(kJoypadMapOptions,
                                                                                     sizeof(kJoypadMapOptions) / sizeof(kJoypadMapOptions[0]),
                                                                                     lv_dropdown_get_selected(_joypadBleRemapDropdowns[index])));
        }
    }

    for (size_t index = 0; index < _joypadManualSpiDropdowns.size(); ++index) {
        if (lv_obj_ready(_joypadManualSpiDropdowns[index])) {
            config.manual_spi_gpio[index] = static_cast<int8_t>(getDropdownValueForIndex(kJoypadGpioOptions,
                                                                                          sizeof(kJoypadGpioOptions) / sizeof(kJoypadGpioOptions[0]),
                                                                                          lv_dropdown_get_selected(_joypadManualSpiDropdowns[index])));
        }
    }
    for (size_t index = 0; index < _joypadManualResistiveDropdowns.size(); ++index) {
        if (lv_obj_ready(_joypadManualResistiveDropdowns[index])) {
            config.manual_resistive_gpio[index] = static_cast<int8_t>(getDropdownValueForIndex(kJoypadAnalogGpioOptions,
                                                                                                sizeof(kJoypadAnalogGpioOptions) / sizeof(kJoypadAnalogGpioOptions[0]),
                                                                                                lv_dropdown_get_selected(_joypadManualResistiveDropdowns[index])));
        }
    }
    for (size_t index = 0; index < _joypadManualResistiveButtonDropdowns.size(); ++index) {
        if (lv_obj_ready(_joypadManualResistiveButtonDropdowns[index])) {
            config.manual_resistive_button_binding[index] = static_cast<int8_t>(getDropdownValueForIndex(kJoypadResistiveBindingOptions,
                                                                                                           sizeof(kJoypadResistiveBindingOptions) / sizeof(kJoypadResistiveBindingOptions[0]),
                                                                                                           lv_dropdown_get_selected(_joypadManualResistiveButtonDropdowns[index])));
        }
    }
    for (size_t index = 0; index < _joypadManualMcpDropdowns.size(); ++index) {
        if (lv_obj_ready(_joypadManualMcpDropdowns[index])) {
            config.manual_mcp_i2c_gpio[index] = static_cast<int8_t>(getDropdownValueForIndex(kJoypadGpioOptions,
                                                                                              sizeof(kJoypadGpioOptions) / sizeof(kJoypadGpioOptions[0]),
                                                                                              lv_dropdown_get_selected(_joypadManualMcpDropdowns[index])));
        }
    }
    for (size_t index = 0; index < _joypadManualMcpButtonDropdowns.size(); ++index) {
        if (lv_obj_ready(_joypadManualMcpButtonDropdowns[index])) {
            config.manual_mcp_button_pin[index] = static_cast<int8_t>(getDropdownValueForIndex(kJoypadMcpPinOptions,
                                                                                                sizeof(kJoypadMcpPinOptions) / sizeof(kJoypadMcpPinOptions[0]),
                                                                                                lv_dropdown_get_selected(_joypadManualMcpButtonDropdowns[index])));
        }
    }

    return jc4880_joypad_set_config(&config);
}

void AppSettings::onJoypadConfigChangedEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    lv_obj_t *target = nullptr;
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    target = lv_event_get_target(e);
    if ((target == app->_joypadBleActiveSwitch) && lv_obj_has_state(app->_joypadBleActiveSwitch, LV_STATE_CHECKED) &&
        lv_obj_ready(app->_joypadManualActiveSwitch)) {
        lv_obj_clear_state(app->_joypadManualActiveSwitch, LV_STATE_CHECKED);
    }
    if ((target == app->_joypadManualActiveSwitch) && lv_obj_has_state(app->_joypadManualActiveSwitch, LV_STATE_CHECKED) &&
        lv_obj_ready(app->_joypadBleActiveSwitch)) {
        lv_obj_clear_state(app->_joypadBleActiveSwitch, LV_STATE_CHECKED);
    }

    app->persistJoypadConfigFromUi();
    app->refreshJoypadUi();

end:
    return;
}

void AppSettings::onJoypadCalibrationClickedEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");

    if (jc4880_joypad_is_ble_calibration_active()) {
        if (!jc4880_joypad_finish_ble_calibration()) {
            jc4880_joypad_cancel_ble_calibration();
        }
    } else {
        if (jc4880_joypad_begin_ble_calibration()) {
            app->_joypadBlePreviewCenterValid = false;
            app->_joypadBlePreviewDeviceAddr.fill('\0');
            app->_joypadBlePreviewCenterAxes.fill(0);
            centerJoypadPreviewKnob(app->_joypadBleStickBases[0], app->_joypadBleStickKnobs[0]);
            centerJoypadPreviewKnob(app->_joypadBleStickBases[1], app->_joypadBleStickKnobs[1]);
        }
    }

    app->refreshJoypadUi();

end:
    return;
}