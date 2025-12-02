#include "WebConfigServer.h"
#include "SerialHelper.h"
#include <WiFi.h>
#include <esp_system.h>

#define DNS_PORT 53

WebConfigServer::WebConfigServer()
    : server(nullptr),
      dnsServer(nullptr),
      running(false),
      shouldReboot(false),
      mode(MODE_SETUP),
      saveConfigCallback(nullptr),
      getNetworksCallback(nullptr),
      startScanCallback(nullptr),
      getStatusCallback(nullptr),
      getTimezoneSettingsCallback(nullptr),
      triggerGifCallback(nullptr),
      triggerWordsTestCallback(nullptr),
      triggerLedTestCallback(nullptr),
      resumeNormalCallback(nullptr)
{
}

WebConfigServer::~WebConfigServer()
{
    stop();
}

void WebConfigServer::start(IPAddress ip, ServerMode serverMode)
{
    if (running)
    {
        SERIAL_PRINTLN("Web server already running");
        return;
    }
    
    mode = serverMode;
    SERIAL_PRINT("WebConfigServer starting in mode: ");
    SERIAL_PRINTLN(mode == MODE_SETUP ? "SETUP" : "NORMAL");
    
    // Setup DNS server only for captive portal (AP mode)
    if (mode == MODE_SETUP)
    {
        dnsServer = new DNSServer();
        dnsServer->start(DNS_PORT, "*", ip);
        SERIAL_PRINTLN("DNS server started for captive portal");
    }
    
    // Setup web server
    SERIAL_PRINTLN("Creating AsyncWebServer on port 80...");
    server = new AsyncWebServer(80);
    SERIAL_PRINTLN("Calling setupRoutes()...");
    setupRoutes();
    SERIAL_PRINTLN("Starting server->begin()...");
    server->begin();
    SERIAL_PRINTLN("server->begin() completed");
    
    running = true;
    shouldReboot = false;
    
    if (mode == MODE_SETUP)
    {
        SERIAL_PRINTLN("=== Web config server started (SETUP mode) ===");
    }
    else
    {
        SERIAL_PRINT("=== Web server started (NORMAL mode) at http://");
        SERIAL_PRINT(ip.toString().c_str());
        SERIAL_PRINTLN(" ===");
    }
}

void WebConfigServer::stop()
{
    if (!running)
    {
        return;
    }
    
    if (dnsServer)
    {
        dnsServer->stop();
        delete dnsServer;
        dnsServer = nullptr;
    }
    
    if (server)
    {
        server->end();
        delete server;
        server = nullptr;
    }
    
    running = false;
    SERIAL_PRINTLN("Web config server stopped");
}

void WebConfigServer::update()
{
    if (running)
    {
        // Process DNS only in setup mode
        if (dnsServer && mode == MODE_SETUP)
        {
            dnsServer->processNextRequest();
            yield();
        }
        
        // Check if reboot was requested after save
        if (shouldReboot)
        {
            SERIAL_PRINTLN("");
            SERIAL_PRINTLN("=================================================");
            SERIAL_PRINTLN("=== REBOOT REQUESTED - shouldReboot is TRUE ===");
            SERIAL_PRINTLN("=================================================");
            SERIAL_PRINTLN("Waiting 2 seconds to send response...");
            delay(2000); // Give web response time to be sent
            
            // Properly shutdown WiFi to ensure clean reboot
            SERIAL_PRINTLN("Disconnecting WiFi...");
            WiFi.disconnect(true);  // Disconnect and turn off WiFi
            delay(100);
            SERIAL_PRINTLN("Setting WiFi mode to OFF...");
            WiFi.mode(WIFI_OFF);    // Ensure WiFi is fully off
            delay(100);
            
            SERIAL_PRINTLN("=== RESTARTING ESP32 NOW ===");
            esp_restart();  // Use esp_restart() for proper hardware reset
        }
    }
}

ServerMode WebConfigServer::getMode()
{
    return mode;
}

bool WebConfigServer::isRunning()
{
    return running;
}

// Helper: Common CSS used across multiple pages
String WebConfigServer::generateCommonCSS()
{
    String css = "";
    css += "body { font-family: Arial; margin: 20px; background: #f0f0f0; }";
    css += ".container { max-width: 500px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }";
    css += "h1 { color: #333; text-align: center; }";
    css += "h3 { color: #555; margin-top: 20px; }";
    css += "label { display: block; margin-top: 10px; font-weight: bold; color: #333; }";
    return css;
}

// Helper: CSS specific to config forms
String WebConfigServer::generateConfigFormCSS()
{
    String css = "";
    css += "input[type=password], select { width: 100%; padding: 10px; margin: 10px 0; border: 1px solid #ddd; border-radius: 5px; box-sizing: border-box; font-size: 14px; }";
    css += ".checkbox-container { display: flex; align-items: center; margin: 15px 0; }";
    css += ".checkbox-container input[type=checkbox] { width: auto; margin-right: 10px; }";
    css += ".checkbox-container label { margin: 0; cursor: pointer; }";
    css += "button { width: 100%; padding: 12px; background: #28a745; color: white; border: none; border-radius: 5px; cursor: pointer; font-size: 16px; margin-top: 10px; }";
    css += "button:hover { background: #218838; }";
    css += "button:disabled { background: #6c757d; cursor: not-allowed; }";
    css += ".btn-secondary { background: #6c757d; margin-top: 20px; }";
    css += ".btn-secondary:hover { background: #5a6268; }";
    css += ".scanning { text-align: center; color: #666; padding: 20px; }";
    css += "#config-section { display: none; margin-top: 20px; }";
    css += ".info { background: #e7f3ff; padding: 10px; border-radius: 5px; margin-bottom: 20px; color: #004085; }";
    return css;
}

// Helper: JavaScript for WiFi scanning
String WebConfigServer::generateWifiScanJS()
{
    String js = "";
    js += "let scanTimeout = null;";
    js += "function scanNetworks() {";
    js += "  document.getElementById('scan-status').innerHTML = 'Scanning for networks...';";
    js += "  document.getElementById('network-select').disabled = true;";
    js += "  fetch('/scan').then(r => r.json()).then(data => {";
    js += "    if (data.status === 'scanning' || data.status === 'retrying') {";
    js += "      scanTimeout = setTimeout(scanNetworks, 2000);";
    js += "    } else if (data.networks && data.networks.length > 0) {";
    js += "      const select = document.getElementById('network-select');";
    js += "      select.innerHTML = '<option value=\"\">-- Select WiFi Network --</option>';";
    js += "      data.networks.forEach(n => {";
    js += "        const option = document.createElement('option');";
    js += "        option.value = n.ssid;";
    js += "        option.textContent = `${n.ssid} (${n.rssi} dBm)`;";
    js += "        select.appendChild(option);";
    js += "      });";
    js += "      select.disabled = false;";
    js += "      document.getElementById('scan-status').innerHTML = `Found ${data.networks.length} network(s)`;";
    js += "      document.getElementById('password-section').style.display = 'block';";
    js += "    } else {";
    js += "      document.getElementById('scan-status').innerHTML = 'No networks found. <button onclick=\"scanNetworks()\">Retry</button>';";
    js += "    }";
    js += "  }).catch(e => {";
    js += "    document.getElementById('scan-status').innerHTML = 'Error scanning. <button onclick=\"scanNetworks()\">Retry</button>';";
    js += "  });";
    js += "}";
    js += "function networkSelected() {";
    js += "  const ssid = document.getElementById('network-select').value;";
    js += "  document.getElementById('save-btn').disabled = !ssid;";
    js += "}";
    js += "function saveWifi() {";
    js += "  const ssid = document.getElementById('network-select').value;";
    js += "  if (!ssid) { alert('Please select a network'); return; }";
    js += "  const password = document.getElementById('password').value;";
    js += "  const formData = new FormData();";
    js += "  formData.append('ssid', ssid);";
    js += "  formData.append('password', password);";
    js += "  document.getElementById('save-btn').disabled = true;";
    js += "  document.getElementById('save-btn').textContent = 'Saving...';";
    js += "  fetch('/save-wifi', { method: 'POST', body: formData })";
    js += "    .then(r => r.text()).then(msg => {";
    js += "      document.body.innerHTML = '<div class=\"container\"><h1>&#x2705; WiFi Settings Saved!</h1><p>Device is rebooting...</p></div>';";
    js += "    }).catch(e => {";
    js += "      alert('Error saving: ' + e);";
    js += "      document.getElementById('save-btn').disabled = false;";
    js += "      document.getElementById('save-btn').textContent = 'Save WiFi Settings';";
    js += "    });";
    js += "}";
    js += "window.onload = () => scanNetworks();";
    return js;
}

// Helper: Generate timezone dropdown options
String WebConfigServer::generateTimezoneDropdown()
{
    // Timezone strings with automatic DST support using POSIX TZ format
    // Format: STD offset DST [offset],start[/time],end[/time]
    String html = "";
    
    // Americas
    html += "<option value='HST10'>UTC-10 Hawaii (no DST)</option>";
    html += "<option value='AKST9AKDT,M3.2.0,M11.1.0'>UTC-9/-8 Alaska</option>";
    html += "<option value='PST8PDT,M3.2.0,M11.1.0'>UTC-8/-7 Pacific Time (US)</option>";
    html += "<option value='MST7MDT,M3.2.0,M11.1.0'>UTC-7/-6 Mountain Time (US)</option>";
    html += "<option value='MST7'>UTC-7 Arizona (no DST)</option>";
    html += "<option value='CST6CDT,M3.2.0,M11.1.0'>UTC-6/-5 Central Time (US)</option>";
    html += "<option value='EST5EDT,M3.2.0,M11.1.0'>UTC-5/-4 Eastern Time (US)</option>";
    html += "<option value='AST4ADT,M3.2.0,M11.1.0'>UTC-4/-3 Atlantic Time (Canada)</option>";
    html += "<option value='NST3:30NDT,M3.2.0,M11.1.0'>UTC-3:30/-2:30 Newfoundland</option>";
    html += "<option value='<-03>3'>UTC-3 Buenos Aires, São Paulo (no DST)</option>";
    
    // Europe & Africa
    html += "<option value='GMT0BST,M3.5.0/1,M10.5.0'>UTC+0/+1 London, Dublin</option>";
    html += "<option value='WET0WEST,M3.5.0/1,M10.5.0'>UTC+0/+1 Lisbon, Canary Islands</option>";
    html += "<option value='CET-1CEST,M3.5.0,M10.5.0/3' selected>UTC+1/+2 Amsterdam, Berlin, Paris</option>";
    html += "<option value='EET-2EEST,M3.5.0/3,M10.5.0/4'>UTC+2/+3 Athens, Helsinki, Kyiv</option>";
    html += "<option value='<+03>-3'>UTC+3 Moscow (no DST)</option>";
    html += "<option value='<+04>-4'>UTC+4 Dubai, Baku (no DST)</option>";
    
    // Asia
    html += "<option value='<+0430>-4:30'>UTC+4:30 Kabul (no DST)</option>";
    html += "<option value='<+05>-5'>UTC+5 Pakistan (no DST)</option>";
    html += "<option value='IST-5:30'>UTC+5:30 India, Sri Lanka (no DST)</option>";
    html += "<option value='<+0545>-5:45'>UTC+5:45 Nepal (no DST)</option>";
    html += "<option value='<+06>-6'>UTC+6 Bangladesh, Dhaka (no DST)</option>";
    html += "<option value='<+0630>-6:30'>UTC+6:30 Myanmar, Yangon (no DST)</option>";
    html += "<option value='<+07>-7'>UTC+7 Bangkok, Jakarta, Hanoi (no DST)</option>";
    html += "<option value='CST-8'>UTC+8 China, Singapore, Perth (no DST)</option>";
    html += "<option value='JST-9'>UTC+9 Japan, Korea (no DST)</option>";
    
    // Australia & Pacific
    html += "<option value='ACST-9:30ACDT,M10.1.0,M4.1.0/3'>UTC+9:30/+10:30 Adelaide</option>";
    html += "<option value='AEST-10AEDT,M10.1.0,M4.1.0/3'>UTC+10/+11 Sydney, Melbourne</option>";
    html += "<option value='AEST-10'>UTC+10 Brisbane (no DST)</option>";
    html += "<option value='<+11>-11'>UTC+11 Solomon Islands (no DST)</option>";
    html += "<option value='NZST-12NZDT,M9.5.0,M4.1.0/3'>UTC+12/+13 New Zealand</option>";
    html += "<option value='<+13>-13'>UTC+13 Tonga (no DST)</option>";
    
    return html;
}

// Helper: Generate complete WiFi settings form (for setup mode - includes timezone)
String WebConfigServer::generateSetupHTML()
{
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>";
    html += generateCommonCSS();
    html += generateConfigFormCSS();
    html += "</style>";
    html += "<script>";
    // Special setup mode JS that saves both WiFi and timezone
    html += generateWifiScanJS();
    html += "function saveWifi() {";  // Override for setup mode
    html += "  const ssid = document.getElementById('network-select').value;";
    html += "  if (!ssid) { alert('Please select a network'); return; }";
    html += "  const password = document.getElementById('password').value;";
    html += "  const timezone = document.getElementById('timezone').value;";  // Now contains TZ string
    html += "  const formData = new FormData();";
    html += "  formData.append('ssid', ssid);";
    html += "  formData.append('password', password);";
    html += "  formData.append('timezone', timezone);";
    html += "  document.getElementById('save-btn').disabled = true;";
    html += "  document.getElementById('save-btn').textContent = 'Saving...';";
    html += "  fetch('/save', { method: 'POST', body: formData })";
    html += "    .then(r => r.text()).then(msg => {";
    html += "      document.body.innerHTML = '<div class=\"container\" style=\"text-align: center;\"><h1>&#x2705; Configuration Saved!</h1><p>Device is rebooting and connecting to WiFi...</p><p style=\"color: #666; margin-top: 20px;\">Please wait 10 seconds, then connect to the WiFi network and visit:<br><strong style=\"color: #007bff;\">http://192.168.22.57</strong></p></div>';";
    html += "    }).catch(e => {";
    html += "      alert('Error: ' + e);";
    html += "      document.getElementById('save-btn').disabled = false;";
    html += "      document.getElementById('save-btn').textContent = 'Save & Connect';";
    html += "    });";
    html += "}";
    html += "</script></head><body>";
    html += "<div class='container'>";
    html += "<h1>&#x1F552; WordClock Setup</h1>";
    html += "<h3>1. Select WiFi Network</h3>";
    html += "<div id='scan-status' class='scanning'>Scanning for networks...</div>";
    html += "<select id='network-select' onchange='networkSelected()' disabled>";
    html += "<option value=''>-- Scanning... --</option>";
    html += "</select>";
    html += "<div id='password-section' style='display:none;'>";
    html += "<h3>2. WiFi Password</h3>";
    html += "<label for='password'>Password:</label>";
    html += "<input type='password' id='password' placeholder='Leave blank if no password'>";
    html += "<h3>3. Timezone Settings</h3>";
    html += "<label for='timezone'>Select Your Timezone:</label>";
    html += "<select id='timezone'>";
    html += generateTimezoneDropdown();
    html += "</select>";
    html += "<button id='save-btn' onclick='saveWifi()' disabled>Save & Connect</button>";
    html += "</div>";
    html += "</div></body></html>";
    return html;
}

void WebConfigServer::onSaveConfig(SaveConfigCallback callback)
{
    saveConfigCallback = callback;
}

void WebConfigServer::onGetNetworks(GetNetworksCallback callback)
{
    getNetworksCallback = callback;
}

void WebConfigServer::onStartScan(StartScanCallback callback)
{
    startScanCallback = callback;
}

void WebConfigServer::onGetStatus(GetStatusCallback callback)
{
    getStatusCallback = callback;
}

void WebConfigServer::onGetTimezoneSettings(GetTimezoneSettingsCallback callback)
{
    getTimezoneSettingsCallback = callback;
}

void WebConfigServer::onTriggerGif(TriggerGifCallback callback)
{
    triggerGifCallback = callback;
}

void WebConfigServer::onTriggerWordsTest(TriggerWordsTestCallback callback)
{
    triggerWordsTestCallback = callback;
}

void WebConfigServer::onTriggerLedTest(TriggerLedTestCallback callback)
{
    triggerLedTestCallback = callback;
}

void WebConfigServer::onResumeNormal(ResumeNormalCallback callback)
{
    resumeNormalCallback = callback;
}


void WebConfigServer::setupRoutes()
{
    SERIAL_PRINT("Setting up routes for mode: ");
    SERIAL_PRINTLN(mode == MODE_SETUP ? "SETUP" : "NORMAL");
    
    if (mode == MODE_SETUP)
    {
        SERIAL_PRINTLN("Registering SETUP mode routes...");
        // Setup mode routes (AP mode)
        // Captive portal - redirect all requests to config page
        server->onNotFound([this](AsyncWebServerRequest *request) {
            handleRoot(request);
        });
        
        server->on("/", HTTP_GET, [this](AsyncWebServerRequest *request) {
            handleRoot(request);
        });
        
        server->on("/scan", HTTP_GET, [this](AsyncWebServerRequest *request) {
            handleScan(request);
        });
        
        server->on("/save", HTTP_POST, [this](AsyncWebServerRequest *request) {
            handleSave(request);
        });
        SERIAL_PRINTLN("SETUP mode routes registered");
    }
    else
    {
        SERIAL_PRINTLN("Registering NORMAL mode routes...");
        // Normal mode routes (connected to WiFi)
        server->on("/", HTTP_GET, [this](AsyncWebServerRequest *request) {
            SERIAL_PRINTLN("Route / called (Normal mode)");
            handleRoot(request);  // Status page
        });
        
        server->on("/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
            SERIAL_PRINTLN("Route /status called");
            handleStatus(request);  // JSON status
        });
        
        server->on("/timezone", HTTP_GET, [this](AsyncWebServerRequest *request) {
            SERIAL_PRINTLN("Route /timezone called");
            handleTimezoneSettings(request);  // Timezone settings page
        });
        
        server->on("/timezone-settings", HTTP_GET, [this](AsyncWebServerRequest *request) {
            SERIAL_PRINTLN("Route /timezone-settings called");
            handleGetTimezoneSettings(request);  // Get current timezone JSON
        });
        
        server->on("/save-timezone", HTTP_POST, [this](AsyncWebServerRequest *request) {
            SERIAL_PRINTLN("Route /save-timezone called");
            handleSaveTimezone(request);
        });
        
        server->on("/trigger/gif", HTTP_POST, [this](AsyncWebServerRequest *request) {
            SERIAL_PRINTLN("Route /trigger/gif called");
            handleTriggerGif(request);
        });
        
        server->on("/trigger/words", HTTP_POST, [this](AsyncWebServerRequest *request) {
            SERIAL_PRINTLN("Route /trigger/words called");
            handleTriggerWords(request);
        });
        
        server->on("/trigger/ledtest", HTTP_POST, [this](AsyncWebServerRequest *request) {
            SERIAL_PRINTLN("Route /trigger/ledtest called");
            handleTriggerLedTest(request);
        });
        
        server->on("/trigger/resume", HTTP_POST, [this](AsyncWebServerRequest *request) {
            handleResume(request);
        });
        SERIAL_PRINTLN("NORMAL mode routes registered (9 routes total)");
    }
    SERIAL_PRINTLN("setupRoutes() complete");
}

void WebConfigServer::handleRoot(AsyncWebServerRequest *request)
{
    String html;
    if (mode == MODE_SETUP)
    {
        html = generateSetupHTML();
    }
    else
    {
        html = generateStatusHTML();
    }
    request->send(200, "text/html", html);
}

void WebConfigServer::handleScan(AsyncWebServerRequest *request)
{
    if (getNetworksCallback)
    {
        String json = getNetworksCallback();
        request->send(200, "application/json", json);
    }
    else
    {
        request->send(500, "application/json", "{\"error\":\"No scan callback\"}");
    }
}

void WebConfigServer::handleSave(AsyncWebServerRequest *request)
{
    if (request->hasParam("ssid", true) && request->hasParam("password", true) &&
        request->hasParam("timezone", true))
    {
        String ssid = request->getParam("ssid", true)->value();
        String password = request->getParam("password", true)->value();
        String tzString = request->getParam("timezone", true)->value();  // Now TZ string
        
        SERIAL_PRINT("Web: Saving configuration for SSID: ");
        SERIAL_PRINT(ssid.c_str());
        SERIAL_PRINT(" with timezone: ");
        SERIAL_PRINTLN(tzString.c_str());
        
        if (saveConfigCallback)
        {
            saveConfigCallback(ssid, password, tzString);
            request->send(200, "text/plain", "Settings saved! Rebooting...");
            shouldReboot = true;
        }
        else
        {
            request->send(500, "text/plain", "No save callback configured");
        }
    }
    else
    {
        request->send(400, "text/plain", "Missing required parameters");
    }
}

void WebConfigServer::handleStatus(AsyncWebServerRequest *request)
{
    SERIAL_PRINTLN("handleStatus called");
    if (getStatusCallback)
    {
        SERIAL_PRINTLN("Calling getStatusCallback...");
        String json = getStatusCallback();
        SERIAL_PRINT("Status JSON: ");
        SERIAL_PRINTLN(json.c_str());
        request->send(200, "application/json", json);
    }
    else
    {
        SERIAL_PRINTLN("ERROR: No status callback set!");
        request->send(500, "application/json", "{\"error\":\"No status callback\"}");
    }
}

void WebConfigServer::handleWifiSettings(AsyncWebServerRequest *request)
{
    request->send(200, "text/html", generateWifiSettingsHTML());
}

void WebConfigServer::handleTimezoneSettings(AsyncWebServerRequest *request)
{
    request->send(200, "text/html", generateTimezoneSettingsHTML());
}

void WebConfigServer::handleGetTimezoneSettings(AsyncWebServerRequest *request)
{
    SERIAL_PRINTLN("handleGetTimezoneSettings called");
    if (getTimezoneSettingsCallback)
    {
        String json = getTimezoneSettingsCallback();
        SERIAL_PRINT("Returning timezone settings: ");
        SERIAL_PRINTLN(json.c_str());
        request->send(200, "application/json", json);
    }
    else
    {
        SERIAL_PRINTLN("ERROR: No getTimezoneSettingsCallback!");
        request->send(500, "application/json", "{\"error\":\"No callback\"}");
    }
}

void WebConfigServer::handleSaveWifi(AsyncWebServerRequest *request)
{
    // Handle WiFi-only save
    if (request->hasParam("ssid", true) && request->hasParam("password", true))
    {
        String ssid = request->getParam("ssid", true)->value();
        String password = request->getParam("password", true)->value();
        
        // Save WiFi credentials - callback will preserve existing timezone
        if (saveConfigCallback)
        {
            saveConfigCallback(ssid, password, "__KEEP_TZ__"); // Special marker to preserve timezone
        }
        
        request->send(200, "text/plain", "WiFi settings saved, rebooting...");
        shouldReboot = true;
    }
    else
    {
        request->send(400, "text/plain", "Missing parameters");
    }
}

void WebConfigServer::handleSaveTimezone(AsyncWebServerRequest *request)
{
    SERIAL_PRINTLN("handleSaveTimezone called");
    
    // Handle timezone-only save
    if (request->hasParam("timezone", true))
    {
        String tzString = request->getParam("timezone", true)->value();  // Now TZ string
        
        SERIAL_PRINT("Timezone string: ");
        SERIAL_PRINTLN(tzString.c_str());
        
        // Save timezone only (pass special marker for WiFi to indicate no WiFi change)
        if (saveConfigCallback)
        {
            SERIAL_PRINTLN("Calling saveConfigCallback with timezone only...");
            saveConfigCallback("__KEEP_WIFI__", "", tzString);
            SERIAL_PRINTLN("saveConfigCallback completed");
        }
        else
        {
            SERIAL_PRINTLN("ERROR: No saveConfigCallback!");
        }
        
        SERIAL_PRINTLN("Sending response and setting shouldReboot flag");
        request->send(200, "text/plain", "Timezone saved, rebooting...");
        shouldReboot = true;
        SERIAL_PRINT("shouldReboot is now: ");
        SERIAL_PRINTLN(shouldReboot ? "true" : "false");
    }
    else
    {
        SERIAL_PRINTLN("ERROR: Missing timezone parameter");
        request->send(400, "text/plain", "Missing parameters");
    }
}

void WebConfigServer::handleTriggerGif(AsyncWebServerRequest *request)
{
    SERIAL_PRINTLN("handleTriggerGif called");
    if (triggerGifCallback)
    {
        SERIAL_PRINTLN("Calling triggerGifCallback...");
        triggerGifCallback();
        request->send(200, "text/plain", "GIF animation triggered");
    }
    else
    {
        SERIAL_PRINTLN("ERROR: No GIF callback set!");
        request->send(500, "text/plain", "No GIF callback");
    }
}

void WebConfigServer::handleTriggerWords(AsyncWebServerRequest *request)
{
    SERIAL_PRINTLN("handleTriggerWords called");
    if (triggerWordsTestCallback)
    {
        SERIAL_PRINTLN("Calling triggerWordsTestCallback...");
        triggerWordsTestCallback();
        request->send(200, "text/plain", "Words test started");
    }
    else
    {
        SERIAL_PRINTLN("ERROR: No words test callback set!");
        request->send(500, "text/plain", "No words test callback");
    }
}

void WebConfigServer::handleTriggerLedTest(AsyncWebServerRequest *request)
{
    SERIAL_PRINTLN("handleTriggerLedTest called");
    if (triggerLedTestCallback)
    {
        SERIAL_PRINTLN("Calling triggerLedTestCallback...");
        triggerLedTestCallback();
        request->send(200, "text/plain", "LED test started");
    }
    else
    {
        SERIAL_PRINTLN("ERROR: No LED test callback set!");
        request->send(500, "text/plain", "No LED test callback");
    }
}

void WebConfigServer::handleResume(AsyncWebServerRequest *request)
{
    if (resumeNormalCallback)
    {
        resumeNormalCallback();
        request->send(200, "text/plain", "Resumed normal operation");
    }
    else
    {
        request->send(500, "text/plain", "No resume callback");
    }
}


String WebConfigServer::generateStatusHTML()
{
    SERIAL_PRINTLN("generateStatusHTML() called");
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    // REMOVED auto-refresh - it was causing repeated page loads
    html += "<style>";
    html += "body { font-family: Arial; margin: 20px; background: #f0f0f0; }";
    html += ".container { max-width: 600px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }";
    html += "h1 { color: #333; text-align: center; margin-bottom: 10px; }";
    html += ".subtitle { text-align: center; color: #666; margin-bottom: 30px; }";
    html += ".status-card { background: #f9f9f9; padding: 15px; margin: 15px 0; border-radius: 8px; border-left: 4px solid #007bff; }";
    html += ".status-label { font-weight: bold; color: #555; margin-bottom: 5px; }";
    html += ".status-value { font-size: 18px; color: #333; }";
    html += ".button-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; margin-top: 20px; }";
    html += "button { width: 100%; padding: 15px; border: none; border-radius: 5px; cursor: pointer; font-size: 14px; font-weight: bold; transition: all 0.3s; }";
    html += ".btn { background: #007bff; color: white; }";
    html += ".btn:hover { background: #0056b3; }";
    html += ".btn:disabled { background: #ccc; color: #666; cursor: not-allowed; }";
    html += ".btn-full { grid-column: 1 / -1; }";
    html += ".btn-resume { background: #28a745; display: none; }";
    html += ".btn-resume:hover { background: #218838; }";
    html += ".loading { text-align: center; color: #666; }";
    html += "</style>";
    html += "<script>";
    html += "function triggerAction(action) {";
    html += "  console.log('triggerAction called with:', action);";
    html += "  const btn = event.target;";
    html += "  const originalText = btn.textContent;";
    html += "  btn.disabled = true;";
    html += "  btn.textContent = 'Running...';";
    html += "  document.querySelectorAll('.btn-test').forEach(b => b.disabled = true);";
    html += "  document.getElementById('resume-btn').style.display = 'block';";
    html += "  console.log('Fetching /trigger/' + action);";
    html += "  fetch('/trigger/' + action, { method: 'POST' })";
    html += "    .then(r => {";
    html += "      console.log('Trigger response:', r.status);";
    html += "      return r.text();";
    html += "    })";
    html += "    .then(msg => {";
    html += "      console.log('Trigger message:', msg);";
    html += "    })";
    html += "    .catch(e => {";
    html += "      console.error('Trigger error:', e);";
    html += "      alert('Error: ' + e);";
    html += "      btn.textContent = originalText;";
    html += "      btn.disabled = false;";
    html += "    });";
    html += "}";
    html += "function resumeNormal() {";
    html += "  fetch('/trigger/resume', { method: 'POST' })";
    html += "    .then(r => r.text())";
    html += "    .then(msg => {";
    html += "      document.getElementById('resume-btn').style.display = 'none';";
    html += "      document.querySelectorAll('.btn-test').forEach(b => {";
    html += "        b.disabled = false;";
    html += "        b.textContent = b.getAttribute('data-original-text');";
    html += "      });";
    html += "    })";
    html += "    .catch(e => alert('Error: ' + e));";
    html += "}";
    html += "function loadStatus() {";
    html += "  console.log('loadStatus() called');";
    html += "  fetch('/status')";
    html += "    .then(r => {";
    html += "      console.log('fetch response received:', r.status);";
    html += "      return r.json();";
    html += "    })";
    html += "    .then(data => {";
    html += "      console.log('Status data:', data);";
    html += "      document.getElementById('wifi-ssid').textContent = data.ssid || 'N/A';";
    html += "      document.getElementById('wifi-rssi').textContent = data.rssi + ' dBm';";
    html += "      document.getElementById('wifi-ip').textContent = data.ip || 'N/A';";
    html += "      document.getElementById('current-time').textContent = data.time || 'N/A';";
    html += "      document.getElementById('timezone').textContent = data.timezone || 'N/A';";
    html += "      document.getElementById('uptime').textContent = data.uptime || 'N/A';";
    html += "    })";
    html += "    .catch(e => {";
    html += "      console.error('Status error:', e);";
    html += "      alert('Failed to load status: ' + e);";
    html += "    });";
    html += "}";
    html += "console.log('Setting up window.onload...');";
    html += "window.onload = function() {";
    html += "  console.log('window.onload fired!');";
    html += "  loadStatus();";
    html += "  setInterval(loadStatus, 5000);";
    html += "};";
    html += "</script></head><body>";
    html += "<div class='container'>";
    html += "<h1>&#x1F550; WordClock</h1>";
    html += "<div class='subtitle'>Status & Control</div>";
    
    html += "<div class='status-card'>";
    html += "<div class='status-label'>WiFi Network</div>";
    html += "<div class='status-value' id='wifi-ssid'>Loading...</div>";
    html += "</div>";
    
    html += "<div class='status-card'>";
    html += "<div class='status-label'>Signal Strength</div>";
    html += "<div class='status-value' id='wifi-rssi'>Loading...</div>";
    html += "</div>";
    
    html += "<div class='status-card'>";
    html += "<div class='status-label'>IP Address</div>";
    html += "<div class='status-value' id='wifi-ip'>Loading...</div>";
    html += "</div>";
    
    html += "<div class='status-card'>";
    html += "<div class='status-label'>Current Time</div>";
    html += "<div class='status-value' id='current-time'>Loading...</div>";
    html += "</div>";
    
    html += "<div class='status-card'>";
    html += "<div class='status-label'>Timezone (from Flash)</div>";
    html += "<div class='status-value' id='timezone' style='font-size: 14px; font-family: monospace;'>Loading...</div>";
    html += "</div>";
    
    html += "<div class='status-card'>";
    html += "<div class='status-label'>Uptime</div>";
    html += "<div class='status-value' id='uptime'>Loading...</div>";
    html += "</div>";
    
    html += "<div class='button-grid'>";
    html += "<button class='btn btn-test' data-original-text='Play GIF' onclick='triggerAction(\"gif\")'>Play GIF</button>";
    html += "<button class='btn btn-test' data-original-text='Test Words' onclick='triggerAction(\"words\")'>Test Words</button>";
    html += "<button class='btn btn-test' data-original-text='LED Test' onclick='triggerAction(\"ledtest\")'>LED Test</button>";
    html += "<button id='resume-btn' class='btn btn-resume btn-full' onclick='resumeNormal()'>Resume Normal Operation</button>";
    html += "<button class='btn btn-full' onclick='window.location=\"/timezone\"'>Change Timezone</button>";
    html += "</div>";
    
    html += "</div></body></html>";
    
    return html;
}

String WebConfigServer::generateWifiSettingsHTML()
{
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>";
    html += generateCommonCSS();
    html += generateConfigFormCSS();
    html += "</style>";
    html += "<script>";
    html += "</script></head><body>";
    html += "<div class='container'>";
    html += "<h1>&#x1F4F6; Change WiFi Network</h1>";
    html += "<div class='info'>⚠️ Cannot scan for networks while connected. Please enter network name manually or use AP mode for setup.</div>";
    html += "<h3>Enter WiFi Network Name</h3>";
    html += "<label for='ssid-input'>Network SSID:</label>";
    html += "<input type='text' id='ssid-input' placeholder='Enter network name' onchange='document.getElementById(\"save-btn\").disabled=!this.value;'>";
    html += "<h3>WiFi Password</h3>";
    html += "<label for='password'>Password:</label>";
    html += "<input type='password' id='password' placeholder='Leave blank if no password'>";
    html += "<script>";
    html += "function saveWifi() {";
    html += "  const ssid = document.getElementById('ssid-input').value;";
    html += "  if (!ssid) { alert('Please enter a network name'); return; }";
    html += "  const password = document.getElementById('password').value;";
    html += "  const formData = new FormData();";
    html += "  formData.append('ssid', ssid);";
    html += "  formData.append('password', password);";
    html += "  document.getElementById('save-btn').disabled = true;";
    html += "  document.getElementById('save-btn').textContent = 'Saving...';";
    html += "  fetch('/save-wifi', { method: 'POST', body: formData })";
    html += "    .then(r => r.text()).then(msg => {";
    html += "      document.body.innerHTML = '<div class=\"container\"><h1>&#x2705; WiFi Settings Saved!</h1><p>Device is rebooting...</p></div>';";
    html += "    }).catch(e => {";
    html += "      alert('Error saving: ' + e);";
    html += "      document.getElementById('save-btn').disabled = false;";
    html += "      document.getElementById('save-btn').textContent = 'Save WiFi Settings';";
    html += "    });";
    html += "}";
    html += "</script>";
    html += "<button id='save-btn' onclick='saveWifi()' disabled>Save WiFi Settings</button>";
    html += "<button class='btn-secondary' onclick='window.location=\"/\"'>&#x2190; Back to Status</button>";
    html += "</div></body></html>";
    
    SERIAL_PRINT("Generated status HTML length: ");
    SERIAL_PRINTLN(String(html.length()).c_str());
    return html;
}

String WebConfigServer::generateTimezoneSettingsHTML()
{
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta charset='UTF-8'>"; // FIX: Add UTF-8 charset for emoji rendering
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>";
    html += generateCommonCSS();
    html += generateConfigFormCSS();
    html += "</style>";
    html += "<script>";
    html += "window.onload = function() {";
    html += "  /* Load current timezone settings */";
    html += "  fetch('/timezone-settings')";
    html += "    .then(r => r.json())";
    html += "    .then(data => {";
    html += "      document.getElementById('timezone').value = data.tzString;";
    html += "    })";
    html += "    .catch(e => console.error('Error loading settings:', e));";
    html += "};";
    html += "function saveTimezone() {";
    html += "  const timezone = document.getElementById('timezone').value;";  // Now contains TZ string
    html += "  const formData = new FormData();";
    html += "  formData.append('timezone', timezone);";
    html += "  document.getElementById('save-btn').disabled = true;";
    html += "  document.getElementById('save-btn').textContent = 'Saving...';";
    html += "  fetch('/save-timezone', { method: 'POST', body: formData })";
    html += "    .then(r => r.text()).then(msg => {";
    html += "      document.body.innerHTML = '<div class=\"container\" style=\"text-align: center;\"><h1>&#x2705; Timezone Saved!</h1><p>Device is rebooting...</p><p style=\"color: #666; margin-top: 20px;\">Redirecting to status page in <span id=\"countdown\">8</span> seconds</p></div>';";
    html += "      let timeLeft = 8;";
    html += "      const countdownEl = document.getElementById('countdown');";
    html += "      const timer = setInterval(() => {";
    html += "        timeLeft--;";
    html += "        countdownEl.textContent = timeLeft;";
    html += "        if (timeLeft <= 0) {";
    html += "          clearInterval(timer);";
    html += "          window.location.href = '/';";
    html += "        }";
    html += "      }, 1000);";
    html += "    }).catch(e => {";
    html += "      alert('Error: ' + e);";
    html += "      document.getElementById('save-btn').disabled = false;";
    html += "      document.getElementById('save-btn').textContent = 'Save Timezone';";
    html += "    });";
    html += "}";
    html += "</script></head><body>";
    html += "<div class='container'>";
    html += "<h1>&#x1F30D; Change Timezone</h1>";
    html += "<div class='info'>⚠️ Device will reboot after saving</div>";
    html += "<h3>Timezone Settings</h3>";
    html += "<label for='timezone'>Select Your Timezone:</label>";
    html += "<select id='timezone'>";
    html += generateTimezoneDropdown();
    html += "</select>";
    html += "<button id='save-btn' onclick='saveTimezone()'>Save Timezone</button>";
    html += "<button class='btn-secondary' onclick='window.location=\"/\"'>&#x2190; Back to Status</button>";
    html += "</div></body></html>";
    
    return html;
}

