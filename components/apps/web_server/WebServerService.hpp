#pragma once

#include <string>

class WebServerService {
public:
    enum class ContentRoot {
        SdCard,
        Spiffs,
        Embedded,
    };

    static WebServerService &instance();

    bool start();
    bool stop();
    bool toggle();

    bool isRunning() const;
    bool isApModeActive() const;
    std::string statusText() const;
    std::string primaryUrl() const;
    std::string recoveryUrl() const;
    std::string sourceSummary() const;
    std::string mdnsUrl() const;
    ContentRoot resolveContentRoot(bool allowMount) const;

private:
    WebServerService();
    ~WebServerService();
    WebServerService(const WebServerService &) = delete;
    WebServerService &operator=(const WebServerService &) = delete;

    bool registerHandlers();
    bool startMdns();
    void stopMdns();
    std::string contentRootLabel(ContentRoot root) const;
    std::string lastErrorText() const;
    void setLastError(const char *format, ...);

    void *_server;
    bool _running;
    bool _mdnsStarted;
    char _lastError[160];
};