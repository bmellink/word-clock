#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <WiFi.h>
#include <time.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include "WebConfigServer.h"

class NetworkManager
{
public:
    NetworkManager(long defaultGmtOffset_sec, int defaultDaylightOffset_sec);
    void setup();
    void update();
    struct tm getLocalTimeStruct();
    bool downloadGIF(const char *gifUrl);
    uint8_t *getGifBuffer();
    size_t getGifBufferSize();
    bool isConnected();
    bool isInAPMode();
    void clearAllSettings();
    void setResetButtonPin(int pin);
    bool checkResetButton();
    
    // Set display control callbacks
    void setTriggerGifCallback(void (*callback)());
    void setTriggerWordsTestCallback(void (*callback)());
    void setTriggerLedTestCallback(void (*callback)());
    void setResumeNormalCallback(void (*callback)());

private:
    String tzString; // POSIX TZ string with automatic DST support
    long gmtOffset_sec; // Deprecated but kept for backward compatibility
    int daylightOffset_sec; // Deprecated but kept for backward compatibility
    long defaultGmtOffset_sec;
    int defaultDaylightOffset_sec;
    unsigned long lastSyncTime;
    unsigned long lastConnectionAttempt;
    unsigned long apModeStartTime;
    const unsigned long syncInterval = 86400000;
    const unsigned long connectionTimeout = 20000; // 20 seconds
    const unsigned long retryInterval = 60000; // 1 minute retry
    const unsigned long apModeTimeout = 600000; // 10 minutes (600 seconds)
    
    uint8_t *gifBuffer = nullptr;
    size_t gifBufferSize = 0;
    
    Preferences preferences;
    WebConfigServer *webConfigServer;
    bool apModeActive;
    
    String storedSSID;
    String storedPassword;
    
    // WiFi scan state
    bool scanInProgress;
    String cachedNetworksJSON;
    unsigned long lastScanTime;
    unsigned long scanStartTime;
    int scanRetryCount;
    
    // Reset button state
    int resetButtonPin;
    unsigned long buttonPressStart;
    bool buttonPressed;

    void syncTimeWithNTP();
    uint8_t *handleDownloadGIFResponse(HTTPClient &http, int gifSize);
    
    // WiFi Manager functions
    bool loadCredentials();
    void saveCredentials(String ssid, String password);
    bool loadTimezoneSettings();
    void saveTimezoneSettings(String tzString);
    bool connectToWiFi();
    void startAPMode();
    void startNetworkScan();
    String buildNetworksJSON(int numNetworks);
    String getNetworksJSON();
    
    // Callbacks for WebConfigServer
    static void onWebSaveConfig(String ssid, String password, String tzString); // Updated for TZ strings
    static String onWebGetNetworks();
    static void onWebStartScan();
    static String onWebGetStatus();
    static String onWebGetTimezoneSettings();
    static void onWebTriggerGif();
    static void onWebTriggerWordsTest();
    static void onWebTriggerLedTest();
    static void onWebResumeNormal();
    
    String getStatusJSON();
    String getTimezoneSettingsJSON(); // New
    void startNormalModeWebServer();
    
    static NetworkManager* instance; // For static callbacks
    
    // Display control callback pointers
    void (*triggerGifCallback)();
    void (*triggerWordsTestCallback)();
    void (*triggerLedTestCallback)();
    void (*resumeNormalCallback)();
};

#endif
