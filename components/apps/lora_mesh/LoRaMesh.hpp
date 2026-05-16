#pragma once

#include <string>
#include <vector>

#include "esp_brookesia.hpp"

class LoRaMeshApp: public ESP_Brookesia_PhoneApp
{
public:
    LoRaMeshApp();
    ~LoRaMeshApp() override;

    bool init() override;
    bool run() override;
    bool pause() override;
    bool resume() override;
    bool back() override;
    bool close() override;
    void requestSelfTestOnNextOpen();
    bool applyStoredSettingsFromSettings(bool run_self_test);
    bool syncRuntimeStateFromStorage();
    bool startSelfTestFromSettings();
    bool startAutoDetectFromSettings();
    std::string getSelfTestStatus(bool *is_running = nullptr, bool *has_result = nullptr) const;

    bool debugShowTargetsVisible();
    bool debugShowCommonChatVisible();
    bool debugShowSettingsVisible();
    bool debugShowPeerChatVisible(const std::string &peer_id);
    bool debugRunSelfTestVisible();
    bool debugStopSelfTestVisible();
    bool debugSendCommonMessageVisible(const std::string &message);
    bool debugSendModuleCommandVisible(const std::string &command);
    std::string debugDescribeState() const;
    std::vector<std::string> debugListPeerSummaries() const;
    size_t debugLogLineCount() const;
    std::vector<std::string> debugLogLinesSince(size_t start_line, size_t max_lines) const;
    std::vector<std::string> debugRecentLogLines(size_t max_lines) const;

private:
    bool queueDebugUiAction(int action, const std::string &peer_id);

    struct Impl;
    Impl *_impl;
};