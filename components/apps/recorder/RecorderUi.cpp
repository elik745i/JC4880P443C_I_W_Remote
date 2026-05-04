#include "RecorderInternal.hpp"

using namespace recorder;

namespace {

std::string make_playback_status(uint32_t elapsedSeconds, uint32_t durationSeconds)
{
    if (durationSeconds > 0) {
        return format_duration(elapsedSeconds) + " / " + format_duration(durationSeconds);
    }

    return format_duration(elapsedSeconds);
}

lv_color_t interpolate_color(lv_color_t left, lv_color_t right, uint8_t mix)
{
    const uint16_t inv = 255U - mix;
    return lv_color_make(
        static_cast<uint8_t>((left.ch.red * inv + right.ch.red * mix) / 255U),
        static_cast<uint8_t>((left.ch.green * inv + right.ch.green * mix) / 255U),
        static_cast<uint8_t>((left.ch.blue * inv + right.ch.blue * mix) / 255U)
    );
}

lv_color_t spectrum_color_for_bin(int index)
{
    constexpr int kSpectrumBinCount = 24;
    const lv_color_t low = lv_color_hex(0xFF7A9A);
    const lv_color_t mid = lv_color_hex(0xFFE27A);
    const lv_color_t high = lv_color_hex(0x62F5EA);
    const int last = kSpectrumBinCount - 1;

    if (index <= (last / 2)) {
        const uint8_t mix = static_cast<uint8_t>((index * 255) / std::max(1, last / 2));
        return interpolate_color(low, mid, mix);
    }

    const uint8_t mix = static_cast<uint8_t>(((index - (last / 2)) * 255) / std::max(1, last - (last / 2)));
    return interpolate_color(mid, high, mix);
}

} // namespace

void RecorderApp::buildUi()
{
    lv_obj_t *screen = lv_scr_act();
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(screen, LV_OPA_TRANSP, 0);

    lv_obj_t *appRoot = lv_obj_create(screen);
    lv_obj_set_pos(appRoot, 0, 0);
    lv_obj_set_size(appRoot, lv_pct(100), lv_pct(100));
    lv_obj_set_style_radius(appRoot, 0, 0);
    lv_obj_set_style_border_width(appRoot, 0, 0);
    lv_obj_set_style_bg_color(appRoot, lv_color_hex(0x11070B), 0);
    lv_obj_set_style_bg_grad_color(appRoot, lv_color_hex(0x2A0C15), 0);
    lv_obj_set_style_bg_grad_dir(appRoot, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(appRoot, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(appRoot, 0, 0);
    lv_obj_clear_flag(appRoot, LV_OBJ_FLAG_SCROLLABLE);

    const lv_area_t area = getVisualArea();
    const lv_coord_t width = area.x2 - area.x1 + 1;
    const lv_coord_t height = area.y2 - area.y1 + 1;

    lv_obj_t *content = lv_obj_create(appRoot);
    lv_obj_set_pos(content, area.x1, area.y1);
    lv_obj_set_size(content, width, height);
    lv_obj_set_style_radius(content, 0, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *header = lv_obj_create(content);
    lv_obj_set_size(header, width - 32, kHeaderHeight);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, kHeaderTopOffset);
    lv_obj_set_style_radius(header, 26, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x2A1218), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_90, 0);
    lv_obj_set_style_pad_all(header, 18, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    _titleLabel = lv_label_create(header);
    lv_obj_set_style_text_font(_titleLabel, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(_titleLabel, lv_color_hex(0xFFF0F3), 0);
    lv_label_set_text(_titleLabel, "Recorder");
    lv_obj_align(_titleLabel, LV_ALIGN_TOP_LEFT, 0, 0);

    _recordPulseRing = lv_obj_create(header);
    lv_obj_set_size(_recordPulseRing, kRecordRingBaseSize, kRecordRingBaseSize);
    lv_obj_align(_recordPulseRing, LV_ALIGN_TOP_RIGHT, -7, 5);
    lv_obj_set_style_radius(_recordPulseRing, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(_recordPulseRing, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_recordPulseRing, 4, 0);
    lv_obj_set_style_border_color(_recordPulseRing, lv_color_hex(0xFF8AA1), 0);
    lv_obj_set_style_border_opa(_recordPulseRing, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(_recordPulseRing, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(_recordPulseRing, LV_OBJ_FLAG_SCROLLABLE);

    _recordButton = lv_btn_create(header);
    lv_obj_set_size(_recordButton, kRecordButtonSize, kRecordButtonSize);
    lv_obj_align(_recordButton, LV_ALIGN_TOP_RIGHT, kRecordButtonOffsetX, kRecordButtonOffsetY);
    lv_obj_set_style_radius(_recordButton, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(_recordButton, 6, 0);
    lv_obj_set_style_border_color(_recordButton, lv_color_hex(0xFFD4DB), 0);
    lv_obj_set_style_bg_color(_recordButton, lv_color_hex(0xD7263D), 0);
    lv_obj_set_style_bg_grad_color(_recordButton, lv_color_hex(0xA80F28), 0);
    lv_obj_set_style_bg_grad_dir(_recordButton, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_pad_all(_recordButton, 0, 0);
    lv_obj_add_event_cb(_recordButton, onRecordButtonEvent, LV_EVENT_CLICKED, this);

    lv_obj_t *recordLabel = lv_label_create(_recordButton);
    lv_obj_set_style_text_font(recordLabel, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(recordLabel, lv_color_hex(0xFFF7F8), 0);
    lv_label_set_text(recordLabel, "REC");
    lv_obj_align(recordLabel, LV_ALIGN_TOP_MID, 0, 16);

    _recordTimeLabel = lv_label_create(_recordButton);
    lv_obj_set_style_text_font(_recordTimeLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_recordTimeLabel, lv_color_hex(0xFFF0F3), 0);
    lv_label_set_text(_recordTimeLabel, "00:00");
    lv_obj_align(_recordTimeLabel, LV_ALIGN_TOP_MID, 0, 48);

    _statusLabel = lv_label_create(header);
    lv_obj_set_width(_statusLabel, width - 170);
    lv_obj_set_style_text_font(_statusLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_statusLabel, lv_color_hex(0xF5B9C5), 0);
    lv_label_set_long_mode(_statusLabel, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(_statusLabel, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(_statusLabel, LV_ALIGN_BOTTOM_RIGHT, -118, -4);

    lv_obj_t *chartCard = lv_obj_create(content);
    lv_obj_set_size(chartCard, width - 32, kChartCardHeight);
    lv_obj_align_to(chartCard, header, LV_ALIGN_OUT_BOTTOM_MID, 0, kSectionGap);
    lv_obj_set_style_radius(chartCard, 28, 0);
    lv_obj_set_style_border_width(chartCard, 0, 0);
    lv_obj_set_style_bg_color(chartCard, lv_color_hex(0x2A141B), 0);
    lv_obj_set_style_bg_grad_color(chartCard, lv_color_hex(0x3A1A24), 0);
    lv_obj_set_style_bg_grad_dir(chartCard, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(chartCard, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(chartCard, 26, 0);
    lv_obj_set_style_shadow_color(chartCard, lv_color_hex(0x4A1B29), 0);
    lv_obj_set_style_shadow_opa(chartCard, LV_OPA_40, 0);
    lv_obj_set_style_pad_all(chartCard, 18, 0);
    lv_obj_clear_flag(chartCard, LV_OBJ_FLAG_SCROLLABLE);

    _spectrumChart = lv_obj_create(chartCard);
    lv_obj_set_size(_spectrumChart, width - 68, 170);
    lv_obj_center(_spectrumChart);
    lv_obj_set_style_radius(_spectrumChart, 20, 0);
    lv_obj_set_style_border_width(_spectrumChart, 2, 0);
    lv_obj_set_style_border_color(_spectrumChart, lv_color_hex(0xE2A6B7), 0);
    lv_obj_set_style_border_opa(_spectrumChart, LV_OPA_80, 0);
    lv_obj_set_style_bg_color(_spectrumChart, lv_color_hex(0xFFF4F7), 0);
    lv_obj_set_style_bg_grad_color(_spectrumChart, lv_color_hex(0xF8DDE6), 0);
    lv_obj_set_style_bg_grad_dir(_spectrumChart, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(_spectrumChart, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(_spectrumChart, 22, 0);
    lv_obj_set_style_shadow_color(_spectrumChart, lv_color_hex(0x7C4454), 0);
    lv_obj_set_style_shadow_opa(_spectrumChart, static_cast<lv_opa_t>(96), 0);
    lv_obj_set_style_pad_all(_spectrumChart, 12, 0);
    lv_obj_clear_flag(_spectrumChart, LV_OBJ_FLAG_SCROLLABLE);
    _spectrumSeries = nullptr;

    for (int grid = 1; grid <= 4; ++grid) {
        lv_obj_t *gridLine = lv_obj_create(_spectrumChart);
        lv_obj_remove_style_all(gridLine);
        lv_obj_set_size(gridLine, lv_pct(100), 1);
        lv_obj_align(gridLine, LV_ALIGN_BOTTOM_MID, 0, -(grid * 28));
        lv_obj_set_style_bg_color(gridLine, lv_color_hex(0xD38A9E), 0);
        lv_obj_set_style_bg_opa(gridLine, static_cast<lv_opa_t>(grid == 4 ? 150 : 110), 0);
        lv_obj_clear_flag(gridLine, LV_OBJ_FLAG_SCROLLABLE);
    }

    const lv_coord_t spectrumInnerWidth = (width - 68) - 24;
    const lv_coord_t columnGap = 4;
    const lv_coord_t barWidth = std::max<lv_coord_t>(6, (spectrumInnerWidth - ((kSpectrumBins - 1) * columnGap)) / kSpectrumBins);
    const lv_coord_t chartBottom = 146;
    const lv_coord_t chartUsableHeight = 124;
    const lv_coord_t chartLeft = 12;
    for (int index = 0; index < kSpectrumBins; ++index) {
        const lv_color_t baseColor = spectrum_color_for_bin(index);
        const lv_color_t glowColor = interpolate_color(baseColor, lv_color_hex(0xFFFFFF), 118);
        const lv_coord_t x = chartLeft + (index * (barWidth + columnGap));

        _spectrumBars[index] = lv_obj_create(_spectrumChart);
        lv_obj_remove_style_all(_spectrumBars[index]);
        lv_obj_set_size(_spectrumBars[index], barWidth, 10);
        lv_obj_set_pos(_spectrumBars[index], x, chartBottom - 10);
        lv_obj_set_style_radius(_spectrumBars[index], barWidth / 2, 0);
        lv_obj_set_style_bg_color(_spectrumBars[index], baseColor, 0);
        lv_obj_set_style_bg_grad_color(_spectrumBars[index], glowColor, 0);
        lv_obj_set_style_bg_grad_dir(_spectrumBars[index], LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_bg_opa(_spectrumBars[index], LV_OPA_COVER, 0);
        lv_obj_set_style_shadow_width(_spectrumBars[index], 24, 0);
        lv_obj_set_style_shadow_color(_spectrumBars[index], glowColor, 0);
        lv_obj_set_style_shadow_opa(_spectrumBars[index], LV_OPA_90, 0);
        lv_obj_clear_flag(_spectrumBars[index], LV_OBJ_FLAG_SCROLLABLE);

        _spectrumCaps[index] = lv_obj_create(_spectrumChart);
        lv_obj_remove_style_all(_spectrumCaps[index]);
        lv_obj_set_size(_spectrumCaps[index], barWidth + 2, 5);
        lv_obj_set_pos(_spectrumCaps[index], x - 1, chartBottom - 12);
        lv_obj_set_style_radius(_spectrumCaps[index], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(_spectrumCaps[index], glowColor, 0);
        lv_obj_set_style_bg_opa(_spectrumCaps[index], LV_OPA_COVER, 0);
        lv_obj_set_style_shadow_width(_spectrumCaps[index], 18, 0);
        lv_obj_set_style_shadow_color(_spectrumCaps[index], glowColor, 0);
        lv_obj_set_style_shadow_opa(_spectrumCaps[index], LV_OPA_90, 0);
        lv_obj_clear_flag(_spectrumCaps[index], LV_OBJ_FLAG_SCROLLABLE);
    }

    lv_obj_t *listCard = lv_obj_create(content);
    const lv_coord_t listTop = kHeaderTopOffset + kHeaderHeight + kSectionGap + kChartCardHeight + kSectionGap;
    const lv_coord_t listCardHeight = std::max<lv_coord_t>(kMinListCardHeight, height - listTop - kBottomMargin);
    lv_obj_set_size(listCard, width - 32, listCardHeight);
    lv_obj_align_to(listCard, chartCard, LV_ALIGN_OUT_BOTTOM_MID, 0, kSectionGap);
    lv_obj_set_style_radius(listCard, 28, 0);
    lv_obj_set_style_border_width(listCard, 0, 0);
    lv_obj_set_style_bg_color(listCard, lv_color_hex(0x160C10), 0);
    lv_obj_set_style_bg_opa(listCard, LV_OPA_90, 0);
    lv_obj_set_style_pad_all(listCard, 18, 0);
    lv_obj_clear_flag(listCard, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *listTitle = lv_label_create(listCard);
    lv_obj_set_style_text_font(listTitle, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(listTitle, lv_color_hex(0xFFD8E0), 0);
    lv_label_set_text(listTitle, "Saved Recordings");
    lv_obj_align(listTitle, LV_ALIGN_TOP_LEFT, 0, 0);

    _recordingsList = lv_obj_create(listCard);
    lv_obj_set_size(_recordingsList, width - 68, lv_obj_get_height(listCard) - 54);
    lv_obj_align(_recordingsList, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_radius(_recordingsList, 18, 0);
    lv_obj_set_style_border_width(_recordingsList, 0, 0);
    lv_obj_set_style_bg_color(_recordingsList, lv_color_hex(0x211216), 0);
    lv_obj_set_style_bg_opa(_recordingsList, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(_recordingsList, 12, 0);
    lv_obj_set_style_pad_row(_recordingsList, 10, 0);
    lv_obj_set_flex_flow(_recordingsList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_recordingsList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_add_flag(_recordingsList, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(_recordingsList, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(_recordingsList, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_style_pad_right(_recordingsList, 16, 0);
    lv_obj_set_style_bg_color(_recordingsList, lv_color_hex(0x7C2D3A), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(_recordingsList, LV_OPA_70, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(_recordingsList, 6, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(_recordingsList, LV_RADIUS_CIRCLE, LV_PART_SCROLLBAR);

    _emptyLabel = lv_label_create(_recordingsList);
    lv_obj_set_style_text_font(_emptyLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_emptyLabel, lv_color_hex(0xC58B97), 0);
    lv_label_set_text(_emptyLabel, "No AAC recordings on the SD card yet.");

    lv_anim_init(&_pulseSizeAnim);
    lv_anim_set_var(&_pulseSizeAnim, _recordPulseRing);
    lv_anim_set_exec_cb(&_pulseSizeAnim, pulseAnimSizeCallback);
    lv_anim_set_values(&_pulseSizeAnim, kRecordRingBaseSize, 126);
    lv_anim_set_time(&_pulseSizeAnim, 850);
    lv_anim_set_playback_time(&_pulseSizeAnim, 850);
    lv_anim_set_repeat_count(&_pulseSizeAnim, LV_ANIM_REPEAT_INFINITE);

    lv_anim_init(&_pulseOpacityAnim);
    lv_anim_set_var(&_pulseOpacityAnim, _recordPulseRing);
    lv_anim_set_exec_cb(&_pulseOpacityAnim, pulseAnimOpacityCallback);
    lv_anim_set_values(&_pulseOpacityAnim, LV_OPA_70, LV_OPA_0);
    lv_anim_set_time(&_pulseOpacityAnim, 850);
    lv_anim_set_playback_time(&_pulseOpacityAnim, 850);
    lv_anim_set_repeat_count(&_pulseOpacityAnim, LV_ANIM_REPEAT_INFINITE);
}

void RecorderApp::refreshRecordingList()
{
    if (_recordingsList == nullptr) {
        return;
    }

    _recordings.clear();
    _playButtonContexts.clear();
    lv_obj_clean(_recordingsList);

    if (!ensureRecordDirectoryAvailable(false)) {
        _emptyLabel = lv_label_create(_recordingsList);
        lv_obj_set_style_text_font(_emptyLabel, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(_emptyLabel, lv_color_hex(0xC58B97), 0);
        lv_label_set_text(_emptyLabel, "Insert an SD card to record and browse files.");
        return;
    }

    const std::vector<RecordingEntry> scannedRecordings = scanRecordingEntries();
    _recordings = std::vector<RecordingEntry, PsramAllocator<RecordingEntry>>(scannedRecordings.begin(), scannedRecordings.end());
    if (_recordings.empty()) {
        _emptyLabel = lv_label_create(_recordingsList);
        lv_obj_set_style_text_font(_emptyLabel, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(_emptyLabel, lv_color_hex(0xC58B97), 0);
        lv_label_set_text(_emptyLabel, "No AAC recordings on the SD card yet.");
        return;
    }

    _playButtonContexts.resize(_recordings.size());
    for (size_t index = 0; index < _recordings.size(); ++index) {
        _playButtonContexts[index] = {this, index, nullptr, nullptr, nullptr};

        lv_obj_t *row = lv_obj_create(_recordingsList);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_style_radius(row, 16, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x2B181E), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(row, 12, 0);
        lv_obj_set_style_pad_column(row, 10, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *textColumn = lv_obj_create(row);
        lv_obj_set_width(textColumn, lv_pct(76));
        lv_obj_set_height(textColumn, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(textColumn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(textColumn, 0, 0);
        lv_obj_set_style_pad_all(textColumn, 0, 0);
        lv_obj_set_style_pad_row(textColumn, 4, 0);
        lv_obj_set_flex_flow(textColumn, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(textColumn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_clear_flag(textColumn, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *nameLabel = lv_label_create(textColumn);
        lv_obj_set_width(nameLabel, lv_pct(100));
        lv_obj_set_style_text_font(nameLabel, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(nameLabel, lv_color_hex(0xFFF3F6), 0);
        lv_label_set_long_mode(nameLabel, LV_LABEL_LONG_DOT);
        lv_label_set_text(nameLabel, _recordings[index].name.c_str());

        lv_obj_t *metaLabel = lv_label_create(textColumn);
        lv_obj_set_width(metaLabel, lv_pct(100));
        lv_obj_set_style_text_font(metaLabel, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(metaLabel, lv_color_hex(0xD0A7B1), 0);
        lv_label_set_long_mode(metaLabel, LV_LABEL_LONG_DOT);
        lv_label_set_text(metaLabel, format_size(_recordings[index].sizeBytes).c_str());

        lv_obj_t *playButton = lv_btn_create(row);
        lv_obj_set_size(playButton, 74, 48);
        lv_obj_set_style_radius(playButton, 18, 0);
        lv_obj_set_style_border_width(playButton, 0, 0);
        lv_obj_set_style_bg_color(playButton, lv_color_hex(0xFF4D6D), 0);
        lv_obj_set_style_bg_grad_color(playButton, lv_color_hex(0xC51F45), 0);
        lv_obj_set_style_bg_grad_dir(playButton, LV_GRAD_DIR_VER, 0);
        lv_obj_add_event_cb(playButton, onPlayButtonEvent, LV_EVENT_CLICKED, &_playButtonContexts[index]);

        lv_obj_t *playLabel = lv_label_create(playButton);
        lv_obj_set_style_text_font(playLabel, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(playLabel, lv_color_hex(0xFFF7F8), 0);
        lv_label_set_text(playLabel, LV_SYMBOL_PLAY);
        lv_obj_center(playLabel);

        _playButtonContexts[index].button = playButton;
        _playButtonContexts[index].label = playLabel;
        _playButtonContexts[index].metaLabel = metaLabel;
    }
}

void RecorderApp::refreshRecordButtonState()
{
    if ((_recordButton == nullptr) || (_recordPulseRing == nullptr)) {
        return;
    }

    lv_obj_t *label = lv_obj_get_child(_recordButton, 0);
    if (label == nullptr) {
        return;
    }

    if (_recordingActive) {
        lv_label_set_text(label, LV_SYMBOL_STOP);
        lv_obj_clear_flag(_recordPulseRing, LV_OBJ_FLAG_HIDDEN);
        lv_anim_start(&_pulseSizeAnim);
        lv_anim_start(&_pulseOpacityAnim);
    } else {
        lv_anim_del(_recordPulseRing, pulseAnimSizeCallback);
        lv_anim_del(_recordPulseRing, pulseAnimOpacityCallback);
        lv_obj_set_size(_recordPulseRing, kRecordRingBaseSize, kRecordRingBaseSize);
        lv_obj_align(_recordPulseRing, LV_ALIGN_TOP_RIGHT, -7, 5);
        lv_obj_set_style_border_opa(_recordPulseRing, LV_OPA_TRANSP, 0);
        lv_obj_add_flag(_recordPulseRing, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(label, "REC");
    }
}

void RecorderApp::tickUi()
{
    syncPlaybackState();

    std::array<int16_t, kSpectrumBins> spectrum = {};
    uint32_t seconds = 0;
    bool recording = false;
    bool refreshList = false;
    bool playbackActive = false;
    size_t playingIndex = std::numeric_limits<size_t>::max();
    uint32_t playbackSeconds = 0;
    uint32_t playbackDurationSeconds = 0;
    std::string status;
    copyUiState(spectrum,
                seconds,
                recording,
                refreshList,
                playbackActive,
                playingIndex,
                playbackSeconds,
                playbackDurationSeconds,
                status);

    _recordingActive = recording;

    if (_spectrumChart != nullptr) {
        constexpr lv_coord_t chartBottom = 146;
        constexpr lv_coord_t chartUsableHeight = 124;
        for (int index = 0; index < kSpectrumBins; ++index) {
            if ((_spectrumBars[index] == nullptr) || (_spectrumCaps[index] == nullptr)) {
                continue;
            }

            const int boostedValue = std::min(100, static_cast<int>(spectrum[index]) + 16 + ((index % 3) == 0 ? 8 : 0));
            const int smoothedValue = (_spectrumDisplayValues[index] * 2 + boostedValue * 3) / 5;
            _spectrumDisplayValues[index] = static_cast<int16_t>(smoothedValue);
            if (smoothedValue >= _spectrumPeakValues[index]) {
                _spectrumPeakValues[index] = static_cast<int16_t>(std::min(100, smoothedValue + 8));
            } else {
                _spectrumPeakValues[index] = static_cast<int16_t>(std::max(smoothedValue, static_cast<int>(_spectrumPeakValues[index]) - 3));
            }

            const lv_coord_t barHeight = std::max<lv_coord_t>(8, static_cast<lv_coord_t>((smoothedValue * chartUsableHeight) / 100));
            const lv_coord_t peakY = chartBottom - std::max<lv_coord_t>(6, static_cast<lv_coord_t>((_spectrumPeakValues[index] * chartUsableHeight) / 100));
            lv_obj_set_height(_spectrumBars[index], barHeight);
            lv_obj_set_y(_spectrumBars[index], chartBottom - barHeight);
            lv_obj_set_y(_spectrumCaps[index], peakY);
            lv_obj_set_style_bg_opa(_spectrumCaps[index], static_cast<lv_opa_t>(std::min<int>(LV_OPA_COVER, 92 + _spectrumPeakValues[index])), 0);
        }
    }

    if (_recordTimeLabel != nullptr) {
        lv_label_set_text(_recordTimeLabel, format_duration(seconds).c_str());
    }

    if (_statusLabel != nullptr) {
        if (playbackActive && (playingIndex < _recordings.size())) {
            const std::string playbackStatus = std::string("Playing ") + make_playback_status(playbackSeconds, playbackDurationSeconds);
            lv_label_set_text(_statusLabel, playbackStatus.c_str());
        } else {
            lv_label_set_text(_statusLabel, status.c_str());
        }
    }

    for (size_t index = 0; index < _playButtonContexts.size(); ++index) {
        PlayButtonContext &context = _playButtonContexts[index];
        if ((context.button == nullptr) || (context.label == nullptr) || (context.metaLabel == nullptr)) {
            continue;
        }

        const bool rowIsActive = playbackActive && (playingIndex == index);
        lv_obj_set_style_bg_color(context.button, rowIsActive ? lv_color_hex(0xFF9F1C) : lv_color_hex(0xFF4D6D), 0);
        lv_obj_set_style_bg_grad_color(context.button, rowIsActive ? lv_color_hex(0xC56A00) : lv_color_hex(0xC51F45), 0);
        lv_label_set_text(context.label, rowIsActive ? LV_SYMBOL_STOP : LV_SYMBOL_PLAY);

        if (rowIsActive) {
            lv_obj_set_style_text_color(context.metaLabel, lv_color_hex(0xFFD6A0), 0);
            lv_label_set_text(context.metaLabel, make_playback_status(playbackSeconds, playbackDurationSeconds).c_str());
        } else if (index < _recordings.size()) {
            lv_obj_set_style_text_color(context.metaLabel, lv_color_hex(0xD0A7B1), 0);
            lv_label_set_text(context.metaLabel, format_size(_recordings[index].sizeBytes).c_str());
        }
    }

    refreshRecordButtonState();

    if (refreshList) {
        refreshRecordingList();
        if (xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            _refreshListPending = false;
            xSemaphoreGive(_stateMutex);
        }
    }
}

void RecorderApp::onRecordButtonEvent(lv_event_t *event)
{
    auto *app = static_cast<RecorderApp *>(lv_event_get_user_data(event));
    if (app == nullptr) {
        return;
    }

    app->requestRecordButtonToggle();
    app->tickUi();
}

void RecorderApp::onPlayButtonEvent(lv_event_t *event)
{
    auto *context = static_cast<PlayButtonContext *>(lv_event_get_user_data(event));
    if ((context == nullptr) || (context->app == nullptr)) {
        return;
    }

    context->app->requestPlaybackToggle(context->index);
    context->app->tickUi();
}

void RecorderApp::onUiTimer(lv_timer_t *timer)
{
    auto *app = static_cast<RecorderApp *>(timer->user_data);
    if (app != nullptr) {
        app->tickUi();
    }
}

void RecorderApp::pulseAnimSizeCallback(void *object, int32_t value)
{
    lv_obj_t *ring = static_cast<lv_obj_t *>(object);
    if ((ring == nullptr) || !lv_obj_is_valid(ring)) {
        return;
    }

    lv_obj_set_size(ring, value, value);
    lv_obj_align(ring, LV_ALIGN_TOP_RIGHT, -7 - ((value - kRecordRingBaseSize) / 2), 5 - ((value - kRecordRingBaseSize) / 2));
}

void RecorderApp::pulseAnimOpacityCallback(void *object, int32_t value)
{
    lv_obj_t *ring = static_cast<lv_obj_t *>(object);
    if ((ring == nullptr) || !lv_obj_is_valid(ring)) {
        return;
    }

    lv_obj_set_style_border_opa(ring, static_cast<lv_opa_t>(value), 0);
}