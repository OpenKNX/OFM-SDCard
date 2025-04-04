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

    if (sdCardModule.isCardInserted() && sdCardModule.isMounted() && sdCardModule.getCardInfo().isValid)
    {
        uint64_t freeBytes = 0, usedBytes = 0, totalBytes = sdCardModule.getSDCardSize();
        if (totalBytes > 0 && sdCardModule.getSDCardUsage(freeBytes, usedBytes))
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

        String CardMessage1 = "SD-CARD REMOVED.";
        String CardMessage2 = "Please insert card.";
        if (sdCardModule.isCardInserted())
        {
            if (sdCardModule.isMounted())
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