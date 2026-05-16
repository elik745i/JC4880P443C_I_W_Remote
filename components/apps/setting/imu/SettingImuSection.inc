void AppSettings::ensureImuScreen(void)
{
#if !APP_SETTINGS_FEATURE_IMU
    return;
#else
    if ((_imuScreen != nullptr) && lv_obj_ready(_imuScreen)) {
        return;
    }

    _imuScreen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(_imuScreen, lv_color_hex(0xE0F2FE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(_imuScreen, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(_imuScreen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *panel = lv_obj_create(_imuScreen);
    lv_obj_set_size(panel, lv_pct(94), lv_pct(96));
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, 12);
    lv_obj_set_style_radius(panel, 20, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_pad_all(panel, 14, 0);
    lv_obj_set_style_pad_row(panel, 12, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(panel, LV_DIR_VER);

    auto createSection = [](lv_obj_t *parent, const char *section_title, const char *hint) {
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

        lv_obj_t *sectionLabel = lv_label_create(section);
        lv_label_set_text(sectionLabel, section_title);
        lv_obj_set_style_text_font(sectionLabel, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(sectionLabel, lv_color_hex(0x0F172A), 0);

        lv_obj_t *hintLabel = lv_label_create(section);
        lv_obj_set_width(hintLabel, lv_pct(100));
        lv_label_set_long_mode(hintLabel, LV_LABEL_LONG_WRAP);
        lv_label_set_text(hintLabel, hint);
        lv_obj_set_style_text_font(hintLabel, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(hintLabel, lv_color_hex(0x475569), 0);
        return section;
    };

    auto createDropdownRow = [this](lv_obj_t *parent, const char *row_title, const char *options, lv_obj_t **dropdown_out) {
        lv_obj_t *row = create_settings_toggle_row(parent, row_title);
        lv_obj_t *dropdown = lv_dropdown_create(row);
        lv_dropdown_set_options_static(dropdown, options);
        lv_obj_set_width(dropdown, 210);
        lv_obj_align(dropdown, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_event_cb(dropdown, onImuConfigChangedEventCallback, LV_EVENT_VALUE_CHANGED, this);
        if (dropdown_out != nullptr) {
            *dropdown_out = dropdown;
        }
        return row;
    };

    auto createTextRow = [this](lv_obj_t *parent, const char *row_title, const char *placeholder, lv_obj_t **textarea_out) {
        lv_obj_t *row = create_settings_toggle_row(parent, row_title);
        lv_obj_t *textarea = lv_textarea_create(row);
        lv_obj_set_size(textarea, 160, 46);
        lv_obj_align(textarea, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_textarea_set_one_line(textarea, true);
        lv_textarea_set_placeholder_text(textarea, placeholder);
        lv_obj_set_style_radius(textarea, 14, 0);
        lv_obj_set_style_border_width(textarea, 1, 0);
        lv_obj_set_style_border_color(textarea, lv_color_hex(0xCBD5E1), 0);
        lv_obj_set_style_bg_color(textarea, lv_color_hex(0xF8FAFC), 0);
        lv_obj_set_style_pad_left(textarea, 12, 0);
        lv_obj_set_style_pad_right(textarea, 12, 0);
        if (textarea_out != nullptr) {
            *textarea_out = textarea;
        }
        return row;
    };

    auto createTelemetryCard = [](lv_obj_t *parent, const char *title, lv_obj_t **value_out) {
        lv_obj_t *card = lv_obj_create(parent);
        lv_obj_set_size(card, 104, 74);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(card, 18, 0);
        lv_obj_set_style_border_width(card, 0, 0);
        lv_obj_set_style_bg_color(card, lv_color_hex(0xE2E8F0), 0);
        lv_obj_set_style_pad_all(card, 10, 0);

        lv_obj_t *titleLabel = lv_label_create(card);
        lv_label_set_text(titleLabel, title);
        lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(titleLabel, lv_color_hex(0x475569), 0);
        lv_obj_align(titleLabel, LV_ALIGN_TOP_LEFT, 0, 0);

        lv_obj_t *valueLabel = lv_label_create(card);
        lv_label_set_text(valueLabel, "--");
        lv_obj_set_style_text_font(valueLabel, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(valueLabel, lv_color_hex(0x0F172A), 0);
        lv_obj_align(valueLabel, LV_ALIGN_BOTTOM_LEFT, 0, 0);
        if (value_out != nullptr) {
            *value_out = valueLabel;
        }
        return card;
    };

    auto createSensorChip = [](lv_obj_t *parent, const char *text, lv_obj_t **dot_out, lv_obj_t **label_out) {
        lv_obj_t *chip = lv_obj_create(parent);
        lv_obj_set_height(chip, LV_SIZE_CONTENT);
        lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(chip, 16, 0);
        lv_obj_set_style_border_width(chip, 0, 0);
        lv_obj_set_style_bg_color(chip, lv_color_hex(0xE2E8F0), 0);
        lv_obj_set_style_pad_left(chip, 10, 0);
        lv_obj_set_style_pad_right(chip, 10, 0);
        lv_obj_set_style_pad_top(chip, 8, 0);
        lv_obj_set_style_pad_bottom(chip, 8, 0);
        lv_obj_set_style_pad_column(chip, 8, 0);
        lv_obj_set_flex_flow(chip, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(chip, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *dot = lv_obj_create(chip);
        lv_obj_set_size(dot, 12, 12);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(0x94A3B8), 0);

        lv_obj_t *label = lv_label_create(chip);
        lv_label_set_text(label, text);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0x334155), 0);

        if (dot_out != nullptr) {
            *dot_out = dot;
        }
        if (label_out != nullptr) {
            *label_out = label;
        }
        return chip;
    };

    lv_obj_t *generalSection = createSection(panel,
                                             "IMU",
                                             "Default profile is BMI160 on JP1: SDA=GPIO31, SCL=GPIO30, INT1=GPIO50, INT2=GPIO51. Only one of IMU, LoRa, or Local Controller may be active at a time.");
    lv_obj_t *enabledRow = create_settings_toggle_row(generalSection, "Enable IMU");
    _imuEnabledSwitch = lv_switch_create(enabledRow);
    lv_obj_align(_imuEnabledSwitch, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(_imuEnabledSwitch, onImuConfigChangedEventCallback, LV_EVENT_VALUE_CHANGED, this);
    createDropdownRow(generalSection, "IMU Model", kImuModelOptionsText, &_imuModelDropdown);
    createDropdownRow(generalSection, "Bus Type", kImuBusOptionsText, &_imuBusDropdown);

    _imuPowerHintLabel = lv_label_create(generalSection);
    lv_obj_set_width(_imuPowerHintLabel, lv_pct(100));
    lv_label_set_long_mode(_imuPowerHintLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_imuPowerHintLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_imuPowerHintLabel, lv_color_hex(0x0369A1), 0);

    lv_obj_t *liveSection = createSection(panel,
                                          "Live Motion",
                                          "This panel streams the latest IMU sample and renders the scene from LVGL arcs, gradients, and shadows so it stays on the board's existing esp_lvgl_port display path. Extra chips light up when magnetometer, barometer, or fusion data are present.");

    _imuLiveScene = lv_obj_create(liveSection);
    lv_obj_set_size(_imuLiveScene, lv_pct(100), 430);
    lv_obj_clear_flag(_imuLiveScene, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(_imuLiveScene, 22, 0);
    lv_obj_set_style_border_width(_imuLiveScene, 1, 0);
    lv_obj_set_style_border_color(_imuLiveScene, lv_color_hex(0x1E3A5F), 0);
    lv_obj_set_style_bg_color(_imuLiveScene, lv_color_hex(0x07111F), 0);
    lv_obj_set_style_bg_grad_color(_imuLiveScene, lv_color_hex(0x16314F), 0);
    lv_obj_set_style_bg_grad_dir(_imuLiveScene, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_shadow_width(_imuLiveScene, 20, 0);
    lv_obj_set_style_shadow_color(_imuLiveScene, lv_color_hex(0x020617), 0);
    lv_obj_set_style_shadow_opa(_imuLiveScene, LV_OPA_40, 0);

    lv_obj_t *sceneHeader = lv_obj_create(_imuLiveScene);
    lv_obj_set_size(sceneHeader, lv_pct(100), 42);
    lv_obj_align(sceneHeader, LV_ALIGN_TOP_MID, 0, 12);
    lv_obj_clear_flag(sceneHeader, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(sceneHeader, 14, 0);
    lv_obj_set_style_border_width(sceneHeader, 1, 0);
    lv_obj_set_style_border_color(sceneHeader, lv_color_hex(0x4ADEFF), 0);
    lv_obj_set_style_bg_color(sceneHeader, lv_color_hex(0x081623), 0);
    lv_obj_set_style_bg_opa(sceneHeader, LV_OPA_70, 0);

    lv_obj_t *sceneHeaderIcon = lv_obj_create(sceneHeader);
    lv_obj_set_size(sceneHeaderIcon, 24, 24);
    lv_obj_align(sceneHeaderIcon, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_clear_flag(sceneHeaderIcon, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(sceneHeaderIcon, 8, 0);
    lv_obj_set_style_border_width(sceneHeaderIcon, 1, 0);
    lv_obj_set_style_border_color(sceneHeaderIcon, lv_color_hex(0x67E8F9), 0);
    lv_obj_set_style_bg_color(sceneHeaderIcon, lv_color_hex(0x0E2236), 0);
    lv_obj_set_style_bg_opa(sceneHeaderIcon, LV_OPA_80, 0);

    lv_obj_t *sceneHeaderIconCore = lv_obj_create(sceneHeaderIcon);
    lv_obj_set_size(sceneHeaderIconCore, 10, 10);
    lv_obj_center(sceneHeaderIconCore);
    lv_obj_clear_flag(sceneHeaderIconCore, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(sceneHeaderIconCore, 3, 0);
    lv_obj_set_style_border_width(sceneHeaderIconCore, 1, 0);
    lv_obj_set_style_border_color(sceneHeaderIconCore, lv_color_hex(0xA5F3FC), 0);
    lv_obj_set_style_bg_color(sceneHeaderIconCore, lv_color_hex(0x164E63), 0);

    _imuLiveCaptionLabel = lv_label_create(sceneHeader);
    lv_label_set_text(_imuLiveCaptionLabel, "BMI160");
    lv_obj_set_style_text_font(_imuLiveCaptionLabel, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(_imuLiveCaptionLabel, lv_color_hex(0xF8FAFC), 0);
    lv_obj_align(_imuLiveCaptionLabel, LV_ALIGN_TOP_LEFT, 42, -1);

    lv_obj_t *sceneHeaderSub = lv_label_create(sceneHeader);
    lv_label_set_text(sceneHeaderSub, "MOTION SENSOR TEST");
    lv_obj_set_style_text_font(sceneHeaderSub, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(sceneHeaderSub, lv_color_hex(0x67E8F9), 0);
    lv_obj_align(sceneHeaderSub, LV_ALIGN_BOTTOM_LEFT, 42, 0);

    lv_obj_t *leftPanel = lv_obj_create(_imuLiveScene);
    lv_obj_set_size(leftPanel, lv_pct(100), 104);
    lv_obj_align(leftPanel, LV_ALIGN_TOP_MID, 0, 62);
    lv_obj_clear_flag(leftPanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(leftPanel, 14, 0);
    lv_obj_set_style_border_width(leftPanel, 1, 0);
    lv_obj_set_style_border_color(leftPanel, lv_color_hex(0x2A5679), 0);
    lv_obj_set_style_bg_color(leftPanel, lv_color_hex(0x091522), 0);
    lv_obj_set_style_bg_opa(leftPanel, LV_OPA_60, 0);
    lv_obj_set_style_pad_all(leftPanel, 14, 0);
    lv_obj_set_style_pad_column(leftPanel, 14, 0);
    lv_obj_set_style_pad_row(leftPanel, 6, 0);
    lv_obj_set_flex_flow(leftPanel, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(leftPanel, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *accelColumn = lv_obj_create(leftPanel);
    lv_obj_set_size(accelColumn, 112, LV_SIZE_CONTENT);
    lv_obj_clear_flag(accelColumn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(accelColumn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(accelColumn, 0, 0);
    lv_obj_set_style_pad_all(accelColumn, 0, 0);

    lv_obj_t *gyroColumn = lv_obj_create(leftPanel);
    lv_obj_set_size(gyroColumn, 112, LV_SIZE_CONTENT);
    lv_obj_clear_flag(gyroColumn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(gyroColumn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(gyroColumn, 0, 0);
    lv_obj_set_style_pad_all(gyroColumn, 0, 0);

    lv_obj_t *accelTitle = lv_label_create(accelColumn);
    lv_label_set_text(accelTitle, "ACCELEROMETER");
    lv_obj_set_style_text_font(accelTitle, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(accelTitle, lv_color_hex(0x67E8F9), 0);

    _imuAccelValueLabel = lv_label_create(accelColumn);
    lv_obj_set_width(_imuAccelValueLabel, lv_pct(100));
    lv_label_set_long_mode(_imuAccelValueLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_imuAccelValueLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_imuAccelValueLabel, lv_color_hex(0xF8FAFC), 0);
    lv_obj_align_to(_imuAccelValueLabel, accelTitle, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);

    lv_obj_t *accelMiniGraph = lv_obj_create(accelColumn);
    lv_obj_set_size(accelMiniGraph, lv_pct(100), 34);
    lv_obj_align_to(accelMiniGraph, _imuAccelValueLabel, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);
    lv_obj_clear_flag(accelMiniGraph, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(accelMiniGraph, 10, 0);
    lv_obj_set_style_border_width(accelMiniGraph, 1, 0);
    lv_obj_set_style_border_color(accelMiniGraph, lv_color_hex(0x17314D), 0);
    lv_obj_set_style_bg_color(accelMiniGraph, lv_color_hex(0x06111D), 0);
    lv_obj_set_style_bg_opa(accelMiniGraph, LV_OPA_50, 0);

    for (int i = 0; i < 3; ++i) {
        lv_obj_t *bar = lv_obj_create(accelMiniGraph);
        lv_obj_set_size(bar, 14, 18 - (i * 4));
        lv_obj_align(bar, LV_ALIGN_BOTTOM_LEFT, 12 + (i * 30), -8);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(bar, 2, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_bg_color(bar, i == 1 ? lv_color_hex(0x67E8F9) : lv_color_hex(0x7DD3FC), 0);
        lv_obj_set_style_bg_opa(bar, i == 1 ? LV_OPA_80 : LV_OPA_60, 0);

        static const char *kAxisLabels[] = {"X", "Y", "Z"};
        lv_obj_t *axis = lv_label_create(accelMiniGraph);
        lv_label_set_text(axis, kAxisLabels[i]);
        lv_obj_set_style_text_font(axis, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(axis, lv_color_hex(0x67E8F9), 0);
        lv_obj_align_to(axis, bar, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);
    }

    lv_obj_t *gyroTitle = lv_label_create(gyroColumn);
    lv_label_set_text(gyroTitle, "GYROSCOPE");
    lv_obj_set_style_text_font(gyroTitle, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(gyroTitle, lv_color_hex(0x67E8F9), 0);

    _imuGyroValueLabel = lv_label_create(gyroColumn);
    lv_obj_set_width(_imuGyroValueLabel, lv_pct(100));
    lv_label_set_long_mode(_imuGyroValueLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_imuGyroValueLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_imuGyroValueLabel, lv_color_hex(0xF8FAFC), 0);
    lv_obj_align_to(_imuGyroValueLabel, gyroTitle, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);

    lv_obj_t *rightPanel = lv_obj_create(_imuLiveScene);
    lv_obj_set_size(rightPanel, lv_pct(100), 108);
    lv_obj_align(rightPanel, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_clear_flag(rightPanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(rightPanel, 14, 0);
    lv_obj_set_style_border_width(rightPanel, 1, 0);
    lv_obj_set_style_border_color(rightPanel, lv_color_hex(0x2A5679), 0);
    lv_obj_set_style_bg_color(rightPanel, lv_color_hex(0x091522), 0);
    lv_obj_set_style_bg_opa(rightPanel, LV_OPA_60, 0);
    lv_obj_set_style_pad_all(rightPanel, 14, 0);
    lv_obj_set_style_pad_column(rightPanel, 12, 0);
    lv_obj_set_style_pad_row(rightPanel, 6, 0);

    lv_obj_t *orientationColumn = lv_obj_create(rightPanel);
    lv_obj_set_size(orientationColumn, 124, 76);
    lv_obj_align(orientationColumn, LV_ALIGN_LEFT_MID, 0, -2);
    lv_obj_clear_flag(orientationColumn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(orientationColumn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(orientationColumn, 0, 0);
    lv_obj_set_style_pad_all(orientationColumn, 0, 0);

    lv_obj_t *orientationTitle = lv_label_create(orientationColumn);
    lv_label_set_text(orientationTitle, "ORIENTATION");
    lv_obj_set_style_text_font(orientationTitle, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(orientationTitle, lv_color_hex(0x67E8F9), 0);
    lv_obj_align(orientationTitle, LV_ALIGN_TOP_LEFT, 0, 0);

    auto createOrientationRow = [](lv_obj_t *parent, const char *title, lv_coord_t y, lv_obj_t **value_out) {
        lv_obj_t *label = lv_label_create(parent);
        lv_label_set_text(label, title);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0x67E8F9), 0);
        lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, y);

        lv_obj_t *value = lv_label_create(parent);
        lv_label_set_text(value, "+0.0 deg");
        lv_obj_set_style_text_font(value, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(value, lv_color_hex(0xF8FAFC), 0);
        lv_obj_align(value, LV_ALIGN_TOP_RIGHT, 0, y - 2);

        if (value_out != nullptr) {
            *value_out = value;
        }
    };

    createOrientationRow(orientationColumn, "ROLL", 20, &_imuRollValueLabel);
    createOrientationRow(orientationColumn, "PITCH", 40, &_imuPitchValueLabel);
    createOrientationRow(orientationColumn, "YAW", 60, &_imuYawValueLabel);

    _imuHeadingArc = lv_arc_create(rightPanel);
    lv_obj_set_size(_imuHeadingArc, 60, 60);
    lv_obj_align(_imuHeadingArc, LV_ALIGN_RIGHT_MID, -10, -8);
    lv_arc_set_range(_imuHeadingArc, 0, 360);
    lv_arc_set_rotation(_imuHeadingArc, 270);
    lv_arc_set_bg_angles(_imuHeadingArc, 0, 360);
    lv_arc_set_value(_imuHeadingArc, 0);
    lv_obj_remove_style(_imuHeadingArc, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(_imuHeadingArc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(_imuHeadingArc, 5, LV_PART_MAIN);
    lv_obj_set_style_arc_color(_imuHeadingArc, lv_color_hex(0x1E3A5F), LV_PART_MAIN);
    lv_obj_set_style_arc_width(_imuHeadingArc, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(_imuHeadingArc, lv_color_hex(0x38BDF8), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(_imuHeadingArc, LV_OPA_TRANSP, 0);

    _imuHeadingValueLabel = lv_label_create(rightPanel);
    lv_label_set_text(_imuHeadingValueLabel, "000 deg");
    lv_obj_set_style_text_font(_imuHeadingValueLabel, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(_imuHeadingValueLabel, lv_color_hex(0xE0F2FE), 0);
    lv_obj_align_to(_imuHeadingValueLabel, _imuHeadingArc, LV_ALIGN_CENTER, 0, 0);

    _imuLiveStatusLabel = lv_label_create(rightPanel);
    lv_obj_set_width(_imuLiveStatusLabel, 96);
    lv_label_set_long_mode(_imuLiveStatusLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_imuLiveStatusLabel, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(_imuLiveStatusLabel, lv_color_hex(0x67E8F9), 0);
    lv_obj_align(_imuLiveStatusLabel, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    lv_obj_t *chamber = lv_obj_create(_imuLiveScene);
    lv_obj_set_size(chamber, lv_pct(100), 150);
    lv_obj_align(chamber, LV_ALIGN_CENTER, 0, 14);
    lv_obj_clear_flag(chamber, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(chamber, 18, 0);
    lv_obj_set_style_border_width(chamber, 1, 0);
    lv_obj_set_style_border_color(chamber, lv_color_hex(0x285E8E), 0);
    lv_obj_set_style_bg_color(chamber, lv_color_hex(0x08131F), 0);
    lv_obj_set_style_bg_grad_color(chamber, lv_color_hex(0x13273C), 0);
    lv_obj_set_style_bg_grad_dir(chamber, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(chamber, LV_OPA_80, 0);

    for (int i = 0; i < 5; ++i) {
        lv_obj_t *vLine = lv_obj_create(chamber);
        lv_obj_set_size(vLine, 1, 132);
        lv_obj_align(vLine, LV_ALIGN_TOP_LEFT, 22 + (i * 48), 8);
        lv_obj_clear_flag(vLine, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_border_width(vLine, 0, 0);
        lv_obj_set_style_bg_color(vLine, lv_color_hex(0x31597E), 0);
        lv_obj_set_style_bg_opa(vLine, i == 2 ? LV_OPA_40 : LV_OPA_20, 0);

        lv_obj_t *hLine = lv_obj_create(chamber);
        lv_obj_set_size(hLine, 220, 1);
        lv_obj_align(hLine, LV_ALIGN_TOP_LEFT, 8, 18 + (i * 28));
        lv_obj_clear_flag(hLine, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_border_width(hLine, 0, 0);
        lv_obj_set_style_bg_color(hLine, lv_color_hex(0x31597E), 0);
        lv_obj_set_style_bg_opa(hLine, i == 2 ? LV_OPA_40 : LV_OPA_20, 0);
    }

    lv_obj_t *ceilingGlow = lv_obj_create(chamber);
    lv_obj_set_size(ceilingGlow, 212, 3);
    lv_obj_align(ceilingGlow, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_clear_flag(ceilingGlow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(ceilingGlow, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ceilingGlow, 0, 0);
    lv_obj_set_style_bg_color(ceilingGlow, lv_color_hex(0x7DD3FC), 0);
    lv_obj_set_style_bg_opa(ceilingGlow, LV_OPA_70, 0);
    lv_obj_set_style_shadow_width(ceilingGlow, 18, 0);
    lv_obj_set_style_shadow_color(ceilingGlow, lv_color_hex(0x7DD3FC), 0);
    lv_obj_set_style_shadow_opa(ceilingGlow, LV_OPA_60, 0);

    lv_obj_t *floorPlane = lv_obj_create(chamber);
    lv_obj_set_size(floorPlane, 170, 28);
    lv_obj_align(floorPlane, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_clear_flag(floorPlane, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(floorPlane, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(floorPlane, 1, 0);
    lv_obj_set_style_border_color(floorPlane, lv_color_hex(0x2D597C), 0);
    lv_obj_set_style_bg_color(floorPlane, lv_color_hex(0x16324E), 0);
    lv_obj_set_style_bg_opa(floorPlane, LV_OPA_20, 0);

    lv_obj_t *leftGhost = lv_obj_create(chamber);
    lv_obj_set_size(leftGhost, 22, 22);
    lv_obj_align(leftGhost, LV_ALIGN_BOTTOM_LEFT, 48, -20);
    lv_obj_clear_flag(leftGhost, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(leftGhost, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(leftGhost, 1, 0);
    lv_obj_set_style_border_color(leftGhost, lv_color_hex(0xBFE8FF), 0);
    lv_obj_set_style_bg_color(leftGhost, lv_color_hex(0x7DD3FC), 0);
    lv_obj_set_style_bg_opa(leftGhost, LV_OPA_20, 0);

    lv_obj_t *rightGhost = lv_obj_create(chamber);
    lv_obj_set_size(rightGhost, 26, 26);
    lv_obj_align(rightGhost, LV_ALIGN_BOTTOM_RIGHT, -38, -18);
    lv_obj_clear_flag(rightGhost, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(rightGhost, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(rightGhost, 1, 0);
    lv_obj_set_style_border_color(rightGhost, lv_color_hex(0xBFE8FF), 0);
    lv_obj_set_style_bg_color(rightGhost, lv_color_hex(0x7DD3FC), 0);
    lv_obj_set_style_bg_opa(rightGhost, LV_OPA_20, 0);

    _imuMotionShadow = lv_obj_create(chamber);
    lv_obj_set_size(_imuMotionShadow, 42, 14);
    lv_obj_align(_imuMotionShadow, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_obj_clear_flag(_imuMotionShadow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(_imuMotionShadow, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(_imuMotionShadow, 0, 0);
    lv_obj_set_style_bg_color(_imuMotionShadow, lv_color_hex(0x020617), 0);
    lv_obj_set_style_bg_opa(_imuMotionShadow, LV_OPA_40, 0);

    _imuMotionDot = lv_obj_create(chamber);
    lv_obj_set_size(_imuMotionDot, 46, 46);
    lv_obj_clear_flag(_imuMotionDot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(_imuMotionDot, 0, 0);
    lv_obj_set_flex_flow(_imuMotionDot, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_imuMotionDot, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_radius(_imuMotionDot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(_imuMotionDot, 1, 0);
    lv_obj_set_style_border_color(_imuMotionDot, lv_color_hex(0xE0F2FE), 0);
    lv_obj_set_style_bg_color(_imuMotionDot, lv_color_hex(0x38BDF8), 0);
    lv_obj_set_style_bg_grad_color(_imuMotionDot, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_bg_grad_dir(_imuMotionDot, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(_imuMotionDot, LV_OPA_60, 0);
    lv_obj_set_style_shadow_width(_imuMotionDot, 20, 0);
    lv_obj_set_style_shadow_opa(_imuMotionDot, LV_OPA_60, 0);
    lv_obj_set_style_shadow_color(_imuMotionDot, lv_color_hex(0x0EA5E9), 0);
    lv_obj_set_pos(_imuMotionDot, 92, 42);

    lv_obj_t *ballHighlight = lv_obj_create(_imuMotionDot);
    lv_obj_set_size(ballHighlight, 14, 10);
    lv_obj_align(ballHighlight, LV_ALIGN_TOP_LEFT, 8, 6);
    lv_obj_clear_flag(ballHighlight, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(ballHighlight, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ballHighlight, 0, 0);
    lv_obj_set_style_bg_color(ballHighlight, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(ballHighlight, LV_OPA_70, 0);

    lv_obj_t *ballBand = lv_obj_create(_imuMotionDot);
    lv_obj_set_size(ballBand, 28, 6);
    lv_obj_align(ballBand, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_clear_flag(ballBand, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(ballBand, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ballBand, 0, 0);
    lv_obj_set_style_bg_color(ballBand, lv_color_hex(0x0284C7), 0);
    lv_obj_set_style_bg_opa(ballBand, LV_OPA_60, 0);

    _imuMotionTempLabel = lv_label_create(_imuMotionDot);
    lv_label_set_text(_imuMotionTempLabel, "--.-");
    lv_obj_set_width(_imuMotionTempLabel, lv_pct(100));
    lv_obj_set_style_text_font(_imuMotionTempLabel, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(_imuMotionTempLabel, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_text_align(_imuMotionTempLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(_imuMotionTempLabel);

    lv_obj_t *sensorRow = lv_obj_create(liveSection);
    lv_obj_set_width(sensorRow, lv_pct(100));
    lv_obj_set_height(sensorRow, LV_SIZE_CONTENT);
    lv_obj_clear_flag(sensorRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(sensorRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sensorRow, 0, 0);
    lv_obj_set_style_pad_all(sensorRow, 0, 0);
    lv_obj_set_style_pad_column(sensorRow, 8, 0);
    lv_obj_set_style_pad_row(sensorRow, 8, 0);
    lv_obj_set_flex_flow(sensorRow, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(sensorRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    createSensorChip(sensorRow, "ACC", &_imuSensorIndicatorDots[0], &_imuSensorIndicatorLabels[0]);
    createSensorChip(sensorRow, "GYR", &_imuSensorIndicatorDots[1], &_imuSensorIndicatorLabels[1]);
    createSensorChip(sensorRow, "MAG", &_imuSensorIndicatorDots[2], &_imuSensorIndicatorLabels[2]);
    createSensorChip(sensorRow, "BAR", &_imuSensorIndicatorDots[3], &_imuSensorIndicatorLabels[3]);
    createSensorChip(sensorRow, "FUS", &_imuSensorIndicatorDots[4], &_imuSensorIndicatorLabels[4]);

    _imuMagValueLabel = lv_label_create(liveSection);
    lv_obj_set_width(_imuMagValueLabel, lv_pct(100));
    lv_label_set_long_mode(_imuMagValueLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_imuMagValueLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_imuMagValueLabel, lv_color_hex(0x0F172A), 0);

    _imuEnvValueLabel = lv_label_create(liveSection);
    lv_obj_set_width(_imuEnvValueLabel, lv_pct(100));
    lv_label_set_long_mode(_imuEnvValueLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_imuEnvValueLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_imuEnvValueLabel, lv_color_hex(0x0F172A), 0);

    lv_obj_t *i2cSection = createSection(panel,
                                         "I2C Wiring",
                                         "Pins can be remapped across the JP1 GPIO list. When LoRa is active, the E22-reserved pins are blocked for IMU use.");
    createDropdownRow(i2cSection, "I2C SDA", kImuPinOptionsText, &_imuI2cSdaDropdown);
    createDropdownRow(i2cSection, "I2C SCL", kImuPinOptionsText, &_imuI2cSclDropdown);
    createDropdownRow(i2cSection, "I2C Address", kImuAddressOptionsText, &_imuI2cAddressDropdown);
    createTextRow(i2cSection, "Manual Hex Address", "68", &_imuI2cAddressTextArea);
    createDropdownRow(i2cSection, "INT1 / INT", kImuPinOptionsText, &_imuIntDropdown);
    createDropdownRow(i2cSection, "INT2 / DRDY", kImuPinOptionsText, &_imuDrdyDropdown);

    lv_obj_t *diagSection = createSection(panel,
                                          "Diagnostics",
                                          "Scan validates the selected I2C wiring. Test attempts a live sample through the IMU service.");
    _imuScanButton = lv_btn_create(diagSection);
    lv_obj_set_size(_imuScanButton, lv_pct(100), 54);
    lv_obj_set_style_radius(_imuScanButton, 16, 0);
    lv_obj_set_style_border_width(_imuScanButton, 0, 0);
    lv_obj_add_event_cb(_imuScanButton, onImuScanClickedEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_t *scanLabel = lv_label_create(_imuScanButton);
    lv_label_set_text(scanLabel, "Scan I2C");
    lv_obj_center(scanLabel);

    _imuTestButton = lv_btn_create(diagSection);
    lv_obj_set_size(_imuTestButton, lv_pct(100), 54);
    lv_obj_set_style_radius(_imuTestButton, 16, 0);
    lv_obj_set_style_border_width(_imuTestButton, 0, 0);
    lv_obj_add_event_cb(_imuTestButton, onImuTestClickedEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_t *testLabel = lv_label_create(_imuTestButton);
    lv_label_set_text(testLabel, "Test IMU");
    lv_obj_center(testLabel);

    _imuZeroButton = lv_btn_create(diagSection);
    lv_obj_set_size(_imuZeroButton, lv_pct(100), 54);
    lv_obj_set_style_radius(_imuZeroButton, 16, 0);
    lv_obj_set_style_border_width(_imuZeroButton, 0, 0);
    lv_obj_add_event_cb(_imuZeroButton, onImuZeroClickedEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_t *zeroLabel = lv_label_create(_imuZeroButton);
    lv_label_set_text(zeroLabel, "Zero Current Orientation");
    lv_obj_center(zeroLabel);

    lv_obj_t *saveButton = lv_btn_create(diagSection);
    lv_obj_set_size(saveButton, lv_pct(100), 54);
    lv_obj_set_style_radius(saveButton, 16, 0);
    lv_obj_set_style_border_width(saveButton, 0, 0);
    lv_obj_add_event_cb(saveButton, onImuSaveClickedEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_t *saveLabel = lv_label_create(saveButton);
    lv_label_set_text(saveLabel, "Save IMU Settings");
    lv_obj_center(saveLabel);

    _imuStatusLabel = lv_label_create(diagSection);
    lv_obj_set_width(_imuStatusLabel, lv_pct(100));
    lv_label_set_long_mode(_imuStatusLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_imuStatusLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_imuStatusLabel, lv_color_hex(0x475569), 0);

    _imuInfoLabel = lv_label_create(diagSection);
    lv_obj_set_width(_imuInfoLabel, lv_pct(100));
    lv_label_set_long_mode(_imuInfoLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(_imuInfoLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_imuInfoLabel, lv_color_hex(0x475569), 0);

    _screen_list[UI_IMU_SETTING_INDEX] = _imuScreen;
    lv_obj_add_event_cb(_imuScreen, onScreenLoadEventCallback, LV_EVENT_SCREEN_LOADED, this);
#endif
}

void AppSettings::refreshImuUi(void)
{
#if !APP_SETTINGS_FEATURE_IMU
    return;
#else
    if ((_imuScreen == nullptr) || !lv_obj_ready(_imuScreen)) {
        return;
    }

    jc4880::imu::ImuConfig config = {};
    jc4880::imu::ImuService::instance().loadConfig(config);
    const bool local_controller_active = is_local_controller_backend_active();
    const bool lora_active = []() {
        jc4880::lora_mesh::StoredState state = {};
        return jc4880::lora_mesh::load_stored_state(state) && state.settings.radio_enabled;
    }();
    const jc4880::imu::ImuModelInfo *model_info = jc4880::imu::find_imu_model_info(config.model);

    if (config.enabled) {
        lv_obj_add_state(_imuEnabledSwitch, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(_imuEnabledSwitch, LV_STATE_CHECKED);
    }

    lv_dropdown_set_selected(_imuModelDropdown, imu_dropdown_index_from_model(config.model));
    lv_dropdown_set_selected(_imuBusDropdown,
                             findDropdownIndexForValue(kImuBusOptions,
                                                       sizeof(kImuBusOptions) / sizeof(kImuBusOptions[0]),
                                                       static_cast<int32_t>(config.busType)));
    lv_dropdown_set_selected(_imuI2cSdaDropdown, imu_pin_choice_index(config.i2cSda));
    lv_dropdown_set_selected(_imuI2cSclDropdown, imu_pin_choice_index(config.i2cScl));
    lv_dropdown_set_selected(_imuI2cAddressDropdown,
                             findDropdownIndexForValue(kImuAddressOptions,
                                                       sizeof(kImuAddressOptions) / sizeof(kImuAddressOptions[0]),
                                                       config.i2cAddress));
    lv_dropdown_set_selected(_imuIntDropdown, imu_pin_choice_index(config.intPin));
    lv_dropdown_set_selected(_imuDrdyDropdown, imu_pin_choice_index(config.drdyPin));

    char address_text[8] = {};
    std::snprintf(address_text, sizeof(address_text), "%02X", static_cast<unsigned>(config.i2cAddress));
    lv_textarea_set_text(_imuI2cAddressTextArea, address_text);

    if (model_info != nullptr) {
        char power_hint[160] = {};
        std::snprintf(power_hint,
                      sizeof(power_hint),
                      "Power wiring: %s. Magnetometer: %s. Fusion: %s.%s",
                      model_info->powerHint,
                      model_info->hasMagnetometer ? "yes" : "no",
                      model_info->hasFusion ? "yes" : "no",
                      model_info->placeholderOnly ? " Driver is staged as a placeholder for this model." : "");
        lv_label_set_text(_imuPowerHintLabel, power_hint);
        if (!model_info->hasMagnetometer) {
            lv_label_set_text(_imuStatusLabel, "Compass heading unavailable without magnetometer.");
        }
    }

    if (local_controller_active) {
        lv_label_set_text(_imuInfoLabel,
                          "Local Controller is active. Enabling IMU will turn Local Controller off so JP1 pins can be reassigned safely.");
    } else if (lora_active) {
        lv_label_set_text(_imuInfoLabel,
                          "LoRa is active. Enabling IMU will turn LoRa off because the default BMI160 wiring overlaps the E22 JP1 pin profile.");
    } else if (config.enabled) {
        lv_label_set_text(_imuInfoLabel,
                          "IMU is enabled. LoRa and Local Controller remain off while IMU owns its selected JP1 pins.");
    } else {
        lv_label_set_text(_imuInfoLabel,
                          "IMU is disabled. The default profile is BMI160 over I2C on GPIO31/GPIO30 with optional interrupts on GPIO50/GPIO51.");
    }

    refreshImuLiveUi();
#endif
}

void AppSettings::refreshImuLiveUi(void)
{
#if !APP_SETTINGS_FEATURE_IMU
    return;
#else
    if ((_imuScreen == nullptr) || !lv_obj_ready(_imuScreen) || (_imuLiveScene == nullptr)) {
        return;
    }

    auto setSensorIndicator = [](lv_obj_t *dot, lv_obj_t *label, const char *text, bool active, lv_color_t color) {
        if (lv_obj_ready(dot)) {
            lv_obj_set_style_bg_color(dot, active ? color : lv_color_hex(0x94A3B8), 0);
            lv_obj_set_style_bg_opa(dot, active ? LV_OPA_COVER : LV_OPA_50, 0);
        }
        if (lv_obj_ready(label)) {
            lv_label_set_text(label, text);
            lv_obj_set_style_text_color(label, active ? lv_color_hex(0x0F172A) : lv_color_hex(0x64748B), 0);
        }
    };

    jc4880::imu::ImuConfig config = {};
    jc4880::imu::ImuService::instance().loadConfig(config);
    jc4880::imu::ImuSample sample = {};
    std::string live_status;
    bool sample_ok = false;

    if (!config.enabled || (config.model == jc4880::imu::ImuModel::IMU_NONE)) {
        jc4880::imu::ImuService::instance().stop();
        live_status = "Live stream idle. Enable IMU to sample motion, heading, and auxiliary sensors.";
    } else {
        sample_ok = jc4880::imu::ImuService::instance().read(sample);
        if (!sample_ok) {
            if (jc4880::imu::ImuService::instance().begin(&config)) {
                sample_ok = jc4880::imu::ImuService::instance().read(sample);
            }
        }

        if (sample_ok) {
            char status[160] = {};
                std::snprintf(status,
                              sizeof(status),
                              "%s\n%.0f Hz %s\n%s",
                              jc4880::imu::imu_model_label(config.model),
                              static_cast<float>(config.sampleRateHz),
                              jc4880::imu::imu_bus_type_label(config.busType),
                              sample.hasMag ? "Compass fused" : "Yaw drift expected");
            live_status = status;
        } else {
            const jc4880::imu::ImuModelInfo *info = jc4880::imu::find_imu_model_info(config.model);
            if ((info != nullptr) && info->placeholderOnly) {
                live_status = std::string(info->label) + "\nDriver staged\nNo live stream";
            } else {
                live_status = "Live sample\nunavailable\nCheck wiring";
            }
        }
    }

    const float roll = sample_ok ? sample.roll : 0.0f;
    const float pitch = sample_ok ? sample.pitch : 0.0f;
    const float yaw = sample_ok ? sample.yaw : 0.0f;
    const int yaw_degrees = static_cast<int>(std::lround(std::fmod((yaw + 360.0f), 360.0f)));

    if (lv_obj_ready(_imuHeadingArc)) {
        lv_arc_set_value(_imuHeadingArc, yaw_degrees);
    }
    if (lv_obj_ready(_imuHeadingValueLabel)) {
        char heading[24] = {};
        std::snprintf(heading, sizeof(heading), "%03d deg", yaw_degrees);
        lv_label_set_text(_imuHeadingValueLabel, heading);
    }
    if (lv_obj_ready(_imuRollValueLabel)) {
        char text[24] = {};
        std::snprintf(text, sizeof(text), "%+.1f deg", roll);
        lv_label_set_text(_imuRollValueLabel, text);
    }
    if (lv_obj_ready(_imuPitchValueLabel)) {
        char text[24] = {};
        std::snprintf(text, sizeof(text), "%+.1f deg", pitch);
        lv_label_set_text(_imuPitchValueLabel, text);
    }
    if (lv_obj_ready(_imuYawValueLabel)) {
        char text[24] = {};
        std::snprintf(text, sizeof(text), "%+.1f deg", yaw);
        lv_label_set_text(_imuYawValueLabel, text);
    }
    if (lv_obj_ready(_imuLiveCaptionLabel)) {
        lv_label_set_text(_imuLiveCaptionLabel, jc4880::imu::imu_model_label(config.model));
    }
    if (lv_obj_ready(_imuLiveStatusLabel)) {
        lv_label_set_text(_imuLiveStatusLabel, live_status.c_str());
    }
    if (lv_obj_ready(_imuAccelValueLabel)) {
        char text[96] = {};
        if (sample_ok && sample.hasAccel) {
            std::snprintf(text, sizeof(text), "X  %+.2f g\nY  %+.2f g\nZ  %+.2f g", sample.ax, sample.ay, sample.az);
        } else {
            std::snprintf(text, sizeof(text), "X  --.-- g\nY  --.-- g\nZ  --.-- g");
        }
        lv_label_set_text(_imuAccelValueLabel, text);
    }
    if (lv_obj_ready(_imuGyroValueLabel)) {
        char text[96] = {};
        if (sample_ok && sample.hasGyro) {
            std::snprintf(text, sizeof(text), "X  %+.1f deg/s\nY  %+.1f deg/s\nZ  %+.1f deg/s", sample.gx, sample.gy, sample.gz);
        } else {
            std::snprintf(text, sizeof(text), "X  --.- deg/s\nY  --.- deg/s\nZ  --.- deg/s");
        }
        lv_label_set_text(_imuGyroValueLabel, text);
    }
    if (lv_obj_ready(_imuMagValueLabel)) {
        char text[112] = {};
        if (sample_ok && sample.hasMag) {
            std::snprintf(text, sizeof(text), "Mag: X=%+.1f  Y=%+.1f  Z=%+.1f uT", sample.mx, sample.my, sample.mz);
        } else {
            std::snprintf(text, sizeof(text), "Mag: unavailable on this reading or this IMU model");
        }
        lv_label_set_text(_imuMagValueLabel, text);
    }
    if (lv_obj_ready(_imuEnvValueLabel)) {
        char text[112] = {};
        if (sample_ok && sample.hasBarometer) {
            std::snprintf(text,
                          sizeof(text),
                          "Temp: %.1f C  Pressure: %.1f hPa  Alt: %.1f m",
                          sample.temperature,
                          sample.pressure,
                          sample.altitude);
        } else if (sample_ok) {
            std::snprintf(text, sizeof(text), "Temp: %.1f C  Barometer: unavailable", sample.temperature);
        } else {
            std::snprintf(text, sizeof(text), "Temp / pressure stream unavailable");
        }
        lv_label_set_text(_imuEnvValueLabel, text);
    }

    setSensorIndicator(_imuSensorIndicatorDots[0], _imuSensorIndicatorLabels[0], "ACC", sample_ok && sample.hasAccel, lv_color_hex(0x22C55E));
    setSensorIndicator(_imuSensorIndicatorDots[1], _imuSensorIndicatorLabels[1], "GYR", sample_ok && sample.hasGyro, lv_color_hex(0x38BDF8));
    setSensorIndicator(_imuSensorIndicatorDots[2], _imuSensorIndicatorLabels[2], "MAG", sample_ok && sample.hasMag, lv_color_hex(0xF59E0B));
    setSensorIndicator(_imuSensorIndicatorDots[3], _imuSensorIndicatorLabels[3], "BAR", sample_ok && sample.hasBarometer, lv_color_hex(0xA855F7));
    setSensorIndicator(_imuSensorIndicatorDots[4], _imuSensorIndicatorLabels[4], "FUS", sample_ok && sample.hasFusion, lv_color_hex(0xF43F5E));

    if (lv_obj_ready(_imuMotionDot)) {
        const float temperature_c = sample_ok ? sample.temperature : 24.0f;
        const float temp_mix = std::clamp((temperature_c - 18.0f) / 22.0f, 0.0f, 1.0f);
        const lv_color_t temp_core_color = lv_color_mix(lv_color_hex(0xFB7185), lv_color_hex(0x38BDF8), static_cast<uint8_t>(255.0f * (1.0f - temp_mix)));
        const lv_color_t temp_edge_color = lv_color_mix(lv_color_hex(0xFFF1F2), lv_color_hex(0xE0F2FE), static_cast<uint8_t>(255.0f * (1.0f - temp_mix)));

        if (sample_ok && sample.hasAccel) {
            const float center_x = 92.0f;
            const float center_y = 42.0f;
            if (!_imuBallDynamicsInitialized) {
                _imuBallPosX = center_x;
                _imuBallPosY = center_y;
                _imuBallPosZ = 0.0f;
                _imuBallVelX = 0.0f;
                _imuBallVelY = 0.0f;
                _imuBallVelZ = 0.0f;
                _imuBallPrevAccelX = sample.ax;
                _imuBallPrevAccelY = sample.ay;
                _imuBallPrevAccelZ = sample.az;
                _imuBallDynamicsInitialized = true;
            }

            const float accel_impulse_x = std::clamp((sample.ax - _imuBallPrevAccelX) * 28.0f, -6.5f, 6.5f);
            const float accel_impulse_y = std::clamp((_imuBallPrevAccelY - sample.ay) * 28.0f, -6.5f, 6.5f);
            const float accel_impulse_z = std::clamp((sample.az - _imuBallPrevAccelZ) * 18.0f, -4.0f, 4.0f);
            if (!_imuBallDynamicsInitialized) {
                _imuBallDynamicsInitialized = true;
            }
            _imuBallPrevAccelX = sample.ax;
            _imuBallPrevAccelY = sample.ay;
            _imuBallPrevAccelZ = sample.az;

            _imuBallVelX = (_imuBallVelX + ((center_x - _imuBallPosX) * 0.11f) + accel_impulse_x) * 0.80f;
            _imuBallVelY = (_imuBallVelY + ((center_y - _imuBallPosY) * 0.11f) + accel_impulse_y) * 0.80f;
            _imuBallVelZ = (_imuBallVelZ + ((0.0f - _imuBallPosZ) * 0.15f) + accel_impulse_z) * 0.78f;
            _imuBallPosX = std::clamp(_imuBallPosX + _imuBallVelX, 28.0f, 136.0f);
            _imuBallPosY = std::clamp(_imuBallPosY + _imuBallVelY, 10.0f, 72.0f);
            _imuBallPosZ = std::clamp(_imuBallPosZ + _imuBallVelZ, -14.0f, 14.0f);
        } else {
            _imuBallVelX *= 0.68f;
            _imuBallVelY *= 0.68f;
            _imuBallVelZ *= 0.68f;
            _imuBallPosX = std::clamp(_imuBallPosX + ((92.0f - _imuBallPosX) * 0.15f) + _imuBallVelX, 28.0f, 136.0f);
            _imuBallPosY = std::clamp(_imuBallPosY + ((42.0f - _imuBallPosY) * 0.15f) + _imuBallVelY, 10.0f, 72.0f);
            _imuBallPosZ = std::clamp(_imuBallPosZ + ((0.0f - _imuBallPosZ) * 0.18f) + _imuBallVelZ, -14.0f, 14.0f);
            _imuBallDynamicsInitialized = false;
        }

        const int32_t x = static_cast<int32_t>(std::lround(_imuBallPosX));
        const int32_t y = static_cast<int32_t>(std::lround(_imuBallPosY));
        const int32_t depth_size = static_cast<int32_t>(std::lround(std::clamp(46.0f + (_imuBallPosZ * 1.0f), 30.0f, 60.0f)));
        const int32_t shadow_width = static_cast<int32_t>(std::lround(std::clamp(40.0f - (_imuBallPosZ * 0.8f), 24.0f, 48.0f)));
        const int32_t shadow_height = static_cast<int32_t>(std::lround(std::clamp(14.0f - (_imuBallPosZ * 0.25f), 8.0f, 18.0f)));

        if (lv_obj_ready(_imuMotionShadow)) {
            lv_obj_set_size(_imuMotionShadow, shadow_width, shadow_height);
            lv_obj_set_pos(_imuMotionShadow,
                           static_cast<int32_t>(std::lround(_imuBallPosX + ((depth_size - shadow_width) * 0.5f))),
                           static_cast<int32_t>(std::lround(104.0f - (_imuBallPosZ * 0.45f))));
            lv_obj_set_style_bg_opa(_imuMotionShadow,
                                    static_cast<lv_opa_t>(std::clamp(90.0f - (_imuBallPosZ * 4.0f), 20.0f, 90.0f)),
                                    0);
        }

        lv_obj_set_size(_imuMotionDot, depth_size, depth_size);
        lv_obj_set_pos(_imuMotionDot, x, y);
        lv_obj_set_style_bg_color(_imuMotionDot, sample_ok ? temp_core_color : lv_color_hex(0x94A3B8), 0);
        lv_obj_set_style_bg_grad_color(_imuMotionDot, sample_ok ? temp_edge_color : lv_color_hex(0xCBD5E1), 0);
        lv_obj_set_style_bg_opa(_imuMotionDot, sample_ok ? LV_OPA_50 : LV_OPA_40, 0);
        lv_obj_set_style_shadow_color(_imuMotionDot, sample_ok ? temp_core_color : lv_color_hex(0x64748B), 0);
        lv_obj_set_style_shadow_width(_imuMotionDot,
                                      static_cast<int32_t>(std::lround(std::clamp(16.0f + (_imuBallPosZ * 0.6f), 10.0f, 22.0f))),
                                      0);
        lv_obj_set_style_shadow_opa(_imuMotionDot,
                                    static_cast<lv_opa_t>(std::clamp(80.0f + (temp_mix * 100.0f), 60.0f, 170.0f)),
                                    0);
        lv_obj_set_style_border_color(_imuMotionDot, sample_ok ? temp_edge_color : lv_color_hex(0xE2E8F0), 0);

        if (lv_obj_ready(_imuMotionTempLabel)) {
            char temp_text[16] = {};
            if (sample_ok) {
                std::snprintf(temp_text, sizeof(temp_text), "%.1fC", temperature_c);
            } else {
                std::snprintf(temp_text, sizeof(temp_text), "--.-C");
            }
            lv_label_set_text(_imuMotionTempLabel, temp_text);
            lv_obj_set_style_text_color(_imuMotionTempLabel,
                                        sample_ok && temp_mix > 0.6f ? lv_color_hex(0xFFF7ED) : lv_color_hex(0xEFF6FF),
                                        0);
            if (depth_size <= 34) {
                lv_obj_set_style_text_font(_imuMotionTempLabel, &lv_font_montserrat_10, 0);
            } else if (depth_size <= 42) {
                lv_obj_set_style_text_font(_imuMotionTempLabel, &lv_font_montserrat_12, 0);
            } else if (depth_size <= 50) {
                lv_obj_set_style_text_font(_imuMotionTempLabel, &lv_font_montserrat_14, 0);
            } else {
                lv_obj_set_style_text_font(_imuMotionTempLabel, &lv_font_montserrat_16, 0);
            }
            lv_obj_center(_imuMotionTempLabel);
        }
    }
#endif
}

void AppSettings::startImuLivePolling(void)
{
#if !APP_SETTINGS_FEATURE_IMU
    return;
#else
    if (_imuLiveTimer != nullptr) {
        return;
    }
    _imuLiveTimer = lv_timer_create(onImuLiveTimerCallback, IMU_LIVE_REFRESH_MS, this);
#endif
}

void AppSettings::stopImuLivePolling(void)
{
#if !APP_SETTINGS_FEATURE_IMU
    return;
#else
    if (_imuLiveTimer != nullptr) {
        lv_timer_del(_imuLiveTimer);
        _imuLiveTimer = nullptr;
    }
#endif
}

bool AppSettings::persistImuConfigFromUi(bool autosave_enabled_only)
{
#if !APP_SETTINGS_FEATURE_IMU
    (void)autosave_enabled_only;
    return false;
#else
    jc4880::imu::ImuConfig config = {};
    jc4880::imu::ImuService::instance().loadConfig(config);

    config.enabled = lv_obj_ready(_imuEnabledSwitch) && lv_obj_has_state(_imuEnabledSwitch, LV_STATE_CHECKED);
    if (!autosave_enabled_only) {
        config.model = static_cast<jc4880::imu::ImuModel>(imu_model_from_dropdown(lv_dropdown_get_selected(_imuModelDropdown)));
        config.busType = static_cast<jc4880::imu::ImuBusType>(getDropdownValueForIndex(kImuBusOptions,
                                                                                       sizeof(kImuBusOptions) / sizeof(kImuBusOptions[0]),
                                                                                       lv_dropdown_get_selected(_imuBusDropdown)));
        config.i2cSda = static_cast<int8_t>(sanitizeImuAssignableGpio(imu_pin_choice_value(lv_dropdown_get_selected(_imuI2cSdaDropdown))));
        config.i2cScl = static_cast<int8_t>(sanitizeImuAssignableGpio(imu_pin_choice_value(lv_dropdown_get_selected(_imuI2cSclDropdown))));
        config.intPin = static_cast<int8_t>(sanitizeImuAssignableGpio(imu_pin_choice_value(lv_dropdown_get_selected(_imuIntDropdown))));
        config.drdyPin = static_cast<int8_t>(sanitizeImuAssignableGpio(imu_pin_choice_value(lv_dropdown_get_selected(_imuDrdyDropdown))));
        config.i2cAddress = static_cast<uint8_t>(getDropdownValueForIndex(kImuAddressOptions,
                                                                          sizeof(kImuAddressOptions) / sizeof(kImuAddressOptions[0]),
                                                                          lv_dropdown_get_selected(_imuI2cAddressDropdown)));
        const char *manual_hex = lv_textarea_get_text(_imuI2cAddressTextArea);
        if ((manual_hex != nullptr) && (*manual_hex != '\0')) {
            char *end = nullptr;
            const unsigned long manual_value = std::strtoul(manual_hex, &end, 16);
            if ((end != manual_hex) && (*end == '\0') && (manual_value >= 0x08) && (manual_value <= 0x77)) {
                config.i2cAddress = static_cast<uint8_t>(manual_value);
            }
        }
    }

    if (config.enabled) {
        if (!disableLocalControllerForLoRa()) {
            if (lv_obj_ready(_imuStatusLabel)) {
                lv_label_set_text(_imuStatusLabel, "Failed to disable Local Controller before enabling IMU.");
            }
            return false;
        }

        jc4880::lora_mesh::StoredState lora_state = {};
        if (jc4880::lora_mesh::load_stored_state(lora_state) && lora_state.settings.radio_enabled) {
            lora_state.settings.radio_enabled = false;
            if (!jc4880::lora_mesh::save_stored_state(lora_state)) {
                if (lv_obj_ready(_imuStatusLabel)) {
                    lv_label_set_text(_imuStatusLabel, "Failed to disable LoRa before enabling IMU.");
                }
                return false;
            }
        }
    }

    std::string error;
    if (!jc4880::imu::ImuService::instance().validateConfig(config, error)) {
        if (lv_obj_ready(_imuStatusLabel)) {
            lv_label_set_text(_imuStatusLabel, error.c_str());
        }
        return false;
    }

    if (!jc4880::imu::ImuService::instance().saveConfig(config)) {
        if (lv_obj_ready(_imuStatusLabel)) {
            lv_label_set_text(_imuStatusLabel, "Failed to save IMU settings.");
        }
        return false;
    }

    refreshImuUi();
    refreshLoRaUi();
    refreshJoypadUi();
    if (!autosave_enabled_only && lv_obj_ready(_imuStatusLabel)) {
        lv_label_set_text(_imuStatusLabel, "IMU settings saved.");
    }
    return true;
#endif
}

void AppSettings::disableImuForLocalController(void)
{
#if !APP_SETTINGS_FEATURE_IMU
    return;
#else
    jc4880::imu::ImuConfig config = {};
    jc4880::imu::ImuService::instance().loadConfig(config);
    if (!config.enabled) {
        return;
    }
    config.enabled = false;
    jc4880::imu::ImuService::instance().saveConfig(config);
    refreshImuUi();
#endif
}

bool AppSettings::disableImuForLoRa(void)
{
#if !APP_SETTINGS_FEATURE_IMU
    return true;
#else
    jc4880::imu::ImuConfig config = {};
    jc4880::imu::ImuService::instance().loadConfig(config);
    if (!config.enabled) {
        return true;
    }
    config.enabled = false;
    const bool saved = jc4880::imu::ImuService::instance().saveConfig(config);
    if (saved) {
        refreshImuUi();
    }
    return saved;
#endif
}


void AppSettings::onImuConfigChangedEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    if (app == nullptr) {
        ESP_LOGE(TAG, "Invalid app pointer");
        return;
    }

#if APP_SETTINGS_FEATURE_IMU
    if (lv_event_get_target(e) == app->_imuEnabledSwitch) {
        if (!app->persistImuConfigFromUi(true)) {
            app->refreshImuUi();
        }
        return;
    }

    app->refreshImuUi();
#else
    (void)e;
#endif
}

void AppSettings::onImuSaveClickedEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    ESP_BROOKESIA_CHECK_NULL_GOTO(app, end, "Invalid app pointer");
    app->persistImuConfigFromUi(false);
end:
    return;
}

void AppSettings::onImuScanClickedEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    if (app == nullptr) {
        return;
    }

#if APP_SETTINGS_FEATURE_IMU
    if (!app->persistImuConfigFromUi(false)) {
        return;
    }

    jc4880::imu::ImuConfig config = {};
    jc4880::imu::ImuService::instance().loadConfig(config);
    std::string status;
    jc4880::imu::ImuModel detected_model = jc4880::imu::ImuModel::IMU_NONE;
    uint8_t detected_address = 0;
    if (jc4880::imu::ImuService::instance().detectI2cModel(config, detected_model, detected_address, status)) {
        if (lv_obj_ready(app->_imuModelDropdown)) {
            lv_dropdown_set_selected(app->_imuModelDropdown, imu_dropdown_index_from_model(detected_model));
        }
        if (lv_obj_ready(app->_imuI2cAddressDropdown)) {
            lv_dropdown_set_selected(app->_imuI2cAddressDropdown,
                                     findDropdownIndexForValue(kImuAddressOptions,
                                                               sizeof(kImuAddressOptions) / sizeof(kImuAddressOptions[0]),
                                                               detected_address));
        }
        if (lv_obj_ready(app->_imuI2cAddressTextArea)) {
            char address_text[8] = {};
            std::snprintf(address_text, sizeof(address_text), "%02X", static_cast<unsigned>(detected_address));
            lv_textarea_set_text(app->_imuI2cAddressTextArea, address_text);
        }
        (void)app->persistImuConfigFromUi(false);
        app->refreshImuUi();
    }
    if (lv_obj_ready(app->_imuStatusLabel)) {
        lv_label_set_text(app->_imuStatusLabel, status.c_str());
    }
#else
    (void)e;
#endif
}

void AppSettings::onImuTestClickedEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    if (app == nullptr) {
        return;
    }

#if APP_SETTINGS_FEATURE_IMU
    if (!app->persistImuConfigFromUi(false)) {
        return;
    }

    jc4880::imu::ImuConfig config = {};
    jc4880::imu::ImuService::instance().loadConfig(config);
    jc4880::imu::ImuSample sample = {};
    std::string status;
    jc4880::imu::ImuService::instance().test(config, sample, status);
    if (lv_obj_ready(app->_imuStatusLabel)) {
        lv_label_set_text(app->_imuStatusLabel, status.c_str());
    }
#else
    (void)e;
#endif
}

void AppSettings::onImuZeroClickedEventCallback(lv_event_t *e)
{
    AppSettings *app = static_cast<AppSettings *>(lv_event_get_user_data(e));
    if (app == nullptr) {
        return;
    }

#if APP_SETTINGS_FEATURE_IMU
    if (!app->persistImuConfigFromUi(false)) {
        return;
    }

    std::string status;
    if (!jc4880::imu::ImuService::instance().begin()) {
        status = "Failed to initialize the IMU for zeroing. Check the saved wiring and model first.";
    } else if (!jc4880::imu::ImuService::instance().setCurrentOrientationAsZero(status) && status.empty()) {
        status = "Failed to capture the current orientation as zero.";
    }

    if (status.empty()) {
        status = "Current orientation captured as zero.";
    }
    if (lv_obj_ready(app->_imuStatusLabel)) {
        lv_label_set_text(app->_imuStatusLabel, status.c_str());
    }
    app->refreshImuUi();
    if (app->isUiActive() && (app->_screen_index == UI_IMU_SETTING_INDEX)) {
        app->refreshImuLiveUi();
    }
#else
    (void)e;
#endif
}

void AppSettings::onImuLiveTimerCallback(lv_timer_t *timer)
{
    if (timer == nullptr) {
        return;
    }

    AppSettings *app = static_cast<AppSettings *>(timer->user_data);
    if (app == nullptr) {
        return;
    }

    if (!app->isUiActive() || (app->_screen_index != UI_IMU_SETTING_INDEX)) {
        return;
    }

    app->refreshImuLiveUi();
}

