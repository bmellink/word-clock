#ifndef CLOCKDISPLAYHAL_H
#define CLOCKDISPLAYHAL_H

#include <Adafruit_NeoPixel.h>

class ClockDisplayHAL
{
public:
    static const uint16_t WIDTH = 12;
    static const uint16_t HEIGHT = 11;
    static const uint16_t NUM_LEDS = WIDTH * HEIGHT;

    ClockDisplayHAL(uint8_t pin, uint8_t brightness);
    Adafruit_NeoPixel pixels;
    void setup();
    void displayWord(const String &word, uint32_t color);
    void setPixel(uint8_t x, uint8_t y, uint32_t color);
    void clearPixels(bool show = true);
    void show();
    
    // Test functions
    void runLedTest(bool (*shouldAbort)() = nullptr); // Optional abort check callback
    
    // Word mapping structure and access
    struct WordMapping
    {
        const char *word;
        uint8_t start;
        uint8_t end;
    };
    
    static const WordMapping WORDS_TO_LEDS[];
    static const int getWordCount();
    
    // Standard color palette (DRY principle - shared across test functions)
    static const uint32_t COLORS[];
    static const int getColorCount();

private:
    uint8_t brightness;
    uint16_t cartesianToWordClockLEDStripIndex(uint8_t x, uint8_t y);
};

#endif