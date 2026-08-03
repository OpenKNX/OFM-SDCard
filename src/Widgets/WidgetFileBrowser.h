// Backward-compat: flag renamed OPENKNX_SD_CARD_MODULE_ENABLE -> OPENKNX_SDCARD; old name still works.
#if defined(OPENKNX_SD_CARD_MODULE_ENABLE) && !defined(OPENKNX_SDCARD)
    #define OPENKNX_SDCARD
#endif
#if defined(DEVICE_DISPLAY_MODULE) && defined(OPENKNX_SDCARD)

#pragma once
/**
 * @file        WidgetFileBrowser.h
 * @brief       Button-driven SD-card file browser widget for the OpenKNX DeviceDisplay
 *              menu system. Provides the internal navigation state (path stack,
 *              selection, scroll window, entry cache) and the directory navigation API.
 * @details     The widget is externally managed: it stays active while the browser is
 *              open and receives button input. It reads directory contents through the
 *              SDCardModule::listDir() / SdDirEntry API.
 * @version     0.0.1
 * @date        2025-02-15
 * @copyright   Copyright (c) 2025, Erkan Çolak
 *              Licensed under GNU GPL v3.0
 **/

#include "Widget.h"
#include "SDCardModule.h"
#ifdef DEVICE_DISPLAY_MODULE
#include "DeviceDisplay.h"
#endif

#include <ctime>
#include <string>
#include <vector>

// Access the singleton SD-card module (declared in SDCardModule.h).
extern SDCardModule sdCardModule;

class WidgetFileBrowser : public Widget
{
  public:
    const std::string logPrefix() { return "Widget FileBrowser"; }
    WidgetFileBrowser(uint32_t displayTime, WidgetFlags action);

    // Core Widget interface
    void start() override;
    void stop() override;
    void pause() override;
    void resume() override;
    void setup() override;
    void loop() override;

    inline const WidgetState getState() const override { return _state; }
    inline const std::string getName() const override { return _name; }
    inline void setName(const std::string &name) override { _name = name; }

    uint32_t getDisplayTime() const override;
    WidgetFlags getAction() const override;

    inline void setDisplayTime(uint32_t displayTime) override { _displayTime = displayTime; }
    inline void setAction(uint8_t action) override { _action = static_cast<WidgetFlags>(action); }
    inline void addAction(uint8_t action) override { _action = static_cast<WidgetFlags>(_action | action); }
    inline void removeAction(uint8_t action) override { _action = static_cast<WidgetFlags>(_action & ~action); }

    void setDisplayModule(i2cDisplay *displayModule) override;
    i2cDisplay *getDisplayModule() const override;

    std::string currentPath() const; // Absolute path of the directory currently shown
    void enterDir(const String &name); // Descend into sub-directory 'name' (relative to current)
    bool goUp();                        // Ascend to the parent directory; false if already at root
    void reload();                      // (Re)populate the entry cache for the current path

    // Returns false only when LEFT is pressed at the SD root, handing control back to the
    // caller (menu) so it can close the browser.
    bool handleButtonEvent(const ButtonEvent &event) override;

  private:
    // Which sub-screen owns the input + display: the list, or a per-file overlay.
    enum class View : uint8_t
    {
        List,          // directory listing (navigation)
        FileInfo,      // Name/Size/Erstellt + [Loeschen] [Umbenennen] [Zurueck]
        ConfirmDelete, // [Nein]/[Ja] guard before remove()
        Rename         // char-scroll editor over the file name
    };

    void drawBrowser();
    void _updateWindow();

    // Per-view button handlers (dispatched from handleButtonEvent by _view).
    bool handleListButton(const ButtonEvent &event);
    bool handleFileInfoButton(const ButtonEvent &event);
    bool handleConfirmDeleteButton(const ButtonEvent &event);
    bool handleRenameButton(const ButtonEvent &event);

    // Per-view renderers.
    void drawFileInfo();
    void drawConfirmDelete();
    void drawRename();

    void openFileInfo(const SdDirEntry &e);                // capture file context + enter FileInfo
    void doDelete();                                       // remove _selName, back to the (reloaded) list
    void commitRename();                                   // rename _selName -> _rename, back to the list
    std::string fullPathOf(const std::string &name) const; // "/name" at root, "/dir/name" else

    static constexpr size_t VISIBLE_ROWS = 5;
    static constexpr uint8_t NAME_EDIT_MAX = 32; // char-scroll rename cap
    static constexpr uint32_t BLINK_MS = 500;    // rename cursor blink

  private:
    WidgetState _state;
    uint32_t _displayTime;
    WidgetFlags _action;
    i2cDisplay *_display;
    std::string _name = "File-Browser";

    std::vector<String> pathStack;      // Directory segments from root (empty = root "/")
    std::vector<SdDirEntry> _entries;   // Cached listing of the current directory
    size_t _selectedIndex = 0;          // Index of the highlighted entry within _entries
    size_t _windowStart = 0;            // First visible entry index (scroll window top)
    bool _needsReload = true;           // Entry cache is stale; reload() required before use

    // Per-file overlay state (FileInfo / ConfirmDelete / Rename).
    View _view = View::List;
    std::string _selName;    // captured file name for the active overlay
    uint64_t _selSize = 0;   // captured size (bytes)
    time_t _selCtime = 0;    // captured creation time (0 = unknown)
    uint8_t _infoSel = 0;    // FileInfo action cursor: 0=Loeschen 1=Umbenennen 2=Zurueck
    uint8_t _confirmSel = 0; // ConfirmDelete cursor: 0=Nein 1=Ja (default Nein)
    std::string _rename;     // working copy for the rename editor
    uint8_t _renameCursor = 0;
    bool _blinkOn = true;
    uint32_t _blinkLast = 0;

    // Reset selection + scroll window to the top of a freshly entered directory.
    inline void _resetView()
    {
        _selectedIndex = 0;
        _windowStart = 0;
        _needsReload = true;
    }
};

#endif // DEVICE_DISPLAY_MODULE && OPENKNX_SDCARD
