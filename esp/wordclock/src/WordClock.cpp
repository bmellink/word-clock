#include "WordClock.h"
#include "SerialHelper.h"
#include "esp_task_wdt.h"

WordClock::WordClock(ClockDisplayHAL *clockDisplayHAL, NetworkManager *networkManager, GifPlayer *gifPlayer)
    : clockDisplayHAL(clockDisplayHAL), networkManager(networkManager), gifPlayer(gifPlayer), lastHour(-1), allLastHighlightedWords(""), gifDownloaded(false) {}

void WordClock::setup()
{
    downloadGIF();
}

void WordClock::forceRefresh()
{
    // Clear the cached state to force an immediate display update
    allLastHighlightedWords = "";
    lastHour = -1;
    SERIAL_PRINTLN("WordClock state cleared, next displayTime() will refresh immediately");
}

void WordClock::downloadGIF()
{
    if (!gifDownloaded)
    {
        const char *gifUrl = "https://raw.githubusercontent.com/johniak/word-clock/refs/heads/main/raspberry-pi/heart_art_small.gif";
        if (networkManager->downloadGIF(gifUrl))
        {
            uint8_t *gifBuffer = networkManager->getGifBuffer();
            size_t gifSize = networkManager->getGifBufferSize();
            if (gifSize > 0 && gifBuffer != nullptr)
            {
                if (gifPlayer->loadGIF(gifBuffer, gifSize))
                {
                    gifDownloaded = true;
                    SERIAL_PRINTLN("GIF downloaded and loaded successfully.");
                }
            }
        }
        else
        {
            SERIAL_PRINTLN("Failed to download GIF.");
        }
    }
}

void WordClock::highlightWord(const String &word, uint32_t color)
{
    clockDisplayHAL->displayWord(word, color);
}

String WordClock::getMinutesWord(int minute)
{
    if (minute < 5)
        return "OCLOCK";
    else if (minute < 10)
        return "FIVE";
    else if (minute < 15)
        return "TEN";
    else if (minute < 20)
        return "FIFTEEN";
    else if (minute < 25)
        return "TWENTY";
    else if (minute < 30)
        return "TWENTYFIVE";
    else if (minute < 35)
        return "THIRTY";
    else if (minute < 40)
        return "TWENTYFIVE";
    else if (minute < 45)
        return "TWENTY";
    else if (minute < 50)
        return "FIFTEEN";
    else if (minute < 55)
        return "TEN";
    else
        return "FIVE";
}

uint32_t WordClock::getRandomColor()
{
    // Use shared color palette from ClockDisplayHAL (DRY principle)
    int index = random(0, ClockDisplayHAL::getColorCount());
    return ClockDisplayHAL::COLORS[index];
}

void WordClock::displayTime()
{
    struct tm currentTime = networkManager->getLocalTimeStruct();
    int hour = currentTime.tm_hour % 12;
    if (hour == 0)
        hour = 12;
    int minute = currentTime.tm_min;

    clockDisplayHAL->clearPixels(false);

    if (hour != lastHour && minute == 0)
    {
        lastHour = hour;
        if (gifDownloaded)
        {
            gifPlayer->playGIF(4000);
        }
        clockDisplayHAL->clearPixels(false);
    }

    highlightWord("IT", getRandomColor());
    highlightWord("IS", getRandomColor());
    String allHighlightedWords = "ITIS";

    if (minute < 5)
    {
        highlightWord("OCLOCK", getRandomColor());
        allHighlightedWords += "OCLOCK";
    }
    else if (minute < 35)
    {
        highlightWord("PAST", getRandomColor());
        highlightWord("MINUTES", getRandomColor());
        allHighlightedWords += "PASTMINUTES";
    }
    else
    {
        highlightWord("TO", getRandomColor());
        highlightWord("MINUTES", getRandomColor());
        allHighlightedWords += "TOMINUTES";
        hour = (hour + 1) % 12;
        if (hour == 0)
            hour = 12;
    }

    String hourWord = "HOUR_" + String(hour);
    highlightWord(getMinutesWord(minute), getRandomColor());
    allHighlightedWords += getMinutesWord(minute);
    highlightWord(hourWord, getRandomColor());
    allHighlightedWords += hourWord;

    if (allLastHighlightedWords != allHighlightedWords)
    {
        clockDisplayHAL->show();
        allLastHighlightedWords = allHighlightedWords;
    }
}

void WordClock::triggerGif()
{
    if (gifDownloaded)
    {
        SERIAL_PRINTLN("Playing GIF animation...");
        gifPlayer->playGIF(4000);
        clockDisplayHAL->clearPixels(false);
    }
    else
    {
        SERIAL_PRINTLN("GIF not downloaded yet");
    }
}

void WordClock::runWordsTest(bool (*shouldAbort)())
{
    SERIAL_PRINTLN("Starting words test - cycling through all words...");
    
    clockDisplayHAL->clearPixels(false);
    
    // Use the word list directly from ClockDisplayHAL (DRY principle)
    int numWords = ClockDisplayHAL::getWordCount();
    
    // Display each word for 1 second with a random color
    for (int i = 0; i < numWords; i++)
    {
        // Check if test should abort
        if (shouldAbort && shouldAbort())
        {
            SERIAL_PRINTLN("Words test aborted by user");
            clockDisplayHAL->clearPixels(true);
            return;
        }
        
        esp_task_wdt_reset(); // Feed watchdog during test
        
        String wordName = String(ClockDisplayHAL::WORDS_TO_LEDS[i].word);
        
        clockDisplayHAL->clearPixels(false);
        highlightWord(wordName, getRandomColor());
        clockDisplayHAL->show();
        
        SERIAL_PRINT("Displaying: ");
        SERIAL_PRINTLN(wordName.c_str());
        
        // Delay with abort checks every 100ms
        for (int d = 0; d < 10; d++)
        {
            if (shouldAbort && shouldAbort())
            {
                SERIAL_PRINTLN("Words test aborted by user");
                clockDisplayHAL->clearPixels(true);
                return;
            }
            delay(100);
        }
    }
    
    clockDisplayHAL->clearPixels(true);
    SERIAL_PRINTLN("Words test complete");
    esp_task_wdt_reset();
}
