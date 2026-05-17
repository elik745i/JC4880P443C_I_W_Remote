void AppSettings::ensureHardwareScreen(void)
{
#if !APP_SETTINGS_FEATURE_HARDWARE_MENU
    return;
#else
    if ((_hardwareScreen != nullptr) && lv_obj_ready(_hardwareScreen)) {
        return;
    }

    _hardwareScreen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(_hardwareScreen, lv_color_hex(0xE5F3FF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(_hardwareScreen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(_hardwareScreen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *hardwareTitle = lv_label_create(_hardwareScreen);
    lv_label_set_text(hardwareTitle, "Hardware Monitor");
    lv_obj_set_style_text_font(hardwareTitle, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(hardwareTitle, lv_color_hex(0x0F172A), 0);
    lv_obj_align(hardwareTitle, LV_ALIGN_TOP_MID, 0, 30);

    lv_obj_t *hardwarePanel = lv_obj_create(_hardwareScreen);
    lv_obj_set_size(hardwarePanel, lv_pct(92), 650);
    lv_obj_align(hardwarePanel, LV_ALIGN_TOP_MID, 0, 92);
    lv_obj_set_style_radius(hardwarePanel, 20, 0);
    lv_obj_set_style_border_width(hardwarePanel, 0, 0);
    lv_obj_set_style_bg_color(hardwarePanel, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_pad_all(hardwarePanel, 14, 0);
    lv_obj_set_style_pad_row(hardwarePanel, 12, 0);
    lv_obj_set_flex_flow(hardwarePanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(hardwarePanel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(hardwarePanel, LV_DIR_VER);

    auto setupTrendCard = [&](HardwareTrendCardIndex index, lv_obj_t *card, lv_obj_t *detail_label) {
        if (!lv_obj_ready(card)) {
            return;
        }

        HardwareTrendUi &trend_ui = _hardwareTrendUi[index];
        trend_ui.card = card;
        trend_ui.expandLabel = nullptr;
        trend_ui.expandedArea = nullptr;
        trend_ui.historyTitleLabel = nullptr;
        trend_ui.historySummaryLabel = nullptr;
        trend_ui.historyChart = nullptr;
        trend_ui.historySeries = nullptr;
        trend_ui.historyLeftLabel = nullptr;
        trend_ui.historyRightLabel = nullptr;
        trend_ui.historyFooterLabel = nullptr;
        trend_ui.expanded = false;

        lv_obj_set_height(card, kHardwareTrendCardCollapsedHeight);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(card, onHardwareTrendCardClickedEventCallback, LV_EVENT_CLICKED, this);

        trend_ui.expandLabel = lv_label_create(card);
        lv_label_set_text(trend_ui.expandLabel, LV_SYMBOL_DOWN);
        lv_obj_set_style_text_font(trend_ui.expandLabel, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(trend_ui.expandLabel, lv_color_hex(0x64748B), 0);
        lv_obj_align(trend_ui.expandLabel, LV_ALIGN_TOP_RIGHT, 0, 30);

        trend_ui.expandedArea = lv_obj_create(card);
        lv_obj_set_size(trend_ui.expandedArea, lv_pct(100), 338);
        lv_obj_align_to(trend_ui.expandedArea, detail_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 14);
        lv_obj_clear_flag(trend_ui.expandedArea, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(trend_ui.expandedArea, 16, 0);
        lv_obj_set_style_border_width(trend_ui.expandedArea, 0, 0);
        lv_obj_set_style_bg_color(trend_ui.expandedArea, lv_color_hex(0xEFF6FF), 0);
        lv_obj_set_style_bg_opa(trend_ui.expandedArea, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(trend_ui.expandedArea, 14, 0);
        lv_obj_set_style_pad_row(trend_ui.expandedArea, 10, 0);
        lv_obj_set_flex_flow(trend_ui.expandedArea, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(trend_ui.expandedArea, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        trend_ui.historyTitleLabel = lv_label_create(trend_ui.expandedArea);
        lv_obj_set_width(trend_ui.historyTitleLabel, lv_pct(100));
        lv_obj_set_style_text_font(trend_ui.historyTitleLabel, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(trend_ui.historyTitleLabel, lv_color_hex(0x0F172A), 0);

        trend_ui.historySummaryLabel = lv_label_create(trend_ui.expandedArea);
        lv_obj_set_width(trend_ui.historySummaryLabel, lv_pct(100));
        lv_label_set_long_mode(trend_ui.historySummaryLabel, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(trend_ui.historySummaryLabel, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(trend_ui.historySummaryLabel, lv_color_hex(0x475569), 0);

        trend_ui.historyChart = lv_chart_create(trend_ui.expandedArea);
        lv_obj_set_size(trend_ui.historyChart, lv_pct(100), 190);
        lv_obj_set_style_radius(trend_ui.historyChart, 14, 0);
        lv_obj_set_style_border_width(trend_ui.historyChart, 0, 0);
        lv_obj_set_style_bg_color(trend_ui.historyChart, lv_color_hex(0xDBEAFE), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(trend_ui.historyChart, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_line_width(trend_ui.historyChart, 2, LV_PART_ITEMS);
        lv_obj_set_style_size(trend_ui.historyChart, 0, LV_PART_INDICATOR);
        lv_chart_set_type(trend_ui.historyChart, LV_CHART_TYPE_LINE);
        lv_chart_set_range(trend_ui.historyChart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
        lv_chart_set_div_line_count(trend_ui.historyChart, 4, 6);
        lv_chart_set_point_count(trend_ui.historyChart, 2);
        trend_ui.historySeries = lv_chart_add_series(trend_ui.historyChart, lv_color_hex(0x2563EB), LV_CHART_AXIS_PRIMARY_Y);
        lv_chart_set_all_value(trend_ui.historyChart, trend_ui.historySeries, 0);

        lv_obj_t *history_axis_row = lv_obj_create(trend_ui.expandedArea);
        lv_obj_set_width(history_axis_row, lv_pct(100));
        lv_obj_set_height(history_axis_row, LV_SIZE_CONTENT);
        lv_obj_clear_flag(history_axis_row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(history_axis_row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(history_axis_row, 0, 0);
        lv_obj_set_style_pad_all(history_axis_row, 0, 0);
        lv_obj_set_flex_flow(history_axis_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(history_axis_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        trend_ui.historyLeftLabel = lv_label_create(history_axis_row);
        lv_obj_set_style_text_font(trend_ui.historyLeftLabel, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(trend_ui.historyLeftLabel, lv_color_hex(0x64748B), 0);

        trend_ui.historyRightLabel = lv_label_create(history_axis_row);
        lv_obj_set_style_text_font(trend_ui.historyRightLabel, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(trend_ui.historyRightLabel, lv_color_hex(0x64748B), 0);

        trend_ui.historyFooterLabel = lv_label_create(trend_ui.expandedArea);
        lv_obj_set_width(trend_ui.historyFooterLabel, lv_pct(100));
        lv_label_set_long_mode(trend_ui.historyFooterLabel, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(trend_ui.historyFooterLabel, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(trend_ui.historyFooterLabel, lv_color_hex(0x475569), 0);

        enableEventBubbleRecursively(card);
    };

    setupTrendCard(HARDWARE_TREND_CPU_LOAD,
                   create_monitor_card(hardwarePanel, "CPU Load", "Processor activity over the last hour", &_hardwareCpuSpeedValueLabel,
                                       &_hardwareCpuSpeedDetailLabel, &_hardwareCpuSpeedBar),
                   _hardwareCpuSpeedDetailLabel);
#if CONFIG_JC4880_FEATURE_BATTERY
    _hardwareBatteryCard = create_monitor_card(hardwarePanel,
                                               "Battery",
                                               "Charge level, history, and ETA",
                                               &_hardwareBatteryValueLabel,
                                               &_hardwareBatteryDetailLabel,
                                               &_hardwareBatteryBar);
    if (lv_obj_ready(_hardwareBatteryCard)) {
        lv_obj_set_height(_hardwareBatteryCard, kBatteryCardCollapsedHeight);
        lv_obj_add_flag(_hardwareBatteryCard, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(_hardwareBatteryCard, onHardwareBatteryCardClickedEventCallback, LV_EVENT_CLICKED, this);

        _hardwareBatteryExpandLabel = lv_label_create(_hardwareBatteryCard);
        lv_label_set_text(_hardwareBatteryExpandLabel, LV_SYMBOL_DOWN);
        lv_obj_set_style_text_font(_hardwareBatteryExpandLabel, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(_hardwareBatteryExpandLabel, lv_color_hex(0x64748B), 0);
        lv_obj_align(_hardwareBatteryExpandLabel, LV_ALIGN_TOP_RIGHT, 0, 30);

        _hardwareBatteryExpandedArea = lv_obj_create(_hardwareBatteryCard);
        lv_obj_set_size(_hardwareBatteryExpandedArea, lv_pct(100), 338);
        lv_obj_align_to(_hardwareBatteryExpandedArea, _hardwareBatteryDetailLabel, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 14);
        lv_obj_clear_flag(_hardwareBatteryExpandedArea, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(_hardwareBatteryExpandedArea, 16, 0);
        lv_obj_set_style_border_width(_hardwareBatteryExpandedArea, 0, 0);
        lv_obj_set_style_bg_color(_hardwareBatteryExpandedArea, lv_color_hex(0xEFF6FF), 0);
        lv_obj_set_style_bg_opa(_hardwareBatteryExpandedArea, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(_hardwareBatteryExpandedArea, 14, 0);
        lv_obj_set_style_pad_row(_hardwareBatteryExpandedArea, 10, 0);
        lv_obj_set_flex_flow(_hardwareBatteryExpandedArea, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(_hardwareBatteryExpandedArea, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        _hardwareBatteryHistoryTitleLabel = lv_label_create(_hardwareBatteryExpandedArea);
        lv_obj_set_width(_hardwareBatteryHistoryTitleLabel, lv_pct(100));
        lv_obj_set_style_text_font(_hardwareBatteryHistoryTitleLabel, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(_hardwareBatteryHistoryTitleLabel, lv_color_hex(0x0F172A), 0);

        _hardwareBatteryHistorySummaryLabel = lv_label_create(_hardwareBatteryExpandedArea);
        lv_obj_set_width(_hardwareBatteryHistorySummaryLabel, lv_pct(100));
        lv_label_set_long_mode(_hardwareBatteryHistorySummaryLabel, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(_hardwareBatteryHistorySummaryLabel, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(_hardwareBatteryHistorySummaryLabel, lv_color_hex(0x475569), 0);

        _hardwareBatteryHistoryChart = lv_chart_create(_hardwareBatteryExpandedArea);
        lv_obj_set_size(_hardwareBatteryHistoryChart, lv_pct(100), 190);
        lv_obj_set_style_radius(_hardwareBatteryHistoryChart, 14, 0);
        lv_obj_set_style_border_width(_hardwareBatteryHistoryChart, 0, 0);
        lv_obj_set_style_bg_color(_hardwareBatteryHistoryChart, lv_color_hex(0xDBEAFE), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(_hardwareBatteryHistoryChart, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_line_width(_hardwareBatteryHistoryChart, 2, LV_PART_ITEMS);
        lv_obj_set_style_size(_hardwareBatteryHistoryChart, 0, LV_PART_INDICATOR);
        lv_chart_set_type(_hardwareBatteryHistoryChart, LV_CHART_TYPE_LINE);
        lv_chart_set_range(_hardwareBatteryHistoryChart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
        lv_chart_set_div_line_count(_hardwareBatteryHistoryChart, 4, 6);
        lv_chart_set_point_count(_hardwareBatteryHistoryChart, 2);
        _hardwareBatteryHistorySeries = lv_chart_add_series(_hardwareBatteryHistoryChart,
                                                            lv_color_hex(0xF59E0B),
                                                            LV_CHART_AXIS_PRIMARY_Y);
        lv_chart_set_all_value(_hardwareBatteryHistoryChart, _hardwareBatteryHistorySeries, 0);

        lv_obj_t *historyAxisRow = lv_obj_create(_hardwareBatteryExpandedArea);
        lv_obj_set_width(historyAxisRow, lv_pct(100));
        lv_obj_set_height(historyAxisRow, LV_SIZE_CONTENT);
        lv_obj_clear_flag(historyAxisRow, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(historyAxisRow, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(historyAxisRow, 0, 0);
        lv_obj_set_style_pad_all(historyAxisRow, 0, 0);
        lv_obj_set_flex_flow(historyAxisRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(historyAxisRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        _hardwareBatteryHistoryLeftLabel = lv_label_create(historyAxisRow);
        lv_obj_set_style_text_font(_hardwareBatteryHistoryLeftLabel, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(_hardwareBatteryHistoryLeftLabel, lv_color_hex(0x64748B), 0);

        _hardwareBatteryHistoryRightLabel = lv_label_create(historyAxisRow);
        lv_obj_set_style_text_font(_hardwareBatteryHistoryRightLabel, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(_hardwareBatteryHistoryRightLabel, lv_color_hex(0x64748B), 0);

        _hardwareBatteryHistoryFooterLabel = lv_label_create(_hardwareBatteryExpandedArea);
        lv_obj_set_width(_hardwareBatteryHistoryFooterLabel, lv_pct(100));
        lv_label_set_long_mode(_hardwareBatteryHistoryFooterLabel, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(_hardwareBatteryHistoryFooterLabel, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(_hardwareBatteryHistoryFooterLabel, lv_color_hex(0x475569), 0);

        enableEventBubbleRecursively(_hardwareBatteryCard);
    }
#endif
    setupTrendCard(HARDWARE_TREND_SRAM,
                   create_monitor_card(hardwarePanel, "SRAM", "Occupied versus total internal memory", &_hardwareSramValueLabel,
                                       &_hardwareSramDetailLabel, &_hardwareSramBar),
                   _hardwareSramDetailLabel);
    setupTrendCard(HARDWARE_TREND_PSRAM,
                   create_monitor_card(hardwarePanel, "PSRAM", "Occupied versus total external memory", &_hardwarePsramValueLabel,
                                       &_hardwarePsramDetailLabel, &_hardwarePsramBar),
                   _hardwarePsramDetailLabel);
    setupTrendCard(HARDWARE_TREND_CPU_TEMP,
                   create_monitor_card(hardwarePanel, "CPU Temperature", "On-die sensor reading over the last hour", &_hardwareCpuTempValueLabel,
                                       &_hardwareCpuTempDetailLabel, &_hardwareCpuTempBar),
                   _hardwareCpuTempDetailLabel);
    create_monitor_card(hardwarePanel, "SD Card Storage", "Used versus total mounted capacity", &_hardwareSdValueLabel,
                        &_hardwareSdDetailLabel, &_hardwareSdBar);
    setupTrendCard(HARDWARE_TREND_WIFI,
                   create_monitor_card(hardwarePanel, "Wi-Fi Signal", "Current station RSSI and last-hour history", &_hardwareWifiValueLabel,
                                       &_hardwareWifiDetailLabel, &_hardwareWifiBar),
                   _hardwareWifiDetailLabel);

    if (_hardwareCpuSpeedDetailLabel != nullptr) {
        lv_label_set_text(_hardwareCpuSpeedDetailLabel, "Tap to expand history.");
        lv_obj_set_style_text_font(_hardwareCpuSpeedDetailLabel, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(_hardwareCpuSpeedDetailLabel, lv_color_hex(0x475569), 0);
    }
    if (_hardwareCpuSpeedBar != nullptr) {
        lv_bar_set_value(_hardwareCpuSpeedBar, 100, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(_hardwareCpuSpeedBar, lv_color_hex(0x2563EB), LV_PART_INDICATOR);
    }

    _screen_list[UI_HARDWARE_SETTING_INDEX] = _hardwareScreen;
    lv_obj_add_event_cb(_hardwareScreen, onScreenLoadEventCallback, LV_EVENT_SCREEN_LOADED, this);
#endif
}

void AppSettings::refreshHardwareMonitorUi(void)
{
#if !APP_SETTINGS_FEATURE_HARDWARE_MENU
    return;
#endif
    if (!isUiActive()) {
        return;
    }

    auto setMonitorBar = [](lv_obj_t *bar, int32_t percent, lv_color_t color) {
        if (!lv_obj_ready(bar)) {
            return;
        }

        lv_bar_set_value(bar, std::max<int32_t>(0, std::min<int32_t>(100, percent)), LV_ANIM_OFF);
        lv_obj_set_style_bg_color(bar, color, LV_PART_INDICATOR);
    };

    hardware_history_service::Snapshot hardware_snapshot = {};
    const bool hardware_snapshot_ready = hardware_history_service::get_snapshot(hardware_snapshot);
    if (_hardwareFastHistoryScratch == nullptr) {
        _hardwareFastHistoryScratch = static_cast<uint8_t *>(allocate_psram_preferred_buffer(hardware_history_service::kFastHistorySamples));
        if (_hardwareFastHistoryScratch != nullptr) {
            std::memset(_hardwareFastHistoryScratch, 0, hardware_history_service::kFastHistorySamples);
        }
    }
    if (_hardwareSlowHistoryScratch == nullptr) {
        _hardwareSlowHistoryScratch = static_cast<uint8_t *>(allocate_psram_preferred_buffer(hardware_history_service::kSlowHistorySamples));
        if (_hardwareSlowHistoryScratch != nullptr) {
            std::memset(_hardwareSlowHistoryScratch, 0, hardware_history_service::kSlowHistorySamples);
        }
    }

    auto updateTrendChart = [&](HardwareTrendCardIndex index,
                                hardware_history_service::Metric metric,
                                const char *title,
                                const string &summary,
                                const char *footer,
                                lv_color_t line_color,
                                lv_color_t background_color) {
        HardwareTrendUi &trend_ui = _hardwareTrendUi[index];

        if (lv_obj_ready(trend_ui.expandLabel)) {
            lv_label_set_text(trend_ui.expandLabel, trend_ui.expanded ? LV_SYMBOL_UP : LV_SYMBOL_DOWN);
        }
        if (lv_obj_ready(trend_ui.historyTitleLabel)) {
            lv_label_set_text(trend_ui.historyTitleLabel, title);
        }
        if (lv_obj_ready(trend_ui.historySummaryLabel)) {
            lv_label_set_text(trend_ui.historySummaryLabel, summary.c_str());
        }

        const bool slow_metric = (metric == hardware_history_service::Metric::WifiSignal);
        uint8_t *history_buffer = slow_metric ? _hardwareSlowHistoryScratch : _hardwareFastHistoryScratch;
        const std::size_t history_capacity = slow_metric ? hardware_history_service::kSlowHistorySamples
                                                         : hardware_history_service::kFastHistorySamples;
        const std::size_t sample_count = (history_buffer != nullptr)
                                             ? hardware_history_service::copy_samples(metric, history_buffer, history_capacity)
                                             : 0;

        if (lv_obj_ready(trend_ui.historyChart) && (trend_ui.historySeries != nullptr)) {
            lv_chart_set_point_count(trend_ui.historyChart, std::max<std::size_t>(sample_count, 2));
            lv_chart_set_all_value(trend_ui.historyChart, trend_ui.historySeries, sample_count > 0 ? history_buffer[0] : 0);
            for (std::size_t sample_index = 0; sample_index < sample_count; ++sample_index) {
                lv_chart_set_next_value(trend_ui.historyChart, trend_ui.historySeries, history_buffer[sample_index]);
            }
            lv_obj_set_style_bg_color(trend_ui.historyChart, background_color, LV_PART_MAIN);
            lv_chart_refresh(trend_ui.historyChart);
        }
        if (lv_obj_ready(trend_ui.historyLeftLabel)) {
            lv_label_set_text(trend_ui.historyLeftLabel,
                              sample_count > 1 ? (slow_metric ? "59m ago" : "60m ago") : "Collecting history");
        }
        if (lv_obj_ready(trend_ui.historyRightLabel)) {
            lv_label_set_text(trend_ui.historyRightLabel, "Now");
        }
        if (lv_obj_ready(trend_ui.historyFooterLabel)) {
            lv_label_set_text(trend_ui.historyFooterLabel, footer);
        }
        if (lv_obj_ready(trend_ui.historyChart)) {
            lv_obj_set_style_bg_color(trend_ui.historyChart, background_color, LV_PART_MAIN);
            lv_obj_set_style_bg_color(trend_ui.historyChart, line_color, LV_PART_ITEMS);
        }
    };
#if CONFIG_JC4880_FEATURE_BATTERY
    battery_history_service::Status battery_status = {};
    battery_history_service::HistorySample battery_samples[battery_history_service::kMaxHistorySamples] = {};
    const std::size_t battery_sample_count = battery_history_service::copy_samples(battery_samples, battery_history_service::kMaxHistorySamples);
    if (battery_history_service::get_status(battery_status)) {
        if (lv_obj_ready(_hardwareBatteryValueLabel)) {
            const string text = std::to_string(battery_status.capacity_percent) + "%";
            lv_label_set_text(_hardwareBatteryValueLabel, text.c_str());
        }
        if (lv_obj_ready(_hardwareBatteryExpandLabel)) {
            lv_label_set_text(_hardwareBatteryExpandLabel, _hardwareBatteryExpanded ? LV_SYMBOL_UP : LV_SYMBOL_DOWN);
        }
        if (lv_obj_ready(_hardwareBatteryDetailLabel)) {
            string detail = battery_status.charging ? "Charging" : "On battery";
            if (battery_status.eta_minutes >= 0) {
                detail += battery_status.charging ? " - full in " : " - ";
                if (!battery_status.charging) {
                    detail += formatDurationMinutes(battery_status.eta_minutes) + " left";
                } else {
                    detail += formatDurationMinutes(battery_status.eta_minutes);
                }
            } else {
                detail += battery_status.charging ? " - estimating time to full" : " - estimating battery life";
            }
            detail += _hardwareBatteryExpanded ? "\nTap to collapse history" : "\nTap to expand history";
            lv_label_set_text(_hardwareBatteryDetailLabel, detail.c_str());
        }
        setMonitorBar(_hardwareBatteryBar,
                      battery_status.capacity_percent,
                      getBatteryBarColor(battery_status.capacity_percent, battery_status.charging));

        if (lv_obj_ready(_hardwareBatteryHistoryTitleLabel)) {
            lv_label_set_text(_hardwareBatteryHistoryTitleLabel,
                              battery_status.charging ? "Charging history" : "Battery drain history");
        }
        if (lv_obj_ready(_hardwareBatteryHistorySummaryLabel)) {
            string summary = std::string(battery_status.charging ? "Currently charging at " : "Currently on battery at ") +
                             std::to_string(battery_status.capacity_percent) + "%";
            if (battery_status.eta_minutes >= 0) {
                summary += battery_status.charging ? (". Full in " + formatDurationMinutes(battery_status.eta_minutes))
                                                   : (". Estimated life: " + formatDurationMinutes(battery_status.eta_minutes));
            }
            lv_label_set_text(_hardwareBatteryHistorySummaryLabel, summary.c_str());
        }
        if (lv_obj_ready(_hardwareBatteryHistoryChart) && (_hardwareBatteryHistorySeries != nullptr)) {
            lv_chart_set_point_count(_hardwareBatteryHistoryChart, std::max<std::size_t>(battery_sample_count, 2));
            lv_chart_set_all_value(_hardwareBatteryHistoryChart,
                                   _hardwareBatteryHistorySeries,
                                   battery_sample_count > 0 ? ((battery_samples[0].capacity_tenths + 5) / 10) : 0);
            for (std::size_t index = 0; index < battery_sample_count; ++index) {
                lv_chart_set_next_value(_hardwareBatteryHistoryChart,
                                        _hardwareBatteryHistorySeries,
                                        (battery_samples[index].capacity_tenths + 5) / 10);
            }
            lv_obj_set_style_bg_color(_hardwareBatteryHistoryChart,
                                      battery_status.charging ? lv_color_hex(0xDCFCE7) : lv_color_hex(0xFEF3C7),
                                      LV_PART_MAIN);
            lv_obj_set_style_bg_color(_hardwareBatteryHistoryChart,
                                      battery_status.charging ? lv_color_hex(0x16A34A) : lv_color_hex(0xF59E0B),
                                      LV_PART_ITEMS);
            lv_chart_refresh(_hardwareBatteryHistoryChart);
        }
        if (lv_obj_ready(_hardwareBatteryHistoryLeftLabel)) {
            if (battery_sample_count > 1) {
                const int64_t oldest_age_sec = std::max<int64_t>(0, battery_status.timestamp_sec - battery_samples[0].timestamp_sec);
                const string label = formatLookbackMinutes(static_cast<int32_t>(oldest_age_sec / 60));
                lv_label_set_text(_hardwareBatteryHistoryLeftLabel, label.c_str());
            } else {
                lv_label_set_text(_hardwareBatteryHistoryLeftLabel, "Waiting for history");
            }
        }
        if (lv_obj_ready(_hardwareBatteryHistoryRightLabel)) {
            lv_label_set_text(_hardwareBatteryHistoryRightLabel, "Now");
        }
        if (lv_obj_ready(_hardwareBatteryHistoryFooterLabel)) {
            string footer = "Sampling every 1 minute, keeping the latest 60 points (~1 hour) in PSRAM.";
            if (battery_sample_count < 2) {
                footer += " More time is needed before trend and ETA stabilize.";
            }
            lv_label_set_text(_hardwareBatteryHistoryFooterLabel, footer.c_str());
        }
    } else {
        if (lv_obj_ready(_hardwareBatteryValueLabel)) {
            lv_label_set_text(_hardwareBatteryValueLabel, "Unavailable");
        }
        if (lv_obj_ready(_hardwareBatteryExpandLabel)) {
            lv_label_set_text(_hardwareBatteryExpandLabel, _hardwareBatteryExpanded ? LV_SYMBOL_UP : LV_SYMBOL_DOWN);
        }
        if (lv_obj_ready(_hardwareBatteryDetailLabel)) {
            lv_label_set_text(_hardwareBatteryDetailLabel, "Battery monitoring is unavailable on this build.\nTap to retry after startup settles.");
        }
        setMonitorBar(_hardwareBatteryBar, 0, lv_color_hex(0x94A3B8));
        if (lv_obj_ready(_hardwareBatteryHistoryTitleLabel)) {
            lv_label_set_text(_hardwareBatteryHistoryTitleLabel, "Battery history");
        }
        if (lv_obj_ready(_hardwareBatteryHistorySummaryLabel)) {
            lv_label_set_text(_hardwareBatteryHistorySummaryLabel, "Battery status is still initializing.");
        }
        if (lv_obj_ready(_hardwareBatteryHistoryChart) && (_hardwareBatteryHistorySeries != nullptr)) {
            lv_chart_set_point_count(_hardwareBatteryHistoryChart, 2);
            lv_chart_set_all_value(_hardwareBatteryHistoryChart, _hardwareBatteryHistorySeries, 0);
            lv_obj_set_style_bg_color(_hardwareBatteryHistoryChart, lv_color_hex(0xE2E8F0), LV_PART_MAIN);
            lv_obj_set_style_bg_color(_hardwareBatteryHistoryChart, lv_color_hex(0x94A3B8), LV_PART_ITEMS);
            lv_chart_refresh(_hardwareBatteryHistoryChart);
        }
        if (lv_obj_ready(_hardwareBatteryHistoryLeftLabel)) {
            lv_label_set_text(_hardwareBatteryHistoryLeftLabel, "Waiting for history");
        }
        if (lv_obj_ready(_hardwareBatteryHistoryRightLabel)) {
            lv_label_set_text(_hardwareBatteryHistoryRightLabel, "Now");
        }
        if (lv_obj_ready(_hardwareBatteryHistoryFooterLabel)) {
            lv_label_set_text(_hardwareBatteryHistoryFooterLabel,
                              "Battery history becomes available after the sampler collects enough points.");
        }
    }
#endif

    const uint64_t total_sram = hardware_snapshot_ready ? hardware_snapshot.sram_total_bytes : 0;
    const uint64_t used_sram = hardware_snapshot_ready ? hardware_snapshot.sram_used_bytes : 0;
    const int32_t sram_percent = hardware_snapshot_ready ? hardware_snapshot.sram_percent : 0;
    const char *sram_hint = _hardwareTrendUi[HARDWARE_TREND_SRAM].expanded ? "\nTap to collapse history" : "\nTap to expand history";

    if (lv_obj_ready(_hardwareSramValueLabel)) {
        const string text = formatPercentUsed(sram_percent);
        lv_label_set_text(_hardwareSramValueLabel, text.c_str());
    }
    if (lv_obj_ready(_hardwareSramDetailLabel)) {
        const string detail = formatStorageAmount(used_sram) + " / " + formatStorageAmount(total_sram) + " occupied" + sram_hint;
        lv_label_set_text(_hardwareSramDetailLabel, detail.c_str());
    }
    setMonitorBar(_hardwareSramBar, sram_percent, getMonitorBarColor(sram_percent));
    updateTrendChart(HARDWARE_TREND_SRAM,
                     hardware_history_service::Metric::SramUsage,
                     "SRAM usage history",
                     formatStorageAmount(used_sram) + " used out of " + formatStorageAmount(total_sram) + ".",
                     "Stored in PSRAM every 1 second, keeping the latest 3600 points (1 hour).",
                     getMonitorBarColor(sram_percent),
                     lv_color_hex(0xDBEAFE));

    const uint64_t total_psram = hardware_snapshot_ready ? hardware_snapshot.psram_total_bytes : 0;
    const uint64_t used_psram = hardware_snapshot_ready ? hardware_snapshot.psram_used_bytes : 0;
    const int32_t psram_percent = hardware_snapshot_ready ? hardware_snapshot.psram_percent : 0;
    const char *psram_hint = _hardwareTrendUi[HARDWARE_TREND_PSRAM].expanded ? "\nTap to collapse history" : "\nTap to expand history";

    if (lv_obj_ready(_hardwarePsramValueLabel)) {
        const string text = formatPercentUsed(psram_percent);
        lv_label_set_text(_hardwarePsramValueLabel, text.c_str());
    }
    if (lv_obj_ready(_hardwarePsramDetailLabel)) {
        const string detail = formatStorageAmount(used_psram) + " / " + formatStorageAmount(total_psram) + " occupied" + psram_hint;
        lv_label_set_text(_hardwarePsramDetailLabel, detail.c_str());
    }
    setMonitorBar(_hardwarePsramBar, psram_percent, getMonitorBarColor(psram_percent));
    updateTrendChart(HARDWARE_TREND_PSRAM,
                     hardware_history_service::Metric::PsramUsage,
                     "PSRAM usage history",
                     formatStorageAmount(used_psram) + " used out of " + formatStorageAmount(total_psram) + ".",
                     "Stored in PSRAM every 1 second, keeping the latest 3600 points (1 hour).",
                     getMonitorBarColor(psram_percent),
                     lv_color_hex(0xDBEAFE));

    uint64_t sd_total = 0;
    uint64_t sd_used = 0;
    const bool sd_mounted = app_storage_is_sdcard_mounted();
    bool sd_capacity_ready = false;
    uint64_t sd_total_bytes = 0;
    uint64_t sd_free_bytes = 0;
    if (sd_mounted &&
        (esp_vfs_fat_info(kSdCardMountPoint, &sd_total_bytes, &sd_free_bytes) == ESP_OK)) {
        sd_total = sd_total_bytes;
        sd_used = (sd_total >= sd_free_bytes) ? (sd_total - sd_free_bytes) : 0;
        sd_capacity_ready = (sd_total > 0);
    }

    if (lv_obj_ready(_hardwareSdValueLabel)) {
        if (sd_capacity_ready) {
            const string text = formatPercentUsed(calculatePercent(sd_used, sd_total));
            lv_label_set_text(_hardwareSdValueLabel, text.c_str());
        } else if (sd_mounted) {
            lv_label_set_text(_hardwareSdValueLabel, "Mounted");
        } else {
            lv_label_set_text(_hardwareSdValueLabel, "Not mounted");
        }
    }
    if (lv_obj_ready(_hardwareSdDetailLabel)) {
        if (sd_capacity_ready) {
            const string detail = formatStorageAmount(sd_used) + " / " + formatStorageAmount(sd_total) + " occupied";
            lv_label_set_text(_hardwareSdDetailLabel, detail.c_str());
        } else if (sd_mounted) {
            lv_label_set_text(_hardwareSdDetailLabel, "SD card is mounted, but capacity information is temporarily unavailable.");
        } else {
            lv_label_set_text(_hardwareSdDetailLabel, "Insert or remount the SD card to monitor storage usage.");
        }
    }
    setMonitorBar(_hardwareSdBar, sd_capacity_ready ? calculatePercent(sd_used, sd_total) : 0,
                  sd_capacity_ready ? getMonitorBarColor(calculatePercent(sd_used, sd_total)) : lv_color_hex(0x94A3B8));

    const bool wifi_connected = hardware_snapshot_ready && hardware_snapshot.wifi_connected;
    const char *wifi_hint = _hardwareTrendUi[HARDWARE_TREND_WIFI].expanded ? "\nTap to collapse history" : "\nTap to expand history";
    if (lv_obj_ready(_hardwareWifiValueLabel)) {
        if (wifi_connected) {
            const string text = formatSignedWithUnit(static_cast<int32_t>(hardware_snapshot.wifi_rssi), "dBm");
            lv_label_set_text(_hardwareWifiValueLabel, text.c_str());
        } else {
            lv_label_set_text(_hardwareWifiValueLabel, "Disconnected");
        }
    }
    if (lv_obj_ready(_hardwareWifiDetailLabel)) {
        if (wifi_connected) {
            string detail = "Connected to ";
            detail += hardware_snapshot.wifi_ssid;
            detail += wifi_hint;
            lv_label_set_text(_hardwareWifiDetailLabel, detail.c_str());
        } else {
            const string detail = string("Join a network to view live signal strength.") + wifi_hint;
            lv_label_set_text(_hardwareWifiDetailLabel, detail.c_str());
        }
    }
    const int32_t wifi_percent = wifi_connected ? hardware_snapshot.wifi_percent : 0;
    setMonitorBar(_hardwareWifiBar, wifi_percent, wifi_connected ? getMonitorBarColor(100 - wifi_percent) : lv_color_hex(0x94A3B8));
    updateTrendChart(HARDWARE_TREND_WIFI,
                     hardware_history_service::Metric::WifiSignal,
                     "Wi-Fi signal history",
                     wifi_connected ? (string("Connected to ") + hardware_snapshot.wifi_ssid + " at " + formatSignedWithUnit(hardware_snapshot.wifi_rssi, "dBm"))
                                    : string("No active Wi-Fi link."),
                     "Stored in PSRAM every 1 minute, keeping the latest 60 points (1 hour).",
                     wifi_connected ? getMonitorBarColor(100 - wifi_percent) : lv_color_hex(0x94A3B8),
                     lv_color_hex(0xDBEAFE));

    if (lv_obj_ready(_hardwareCpuSpeedValueLabel)) {
        const string text = hardware_snapshot_ready && hardware_snapshot.cpu_load_available
                                ? (std::to_string(hardware_snapshot.cpu_load_percent) + "%")
                                : string("Measuring");
        lv_label_set_text(_hardwareCpuSpeedValueLabel, text.c_str());
    }

    if (lv_obj_ready(_hardwareCpuSpeedDetailLabel)) {
        const char *cpu_hint = _hardwareTrendUi[HARDWARE_TREND_CPU_LOAD].expanded ? "\nTap to collapse history" : "\nTap to expand history";
        string uptime_text = hardware_snapshot_ready && (hardware_snapshot.cpu_clock_mhz > 0)
                                 ? (formatSignedWithUnit(hardware_snapshot.cpu_clock_mhz, "MHz") + string(" configured"))
                                 : string("CPU clock unavailable");
        uptime_text += "\nUptime: ";
        uptime_text += formatUptime(hardware_snapshot_ready ? hardware_snapshot.uptime_sec : 0);
        uptime_text += cpu_hint;
        lv_label_set_text(_hardwareCpuSpeedDetailLabel, uptime_text.c_str());
    }
    setMonitorBar(_hardwareCpuSpeedBar,
                  hardware_snapshot_ready && hardware_snapshot.cpu_load_available ? hardware_snapshot.cpu_load_percent : 0,
                  hardware_snapshot_ready && hardware_snapshot.cpu_load_available ? getMonitorBarColor(hardware_snapshot.cpu_load_percent)
                                                                                  : lv_color_hex(0x94A3B8));
    updateTrendChart(HARDWARE_TREND_CPU_LOAD,
                     hardware_history_service::Metric::CpuLoad,
                     "CPU load history",
                     hardware_snapshot_ready && hardware_snapshot.cpu_load_available
                         ? (string("Current load: ") + std::to_string(hardware_snapshot.cpu_load_percent) + "% at " +
                            formatSignedWithUnit(hardware_snapshot.cpu_clock_mhz, "MHz"))
                         : string("Collecting CPU runtime statistics."),
                     "Stored in PSRAM every 1 second, keeping the latest 3600 points (1 hour).",
                     hardware_snapshot_ready && hardware_snapshot.cpu_load_available ? getMonitorBarColor(hardware_snapshot.cpu_load_percent)
                                                                                      : lv_color_hex(0x94A3B8),
                     lv_color_hex(0xDBEAFE));

    const bool has_cpu_temp = hardware_snapshot_ready && hardware_snapshot.cpu_temperature_available;
    const int32_t cpu_temp_tenths = hardware_snapshot_ready ? hardware_snapshot.cpu_temperature_tenths : 0;
    const float cpu_temp_celsius = static_cast<float>(cpu_temp_tenths) / 10.0f;
    if (lv_obj_ready(_hardwareCpuTempValueLabel)) {
        if (has_cpu_temp) {
            const string text = formatTemperatureCelsius(cpu_temp_celsius);
            lv_label_set_text(_hardwareCpuTempValueLabel, text.c_str());
        } else {
            lv_label_set_text(_hardwareCpuTempValueLabel, "Unavailable");
        }
    }
    if (lv_obj_ready(_hardwareCpuTempDetailLabel)) {
        const char *temp_hint = _hardwareTrendUi[HARDWARE_TREND_CPU_TEMP].expanded ? "\nTap to collapse history" : "\nTap to expand history";
        const string detail = has_cpu_temp ? (string("Background sampling every 10 seconds.") + temp_hint)
                                           : string("Temperature sensor is not available on this build.");
        lv_label_set_text(_hardwareCpuTempDetailLabel, detail.c_str());
    }
    const int32_t temp_percent = has_cpu_temp ? std::max<int32_t>(0, std::min<int32_t>(100, static_cast<int32_t>(cpu_temp_celsius))) : 0;
    setMonitorBar(_hardwareCpuTempBar, temp_percent, has_cpu_temp ? getMonitorBarColor(temp_percent) : lv_color_hex(0x94A3B8));
    updateTrendChart(HARDWARE_TREND_CPU_TEMP,
                     hardware_history_service::Metric::CpuTemperature,
                     "CPU temperature history",
                     has_cpu_temp ? (string("Current temperature: ") + formatTemperatureCelsius(cpu_temp_celsius))
                                  : string("Temperature sensor is unavailable."),
                     "Stored in PSRAM every 10 seconds, keeping the latest 360 points (1 hour).",
                     has_cpu_temp ? getMonitorBarColor(temp_percent) : lv_color_hex(0x94A3B8),
                     lv_color_hex(0xDBEAFE));

    refreshHardwareGpioTestUi();
}

void AppSettings::setBatteryHistoryExpanded(bool expanded, bool animate)
{
#if !CONFIG_JC4880_FEATURE_BATTERY
    (void)expanded;
    (void)animate;
    return;
#else
    if (!lv_obj_ready(_hardwareBatteryCard)) {
        return;
    }
    _hardwareBatteryExpanded = expanded;
    if (lv_obj_ready(_hardwareBatteryExpandLabel)) {
        lv_label_set_text(_hardwareBatteryExpandLabel, _hardwareBatteryExpanded ? LV_SYMBOL_UP : LV_SYMBOL_DOWN);
    }

    const lv_coord_t target_height = expanded ? kBatteryCardExpandedHeight : kBatteryCardCollapsedHeight;
    lv_anim_del(_hardwareBatteryCard, animateObjectHeight);
    if (!animate) {
        lv_obj_set_height(_hardwareBatteryCard, target_height);
    } else {
        lv_anim_t animation;
        lv_anim_init(&animation);
        lv_anim_set_var(&animation, _hardwareBatteryCard);
        lv_anim_set_exec_cb(&animation, animateObjectHeight);
        lv_anim_set_time(&animation, kBatteryCardExpandAnimMs);
        lv_anim_set_values(&animation, lv_obj_get_height(_hardwareBatteryCard), target_height);
        lv_anim_set_path_cb(&animation, lv_anim_path_ease_in_out);
        lv_anim_start(&animation);
    }

    if (expanded) {
        lv_obj_scroll_to_view(_hardwareBatteryCard, LV_ANIM_ON);
    }
#endif
}

void AppSettings::setHardwareTrendExpanded(HardwareTrendCardIndex index, bool expanded, bool animate)
{
    if ((index < HARDWARE_TREND_CPU_LOAD) || (index >= HARDWARE_TREND_CARD_COUNT)) {
        return;
    }

    HardwareTrendUi &trend_ui = _hardwareTrendUi[index];
    if (!lv_obj_ready(trend_ui.card)) {
        return;
    }

    trend_ui.expanded = expanded;
    if (lv_obj_ready(trend_ui.expandLabel)) {
        lv_label_set_text(trend_ui.expandLabel, expanded ? LV_SYMBOL_UP : LV_SYMBOL_DOWN);
    }

    const lv_coord_t target_height = expanded ? kHardwareTrendCardExpandedHeight : kHardwareTrendCardCollapsedHeight;
    lv_anim_del(trend_ui.card, animateObjectHeight);
    if (!animate) {
        lv_obj_set_height(trend_ui.card, target_height);
    } else {
        lv_anim_t animation;
        lv_anim_init(&animation);
        lv_anim_set_var(&animation, trend_ui.card);
        lv_anim_set_exec_cb(&animation, animateObjectHeight);
        lv_anim_set_time(&animation, kHardwareTrendCardExpandAnimMs);
        lv_anim_set_values(&animation, lv_obj_get_height(trend_ui.card), target_height);
        lv_anim_set_path_cb(&animation, lv_anim_path_ease_in_out);
        lv_anim_start(&animation);
    }

    if (expanded) {
        lv_obj_scroll_to_view(trend_ui.card, LV_ANIM_ON);
    }
}