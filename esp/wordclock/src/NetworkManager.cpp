#include "NetworkManager.h"
#include "SerialHelper.h"
#include "esp_task_wdt.h"

#define MAX_GIF_SIZE 32768 // 32KB limit
#define AP_SSID "WordClock"

// Static instance pointer for callbacks
NetworkManager* NetworkManager::instance = nullptr;

NetworkManager::NetworkManager(long defaultGmtOffset_sec, int defaultDaylightOffset_sec)
    : defaultGmtOffset_sec(defaultGmtOffset_sec),
      defaultDaylightOffset_sec(defaultDaylightOffset_sec),
      gmtOffset_sec(defaultGmtOffset_sec), 
      daylightOffset_sec(defaultDaylightOffset_sec), 
      lastSyncTime(0), 
      lastConnectionAttempt(0),
      apModeStartTime(0),
      gifBuffer(nullptr), 
      gifBufferSize(0),
      webConfigServer(nullptr),
      apModeActive(false),
      scanInProgress(false),
      cachedNetworksJSON(""),
      lastScanTime(0),
      scanStartTime(0),
      scanRetryCount(0),
      resetButtonPin(-1),
      buttonPressStart(0),
      buttonPressed(false),
      triggerGifCallback(nullptr),
      triggerWordsTestCallback(nullptr),
      triggerLedTestCallback(nullptr),
      resumeNormalCallback(nullptr)
{
    instance = this; // Set static instance for callbacks
}

void NetworkManager::setup()
{
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);
    
    // Load timezone settings from flash (or use defaults)
    loadTimezoneSettings();
    
    // Try to load stored credentials
    if (loadCredentials() && storedSSID.length() > 0)
    {
        SERIAL_PRINTLN("Attempting to connect to stored WiFi...");
        esp_task_wdt_reset(); // Feed watchdog before connection attempt
        
        if (connectToWiFi())
        {
            SERIAL_PRINTLN("WiFi connected successfully");
            syncTimeWithNTP();
            apModeActive = false;
            
            // Start web server in normal mode
            startNormalModeWebServer();
            return;
        }
    }
    
    // If connection failed, start AP mode
    SERIAL_PRINTLN("Could not connect to WiFi. Starting AP mode...");
    esp_task_wdt_reset(); // Feed watchdog before starting AP mode
    startAPMode();
}

void NetworkManager::update()
{
    // Feed watchdog at start of update
    esp_task_wdt_reset();
    
    // Check if async WiFi scan is complete
    if (scanInProgress)
    {
        unsigned long currentMillis = millis();
        int n = WiFi.scanComplete();
        
        if (n >= 0)
        {
            // Scan completed successfully
            unsigned long scanDuration = currentMillis - scanStartTime;
            SERIAL_PRINT("Async scan complete in ");
            SERIAL_PRINT(String(scanDuration).c_str());
            SERIAL_PRINT("ms. Found ");
            SERIAL_PRINT(String(n).c_str());
            SERIAL_PRINTLN(" networks");
            
            cachedNetworksJSON = getNetworksJSON();
            scanInProgress = false;
            lastScanTime = millis();
            scanRetryCount = 0;
            
            // Clean up scan results
            WiFi.scanDelete();
        }
        else if (n == WIFI_SCAN_FAILED)
        {
            unsigned long scanDuration = currentMillis - scanStartTime;
            SERIAL_PRINT("Async WiFi scan failed after ");
            SERIAL_PRINT(String(scanDuration).c_str());
            SERIAL_PRINTLN("ms");
            
            WiFi.scanDelete();
            scanInProgress = false;
            lastScanTime = millis();
            
            // Retry scan up to 3 times
            if (apModeActive && scanRetryCount < 3)
            {
                scanRetryCount++;
                SERIAL_PRINT("Will retry scan (attempt ");
                SERIAL_PRINT(String(scanRetryCount).c_str());
                SERIAL_PRINTLN("/3)");
                cachedNetworksJSON = "{\"status\":\"retrying\"}";
            }
            else
            {
                cachedNetworksJSON = "{\"networks\":[]}";
                scanRetryCount = 0;
            }
        }
        else if (n == WIFI_SCAN_RUNNING)
        {
            // Check for timeout (30 seconds max)
            if (currentMillis - scanStartTime > 30000)
            {
                SERIAL_PRINTLN("Scan timeout after 30 seconds, canceling...");
                WiFi.scanDelete();
                scanInProgress = false;
                lastScanTime = millis();
                cachedNetworksJSON = "{\"status\":\"retrying\"}";
                
                if (scanRetryCount < 3)
                {
                    scanRetryCount++;
                }
            }
            // else scan still running normally
        }
    }
    
    // Retry scan if needed
    if (apModeActive && scanRetryCount > 0 && scanRetryCount <= 3 && !scanInProgress)
    {
        unsigned long currentMillis = millis();
        // Wait 2 seconds between retries
        if (currentMillis - lastScanTime > 2000)
        {
            SERIAL_PRINTLN("Retrying scan...");
            startNetworkScan();
        }
    }
    
    // CRITICAL: Update web config server in BOTH AP and Normal modes (handles reboots!)
    if (webConfigServer)
    {
        webConfigServer->update();
        yield(); // Allow other tasks to run
    }
    
    // AP mode specific logic
    if (apModeActive)
    {
        // Check if we should try to reconnect or timeout
        unsigned long currentMillis = millis();
        
        // Check for timeout (10 minutes in AP mode)
        if (currentMillis - apModeStartTime >= apModeTimeout)
        {
            SERIAL_PRINTLN("AP mode timeout reached. Rebooting...");
            delay(1000);
            ESP.restart();
        }
        
        // Periodically retry connection if credentials exist
        if (storedSSID.length() > 0 && currentMillis - lastConnectionAttempt >= retryInterval)
        {
            SERIAL_PRINTLN("Retrying WiFi connection...");
            lastConnectionAttempt = currentMillis;
            
            esp_task_wdt_reset(); // Feed watchdog before connection attempt
            
            if (connectToWiFi())
            {
                SERIAL_PRINTLN("WiFi connected! Stopping AP mode...");
                
                // Stop web config server
                if (webConfigServer)
                {
                    webConfigServer->stop();
                    delete webConfigServer;
                    webConfigServer = nullptr;
                }
                
                // Clear scan state
                if (scanInProgress || WiFi.scanComplete() != WIFI_SCAN_FAILED)
                {
                    WiFi.scanDelete();
                }
                scanInProgress = false;
                cachedNetworksJSON = "";
                
                WiFi.softAPdisconnect(true);
                WiFi.mode(WIFI_STA); // Switch to STA only mode
                apModeActive = false;
                syncTimeWithNTP();
                
                // Start web server in normal mode
                startNormalModeWebServer();
                return;
            }
        }
    }
    else
    {
        // Normal mode - sync time periodically
        unsigned long currentMillis = millis();
        if (currentMillis - lastSyncTime >= syncInterval)
        {
            syncTimeWithNTP();
        }
        
        // Check if WiFi is still connected
        if (WiFi.status() != WL_CONNECTED)
        {
            SERIAL_PRINTLN("WiFi disconnected. Attempting reconnect...");
            esp_task_wdt_reset(); // Feed watchdog before reconnection
            
            if (!connectToWiFi())
            {
                SERIAL_PRINTLN("Reconnection failed. Starting AP mode...");
                startAPMode();
            }
        }
    }
}

bool NetworkManager::loadCredentials()
{
    preferences.begin("wifi", true); // Read-only
    storedSSID = preferences.getString("ssid", "");
    storedPassword = preferences.getString("password", "");
    preferences.end();
    
    SERIAL_PRINT("Loaded SSID: ");
    SERIAL_PRINTLN(storedSSID.c_str());
    
    return storedSSID.length() > 0;
}

void NetworkManager::saveCredentials(String ssid, String password)
{
    preferences.begin("wifi", false); // Read-write
    preferences.putString("ssid", ssid);
    preferences.putString("password", password);
    preferences.end();
    
    storedSSID = ssid;
    storedPassword = password;
    
    SERIAL_PRINTLN("Credentials saved to flash");
}

bool NetworkManager::loadTimezoneSettings()
{
    preferences.begin("timezone", false); // Read-write for migration
    
    // Try to load TZ string (new format)
    tzString = preferences.getString("tzString", "");
    
    SERIAL_PRINTLN("=== Loading Timezone Settings ===");
    SERIAL_PRINT("TZ String from flash: '");
    SERIAL_PRINT(tzString.c_str());
    SERIAL_PRINT("' (length: ");
    SERIAL_PRINT(String(tzString.length()).c_str());
    SERIAL_PRINTLN(")");
    
    // If empty or invalid, check for old format and migrate OR use default
    if (tzString.length() == 0)
    {
        SERIAL_PRINTLN("No TZ string found. Checking for old format...");
        
        // Check if old format exists (gmtOffset and dstOffset)
        if (preferences.isKey("gmtOffset"))
        {
            long oldGmtOffset = preferences.getLong("gmtOffset", 0);
            SERIAL_PRINT("Found old format with GMT offset: ");
            SERIAL_PRINTLN(String(oldGmtOffset).c_str());
            SERIAL_PRINTLN("WARNING: Old timezone format detected!");
            SERIAL_PRINTLN("Please reconfigure timezone in settings for automatic DST support.");
            
            // Remove old keys to avoid confusion
            preferences.remove("gmtOffset");
            preferences.remove("dstOffset");
        }
        
        // Use default CET timezone
        SERIAL_PRINTLN("Using default timezone: CET (Amsterdam/Berlin/Paris)");
        tzString = "CET-1CEST,M3.5.0,M10.5.0/3";
        
        // Save the default to flash
        preferences.putString("tzString", tzString);
        SERIAL_PRINTLN("Default timezone saved to flash");
    }
    
    preferences.end();
    
    SERIAL_PRINT("Final TZ String: ");
    SERIAL_PRINTLN(tzString.c_str());
    
    return true;
}

void NetworkManager::saveTimezoneSettings(String newTzString)
{
    SERIAL_PRINTLN("=== Saving Timezone Settings ===");
    SERIAL_PRINT("New TZ String: '");
    SERIAL_PRINT(newTzString.c_str());
    SERIAL_PRINT("' (length: ");
    SERIAL_PRINT(String(newTzString.length()).c_str());
    SERIAL_PRINTLN(")");
    
    preferences.begin("timezone", false); // Read-write
    
    // Remove old format keys if they exist
    if (preferences.isKey("gmtOffset"))
    {
        SERIAL_PRINTLN("Removing old gmtOffset key");
        preferences.remove("gmtOffset");
    }
    if (preferences.isKey("dstOffset"))
    {
        SERIAL_PRINTLN("Removing old dstOffset key");
        preferences.remove("dstOffset");
    }
    
    // Save new TZ string
    preferences.putString("tzString", newTzString);
    preferences.end();
    
    tzString = newTzString;
    
    SERIAL_PRINTLN("Timezone settings saved to flash successfully");
    SERIAL_PRINT("Stored TZ String: ");
    SERIAL_PRINTLN(tzString.c_str());
}

void NetworkManager::clearAllSettings()
{
    SERIAL_PRINTLN("Clearing all settings from flash...");
    
    preferences.begin("wifi", false);
    preferences.clear();
    preferences.end();
    
    preferences.begin("timezone", false);
    preferences.clear();
    preferences.end();
    
    storedSSID = "";
    storedPassword = "";
    gmtOffset_sec = defaultGmtOffset_sec;
    daylightOffset_sec = defaultDaylightOffset_sec;
    
    SERIAL_PRINTLN("All settings cleared!");
}

void NetworkManager::setResetButtonPin(int pin)
{
    resetButtonPin = pin;
}

bool NetworkManager::checkResetButton()
{
    if (resetButtonPin < 0)
    {
        return false; // Button not configured
    }
    
    int buttonState = digitalRead(resetButtonPin);
    
    // Button is pressed (LOW on most ESP32 boards with pull-up)
    if (buttonState == LOW)
    {
        if (!buttonPressed)
        {
            // Button just pressed
            buttonPressed = true;
            buttonPressStart = millis();
            SERIAL_PRINTLN("Reset button pressed...");
        }
        else
        {
            // Button held down
            unsigned long pressDuration = millis() - buttonPressStart;
            
            // Check if held for 3+ seconds
            if (pressDuration >= 3000)
            {
                SERIAL_PRINTLN("RESET! Clearing all settings...");
                
                // Clear all settings
                clearAllSettings();
                
                // Return true to signal that reset was triggered
                return true;
            }
        }
    }
    else
    {
        // Button released
        if (buttonPressed)
        {
            unsigned long pressDuration = millis() - buttonPressStart;
            SERIAL_PRINT("Button released after ");
            SERIAL_PRINT(String(pressDuration).c_str());
            SERIAL_PRINTLN("ms");
            
            buttonPressed = false;
        }
    }
    
    return false;
}

bool NetworkManager::connectToWiFi()
{
    if (storedSSID.length() == 0)
    {
        return false;
    }
    
    SERIAL_PRINT("Connecting to: ");
    SERIAL_PRINTLN(storedSSID.c_str());
    
    // Only change WiFi mode if we're not already in AP mode
    // If in AP mode, we're already in AP_STA mode which allows connection attempts
    if (!apModeActive)
    {
        WiFi.mode(WIFI_STA);
    }
    
    WiFi.begin(storedSSID.c_str(), storedPassword.c_str());
    
    unsigned long startAttempt = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < connectionTimeout)
    {
        delay(100); // Shorter delay for more responsive button checking
        esp_task_wdt_reset(); // Feed watchdog during connection attempt
        yield(); // Allow other tasks to run
        
        // Check reset button during connection attempt
        if (checkResetButton())
        {
            SERIAL_PRINTLN("\nReset triggered during connection. Rebooting...");
            delay(1000);
            ESP.restart();
        }
        
        // Print progress every 500ms
        static unsigned long lastPrint = 0;
        if (millis() - lastPrint >= 500)
        {
            SERIAL_PRINT(".");
            lastPrint = millis();
        }
    }
    SERIAL_PRINTLN("");
    
    if (WiFi.status() == WL_CONNECTED)
    {
        SERIAL_PRINT("Connected! IP: ");
        SERIAL_PRINTLN(WiFi.localIP().toString().c_str());
        return true;
    }
    
    SERIAL_PRINTLN("Connection failed");
    return false;
}

void NetworkManager::startAPMode()
{
    apModeActive = true;
    apModeStartTime = millis();
    lastConnectionAttempt = millis();
    
    // Disconnect from any WiFi connections first
    WiFi.disconnect(true);
    delay(100);
    
    // Use AP_STA mode to allow scanning while in AP mode
    WiFi.mode(WIFI_AP_STA);
    delay(100); // Let WiFi mode stabilize
    
    // Start the AP
    WiFi.softAP(AP_SSID);
    delay(100); // Let AP stabilize
    
    SERIAL_PRINT("AP Mode started. SSID: ");
    SERIAL_PRINTLN(AP_SSID);
    SERIAL_PRINT("AP IP address: ");
    SERIAL_PRINTLN(WiFi.softAPIP().toString().c_str());
    
    // Do initial WiFi scan BEFORE starting web server to avoid TCP conflicts
    SERIAL_PRINTLN("Performing initial WiFi scan before starting web server...");
    startNetworkScan();
    
    // Wait for scan to complete (with timeout)
    unsigned long scanStart = millis();
    while (scanInProgress && (millis() - scanStart < 15000))
    {
        delay(100);
        esp_task_wdt_reset();
        // Check if async scan completed
        int result = WiFi.scanComplete();
        if (result >= 0 || result == WIFI_SCAN_FAILED)
        {
            if (result >= 0)
            {
                SERIAL_PRINT("Initial scan found ");
                SERIAL_PRINT(String(result).c_str());
                SERIAL_PRINTLN(" networks");
                cachedNetworksJSON = buildNetworksJSON(result);
                lastScanTime = millis();
            }
            scanInProgress = false;
            break;
        }
    }
    
    if (scanInProgress)
    {
        SERIAL_PRINTLN("Initial scan timed out, will retry later");
        WiFi.scanDelete();
        scanInProgress = false;
    }
    
    // Now it's safe to start the web server with cached results
    webConfigServer = new WebConfigServer();
    webConfigServer->onSaveConfig(onWebSaveConfig);
    webConfigServer->onGetNetworks(onWebGetNetworks);
    webConfigServer->onStartScan(onWebStartScan);
    webConfigServer->start(WiFi.softAPIP(), MODE_SETUP);
    
    // WebConfigServer will trigger the initial scan automatically
    // No blocking delays here to prevent watchdog timeouts
}

void NetworkManager::startNetworkScan()
{
    if (scanInProgress)
    {
        SERIAL_PRINTLN("Scan already in progress, skipping...");
        return;
    }
    
    // Don't scan too soon after AP mode starts - let system stabilize
    if (apModeActive && (millis() - apModeStartTime < 2000))
    {
        SERIAL_PRINTLN("AP mode too new, postponing scan...");
        cachedNetworksJSON = "{\"status\":\"scanning\"}";
        return;
    }
    
    // Clear any previous scan results first
    int scanStatus = WiFi.scanComplete();
    if (scanStatus != WIFI_SCAN_FAILED)
    {
        WiFi.scanDelete();
    }
    
    SERIAL_PRINTLN("Starting async WiFi scan...");
    
    // Ensure we're in the right mode
    WiFi.mode(WIFI_AP_STA);
    delay(100); // Give mode change time to settle
    
    // Make sure station is disconnected
    if (WiFi.status() == WL_CONNECTED)
    {
        WiFi.disconnect(false);
        delay(200); // Increased delay for disconnect to complete
    }
    
    scanInProgress = true;
    scanStartTime = millis();
    
    // Start ASYNC scan (non-blocking)
    // Parameters: async=true, show_hidden=false, passive=false, max_ms_per_chan=500
    // Increased from 300ms to 500ms per channel for better reliability
    int result = WiFi.scanNetworks(true, false, false, 500);
    
    if (result == WIFI_SCAN_FAILED)
    {
        SERIAL_PRINTLN("Failed to start WiFi scan");
        scanInProgress = false;
        
        // Retry up to 3 times
        if (apModeActive && scanRetryCount < 3)
        {
            scanRetryCount++;
            SERIAL_PRINT("Will retry scan (attempt ");
            SERIAL_PRINT(String(scanRetryCount).c_str());
            SERIAL_PRINTLN("/3)");
            cachedNetworksJSON = "{\"status\":\"retrying\"}";
            lastScanTime = millis();
        }
        else
        {
            cachedNetworksJSON = "{\"networks\":[]}";
            scanRetryCount = 0;
        }
    }
    else
    {
        SERIAL_PRINTLN("Async WiFi scan started successfully");
        cachedNetworksJSON = "{\"status\":\"scanning\"}";
    }
}

String NetworkManager::buildNetworksJSON(int n)
{
    // Build JSON from scan results
    String json = "{\"networks\":[";
    int addedNetworks = 0;
    
    for (int i = 0; i < n; i++)
    {
        String ssid = WiFi.SSID(i);
        
        // Skip networks with empty SSID (hidden networks)
        if (ssid.length() == 0)
        {
            continue;
        }
        
        if (addedNetworks > 0) json += ",";
        json += "{";
        json += "\"ssid\":\"" + ssid + "\",";
        json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
        json += "\"encryption\":" + String(WiFi.encryptionType(i));
        json += "}";
        addedNetworks++;
        
        // Yield periodically to prevent blocking
        if (i % 5 == 0)
        {
            yield();
        }
    }
    json += "]}";
    
    SERIAL_PRINT("Built JSON with ");
    SERIAL_PRINT(String(addedNetworks).c_str());
    SERIAL_PRINTLN(" networks");
    
    return json;
}

String NetworkManager::getNetworksJSON()
{
    // This method should only be called after scanComplete() returns a positive number
    // It reads the results from the last completed scan
    int n = WiFi.scanComplete();
    
    SERIAL_PRINT("getNetworksJSON - scanComplete returned: ");
    SERIAL_PRINTLN(String(n).c_str());
    
    if (n < 0)
    {
        // No scan results available (-1 = running, -2 = failed)
        return "{\"networks\":[]}";
    }
    
    return buildNetworksJSON(n);
}

bool NetworkManager::isConnected()
{
    return !apModeActive && WiFi.status() == WL_CONNECTED;
}

bool NetworkManager::isInAPMode()
{
    return apModeActive;
}

void NetworkManager::syncTimeWithNTP()
{
    const char *ntpServer = "pool.ntp.org";
    
    SERIAL_PRINTLN("=== Starting NTP Time Sync ===");
    SERIAL_PRINT("Timezone string: ");
    SERIAL_PRINTLN(tzString.c_str());
    
    // Configure NTP client FIRST (with 0 offsets)
    SERIAL_PRINTLN("Configuring NTP client...");
    configTime(0, 0, ntpServer);

    // Wait for time to be set (up to 10 seconds)
    SERIAL_PRINTLN("Waiting for NTP time sync...");
    int retry = 0;
    const int maxRetries = 20;
    struct tm timeinfo;
    
    while (retry < maxRetries)
    {
        if (getLocalTime(&timeinfo))
        {
            // Time successfully retrieved from NTP
            SERIAL_PRINTLN("NTP sync successful!");
            break;
        }
        delay(500);
        retry++;
        esp_task_wdt_reset(); // Feed watchdog during wait
    }
    
    if (retry >= maxRetries)
    {
        SERIAL_PRINTLN("ERROR: Failed to obtain time from NTP after retries");
        lastSyncTime = millis();
        return;
    }
    
    // NOW set timezone using POSIX TZ string (enables automatic DST switching)
    SERIAL_PRINTLN("Applying timezone...");
    SERIAL_PRINT("Setting TZ environment variable to: ");
    SERIAL_PRINTLN(tzString.c_str());
    
    setenv("TZ", tzString.c_str(), 1);
    tzset();
    
    // Small delay to ensure timezone is applied
    delay(100);
    
    // Verify timezone was applied correctly
    if (getLocalTime(&timeinfo))
    {
        char timeStr[64];
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S %Z (UTC%z)", &timeinfo);
        SERIAL_PRINTLN("=== Timezone Applied Successfully ===");
        SERIAL_PRINT("Local time: ");
        SERIAL_PRINTLN(timeStr);
        SERIAL_PRINT("Hour: ");
        SERIAL_PRINT(String(timeinfo.tm_hour).c_str());
        SERIAL_PRINT(", Minute: ");
        SERIAL_PRINTLN(String(timeinfo.tm_min).c_str());
        SERIAL_PRINT("Is DST active: ");
        SERIAL_PRINTLN(timeinfo.tm_isdst > 0 ? "Yes" : "No");
    }
    else
    {
        SERIAL_PRINTLN("ERROR: Could not get local time after timezone set");
    }
    
    lastSyncTime = millis();
}

struct tm NetworkManager::getLocalTimeStruct()
{
    struct tm timeinfo;
    if (getLocalTime(&timeinfo))
    {
        return timeinfo;
    }
    else
    {
        memset(&timeinfo, 0, sizeof(struct tm));
        SERIAL_PRINTLN("Failed to obtain local time");
        return timeinfo;
    }
}

bool NetworkManager::downloadGIF(const char *gifUrl)
{
    if (WiFi.status() == WL_CONNECTED)
    {
        HTTPClient http;
        http.begin(gifUrl);

        int httpResponseCode = http.GET();
        if (httpResponseCode == HTTP_CODE_OK)
        {
            int gifSize = http.getSize();

            if (gifSize > MAX_GIF_SIZE)
            {
                SERIAL_PRINTLN("GIF is too large. Max size allowed is 32KB.");
                http.end();
                return false;
            }

            gifBuffer = handleDownloadGIFResponse(http, gifSize);
            gifBufferSize = gifSize;
            http.end();
            return gifBuffer != nullptr;
        }
        else
        {
            SERIAL_PRINTLN("Failed to download GIF");
            http.end();
            return false;
        }
    }
    else
    {
        SERIAL_PRINTLN("WiFi not connected");
        return false;
    }
}

uint8_t *NetworkManager::handleDownloadGIFResponse(HTTPClient &http, int gifSize)
{
    WiFiClient *stream = http.getStreamPtr();

    if (gifSize > 0)
    {
        SERIAL_PRINTLN("Downloading GIF...");

        uint8_t *buffer = (uint8_t *)malloc(gifSize);
        if (buffer == nullptr)
        {
            SERIAL_PRINTLN("Memory allocation failed for GIF");
            return nullptr;
        }

        int bytesRead = 0;
        while (http.connected() && stream->available() > 0 && bytesRead < gifSize)
        {
            int byte = stream->read();
            buffer[bytesRead++] = byte;
        }

        SERIAL_PRINTLN("GIF downloaded and stored in memory");
        return buffer;
    }
    else
    {
        SERIAL_PRINTLN("No data available for GIF");
        return nullptr;
    }
}

uint8_t *NetworkManager::getGifBuffer()
{
    return gifBuffer;
}

size_t NetworkManager::getGifBufferSize()
{
    return gifBufferSize;
}

// Static callback functions for WebConfigServer
void NetworkManager::onWebSaveConfig(String ssid, String password, String tzString)
{
    SERIAL_PRINTLN("=== onWebSaveConfig called ===");
    SERIAL_PRINT("SSID: '");
    SERIAL_PRINT(ssid.c_str());
    SERIAL_PRINT("', Password length: ");
    SERIAL_PRINT(String(password.length()).c_str());
    SERIAL_PRINT(", TZ string: ");
    SERIAL_PRINTLN(tzString.c_str());
    
    if (instance)
    {
        // Check for special marker to keep WiFi unchanged
        if (ssid != "__KEEP_WIFI__" && ssid.length() > 0)
        {
            SERIAL_PRINTLN("Saving WiFi credentials...");
            instance->saveCredentials(ssid, password);
        }
        else
        {
            SERIAL_PRINTLN("Keeping existing WiFi credentials (timezone-only update)");
        }
        
        // Check for special marker to keep timezone unchanged
        if (tzString != "__KEEP_TZ__" && tzString.length() > 0)
        {
            SERIAL_PRINTLN("Saving timezone settings...");
            instance->saveTimezoneSettings(tzString);
        }
        else
        {
            SERIAL_PRINTLN("Keeping existing timezone settings (WiFi-only update)");
        }
        
        SERIAL_PRINTLN("=== onWebSaveConfig complete ===");
    }
    else
    {
        SERIAL_PRINTLN("ERROR: No NetworkManager instance!");
    }
}

String NetworkManager::onWebGetNetworks()
{
    if (instance)
    {
        // If we have cached results less than 30 seconds old, return them
        if (instance->cachedNetworksJSON.length() > 0 && 
            instance->cachedNetworksJSON != "{\"status\":\"retrying\"}" &&
            instance->cachedNetworksJSON != "{\"status\":\"scanning\"}" &&
            (millis() - instance->lastScanTime < 30000))
        {
            SERIAL_PRINTLN("Returning cached scan results");
            return instance->cachedNetworksJSON;
        }
        // If a scan is already in progress, return in-progress status
        else if (instance->scanInProgress)
        {
            SERIAL_PRINTLN("Scan in progress...");
            return "{\"status\":\"scanning\"}";
        }
        // Start a new async scan
        else
        {
            SERIAL_PRINTLN("Starting scan on user request");
            instance->startNetworkScan();
            // Return scanning status (async scan is now running)
            return instance->cachedNetworksJSON;
        }
    }
    return "{\"error\":\"No instance\"}";
}

void NetworkManager::onWebStartScan()
{
    if (instance)
    {
        instance->startNetworkScan();
    }
}

String NetworkManager::onWebGetStatus()
{
    SERIAL_PRINTLN("onWebGetStatus called");
    if (instance)
    {
        SERIAL_PRINTLN("Getting status JSON from instance...");
        String json = instance->getStatusJSON();
        SERIAL_PRINT("Returning status: ");
        SERIAL_PRINTLN(json.c_str());
        return json;
    }
    SERIAL_PRINTLN("ERROR: No NetworkManager instance!");
    return "{\"error\":\"No instance\"}";
}

String NetworkManager::onWebGetTimezoneSettings()
{
    SERIAL_PRINTLN("onWebGetTimezoneSettings called");
    if (instance)
    {
        String json = instance->getTimezoneSettingsJSON();
        SERIAL_PRINT("Returning timezone settings: ");
        SERIAL_PRINTLN(json.c_str());
        return json;
    }
    SERIAL_PRINTLN("ERROR: No NetworkManager instance!");
    return "{\"error\":\"No instance\"}";
}

void NetworkManager::onWebTriggerGif()
{
    if (instance && instance->triggerGifCallback)
    {
        SERIAL_PRINTLN("GIF trigger requested from web");
        instance->triggerGifCallback();
    }
}

void NetworkManager::onWebTriggerWordsTest()
{
    if (instance && instance->triggerWordsTestCallback)
    {
        SERIAL_PRINTLN("Words test requested from web");
        instance->triggerWordsTestCallback();
    }
}

void NetworkManager::onWebTriggerLedTest()
{
    if (instance && instance->triggerLedTestCallback)
    {
        SERIAL_PRINTLN("LED test requested from web");
        instance->triggerLedTestCallback();
    }
}

void NetworkManager::onWebResumeNormal()
{
    if (instance && instance->resumeNormalCallback)
    {
        SERIAL_PRINTLN("Resume normal requested from web");
        instance->resumeNormalCallback();
    }
}

void NetworkManager::setTriggerGifCallback(void (*callback)())
{
    triggerGifCallback = callback;
}

void NetworkManager::setTriggerWordsTestCallback(void (*callback)())
{
    triggerWordsTestCallback = callback;
}

void NetworkManager::setTriggerLedTestCallback(void (*callback)())
{
    triggerLedTestCallback = callback;
}

void NetworkManager::setResumeNormalCallback(void (*callback)())
{
    resumeNormalCallback = callback;
}

String NetworkManager::getStatusJSON()
{
    SERIAL_PRINTLN("Building status JSON...");
    String json = "{";
    
    // WiFi info
    json += "\"ssid\":\"" + WiFi.SSID() + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
    json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
    
    // Time info
    struct tm timeinfo = getLocalTimeStruct();
    char timeStr[64];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
    
    SERIAL_PRINT("Status JSON time - Hour: ");
    SERIAL_PRINT(String(timeinfo.tm_hour).c_str());
    SERIAL_PRINT(", Minute: ");
    SERIAL_PRINT(String(timeinfo.tm_min).c_str());
    SERIAL_PRINT(", Formatted: ");
    SERIAL_PRINTLN(timeStr);
    
    json += "\"time\":\"" + String(timeStr) + "\",";
    
    // Timezone info
    json += "\"timezone\":\"" + tzString + "\",";
    
    // Uptime
    unsigned long uptimeSeconds = millis() / 1000;
    unsigned long days = uptimeSeconds / 86400;
    unsigned long hours = (uptimeSeconds % 86400) / 3600;
    unsigned long minutes = (uptimeSeconds % 3600) / 60;
    unsigned long seconds = uptimeSeconds % 60;
    
    char uptimeStr[64];
    sprintf(uptimeStr, "%lud %02lu:%02lu:%02lu", days, hours, minutes, seconds);
    json += "\"uptime\":\"" + String(uptimeStr) + "\"";
    
    json += "}";
    return json;
}

String NetworkManager::getTimezoneSettingsJSON()
{
    SERIAL_PRINTLN("Building timezone settings JSON...");
    String json = "{";
    json += "\"tzString\":\"" + tzString + "\"";
    json += "}";
    SERIAL_PRINT("Timezone settings JSON: ");
    SERIAL_PRINTLN(json.c_str());
    return json;
}

void NetworkManager::startNormalModeWebServer()
{
    if (webConfigServer && webConfigServer->isRunning())
    {
        SERIAL_PRINTLN("Web server already running");
        return;
    }
    
    if (!isConnected())
    {
        SERIAL_PRINTLN("Cannot start web server - not connected to WiFi");
        return;
    }
    
    // Create and start web server in normal mode
    webConfigServer = new WebConfigServer();
    webConfigServer->onSaveConfig(onWebSaveConfig);
    webConfigServer->onGetNetworks(onWebGetNetworks);
    webConfigServer->onStartScan(onWebStartScan);
    webConfigServer->onGetStatus(onWebGetStatus);
    webConfigServer->onGetTimezoneSettings(onWebGetTimezoneSettings); // NEW: timezone settings
    webConfigServer->onTriggerGif(onWebTriggerGif);
    webConfigServer->onTriggerWordsTest(onWebTriggerWordsTest);
    webConfigServer->onTriggerLedTest(onWebTriggerLedTest);
    webConfigServer->onResumeNormal(onWebResumeNormal);
    webConfigServer->start(WiFi.localIP(), MODE_NORMAL);
}
