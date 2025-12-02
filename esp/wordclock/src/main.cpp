#include <Arduino.h>
#include "ClockDisplayHAL.h"
#include "NetworkManager.h"
#include "SerialHelper.h"
#include "config.h"
#include "GifPlayer.h"
#include "WordClock.h"
#include "esp_task_wdt.h"

NetworkManager networkManager(DEFAULT_GMT_OFFSET_SEC, DEFAULT_DAYLIGHT_OFFSET_SEC);
ClockDisplayHAL clockDisplayHAL(LED_PIN, 255);
GifPlayer gifPlayer(&clockDisplayHAL);
WordClock wordClock(&clockDisplayHAL, &networkManager, &gifPlayer);

// Test mode state
enum TestMode {
  TEST_MODE_NONE,
  TEST_MODE_GIF,
  TEST_MODE_WORDS,
  TEST_MODE_LED
};

volatile TestMode currentTestMode = TEST_MODE_NONE;

// Helper function to check if test should abort
bool shouldAbortTest()
{
  return (currentTestMode == TEST_MODE_NONE);
}

void displayAPMode()
{
  // Show a simple pattern to indicate AP mode
  static uint8_t hue = 0;
  clockDisplayHAL.clearPixels(false);
  
  // Slowly cycle through colors on "IT" and "IS" to indicate AP mode
  uint32_t color = clockDisplayHAL.pixels.ColorHSV(hue * 256, 255, 128);
  clockDisplayHAL.displayWord("IT", color);
  clockDisplayHAL.displayWord("IS", color);
  clockDisplayHAL.show();
  
  hue = (hue + 1) % 256;
}

void checkResetButtonWithVisuals()
{
  // Check if reset button was triggered (held for 3+ seconds)
  if (networkManager.checkResetButton())
  {
    // Show visual feedback - flash all LEDs red
    clockDisplayHAL.clearPixels(false);
    for (int i = 0; i < 3; i++)
    {
      clockDisplayHAL.pixels.fill(0xFF0000); // Red
      clockDisplayHAL.show();
      delay(200);
      clockDisplayHAL.clearPixels(true);
      delay(200);
    }
    
    // Reboot
    SERIAL_PRINTLN("Rebooting...");
    delay(1000);
    ESP.restart();
  }
}

// Wrapper functions for callbacks - just set test mode flags
void triggerGif()
{
  SERIAL_PRINTLN("Entering GIF test mode");
  currentTestMode = TEST_MODE_GIF;
}

void triggerWordsTest()
{
  SERIAL_PRINTLN("Entering words test mode");
  currentTestMode = TEST_MODE_WORDS;
}

void triggerLedTest()
{
  SERIAL_PRINTLN("Entering LED test mode");
  currentTestMode = TEST_MODE_LED;
}

void resumeNormal()
{
  SERIAL_PRINTLN("=== Resume Normal Function Called ===");
  SERIAL_PRINT("Current test mode before: ");
  SERIAL_PRINTLN(String(currentTestMode).c_str());
  currentTestMode = TEST_MODE_NONE;
  SERIAL_PRINT("Current test mode after: ");
  SERIAL_PRINTLN(String(currentTestMode).c_str());
  SERIAL_PRINTLN("Clearing display...");
  clockDisplayHAL.clearPixels(true);
  SERIAL_PRINTLN("Forcing clock refresh...");
  wordClock.forceRefresh(); // Clear cached state to force immediate update
  SERIAL_PRINTLN("=== Resume Complete ===");
}

void setup()
{
  initSerial();
  
  // Initialize display first for startup progress indicators
  clockDisplayHAL.setup();
  clockDisplayHAL.clearPixels(true);
  
  // Progress LED 1: Serial initialized
  SERIAL_PRINTLN("=== STARTUP: Step 1 - Serial OK ===");
  clockDisplayHAL.setPixel(0, 0, 0x0000FF); // Blue
  clockDisplayHAL.show();
  delay(200);
  
  // Setup reset button with internal pull-up
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);
  networkManager.setResetButtonPin(RESET_BUTTON_PIN);
  
  // Progress LED 2: Setting up callbacks
  SERIAL_PRINTLN("=== STARTUP: Step 2 - Setting callbacks ===");
  clockDisplayHAL.setPixel(1, 0, 0x0000FF); // Blue
  clockDisplayHAL.show();
  networkManager.setTriggerGifCallback(triggerGif);
  networkManager.setTriggerWordsTestCallback(triggerWordsTest);
  networkManager.setTriggerLedTestCallback(triggerLedTest);
  networkManager.setResumeNormalCallback(resumeNormal);
  delay(200);
  
  // Progress LED 3: Starting network manager
  SERIAL_PRINTLN("=== STARTUP: Step 3 - Starting network ===");
  clockDisplayHAL.setPixel(2, 0, 0x00FF00); // Green
  clockDisplayHAL.show();
  
  // NetworkManager setup will check button during connection attempts
  networkManager.setup();
  
  // Progress LED 4: Network ready
  SERIAL_PRINTLN("=== STARTUP: Step 4 - Network ready ===");
  clockDisplayHAL.setPixel(3, 0, 0x00FF00); // Green
  clockDisplayHAL.show();
  delay(200);
  
  // Progress LED 5: WordClock setup (if connected)
  if (networkManager.isConnected())
  {
    SERIAL_PRINTLN("=== STARTUP: Step 5 - Setting up WordClock ===");
    clockDisplayHAL.setPixel(4, 0, 0x00FF00); // Green
    clockDisplayHAL.show();
    wordClock.setup();
    delay(200);
  }
  
  // All done - flash all progress LEDs green
  SERIAL_PRINTLN("=== STARTUP: COMPLETE ===");
  for (int i = 0; i < 5; i++)
  {
    clockDisplayHAL.setPixel(i, 0, 0x00FF00); // Green
  }
  clockDisplayHAL.show();
  delay(500);
  
  // Clear display for normal operation
  clockDisplayHAL.clearPixels(true);
}

void loop()
{
  // Feed the watchdog to prevent resets
  esp_task_wdt_reset();
  
  // Check reset button (with visual feedback)
  checkResetButtonWithVisuals();
  
  // Update network manager (handles reconnection, AP mode, etc.)
  networkManager.update();
  
  // Handle test modes (run continuously until resume button clicked)
  if (currentTestMode != TEST_MODE_NONE)
  {
    static TestMode lastReportedTestMode = TEST_MODE_NONE;
    if (lastReportedTestMode != currentTestMode)
    {
      SERIAL_PRINT("TEST MODE ACTIVE: ");
      SERIAL_PRINTLN(String(currentTestMode).c_str());
      lastReportedTestMode = currentTestMode;
    }
    
    switch (currentTestMode)
    {
      case TEST_MODE_GIF:
        wordClock.triggerGif();
        delay(500); // Pause between loops
        break;
        
      case TEST_MODE_WORDS:
        wordClock.runWordsTest(shouldAbortTest);
        delay(500); // Pause between loops
        break;
        
      case TEST_MODE_LED:
        clockDisplayHAL.runLedTest(shouldAbortTest);
        delay(500); // Pause between loops
        break;
        
      default:
        currentTestMode = TEST_MODE_NONE;
        break;
    }
    
    // If resume was clicked during the test, currentTestMode is now NONE
    // Don't return, let it fall through to normal operation
    if (currentTestMode != TEST_MODE_NONE)
    {
      return; // Skip normal display, loop again for next test iteration
    }
    else
    {
      SERIAL_PRINTLN("Test mode ended, resuming normal operation");
      lastReportedTestMode = TEST_MODE_NONE;
      // Don't return - fall through to normal operation
    }
  }
  
  // Normal operation mode (reduce log spam)
  static bool lastWasAPMode = false;
  static bool lastWasConnected = false;
  
  // Only display time if we're connected to WiFi
  // In AP mode, show a visual indicator instead
  if (networkManager.isInAPMode())
  {
    if (!lastWasAPMode)
    {
      SERIAL_PRINTLN("=== ENTERED AP MODE ===");
      lastWasAPMode = true;
      lastWasConnected = false;
    }
    displayAPMode();
    delay(50); // Faster update for animation
  }
  else if (networkManager.isConnected())
  {
    if (!lastWasConnected)
    {
      SERIAL_PRINTLN("=== ENTERED NORMAL MODE (Connected) ===");
      lastWasConnected = true;
      lastWasAPMode = false;
    }
    // Try to download GIF if we haven't yet
    wordClock.setup();
    
    // Display the actual time
    wordClock.displayTime();
    delay(1000);
  }
  else
  {
    if (lastWasAPMode || lastWasConnected)
    {
      SERIAL_PRINTLN("=== WAITING TO CONNECT ===");
      lastWasAPMode = false;
      lastWasConnected = false;
    }
    // Waiting to connect - just delay
    delay(500);
  }
}
