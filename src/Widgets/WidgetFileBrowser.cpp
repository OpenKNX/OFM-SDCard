// Backward-compat: flag renamed OPENKNX_SD_CARD_MODULE_ENABLE -> OPENKNX_SDCARD; old name still works.
#if defined(OPENKNX_SD_CARD_MODULE_ENABLE) && !defined(OPENKNX_SDCARD)
    #define OPENKNX_SDCARD
#endif
#if defined(DEVICE_DISPLAY_MODULE) && defined(OPENKNX_SDCARD)
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

    // (re)open at the SD root every time the browser is shown, on the list view.
    _view = View::List;
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

    // Only the list view reads the FS; the overlays work off the captured snapshot.
    if (_view == View::List && _needsReload)
        reload();

    // Drive the rename-cursor blink (~500ms) so it toggles without a button press.
    if (_view == View::Rename && (millis() - _blinkLast) >= BLINK_MS)
    {
        _blinkOn = !_blinkOn;
        _blinkLast = millis();
    }

    // Listing is cached (reload() only re-reads on folder change / explicit reload),
    // so this is a cheap buffer redraw, not an FS read per frame.
    switch (_view)
    {
        case View::List: drawBrowser(); break;
        case View::FileInfo: drawFileInfo(); break;
        case View::ConfirmDelete: drawConfirmDelete(); break;
        case View::Rename: drawRename(); break;
    }
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

    switch (_view)
    {
        case View::List: return handleListButton(event);
        case View::FileInfo: return handleFileInfoButton(event);
        case View::ConfirmDelete: return handleConfirmDeleteButton(event);
        case View::Rename: return handleRenameButton(event);
    }
    return false;
}

// LEFT navigation model (user spec): not on the first row -> jump to the first row; on the first
// row inside a sub-directory -> up one level; on the first row at the SD root (or empty) -> leave
// the browser (drop DisplayEnabled; the menu regains the screen at the SD submenu).
bool WidgetFileBrowser::handleListButton(const ButtonEvent &event)
{
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
            if (total == 0) // empty root -> nothing to browse, leave
            {
                removeAction(static_cast<uint8_t>(WidgetFlags::DisplayEnabled));
                return true;
            }
            if (_selectedIndex != 0) // not on the first row -> jump to the top
            {
                _selectedIndex = 0;
                _windowStart = 0;
                drawBrowser();
                return true;
            }
            if (hasUp) // first row inside a sub-directory -> up one level
            {
                goUp();
                reload();
                drawBrowser();
                return true;
            }
            // First row at the SD root -> hand control back to the menu.
            removeAction(static_cast<uint8_t>(WidgetFlags::DisplayEnabled));
            return true;

        case ButtonType::RIGHT:
        case ButtonType::SELECT:
            if (total == 0)
                return true;
            if (hasUp && _selectedIndex == 0) // ".." up-entry
            {
                goUp();
                reload();
                drawBrowser();
                return true;
            }
            {
                const SdDirEntry &e = _entries[_selectedIndex - upCount];
                if (e.isDir)
                {
                    enterDir(e.name);
                    reload();
                    drawBrowser();
                }
                else
                {
                    openFileInfo(e); // file -> Name/Size/Erstellt + Loeschen/Umbenennen overlay
                    drawFileInfo();
                }
            }
            return true;

        default:
            return false;
    }
}

// FileInfo overlay: UP/DOWN move the action cursor, OK activates, LEFT returns to the listing.
bool WidgetFileBrowser::handleFileInfoButton(const ButtonEvent &event)
{
    switch (event.type)
    {
        case ButtonType::UP:
            _infoSel = (_infoSel == 0) ? 2 : (uint8_t)(_infoSel - 1);
            drawFileInfo();
            return true;

        case ButtonType::DOWN:
            _infoSel = (uint8_t)((_infoSel + 1) % 3);
            drawFileInfo();
            return true;

        case ButtonType::LEFT:
            _view = View::List;
            drawBrowser();
            return true;

        case ButtonType::RIGHT:
        case ButtonType::SELECT:
            if (_infoSel == 0) // Loeschen -> confirm guard (default Nein)
            {
                _confirmSel = 0;
                _view = View::ConfirmDelete;
                drawConfirmDelete();
            }
            else if (_infoSel == 1) // Umbenennen -> char-scroll editor over the name
            {
                _rename = _selName;
                if (_rename.empty())
                    _rename = " ";
                if (_rename.size() > NAME_EDIT_MAX)
                    _rename.resize(NAME_EDIT_MAX);
                _renameCursor = 0;
                _blinkOn = true;
                _blinkLast = millis();
                _view = View::Rename;
                drawRename();
            }
            else // Zurueck
            {
                _view = View::List;
                drawBrowser();
            }
            return true;

        default:
            return true; // consume anything else while the overlay is up
    }
}

// Delete guard, horizontal [Nein][Ja]: LEFT selects Nein, RIGHT selects Ja (LEFT does NOT exit).
// UP/DOWN toggle. OK commits — Ja deletes, Nein returns to the file-info screen.
bool WidgetFileBrowser::handleConfirmDeleteButton(const ButtonEvent &event)
{
    switch (event.type)
    {
        case ButtonType::LEFT:
            _confirmSel = 0; // Nein (left button)
            drawConfirmDelete();
            return true;

        case ButtonType::RIGHT:
            _confirmSel = 1; // Ja (right button)
            drawConfirmDelete();
            return true;

        case ButtonType::UP:
        case ButtonType::DOWN:
            _confirmSel = _confirmSel ? 0 : 1; // toggle
            drawConfirmDelete();
            return true;

        case ButtonType::SELECT:
            if (_confirmSel == 1)
                doDelete(); // Ja -> remove + back to the reloaded list
            else
            {
                _view = View::FileInfo; // Nein
                drawFileInfo();
            }
            return true;

        default:
            return true;
    }
}

// Rename editor (mirrors the MenuWidget char-scroll editor): UP/DOWN cycle the char at the cursor,
// LEFT moves left (LEFT at 0 cancels), RIGHT moves/grows, OK commits.
bool WidgetFileBrowser::handleRenameButton(const ButtonEvent &event)
{
    static const std::string CHARSET =
        " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-";
    const size_t N = CHARSET.size();

    switch (event.type)
    {
        case ButtonType::UP:
        case ButtonType::DOWN:
        {
            if (_renameCursor >= _rename.size())
                break;
            size_t idx = CHARSET.find(_rename[_renameCursor]);
            if (idx == std::string::npos)
                idx = 0;
            idx = (event.type == ButtonType::UP) ? (idx + 1) % N : (idx + N - 1) % N;
            _rename[_renameCursor] = CHARSET[idx];
            break;
        }

        case ButtonType::LEFT:
            if (_renameCursor == 0)
            {
                _view = View::FileInfo; // cancel (no rename)
                drawFileInfo();
                return true;
            }
            _renameCursor--;
            break;

        case ButtonType::RIGHT:
            if ((size_t)_renameCursor + 1 < _rename.size())
                _renameCursor++;
            else if (_rename.size() < NAME_EDIT_MAX)
            {
                _rename.push_back(' ');
                _renameCursor = (uint8_t)(_rename.size() - 1);
            }
            break;

        case ButtonType::SELECT:
            commitRename();
            return true;

        default:
            return true;
    }

    _blinkOn = true;
    _blinkLast = millis();
    drawRename();
    return true;
}

std::string WidgetFileBrowser::fullPathOf(const std::string &name) const
{
    const std::string base = currentPath(); // "/" (root) or "/seg/seg"
    if (base == "/")
        return "/" + name;
    return base + "/" + name;
}

void WidgetFileBrowser::openFileInfo(const SdDirEntry &e)
{
    _selName = e.name.c_str();
    _selSize = e.size;
    _selCtime = 0;

    // Creation time via Statistics(folder, name) — it builds folder + "/" + name internally, so
    // pass "" at the root to avoid a "//name" double slash.
    const std::string base = currentPath();
    const char *folder = (base == "/") ? "" : base.c_str();
    FileInfo info;
    if (sdCardModule.Statistics(folder, _selName.c_str(), info))
        _selCtime = info.ctime;

    _infoSel = 0;
    _view = View::FileInfo;
}

void WidgetFileBrowser::doDelete()
{
    const std::string full = fullPathOf(_selName);
    const bool ok = sdCardModule.remove(full.c_str());
    logInfoP("Delete \"%s\": %s", full.c_str(), ok ? "ok" : "failed");
    _view = View::List;
    _needsReload = true;
    reload();
    drawBrowser();
}

void WidgetFileBrowser::commitRename()
{
    // Trim trailing spaces; an empty or unchanged name is a no-op back to the info screen.
    const size_t end = _rename.find_last_not_of(' ');
    const std::string newName = (end == std::string::npos) ? std::string() : _rename.substr(0, end + 1);
    if (newName.empty() || newName == _selName)
    {
        _view = View::FileInfo;
        drawFileInfo();
        return;
    }
    const std::string oldFull = fullPathOf(_selName);
    const std::string newFull = fullPathOf(newName);
    const bool ok = sdCardModule.rename(oldFull.c_str(), newFull.c_str());
    logInfoP("Rename \"%s\" -> \"%s\": %s", oldFull.c_str(), newFull.c_str(), ok ? "ok" : "failed");
    _view = View::List;
    _needsReload = true;
    reload();
    drawBrowser();
}

void WidgetFileBrowser::drawFileInfo()
{
    if (!_display)
        return;
    auto *d = _display->display;
    const int16_t W = _display->GetDisplayWidth();

    d->clearDisplay();
    d->setTextWrap(false);
    d->setTextSize(1);
    d->setTextColor(WHITE);

    // Title: file name (truncated) + separator.
    std::string name = _selName;
    const int cells = W / 6;
    if (cells > 3 && (int)name.size() > cells)
        name = name.substr(0, (size_t)cells - 3) + "...";
    d->setCursor(0, 0);
    d->print(name.c_str());
    d->drawLine(0, 10, W, 10, WHITE);

    // Info block: size + creation time.
    char line[32];
    snprintf(line, sizeof(line), "Groesse: %s", sdCardModule.formatSize(_selSize));
    d->setCursor(0, 13);
    d->print(line);

    if (_selCtime != 0)
    {
        struct tm tmv;
        gmtime_r(&_selCtime, &tmv);
        snprintf(line, sizeof(line), "Erst: %02d.%02d.%02d %02d:%02d",
                 tmv.tm_mday, tmv.tm_mon + 1, (tmv.tm_year + 1900) % 100, tmv.tm_hour, tmv.tm_min);
    }
    else
        snprintf(line, sizeof(line), "Erst: --");
    d->setCursor(0, 23);
    d->print(line);

    // Action rows: Loeschen / Umbenennen / Zurueck (selected row inverted).
    static const char *const ACTIONS[3] = {"Loeschen", "Umbenennen", "Zurueck"};
    const int16_t ROW_TOP = 36, ROW_H = 9;
    int16_t y = ROW_TOP;
    for (uint8_t i = 0; i < 3; i++, y += ROW_H)
    {
        if (i == _infoSel)
        {
            d->fillRect(0, y - 1, W, ROW_H, WHITE);
            d->setTextColor(BLACK);
        }
        else
            d->setTextColor(WHITE);
        d->setCursor(2, y);
        d->print(ACTIONS[i]);
    }

    d->setTextColor(WHITE);
    _display->displayBuff();
}

void WidgetFileBrowser::drawConfirmDelete()
{
    if (!_display)
        return;
    auto *d = _display->display;
    const int16_t W = _display->GetDisplayWidth();

    d->clearDisplay();
    d->setTextWrap(false);
    d->setTextSize(1);
    d->setTextColor(WHITE);

    d->setCursor(0, 0);
    d->print("Loeschen?");
    d->drawLine(0, 10, W, 10, WHITE);

    std::string name = _selName;
    const int cells = W / 6;
    if (cells > 3 && (int)name.size() > cells)
        name = name.substr(0, (size_t)cells - 3) + "...";
    d->setCursor(0, 16);
    d->print(name.c_str());

    // Two buttons [Nein] [Ja]; the selected one is filled, the other outlined.
    static const char *const OPTS[2] = {"Nein", "Ja"};
    const int16_t by = 40, bh = 14, bw = 50;
    for (uint8_t i = 0; i < 2; i++)
    {
        const int16_t bx = (i == 0) ? 8 : (int16_t)(W - bw - 8);
        if (i == _confirmSel)
        {
            d->fillRect(bx, by, bw, bh, WHITE);
            d->setTextColor(BLACK);
        }
        else
        {
            d->drawRect(bx, by, bw, bh, WHITE);
            d->setTextColor(WHITE);
        }
        const int16_t tw = (int16_t)(strlen(OPTS[i]) * 6);
        d->setCursor((int16_t)(bx + (bw - tw) / 2), (int16_t)(by + 4));
        d->print(OPTS[i]);
    }

    d->setTextColor(WHITE);
    _display->displayBuff();
}

void WidgetFileBrowser::drawRename()
{
    if (!_display)
        return;
    auto *d = _display->display;
    const int16_t W = _display->GetDisplayWidth();
    const int16_t H = _display->GetDisplayHeight();

    d->clearDisplay();
    d->setTextWrap(false);

    d->setTextSize(1);
    d->setTextColor(WHITE);
    d->setCursor(2, 0);
    d->print("Umbenennen:");

    // Characters at size 2, windowed so the cursor stays visible; the active cell blinks.
    d->setTextSize(2);
    const int16_t cellW = 12, cellH = 16;
    const uint8_t visible = (uint8_t)((W - 4) / cellW);
    uint8_t start = 0;
    if (visible > 0 && _renameCursor >= visible)
        start = (uint8_t)(_renameCursor - visible + 1);
    const int16_t y = (int16_t)((H - cellH) / 2);

    int16_t cx = 2;
    for (uint8_t i = start; i < _rename.size() && i < start + visible; ++i)
    {
        const bool active = (i == _renameCursor);
        const char c = _rename[i];
        if (active)
        {
            if (_blinkOn)
            {
                d->fillRect(cx - 1, y - 1, cellW + 1, cellH + 1, WHITE);
                d->setTextColor(BLACK);
                d->setCursor(cx, y);
                d->write((uint8_t)c);
                d->setTextColor(WHITE);
            }
            // "off" phase: leave the active cell blank.
        }
        else
        {
            d->setTextColor(WHITE);
            d->setCursor(cx, y);
            d->write((uint8_t)c);
        }
        cx = (int16_t)(cx + cellW);
    }

    d->setTextSize(1);
    d->setTextColor(WHITE);
    d->setCursor(2, (int16_t)(H - 9));
    d->print("OK=ok  <=zurueck");
    _display->displayBuff();
}

#endif // DEVICE_DISPLAY_MODULE && OPENKNX_SDCARD
