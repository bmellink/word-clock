#ifndef WEB_CONFIG_SERVER_H
#define WEB_CONFIG_SERVER_H

#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <WiFi.h>

// Callback function types
typedef void (*SaveConfigCallback)(String ssid, String password, String tzString); // Updated for TZ strings
typedef String (*GetNetworksCallback)();
typedef void (*StartScanCallback)();
typedef String (*GetStatusCallback)();
typedef String (*GetTimezoneSettingsCallback)(); // Get current timezone settings
typedef void (*TriggerGifCallback)();
typedef void (*TriggerWordsTestCallback)();
typedef void (*TriggerLedTestCallback)();
typedef void (*ResumeNormalCallback)();

enum ServerMode {
    MODE_SETUP,    // AP mode - initial configuration
    MODE_NORMAL    // Connected mode - status and control
};

class WebConfigServer
{
public:
    WebConfigServer();
    ~WebConfigServer();
    
    void start(IPAddress ip, ServerMode mode = MODE_SETUP);
    void stop();
    void update();
    bool isRunning();
    ServerMode getMode();
    
    // Set callbacks
    void onSaveConfig(SaveConfigCallback callback);
    void onGetNetworks(GetNetworksCallback callback);
    void onStartScan(StartScanCallback callback);
    void onGetStatus(GetStatusCallback callback);
    void onGetTimezoneSettings(GetTimezoneSettingsCallback callback); // New
    void onTriggerGif(TriggerGifCallback callback);
    void onTriggerWordsTest(TriggerWordsTestCallback callback);
    void onTriggerLedTest(TriggerLedTestCallback callback);
    void onResumeNormal(ResumeNormalCallback callback);
    
    // Notify that save was successful (triggers reboot)
    void notifySaveSuccess();

private:
    AsyncWebServer *server;
    DNSServer *dnsServer;
    bool running;
    bool shouldReboot;
    ServerMode mode;
    
    // Callbacks
    SaveConfigCallback saveConfigCallback;
    GetNetworksCallback getNetworksCallback;
    StartScanCallback startScanCallback;
    GetStatusCallback getStatusCallback;
    GetTimezoneSettingsCallback getTimezoneSettingsCallback; // New
    TriggerGifCallback triggerGifCallback;
    TriggerWordsTestCallback triggerWordsTestCallback;
    TriggerLedTestCallback triggerLedTestCallback;
    ResumeNormalCallback resumeNormalCallback;
    
    void setupRoutes();
    void handleRoot(AsyncWebServerRequest *request);
    void handleScan(AsyncWebServerRequest *request);
    void handleSave(AsyncWebServerRequest *request);  // For setup mode (saves both WiFi and timezone)
    void handleSaveWifi(AsyncWebServerRequest *request);
    void handleSaveTimezone(AsyncWebServerRequest *request);
    void handleStatus(AsyncWebServerRequest *request);
    void handleWifiSettings(AsyncWebServerRequest *request);
    void handleTimezoneSettings(AsyncWebServerRequest *request);
    void handleGetTimezoneSettings(AsyncWebServerRequest *request); // New
    void handleTriggerGif(AsyncWebServerRequest *request);
    void handleTriggerWords(AsyncWebServerRequest *request);
    void handleTriggerLedTest(AsyncWebServerRequest *request);
    void handleResume(AsyncWebServerRequest *request);
    
    String generateSetupHTML();
    String generateStatusHTML();
    String generateWifiSettingsHTML();
    String generateTimezoneSettingsHTML();
    
    // Helper functions to reduce code duplication (DRY principle)
    String generateCommonCSS();
    String generateConfigFormCSS();
    String generateWifiScanJS();
    String generateTimezoneDropdown();
};

#endif

