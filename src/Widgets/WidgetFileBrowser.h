#if defined(DEVICE_DISPLAY_MODULE) && defined(OPENKNX_SD_CARD_MODULE_ENABLE)

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
    void drawBrowser();

    void _updateWindow();

    static constexpr size_t VISIBLE_ROWS = 5;

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

    // Reset selection + scroll window to the top of a freshly entered directory.
    inline void _resetView()
    {
        _selectedIndex = 0;
        _windowStart = 0;
        _needsReload = true;
    }
};

#endif // DEVICE_DISPLAY_MODULE && OPENKNX_SD_CARD_MODULE_ENABLE
