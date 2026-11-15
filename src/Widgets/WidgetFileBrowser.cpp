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
    #include "DisplayUi.h"
    #include "OpenKNX.h"

    #include <algorithm> // std::min
    #include <cstdio>    // snprintf
    #include <cstring>   // strlen

namespace Ui = OpenKNX::Ui;

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
    const std::string path = currentPath();
    _entries.clear();
    sdCardModule.listDir(path.c_str(), _entries);
    _needsReload = false;

    // Keep the selection on a valid row; outside the root the ".." row comes first.
    const size_t rows = (path == "/" ? 0 : 1) + _entries.size();
    if (_selectedIndex >= rows)
        _selectedIndex = rows == 0 ? 0 : rows - 1;
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

    // Clamp the selection to the rows (reload() does the same) and refresh the scroll window.
    if (total == 0)
        _selectedIndex = 0;
    else if (_selectedIndex >= total)
        _selectedIndex = total - 1;
    _updateWindow();
    // A listing that shrank after a delete keeps the window full; the selection stays inside it.
    if (total <= VISIBLE_ROWS)
        _windowStart = 0;
    else if (_windowStart > total - VISIBLE_ROWS)
        _windowStart = total - VISIBLE_ROWS;

    Adafruit_SSD1306 &d = *_display->display;
    Ui::begin(d);
    d.clearDisplay();

    // Header: "SD:<path>" left (cut with "..."), "(sel+1)/total" right, dotted line.
    char title[24];
    snprintf(title, sizeof(title), "SD:%s", path.c_str());
    Ui::headerPos(d, title, (uint16_t)(total == 0 ? 0 : _selectedIndex + 1), (uint16_t)total);

    // Empty directory (no entries and no up-entry -> only possible at an empty root).
    if (total == 0)
    {
        d.setCursor(TEXT_X, LIST_TOP + 1);
        d.print("(empty)");
        _display->displayBuff();
        return;
    }

    // Rows: inverse selection up to x123 and value cells ending at x123 when the scrollbar is drawn, else x127 / x125.
    const bool scrollbar = total > VISIBLE_ROWS;
    const int16_t rowW = scrollbar ? SCROLL_X : d.width();
    const int16_t valueEnd = scrollbar ? SCROLL_X : (int16_t)(d.width() - Ui::ValueEnd);
    const size_t windowEnd = std::min(total, _windowStart + VISIBLE_ROWS);
    int16_t y = LIST_TOP;
    for (size_t i = _windowStart; i < windowEnd; i++, y += Ui::Pitch)
    {
        const bool selected = (i == _selectedIndex);

        // Label + right-aligned value; formatSize() is printed before any further call reuses its buffer.
        const char *name = "..";    // Up-entry
        const char *value = nullptr;
        bool dir = false;
        if (!(hasUp && i == 0))
        {
            const SdDirEntry &e = _entries[i - upCount];
            name = e.name.c_str();
            dir = e.isDir;
            value = dir ? "DIR" : sdCardModule.formatSize(e.size);
        }

        if (selected)
            d.fillRect(0, y, rowW, Ui::Pitch, WHITE);
        d.setTextColor(selected ? BLACK : WHITE);

        int16_t labelEnd = valueEnd; // the label ends 2 px before the value, else before valueEnd
        if (value)
        {
            int16_t vx = (int16_t)(valueEnd - (int16_t)strlen(value) * Ui::CharW);
            if (vx < 0)
                vx = 0;
            d.setCursor(vx, (int16_t)(y + 1));
            d.print(value);
            labelEnd = (int16_t)(vx - 2);
        }
        printName(d, (int16_t)(y + 1), name, dir, (int16_t)((labelEnd - TEXT_X) / Ui::CharW));
    }

    if (scrollbar)
        drawScrollbar(d, _windowStart, total);
    d.setTextColor(WHITE);
    _display->displayBuff();
}

void WidgetFileBrowser::printName(Adafruit_SSD1306 &d, int16_t y, const char *name, bool dir, int16_t maxChars)
{
    // A folder shows "name/"; the "/" is part of the text that is cut with "...".
    if (!dir)
    {
        Ui::printCut(d, TEXT_X, y, name, maxChars);
        return;
    }
    char b[24];
    snprintf(b, sizeof(b), "%s/", name);
    Ui::printCut(d, TEXT_X, y, b, maxChars);
}

void WidgetFileBrowser::drawScrollbar(Adafruit_SSD1306 &d, size_t first, size_t count)
{
    // Dots at x127 every second row y12..60, thumb x126..127 placed by the first visible row.
    const int16_t track = (int16_t)(VISIBLE_ROWS * Ui::Pitch);
    for (int16_t py = LIST_TOP; py < LIST_TOP + track; py += 2)
        d.drawPixel(127, py, WHITE);
    int16_t thumb = (int16_t)(track * (int32_t)VISIBLE_ROWS / (int32_t)count);
    if (thumb < 4)
        thumb = 4;
    const int16_t ty = (int16_t)(LIST_TOP + (int32_t)(track - thumb) * (int32_t)first / (int32_t)(count - VISIBLE_ROWS));
    d.fillRect(126, ty, 2, thumb, WHITE);
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
                    openFileInfo(e); // file -> Name/Size/Created + Delete/Rename overlay
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
            if (_infoSel == 0) // Delete -> confirm guard (default No)
            {
                _confirmSel = 0;
                _view = View::ConfirmDelete;
                drawConfirmDelete();
            }
            else if (_infoSel == 1) // Rename -> char-scroll editor over the name
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
            else // Back
            {
                _view = View::List;
                drawBrowser();
            }
            return true;

        default:
            return true; // consume anything else while the overlay is up
    }
}

// Delete guard, horizontal [No][Yes]: LEFT selects No, RIGHT selects Yes (LEFT does NOT exit).
// UP/DOWN toggle. OK commits — Yes deletes, No returns to the file-info screen.
bool WidgetFileBrowser::handleConfirmDeleteButton(const ButtonEvent &event)
{
    switch (event.type)
    {
        case ButtonType::LEFT:
            _confirmSel = 0; // No (left button)
            drawConfirmDelete();
            return true;

        case ButtonType::RIGHT:
            _confirmSel = 1; // Yes (right button)
            drawConfirmDelete();
            return true;

        case ButtonType::UP:
        case ButtonType::DOWN:
            _confirmSel = _confirmSel ? 0 : 1; // toggle
            drawConfirmDelete();
            return true;

        case ButtonType::SELECT:
            if (_confirmSel == 1)
                doDelete(); // Yes -> remove + back to the reloaded list
            else
            {
                _view = View::FileInfo; // No
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

    Ui::begin(*d);
    d->clearDisplay();

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
    snprintf(line, sizeof(line), "Size: %s", sdCardModule.formatSize(_selSize));
    d->setCursor(0, 13);
    d->print(line);

    if (_selCtime != 0)
    {
        struct tm tmv;
        gmtime_r(&_selCtime, &tmv);
        snprintf(line, sizeof(line), "Date: %02d.%02d.%02d %02d:%02d",
                 tmv.tm_mday, tmv.tm_mon + 1, (tmv.tm_year + 1900) % 100, tmv.tm_hour, tmv.tm_min);
    }
    else
        snprintf(line, sizeof(line), "Date: --");
    d->setCursor(0, 23);
    d->print(line);

    // Action rows: Delete / Rename / Back (selected row inverted).
    static const char *const ACTIONS[3] = {"Delete", "Rename", "Back"};
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

    Ui::begin(*d);
    d->clearDisplay();

    d->setCursor(0, 0);
    d->print("Delete?");
    d->drawLine(0, 10, W, 10, WHITE);

    std::string name = _selName;
    const int cells = W / 6;
    if (cells > 3 && (int)name.size() > cells)
        name = name.substr(0, (size_t)cells - 3) + "...";
    d->setCursor(0, 16);
    d->print(name.c_str());

    // Two buttons [No] [Yes]; the selected one is filled, the other outlined.
    static const char *const OPTS[2] = {"No", "Yes"};
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

    Ui::begin(*d);
    d->clearDisplay();
    d->setCursor(2, 0);
    d->print("Rename:");

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
    d->print("OK=ok  <=back");
    _display->displayBuff();
}

#endif // DEVICE_DISPLAY_MODULE && OPENKNX_SDCARD
