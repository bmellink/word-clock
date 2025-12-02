#include "ClockDisplayHAL.h"
#include "esp_task_wdt.h"
#include "SerialHelper.h"

ClockDisplayHAL::WordMapping const ClockDisplayHAL::WORDS_TO_LEDS[] = {
    {"HOUR_1", 20, 22},
    {"HOUR_2", 45, 47},
    {"HOUR_3", 15, 19},
    {"HOUR_4", 67, 70},
    {"HOUR_5", 40, 43},
    {"HOUR_6", 12, 14},
    {"HOUR_7", 55, 59},
    {"HOUR_8", 31, 35},
    {"HOUR_9", 36, 39},
    {"HOUR_10", 9, 11},
    {"HOUR_11", 24, 29},
    {"HOUR_12", 48, 53},
    {"OCLOCK", 0, 5},
    {"PAST", 60, 63},
    {"TO", 63, 64},
    {"MINUTES", 77, 83},
    {"THIRTY", 84, 89},
    {"TWENTY", 102, 107},
    {"TWENTYFIVE", 98, 107},
    {"FIVE", 98, 101},
    {"TEN", 91, 93},
    {"FIFTEEN", 110, 116},
    {"IS", 127, 128},
    {"IT", 130, 131}};

const uint32_t ClockDisplayHAL::COLORS[] = {
    0xFF0000, // Red
    0x00FF00, // Green
    0x0000FF, // Blue
    0xFFFF00, // Yellow
    0xFF00FF, // Magenta
    0x00FFFF, // Cyan
    0xFFFFFF, // White
    0xA52A2A  // Brown
};

const int ClockDisplayHAL::getWordCount()
{
    return sizeof(WORDS_TO_LEDS) / sizeof(WORDS_TO_LEDS[0]);
}

const int ClockDisplayHAL::getColorCount()
{
    return sizeof(COLORS) / sizeof(COLORS[0]);
}

ClockDisplayHAL::ClockDisplayHAL(uint8_t pin, uint8_t brightness)
    : pixels(NUM_LEDS, pin, NEO_GRB + NEO_KHZ800), brightness(brightness)
{
}

void ClockDisplayHAL::setup()
{
    pixels.setBrightness(255);
    pixels.begin();
    pixels.show();
}

void ClockDisplayHAL::displayWord(const String &word, uint32_t color)
{
    for (auto mapping : WORDS_TO_LEDS)
    {
        if (word.equals(mapping.word))
        {
            for (uint8_t i = mapping.start; i <= mapping.end; ++i)
            {
                pixels.setPixelColor(i, color);
            }
            break;
        }
    }
}

uint16_t ClockDisplayHAL::cartesianToWordClockLEDStripIndex(uint8_t x, uint8_t y)
{
    uint16_t row_index;
    uint16_t index;

    if (y % 2 == 0)
    {
        row_index = NUM_LEDS - (y * WIDTH);
        index = row_index - (x + 1);
    }
    else
    {
        row_index = NUM_LEDS - ((y + 1) * WIDTH);
        index = row_index + x;
    }

    if (index < 0 || index >= NUM_LEDS)
    {
        return 0;
    }

    return index;
}

void ClockDisplayHAL::setPixel(uint8_t x, uint8_t y, uint32_t color)
{
    uint16_t index = cartesianToWordClockLEDStripIndex(x, y);
    pixels.setPixelColor(index, color);
}

void ClockDisplayHAL::clearPixels(bool show)
{
    pixels.clear();
    if (show)
    {
        pixels.show();
    }
}

void ClockDisplayHAL::show()
{
    pixels.show();
}

void ClockDisplayHAL::runLedTest(bool (*shouldAbort)())
{
    // Test all LEDs row by row, cycling through colors (use shared color palette)
    int numColors = getColorCount();
    
    // Cycle through each row
    for (uint8_t row = 0; row < HEIGHT; row++)
    {
        // Check if test should abort
        if (shouldAbort && shouldAbort())
        {
            SERIAL_PRINTLN("LED test aborted by user");
            clearPixels(true);
            return;
        }
        
        esp_task_wdt_reset(); // Feed watchdog at start of each row
        
        // Cycle through colors for this row
        for (int colorIndex = 0; colorIndex < numColors; colorIndex++)
        {
            // Check if test should abort
            if (shouldAbort && shouldAbort())
            {
                SERIAL_PRINTLN("LED test aborted by user");
                clearPixels(true);
                return;
            }
            
            clearPixels(false);
            
            // Light up entire row with current color
            for (uint8_t col = 0; col < WIDTH; col++)
            {
                setPixel(col, row, COLORS[colorIndex]);
            }
            
            show();
            delay(200); // Show each color for 200ms
        }
    }
    
    // Clear at the end
    clearPixels(true);
    esp_task_wdt_reset(); // Feed watchdog at end of test
}