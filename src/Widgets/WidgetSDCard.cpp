#ifdef DEVICE_DISPLAY_MODULE
    #include "WidgetSDCard.h"
    #include "OpenKNX.h"

WidgetSDCard::WidgetSDCard(uint32_t displayTime, WidgetFlags action)
    : _displayTime(displayTime), _action(action), _state(WidgetState::STOPPED), _display(nullptr)
{
}

void WidgetSDCard::setup()
{
    logDebugP("Setup...");
    if (_display == nullptr)
    {
        logErrorP("Display is NULL.");
        return;
    }
}

void WidgetSDCard::start()
{
    if (_state == WidgetState::RUNNING)
        return;

    logDebugP("Starting...");
    _state = WidgetState::RUNNING;
    _duration_timerStart = millis();
}

void WidgetSDCard::stop()
{
    logDebugP("Stopping...");
    _state = WidgetState::STOPPED;
    if (_display)
    {
        _display->display->clearDisplay();
        _display->displayBuff();
    }
}

void WidgetSDCard::pause()
{
    if (_state == WidgetState::RUNNING)
    {
        logDebugP("Pausing...");
        _state = WidgetState::PAUSED;
    }
}

void WidgetSDCard::resume()
{
    if (_state == WidgetState::PAUSED)
    {
        logDebugP("Resuming...");
        _state = WidgetState::RUNNING;
    }
}

static uint32_t lastUpdate = millis();
void WidgetSDCard::loop()
{
    if (_state != WidgetState::RUNNING || !_display)
        return;

    if (delayCheck(lastUpdate, 1000)) // Update every second
    {
        lastUpdate = millis();
        drawSDInfo();
    }
}

uint32_t WidgetSDCard::getDisplayTime() const
{
    return _displayTime;
}

WidgetFlags WidgetSDCard::getAction() const
{
    return _action;
}

void WidgetSDCard::setDisplayModule(i2cDisplay *displayModule)
{
    _display = displayModule;
}

i2cDisplay *WidgetSDCard::getDisplayModule() const
{
    return _display;
}

void WidgetSDCard::drawSDInfo()
{
    if (!_display)
        return;

    _display->display->clearDisplay();
    _display->display->setTextColor(WHITE);
    _display->display->setTextSize(1);

    const uint16_t SCREEN_WIDTH = _display->GetDisplayWidth();
    const uint16_t CENTER_X = SCREEN_WIDTH / 2;

    _display->display->setCursor((SCREEN_WIDTH - (getName().length() * 6)) / 2, 0);
    _display->display->print(getName().c_str());

    uint32_t elapsedMillis = millis() - _duration_timerStart;
    uint32_t remainingMillis = (_displayTime > elapsedMillis) ? (_displayTime - elapsedMillis) : 0;
    if (remainingMillis == 0)
    {
        _duration_timerStart = millis();
        remainingMillis = _displayTime; // Reset to initial time
    }
    uint16_t circlePosition = (SCREEN_WIDTH * elapsedMillis) / _displayTime;
    _display->display->fillCircle(circlePosition, 10, 2, WHITE);
    _display->display->drawCircle(circlePosition, 10, 2, BLACK);

    _display->display->drawLine(0, 10, SCREEN_WIDTH, 10, WHITE);

    // A running format takes over the whole widget body. Redrawn 1x/s by loop() while the widget is
    // shown, so the percent updates live. Percent advances only for Low-Level (Quick/exFAT complete
    // in a single tick and are practically never caught mid-format). The console logs in parallel.
    if (sdCardModule.isFormatting())
    {
        _display->display->setTextWrap(false);
        String l1 = String("FORMATIERE ") + sdCardModule.formatOpName();
        _display->display->setCursor(CENTER_X - (l1.length() * 3), 24);
        _display->display->print(l1.c_str());

        // Fine percent (0.01 % resolution) so the number visibly moves even on a huge card.
        const uint16_t pm = sdCardModule.formatPermyriad(); // 0..10000 = 0.00..100.00 %
        const unsigned whole = pm / 100, frac = pm % 100;
        String l2 = String(whole) + "." + (frac < 10 ? "0" : "") + String(frac) + "%";
        _display->display->setCursor(CENTER_X - (l2.length() * 3), 38);
        _display->display->print(l2.c_str());

        // Progress bar (fine: fills by hundredths of a percent).
        const int16_t barX = 14, barW = SCREEN_WIDTH - 28;
        _display->display->drawRect(barX, 52, barW, 8, WHITE);
        if (pm > 0)
            _display->display->fillRect(barX, 52, (int16_t)((uint32_t)barW * pm / 10000), 8, WHITE);

        _display->displayBuff();
        return;
    }

    if (sdCardModule.isCardInserted() && sdCardModule.isMounted() && sdCardModule.getCardInfo().isValid)
    {
        // Free/used come from the NON-BLOCKING incremental scan: SDCardModule advances the FAT /
        // exFAT-bitmap count a few sectors per loop() (never a single spike). We only (re)TRIGGER a
        // scan here — on mount (_lastUsageQuery == 0) and then periodically — and read the last
        // COMPLETED result via getCachedUsage(). Because triggering is free, refreshing often is now
        // safe. The 1x/s countdown redraw just reuses whatever result is cached.
        if (_lastUsageQuery == 0 || (uint32_t)(millis() - _lastUsageQuery) >= SDINFO_USAGE_REFRESH_MS)
        {
            _lastUsageQuery = millis();
            sdCardModule.beginUsageScan(); // non-blocking; result lands over the next loops
        }
        {
            uint64_t f = 0, u = 0, t = 0;
            _cachedUsageValid = sdCardModule.getCachedUsage(f, u, t);
            _cachedTotal = t;
            _cachedFree = f;
            _cachedUsed = u;
        }
        uint64_t freeBytes = _cachedFree, usedBytes = _cachedUsed, totalBytes = _cachedTotal;
        if (_cachedUsageValid && totalBytes > 0)
        {
            float usedPercentage = (totalBytes > 0) ? ((float)usedBytes * 100.0f / totalBytes) : 0.0f;

            // Infos
            String cardType = String("Type: ") + sdCardModule.getCardType(true).c_str() + " (" + String(sdCardModule.getCardInfo().manufacturer) + ")";
            if (cardType.length() > 18)
            {
                cardType = cardType.substring(0, 18) + "...";
            }
            String total = "Total: " + String(sdCardModule.formatSize(totalBytes));
            String free = "Free: " + String(sdCardModule.formatSize(freeBytes));
            String used = "Used: " + String(sdCardModule.formatSize(usedBytes));

            // Anzeige
            _display->display->setTextWrap(false);
            _display->display->setTextSize(1);
            _display->display->setCursor(CENTER_X - (cardType.length() * 3), 15);
            _display->display->print(cardType.c_str());

            _display->display->setCursor(CENTER_X - (total.length() * 3), 25);
            _display->display->print(total.c_str());

            _display->display->setCursor(CENTER_X - (free.length() * 3), 35);
            _display->display->print(free.c_str());

            _display->display->setCursor(CENTER_X - (used.length() * 3), 45);
            _display->display->print(used.c_str());

            // Show the storage bar with dynamic width based on usage percentage
            String percentageText = String((int)usedPercentage) + "%";
            int barWidth = SCREEN_WIDTH - (percentageText.length() * 12); // Total width of the bar
            int usedBarLength = (int)(usedPercentage * barWidth / 100);   // Dynamic width based on usage percentage

            _display->display->drawRect(1, 55, barWidth, 8, WHITE);      // Draw the outline of the bar
            _display->display->fillRect(1, 55, usedBarLength, 8, WHITE); // Fill the bar based on usage

            // Display the percentage text at the end of the bar
            _display->display->setCursor(1 + barWidth + 5, 55); // Position the text dynamically
            _display->display->print(percentageText.c_str());
        }
        else
        {
            _display->display->setCursor(CENTER_X - 40, 20);
            _display->display->print("Could not read SD info.");
        }
    }
    else
    {
        _lastUsageQuery = 0; // invalidate cache -> re-query immediately on (re-)mount

        String CardMessage1 = "SD-CARD REMOVED.";
        String CardMessage2 = "Please insert card.";
        if (sdCardModule.isCardInserted())
        {
            if (sdCardModule.isCardUnformatted())
            {
                CardMessage1 = "NICHT FORMATIERT";
                CardMessage2 = "Bitte formatieren!";
            }
            else if (sdCardModule.isMounted())
            {
                CardMessage1 = "SD-CARD INSERTED.";
                CardMessage2 = "Mounted. Could not read info!";
            }
            else if (sdCardModule.isUnmounted())
            {
                CardMessage1 = "SD-CARD INSERTED.";
                CardMessage2 = "Not mounted! Check card.";
            }
            else if (sdCardModule.isMounting())
            {
                CardMessage1 = "SD-CARD MOUNTING.";
                CardMessage2 = "Please wait...";
            }
            else if (sdCardModule.isUnmounting())
            {
                CardMessage1 = "SD-CARD UNMOUNTING.";
                CardMessage2 = "Please wait...";
            }
            else if (sdCardModule.isError())
            {
                CardMessage1 = "SD-CARD ERROR!";
                CardMessage2 = "Check card!";
            }
        }

        _display->display->setCursor(CENTER_X - (CardMessage1.length() * 3), 30);
        _display->display->print(CardMessage1);
        _display->display->setCursor(CENTER_X - (CardMessage2.length() * 3), 45);
        _display->display->print(CardMessage2);
    }

    _display->displayBuff();
}

#endif // DEVICE_DISPLAY_MODULE
