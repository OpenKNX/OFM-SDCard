#if defined(DEVICE_DISPLAY_MODULE) && defined(OPENKNX_SD_CARD_MODULE_ENABLE)
/**
 * @file        WidgetFileBrowser.cpp
 * @brief       Button-driven SD-card file browser widget for the OpenKNX DeviceDisplay
 *              menu system (implementation).
 * @version     0.0.1
 * @date        2025-02-15
 * @copyright   Copyright (c) 2025, Erkan Çolak
 *              Licensed under GNU GPL v3.0
 **/

    #include "WidgetFileBrowser.h"
    #include "OpenKNX.h"

    #include <algorithm> // std::min
    #include <cstdio>    // snprintf
    #include <cstring>   // strlen

WidgetFileBrowser::WidgetFileBrowser(uint32_t displayTime, WidgetFlags action)
    : _state(WidgetState::STOPPED), _displayTime(displayTime), _action(action), _display(nullptr)
{
}

void WidgetFileBrowser::setup()
{
    logDebugP("Setup...");
    if (_display == nullptr)
    {
        logErrorP("Display is NULL.");
        return;
    }
}

void WidgetFileBrowser::start()
{
    if (_state == WidgetState::RUNNING)
        return;

    logDebugP("Starting...");
    _state = WidgetState::RUNNING;

    // (re)open at the SD root every time the browser is shown.
    pathStack.clear();
    _resetView();
    reload();
}

void WidgetFileBrowser::stop()
{
    logDebugP("Stopping...");
    _state = WidgetState::STOPPED;
    _entries.clear();
    _needsReload = true;
    if (_display)
    {
        _display->display->clearDisplay();
        _display->displayBuff();
    }
}

void WidgetFileBrowser::pause()
{
    if (_state == WidgetState::RUNNING)
    {
        logDebugP("Pausing...");
        _state = WidgetState::PAUSED;
    }
}

void WidgetFileBrowser::resume()
{
    if (_state == WidgetState::PAUSED)
    {
        logDebugP("Resuming...");
        _state = WidgetState::RUNNING;
    }
}

void WidgetFileBrowser::loop()
{
    if (_state != WidgetState::RUNNING || !_display)
        return;

    if (_needsReload)
        reload();

    // Listing is cached (reload() only re-reads on folder change / explicit reload),
    // so this is a cheap buffer redraw, not an FS read per frame.
    drawBrowser();
}

uint32_t WidgetFileBrowser::getDisplayTime() const
{
    return _displayTime;
}

WidgetFlags WidgetFileBrowser::getAction() const
{
    return _action;
}

void WidgetFileBrowser::setDisplayModule(i2cDisplay *displayModule)
{
    _display = displayModule;
}

i2cDisplay *WidgetFileBrowser::getDisplayModule() const
{
    return _display;
}

    // GCC 14 (arm-none-eabi + xtensa, -Os) misfires -Wfree-nonheap-object on the inlined
    // ~basic_string() SSO branch of the locally-built string below — the guarded operator
    // delete on the stack SSO buffer is unreachable at runtime (GCC PR 100862/109703). The
    // reserve() is kept as a genuine single-allocation win; this localized pragma silences
    // the remaining false positive for this one function only (never a global -Wno-).
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wfree-nonheap-object"
std::string WidgetFileBrowser::currentPath() const
{
    // Root == "/". Otherwise "/seg0/seg1/...". Mirrors the mock's
    // "/"+S.fpath.map(name).join("/") behavior.
    if (pathStack.empty())
        return "/";

    // Pre-size to the exact final length: one heap allocation instead of several
    // grow-reallocs, and forcing the buffer off the SSO stack buffer sidesteps the
    // GCC-14 -Wfree-nonheap-object false positive on the inlined ~basic_string()
    // (guarded operator delete on the SSO buffer; GCC PR 100862/109703).
    size_t reserveLen = 0;
    for (size_t i = 0; i < pathStack.size(); i++)
        reserveLen += 1 + pathStack[i].length();

    std::string path;
    path.reserve(reserveLen);
    for (size_t i = 0; i < pathStack.size(); i++)
    {
        path.append("/");
        path.append(pathStack[i].c_str());
    }
    return path;
}
    #pragma GCC diagnostic pop

void WidgetFileBrowser::enterDir(const String &name)
{
    // Descend into sub-directory 'name' (relative to the current directory) and reset the
    // selection + scroll window to the top of the new directory (mock: fsel=0, win=0).
    pathStack.push_back(name);
    _resetView();
}

bool WidgetFileBrowser::goUp()
{
    // Ascend to the parent directory. Already at root -> nothing to do.
    if (pathStack.empty())
        return false;

    pathStack.pop_back();
    _resetView();
    return true;
}

void WidgetFileBrowser::reload()
{
    // (Re)populate the entry cache for the current path via the Phase-0 single-pass
    // listing API. Leaves the cache empty when the card is not mounted (listDir no-ops).
    _entries.clear();
    sdCardModule.listDir(currentPath().c_str(), _entries);
    _needsReload = false;

    // Keep the selection valid after the listing changed (mock clamps fsel to len-1).
    if (_selectedIndex >= _entries.size())
        _selectedIndex = _entries.empty() ? 0 : (_entries.size() - 1);
    if (_windowStart > _selectedIndex)
        _windowStart = _selectedIndex;
}

// Visible list: an optional ".." up-entry row (only when not at root) followed by the cached
// entries. listDir() yields directories before files, so entry order matches the mock.
static inline bool _atRootPath(const std::string &p) { return p == "/"; }

void WidgetFileBrowser::_updateWindow()
{
    // Keep _selectedIndex inside [_windowStart, _windowStart + VISIBLE_ROWS - 1].
    if (_selectedIndex < _windowStart)
        _windowStart = _selectedIndex;
    else if (_selectedIndex > _windowStart + (VISIBLE_ROWS - 1))
        _windowStart = _selectedIndex - (VISIBLE_ROWS - 1);
    // _windowStart / _selectedIndex are size_t; the branches above keep _windowStart >= 0.
}

void WidgetFileBrowser::drawBrowser()
{
    if (!_display)
        return;

    const std::string path = currentPath();
    const bool hasUp = !_atRootPath(path); // ".." up-entry shown in every non-root directory

    // Total navigable rows = optional up-entry + cached entries (mock fEntries()).
    const size_t upCount = hasUp ? 1 : 0;
    const size_t total = upCount + _entries.size();

    // Clamp selection defensively (reload() already clamps against _entries, but the
    // up-entry shifts indices by one) and refresh the scroll window before drawing.
    if (total == 0)
        _selectedIndex = 0;
    else if (_selectedIndex >= total)
        _selectedIndex = total - 1;
    _updateWindow();

    _display->display->clearDisplay();
    _display->display->setTextWrap(false);
    _display->display->setTextColor(WHITE);
    _display->display->setTextSize(1);

    const int16_t SCREEN_WIDTH = _display->GetDisplayWidth();

    // --- Header: "SD:<path>" left, "(sel+1)/total" right, separator line (like WidgetSDCard).
    // Header path is truncated with "..." so it never collides with the page counter.
    char counter[16];
    snprintf(counter, sizeof(counter), "%u/%u",
             (unsigned)(total == 0 ? 0 : _selectedIndex + 1), (unsigned)total);
    const int counterChars = (int)strlen(counter);
    const int counterPx = counterChars * 6;

    std::string header = "SD:";
    header += path;
    // Available character cells for the header before the right-aligned counter (+1 gap).
    const int headerCells = (SCREEN_WIDTH - counterPx) / 6 - 1;
    if (headerCells > 3 && (int)header.size() > headerCells)
        header = header.substr(0, (size_t)headerCells - 3) + "...";

    _display->display->setCursor(0, 0);
    _display->display->print(header.c_str());
    _display->display->setCursor(SCREEN_WIDTH - counterPx, 0);
    _display->display->print(counter);
    _display->display->drawLine(0, 10, SCREEN_WIDTH, 10, WHITE);

    // Empty directory (no entries and no up-entry -> only possible at an empty root).
    if (total == 0)
    {
        _display->display->setCursor(0, 14);
        _display->display->print("(empty)");
        _display->displayBuff();
        return;
    }

    // --- Rows -----------------------------------------------------------------------------
    const int16_t ROW_TOP = 13;    // First row baseline just below the separator line
    const int16_t ROW_HEIGHT = 10; // Vertical pitch per row (fits 5 rows in y=13..63)
    const int16_t ROW_TEXT_DY = 1; // Text offset inside the inverted row rectangle

    const size_t windowEnd = std::min(total, _windowStart + VISIBLE_ROWS);
    int16_t y = ROW_TOP;
    for (size_t i = _windowStart; i < windowEnd; i++, y += ROW_HEIGHT)
    {
        const bool selected = (i == _selectedIndex);

        // Build the label + right-aligned value for this row.
        std::string label;
        std::string value;
        if (hasUp && i == 0)
        {
            label = ".."; // Up-entry (mock: "< ..")
        }
        else
        {
            const size_t entryIdx = i - upCount;
            const SdDirEntry &e = _entries[entryIdx];
            if (e.isDir)
            {
                label = std::string(e.name.c_str()) + "/"; // Folder: name + "/" suffix
                value = "DIR";
            }
            else
            {
                label = e.name.c_str();
                value = sdCardModule.formatSize(e.size); // Right-aligned human size
            }
        }

        // Reserve space for the right-aligned value (+1 cell gap) so the label truncation
        // never overlaps it. valueCells==0 when there is no value (up-entry).
        const int valueCells = value.empty() ? 0 : ((int)value.size() + 1);
        const int totalCells = SCREEN_WIDTH / 6;
        int labelCells = totalCells - valueCells;
        if (labelCells < 1)
            labelCells = 1;

        // Truncate long names with "..." (mock marquees; on-device we truncate).
        if ((int)label.size() > labelCells)
        {
            if (labelCells > 3)
                label = label.substr(0, (size_t)labelCells - 3) + "...";
            else
                label = label.substr(0, (size_t)labelCells);
        }

        if (selected)
        {
            // Selected row inverted: filled bar + black text (matches the mock's .sel row).
            _display->display->fillRect(0, y - 1, SCREEN_WIDTH, ROW_HEIGHT, WHITE);
            _display->display->setTextColor(BLACK);
        }
        else
        {
            _display->display->setTextColor(WHITE);
        }

        _display->display->setCursor(0, y + ROW_TEXT_DY - 1);
        _display->display->print(label.c_str());

        if (!value.empty())
        {
            const int valuePx = (int)value.size() * 6;
            _display->display->setCursor(SCREEN_WIDTH - valuePx, y + ROW_TEXT_DY - 1);
            _display->display->print(value.c_str());
        }
    }

    _display->display->setTextColor(WHITE); // Restore default for subsequent draws
    _display->displayBuff();
}

bool WidgetFileBrowser::handleButtonEvent(const ButtonEvent &event)
{
    // Only short presses drive the browser; ignore LONG/VERY_LONG/RELEASE (those belong to
    // the gesture engine). Not-consumed events return false so the caller can react.
    if (event.action != ButtonAction::PRESS)
        return false;

    // Make sure the listing is current before acting on it.
    if (_needsReload)
        reload();

    const bool hasUp = !_atRootPath(currentPath());
    const size_t upCount = hasUp ? 1 : 0;
    const size_t total = upCount + _entries.size();

    switch (event.type)
    {
        case ButtonType::UP:
            if (total > 0)
                _selectedIndex = (_selectedIndex == 0) ? (total - 1) : (_selectedIndex - 1);
            _updateWindow();
            drawBrowser();
            return true;

        case ButtonType::DOWN:
            if (total > 0)
                _selectedIndex = (_selectedIndex + 1) % total;
            _updateWindow();
            drawBrowser();
            return true;

        case ButtonType::LEFT:
            // Up one level; at the SD root hand control back to the menu (mock: files->menu).
            if (!goUp())
            {
                // At the SD root, hand the display back ourselves by dropping DisplayEnabled:
                // returning false won't close us because DeviceDisplay only wakes on a non-consumed
                // event, so the browser would stay stuck open. Consume the event afterwards.
                removeAction(static_cast<uint8_t>(WidgetFlags::DisplayEnabled));
                return true;
            }
            reload();
            drawBrowser();
            return true;

        case ButtonType::RIGHT:
        case ButtonType::SELECT:
            // OK/RIGHT: activate the selected row (up-entry -> goUp, folder -> enterDir,
            // file -> file action). File actions are not wired yet; for now this is a no-op
            // that still consumes the event so the browser stays put.
            if (total == 0)
                return true;
            if (hasUp && _selectedIndex == 0)
            {
                goUp();
                reload();
            }
            else
            {
                const SdDirEntry &e = _entries[_selectedIndex - upCount];
                if (e.isDir)
                {
                    enterDir(e.name);
                    reload();
                }
                // else: file selected -> file-action overlay not yet wired.
            }
            drawBrowser();
            return true;

        default:
            return false;
    }
}

#endif // DEVICE_DISPLAY_MODULE && OPENKNX_SD_CARD_MODULE_ENABLE
