#if defined(DEVICE_DISPLAY_MODULE) && (defined(OPENKNX_SDCARD) || defined(OPENKNX_SD_CARD_MODULE_ENABLE))
    #include "WidgetSDCard.h"
    #include "DisplayUi.h"
    #include "OpenKNX.h"

namespace Ui = OpenKNX::Ui;

namespace
{
    constexpr uint8_t kLabelCols = 6; // details rows: labels up to 5 characters, values up to 15
    constexpr int16_t kPctW = 4 * Ui::CharW; // "100%"

    /** @brief Card symbol: SD card with a cut corner and four contacts, in x53..75 y1..27. */
    void drawSdSymbol(Adafruit_SSD1306 &d)
    {
        d.drawFastHLine(53, 1, 17, WHITE);
        d.drawLine(69, 1, 75, 7, WHITE);
        d.drawFastVLine(75, 7, 21, WHITE);
        d.drawFastHLine(53, 27, 23, WHITE);
        d.drawFastVLine(53, 1, 27, WHITE);
        for (int16_t x = 56; x <= 68; x += 4)
            d.fillRect(x, 4, 2, 6, WHITE);
    }
} // namespace

WidgetSDCard::WidgetSDCard(uint32_t displayTime, WidgetFlags action)
    : _state(WidgetState::STOPPED), _displayTime(displayTime), _action(action), _display(nullptr)
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
    _forceRedraw = true;
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
        _forceRedraw = true;
    }
}

void WidgetSDCard::setPage(size_t page)
{
    if (page >= _pages)
        page = _pages - 1;
    if (page != _page)
    {
        _page = (uint8_t)page;
        _forceRedraw = true;
    }
}

void WidgetSDCard::loop()
{
    if (_state != WidgetState::RUNNING || !_display)
        return;

    // Once per second for the live values, at once after a page change.
    const uint32_t now = millis();
    if (!_forceRedraw && now - _lastDraw < REDRAW_INTERVAL_MS)
        return;
    _lastDraw = now;
    _forceRedraw = false;
    drawSDInfo();
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

    Adafruit_SSD1306 &d = *_display->display;
    Ui::begin(d);
    d.clearDisplay();

    // A running format takes over the whole widget, on one page. Percent advances only for Low-Level
    // (Quick/exFAT complete in a single tick and are practically never caught mid-format).
    if (sdCardModule.isFormatting())
    {
        _pages = 1;
        _page = 0;
        drawFormat(d);
        _display->displayBuff();
        return;
    }

    if (sdCardModule.isCardInserted() && sdCardModule.isMounted() && sdCardModule.getCardInfo().isValid)
    {
        // Free/used come from the NON-BLOCKING incremental scan: SDCardModule advances the FAT /
        // exFAT-bitmap count a few sectors per loop() (never a single spike). We only (re)TRIGGER a
        // scan here — on mount (_lastUsageQuery == 0) and then periodically — and read the last
        // COMPLETED result via getCachedUsage(). The 1x/s redraw just reuses whatever result is cached.
        if (_lastUsageQuery == 0 || (uint32_t)(millis() - _lastUsageQuery) >= SDINFO_USAGE_REFRESH_MS)
        {
            _lastUsageQuery = millis();
            sdCardModule.beginUsageScan(); // non-blocking; result lands over the next loops
        }
        uint64_t freeBytes = 0, usedBytes = 0, totalBytes = 0;
        if (sdCardModule.getCachedUsage(freeBytes, usedBytes, totalBytes) && totalBytes > 0)
        {
            _pages = 2;
            if (_page == 1)
                drawDetails(d, usedBytes, totalBytes);
            else
                drawCard(d, freeBytes, totalBytes);
        }
        else
        {
            _pages = 1;
            _page = 0;
            static const char *const kNoInfo[] = {"Card info", "not readable"};
            drawState(d, kNoInfo, 2);
        }
        _display->displayBuff();
        return;
    }

    _lastUsageQuery = 0; // invalidate cache -> re-query immediately on (re-)mount
    _pages = 1;
    _page = 0;

    static const char *const kRemoved[] = {"No SD card", "Insert a card"};
    static const char *const kUnformatted[] = {"Not formatted", "Format the card"};
    static const char *const kNoRead[] = {"Card mounted", "Card info", "not readable"};
    static const char *const kNotMounted[] = {"Card inserted", "Not mounted", "Check the card"};
    static const char *const kMounting[] = {"Mounting", "Please wait"};
    static const char *const kUnmounting[] = {"Unmounting", "Please wait"};
    static const char *const kError[] = {"Card error", "Check the card"};
    const char *const *lines = kRemoved;
    uint8_t count = 2;
    if (sdCardModule.isCardInserted())
    {
        if (sdCardModule.isCardUnformatted())
            lines = kUnformatted;
        else if (sdCardModule.isMounted())
            lines = kNoRead, count = 3;
        else if (sdCardModule.isUnmounted())
            lines = kNotMounted, count = 3;
        else if (sdCardModule.isMounting())
            lines = kMounting;
        else if (sdCardModule.isUnmounting())
            lines = kUnmounting;
        else if (sdCardModule.isError())
            lines = kError;
    }
    drawState(d, lines, count);
    _display->displayBuff();
}

void WidgetSDCard::drawFormat(Adafruit_SSD1306 &d)
{
    Ui::header(d, _name.c_str());
    // Fine percent (0.01 % resolution) so the number visibly moves even on a huge card.
    const uint16_t pm = sdCardModule.formatPermyriad(); // 0..10000 = 0.00..100.00 %
    char b[12];
    snprintf(b, sizeof(b), "%u.%02u%%", (unsigned)(pm / 100), (unsigned)(pm % 100));
    Ui::printCentred(d, 14, b, 2);
    Ui::bar(d, 0, 32, d.width(), (uint16_t)(pm / 10));
    Ui::printCentred(d, 44, "Formatting");
    Ui::printCentred(d, 54, sdCardModule.formatOpName());
}

void WidgetSDCard::drawCard(Adafruit_SSD1306 &d, uint64_t freeBytes, uint64_t totalBytes)
{
    // formatSize() returns one static buffer: copy the first value before the second call.
    char freeText[12], detail[24];
    snprintf(freeText, sizeof(freeText), "%s", sdCardModule.formatSize(freeBytes));
    snprintf(detail, sizeof(detail), "free of %s", sdCardModule.formatSize(totalBytes));
    Ui::statusCard(d, drawSdSymbol, nullptr, freeText, detail);
    Ui::pageDots(d, _pages, 0);
}

void WidgetSDCard::drawDetails(Adafruit_SSD1306 &d, uint64_t usedBytes, uint64_t totalBytes)
{
    Ui::header(d, _name.c_str(), _pages, 1);
    Ui::row(d, 0, "Type", sdCardModule.cardTypeName(true), kLabelCols);
    Ui::row(d, 1, "Maker", sdCardModule.getCardInfo().manufacturer.c_str(), kLabelCols);
    Ui::row(d, 2, "Total", sdCardModule.formatSize(totalBytes), kLabelCols);
    Ui::row(d, 3, "Used", sdCardModule.formatSize(usedBytes), kLabelCols);

    // Used share as a bar with the percent after it, both on the last row.
    const uint64_t permille = usedBytes * 1000 / totalBytes;
    char pct[8];
    snprintf(pct, sizeof(pct), "%u%%", (unsigned)(usedBytes * 100 / totalBytes));
    const int16_t pctX = (int16_t)(d.width() - Ui::ValueEnd - (int16_t)strlen(pct) * Ui::CharW);
    Ui::bar(d, 0, Ui::Row0 + 4 * Ui::Pitch, (int16_t)(d.width() - Ui::ValueEnd - kPctW - 4), (uint16_t)(permille > 1000 ? 1000 : permille));
    d.setCursor(pctX, Ui::Row0 + 4 * Ui::Pitch);
    d.print(pct);
}

void WidgetSDCard::drawState(Adafruit_SSD1306 &d, const char *const *lines, uint8_t count)
{
    drawSdSymbol(d);
    for (uint8_t i = 0; i < count; i++)
        Ui::printCentred(d, (int16_t)(34 + i * Ui::Pitch), lines[i]);
}

#endif // DEVICE_DISPLAY_MODULE
