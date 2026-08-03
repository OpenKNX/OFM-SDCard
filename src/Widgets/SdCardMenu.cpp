// Backward-compat: flag renamed OPENKNX_SD_CARD_MODULE_ENABLE -> OPENKNX_SDCARD; old name still works.
#if defined(OPENKNX_SD_CARD_MODULE_ENABLE) && !defined(OPENKNX_SDCARD)
    #define OPENKNX_SDCARD
#endif
#ifdef OPENKNX_SDCARD
/**
 * @file        SdCardMenu.cpp
 * @brief       DeviceDisplay / menu integration for the SD-card module (settings
 *              sub-menu tree + action bindings), split out of SDCardModule.cpp.
 * @version     0.0.1
 * @date        2025-02-15
 * @copyright   Copyright (c) 2025, Erkan Çolak
 *              Licensed under GNU GPL v3.0
 **/
    #include "../SDCardModule.h"

// DeviceDisplay / menu integration for the SD-card module, split out of SDCardModule.cpp to keep
// the core module slim. SDCardModule member functions; only compiled under DEVICE_DISPLAY_MODULE.
    #ifdef DEVICE_DISPLAY_MODULE
/**
 * @brief Build the "SD-Karte" settings sub-menu tree.
 *
 * Item keys are unique so the MenuRegistry can dedup and the action registry can bind. The
 * read-only Info rows pull live values via valueProvider(), and stay "—" until a card is mounted.
 * @return The assembled root MenuOption representing the "SD-Karte" submenu.
 */
MenuConfig::MenuOption SDCardModule::buildSdMenu()
{
    using MenuOption = MenuConfig::MenuOption;
    using MenuBuild::addRow; // rows are built in the vector, not on the stack — see MenuConfig.h

    MenuOption sd;
    sd.label = "SD-Karte";
    sd.type = MenuConfig::Submenu;
    sd.key = "sd_root";
    // Sit after Home-Tasten (60), before the pinned-last "Ueber". Without this it defaults to 0
    // and would sort first in the root menu.
    sd.sortOrder = 65;

    auto& root = sd.submenu;
    root.reserve(9);

    addRow(root, [](MenuOption& files) {
        files.label = "Dateien";
        files.type = MenuConfig::Files;
        files.key = "sd_files";
    });

    // Each row's valueProvider() runs every frame, so none queries the card directly: they read
    // _sdInfoCache, (re)populated at most once per open so the expensive getSDCardUsage() (walks the
    // whole FAT) runs ≤1× per open, not per frame. Unmounted → cache invalid, rows show SD_INFO_HINT.
    addRow(root, [](MenuOption& info) {
        info.label = "Info";
        info.type = MenuConfig::Submenu;
        info.key = "sd_info";

        auto& sub = info.submenu;
        sub.reserve(7);

        addRow(sub, [](MenuOption& o) {
            o.label = "Typ";
            o.type = MenuConfig::Readonly;
            o.key = "sd_info_type";
            o.valueProvider = []() -> std::string {
                sdCardModule._refreshSdInfoCache();
                if (!sdCardModule._sdInfoCache.valid) return SD_INFO_HINT;
                return sdCardModule._sdInfoCache.type;
            };
        });
        addRow(sub, [](MenuOption& o) {
            o.label = "Kapazitaet";
            o.type = MenuConfig::Readonly;
            o.key = "sd_info_cap";
            o.valueProvider = []() -> std::string {
                sdCardModule._refreshSdInfoCache();
                if (!sdCardModule._sdInfoCache.valid) return SD_INFO_HINT;
                return sdCardModule._sdInfoCache.capacity;
            };
        });
        addRow(sub, [](MenuOption& o) {
            o.label = "Frei";
            o.type = MenuConfig::Readonly;
            o.key = "sd_info_free";
            o.valueProvider = []() -> std::string {
                sdCardModule._refreshSdInfoCache();
                if (!sdCardModule._sdInfoCache.valid) return SD_INFO_HINT;
                return sdCardModule._sdInfoCache.freeSpace;
            };
        });
        addRow(sub, [](MenuOption& o) {
            o.label = "Belegt";
            o.type = MenuConfig::Readonly;
            o.key = "sd_info_used";
            o.valueProvider = []() -> std::string {
                sdCardModule._refreshSdInfoCache();
                if (!sdCardModule._sdInfoCache.valid) return SD_INFO_HINT;
                return sdCardModule._sdInfoCache.usedSpace;
            };
        });
        addRow(sub, [](MenuOption& o) {
            o.label = "FS";
            o.type = MenuConfig::Readonly;
            o.key = "sd_info_fs";
            o.valueProvider = []() -> std::string {
                sdCardModule._refreshSdInfoCache();
                if (!sdCardModule._sdInfoCache.valid) return SD_INFO_HINT;
                return sdCardModule._sdInfoCache.fs;
            };
        });
        addRow(sub, [](MenuOption& o) {
            o.label = "Label";
            o.type = MenuConfig::Readonly;
            o.key = "sd_info_label";
            o.valueProvider = []() -> std::string {
                sdCardModule._refreshSdInfoCache();
                if (!sdCardModule._sdInfoCache.valid) return SD_INFO_HINT;
                // Empty volume label → dash so the row never renders blank.
                return sdCardModule._sdInfoCache.label.empty() ? std::string(SD_INFO_HINT)
                                                               : sdCardModule._sdInfoCache.label;
            };
        });
        addRow(sub, [](MenuOption& o) {
            o.label = "< Zurueck";
            o.type = MenuConfig::Back;
        });
    });

    // SD-Name: edit the volume label in place (char-scroll OSD editor). The item owns everything:
    // valueProvider reads the live label, onValueChanged relabels the card (charset-checked in the
    // API) and invalidates the info cache so the "Label" row refreshes.
    addRow(root, [](MenuOption& sdName) {
        sdName.label = "SD-Name";
        sdName.type = MenuConfig::TextInput;
        sdName.key = "sd_name";
        sdName.valueProvider = []() -> std::string { return std::string(sdCardModule.getVolumeLabel().c_str()); };
        sdName.onValueChanged = [](const MenuConfig::MenuOption&, const MenuValue& val) {
            sdCardModule.setVolumeLabel(val.getString().c_str());
            sdCardModule._sdInfoCache.valid = false; // force the Info "Label" row to re-read
        };
    });

    addRow(root, [](MenuOption& qformat) {
        qformat.label = "Quick-Format";
        qformat.type = MenuConfig::Toast;
        qformat.key = "sd_qformat";
        qformat.toast = "Quick-Format ...";
        qformat.confirmText = "MBR wird geloescht! Danach neu formatieren.";
    });

    addRow(root, [](MenuOption& format) {
        format.label = "Formatieren (exFAT)";
        format.type = MenuConfig::Toast;
        format.key = "sd_format";
        format.toast = "Formatieren ...";
        // exFAT is the only format that BLOCKS (~4s, single SdFat call) -> warn that the device is
        // briefly not ready. Quick/Low-Level run non-blocking in the background (no such hint needed).
        format.confirmText = "ALLE Daten weg! Geraet ca. 4s nicht bereit.";
    });

    addRow(root, [](MenuOption& llf) {
        llf.label = "Low-Level-Format";
        llf.type = MenuConfig::Toast;
        llf.key = "sd_llf";
        llf.toast = "Low-Level-Format ...";
        llf.confirmText = "Nullt die GANZE Karte! Dauert lange.";
    });

    // Partition-Info: a lazy submenu of read-only lines built on entry from the live MBR/GPT
    // table, so it renders ON the display (not just the console). Console stays via `sdc rpi`.
    addRow(root, [](MenuOption& rpi) {
        rpi.label = "Partition-Info (GPT/MBR)";
        rpi.type = MenuConfig::Submenu;
        rpi.key = "sd_rpi";
        rpi.submenuBuilder = []() -> std::vector<MenuConfig::MenuOption> {
            std::vector<std::string> lines;
            sdCardModule.readPartitionInfoLines(lines);
            std::vector<MenuConfig::MenuOption> items;
            items.reserve(lines.size() + 1);
            for (const std::string& l : lines)
                MenuBuild::addRow(items, [&l](MenuConfig::MenuOption& row) {
                    row.label = l;
                    row.type = MenuConfig::Readonly;
                });
            MenuBuild::addRow(items, [](MenuConfig::MenuOption& back) {
                back.label = "< Zurueck";
                back.type = MenuConfig::Back;
            });
            return items;
        };
    });

    addRow(root, [](MenuOption& eject) {
        eject.label = "Sicher auswerfen";
        eject.type = MenuConfig::Toast;
        eject.key = "sd_eject";
        eject.toast = "Ausgeworfen";
    });

    addRow(root, [](MenuOption& back) {
        back.label = "< Zurueck";
        back.type = MenuConfig::Back;
    });

    return sd;
}

/**
 * @brief Register the "SD-Karte" sub-menu + its action callbacks with the DeviceDisplay MenuRegistry.
 *
 * Everything routes through the presence-guarded tryRegisterRootItem/tryRegisterAction, which are a
 * logged no-op (never a null-deref) when the display module or its registry is absent — e.g. when
 * SDCardModule::setup() runs before DeviceDisplay::init(). The MenuRegistry keeps its own copy of the
 * item by value, so the local tree may go out of scope right after registering. Action callbacks
 * capture only the global sdCardModule facade, so they safely outlive this call.
 */
void SDCardModule::registerSdMenu()
{
    openknxDisplayModule.tryRegisterRootItem(buildSdMenu());

    // Destructive format actions — the menu gates these behind a Nein/Ja confirm (the items carry
    // confirmText), so the action fires only on Ja. requestFormat() is NON-blocking: it unmounts and
    // hands the work to the loop-driven state machine (Low-Level is spread over many ticks).
    openknxDisplayModule.tryRegisterAction("sd_qformat", []() { sdCardModule.requestFormat(0); });
    openknxDisplayModule.tryRegisterAction("sd_format", []() { sdCardModule.requestFormat(1); });
    openknxDisplayModule.tryRegisterAction("sd_llf", []() { sdCardModule.requestFormat(2); });

    // Thin wrappers → member handlers (so logInfoP resolves logPrefix() via this).
    // sd_rpi has no action: it is a submenu whose builder reads the table into read-only rows.
    openknxDisplayModule.tryRegisterAction("sd_info", []() { sdCardModule._menuActionSdInfo(); });
    openknxDisplayModule.tryRegisterAction("sd_eject", []() { sdCardModule._menuActionSafeEject(); });

    // "Dateien" — activate the WidgetFileBrowser. The sd_files item is type Files, so MenuWidget
    // fires _onFilesRequested first if wired; else it falls back to item.action bound here by key.
    // Binding via the action registry keeps browser activation owned by OFM-SDCard regardless of
    // which path the DeviceDisplay uses.
    openknxDisplayModule.tryRegisterAction("sd_files", []() { sdCardModule._menuActionFileBrowser(); });
}

/**
 * @brief "Info" action — guard card state, then force ONE cache refresh on open.
 * The read-only rows then draw from the cached snapshot (see _refreshSdInfoCache).
 */
void SDCardModule::_menuActionSdInfo()
{
    if (!isCardInserted())
    {
        logInfoP("SD-Info: no card inserted");
        return;
    }
    if (!isMounted())
    {
        logInfoP("SD-Info: card not mounted");
        return;
    }
    _refreshSdInfoCache(true); // one fresh (expensive) usage query on open
}

/**
 * @brief "Sicher auswerfen" action — SAFE eject via Unmount(false).
 * Unmount(false) honours the busy-guard: while the card is busy it refuses (returns false and
 * stays mounted); on success isMounted() becomes false. Never forces (no Unmount(true)).
 */
void SDCardModule::_menuActionSafeEject()
{
    if (!isCardInserted())
    {
        logInfoP("Eject: no card inserted");
        return;
    }
    if (!isMounted())
    {
        logInfoP("Eject: card already unmounted");
        return;
    }
    if (!Unmount(false)) // safe (non-forced): busy → refused, stays mounted
        logInfoP("Eject: card busy, try again");
    else
        logInfoP("Eject: card safely unmounted"); // isMounted()==false now
}

/**
 * @brief "Dateien" action — activate the reused WidgetFileBrowser at the SD root.
 *
 * Guarded by isMounted(): an unmounted (or absent) card has nothing to browse, so we log a hint and
 * no-op instead of opening an empty browser over a dead SdFat handle. When mounted, we make the
 * browser the active button widget by OR-ing the required flags into its action mask and calling
 * start(), which clears the path stack, resets the view and reloads — so it always opens at root "/".
 *
 * The browser hands control back by returning false from handleButtonEvent() on a LEFT press at the
 * SD root; _serviceFileBrowser() (polled from loop()) then observes it is no longer the active button
 * widget and hides it — see _hideFileBrowser().
 */
void SDCardModule::_menuActionFileBrowser()
{
    if (_fileBrowser == nullptr) // no WidgetsManager / browser never added — nothing to activate
    {
        logInfoP("Files: file browser unavailable");
        return;
    }
    if (!isMounted())
    {
        logInfoP("Files: card not mounted");
        return;
    }

    // Make the browser the active button widget: DisplayEnabled so the WidgetsManager shows it,
    // Background so it lives in the background cache, WantsButtonInput so it receives navigation,
    // ManagedExternally so the manager leaves start()/stop() to us. addAction() OR-merges these
    // (it already carries Background|WantsButtonInput|ManagedExternally from setup()).
    _fileBrowser->addAction(static_cast<uint8_t>(
        WidgetFlags::DisplayEnabled | WidgetFlags::Background |
        WidgetFlags::WantsButtonInput | WidgetFlags::ManagedExternally));

    // The menu MUST release the screen here. findActiveBackgroundWidget() returns the first
    // background widget carrying DisplayEnabled|ManagedExternally, and the menu sits ahead of the
    // browser in that list — so while both are enabled the browser is drawn but the MENU receives
    // every button. That also stalls _serviceFileBrowser(), whose activation latch waits for the
    // browser to become the active button widget, which then never happens.
    openknxDisplayModule.setMenuDisplayEnabled(false);

    _fileBrowser->start(); // resets to root "/" + reloads the listing (WidgetFileBrowser::start())
    _fileBrowserOpen = true;
    _fileBrowserActivated = false; // not yet observed in front; latched by _serviceFileBrowser()
    logInfoP("Files: opening file browser at root");
}

/**
 * @brief Loop-polled close-on-handback watchdog for the file browser.
 *
 * While open, the browser is the WidgetsManager's active button widget. On a LEFT press at the SD
 * root it hands control back and the MenuWidget re-shows the menu, which then wins
 * getActiveButtonWidget(). We detect that transition — the active button widget is no longer our
 * browser — and hide the browser so it does not linger with DisplayEnabled set. Presence-guarded so
 * it is a safe no-op when the WidgetsManager is absent.
 */
void SDCardModule::_serviceFileBrowser()
{
    if (!_fileBrowserOpen || _fileBrowser == nullptr)
        return;

    WidgetsManager *wm = openknxDisplayModule.getWidgetManager();
    if (wm == nullptr) // display module went away — treat as closed and drop our flags
    {
        _hideFileBrowser();
        return;
    }

    const bool isActive = (wm->getActiveButtonWidget() == static_cast<Widget *>(_fileBrowser));

    // The WidgetsManager only promotes our just-activated browser to the active button widget on
    // its NEXT state-machine pass, which may run after this loop(). Latch _fileBrowserActivated on
    // the first frame we actually observe the browser in front, and only treat a later loss of that
    // role as the LEFT-at-root hand-back. Without this latch the first poll (before the manager has
    // switched) would misread the still-active menu and close the browser immediately.
    if (isActive)
    {
        _fileBrowserActivated = true;
        return; // in the foreground → keep it open
    }
    if (_fileBrowserActivated) // was in front, now isn't → browser handed control back → hide it
        _hideFileBrowser();
}

/**
 * @brief Hide the file browser — drop DisplayEnabled and stop it.
 *
 * Clears the flags that make the browser the active/visible widget (DisplayEnabled so the
 * WidgetsManager stops showing it; the ManagedExternally "loses DisplayEnabled" path then clears
 * _currentWidget) and stops the widget so it releases the display buffer. Idempotent: a no-op when
 * the browser is not open.
 */
void SDCardModule::_hideFileBrowser()
{
    _fileBrowserOpen = false;
    _fileBrowserActivated = false;
    if (_fileBrowser == nullptr)
        return;
    // Remove DisplayEnabled so the manager no longer treats the browser as the active view. We
    // leave Background|WantsButtonInput|ManagedExternally in place (as registered in setup()) so a
    // later reactivation only needs to re-add DisplayEnabled + start().
    _fileBrowser->removeAction(static_cast<uint8_t>(WidgetFlags::DisplayEnabled));
    _fileBrowser->stop();

    // Give the screen back to the menu (released in _menuActionFileBrowser()); it still holds its
    // tree and cursor, so it reappears on the SD submenu where the user left it.
    openknxDisplayModule.setMenuDisplayEnabled(true);

    logInfoP("Files: file browser closed");
}

/**
 * @brief (Re)populate the cached SD-Info snapshot from the live adapter.
 *
 * The Info submenu's read-only rows are redrawn every frame, but getSDCardUsage() is
 * expensive (freeClusterCount() walks the whole FAT). To keep it to ≤1 query per open,
 * this refreshes only when necessary:
 *   - not mounted            → mark cache invalid (rows show SD_INFO_HINT) and return;
 *   - stale mount generation → force a refresh (card changed since last populate);
 *   - already valid & recent → no-op (skips the expensive usage query per frame).
 * @param force  bypass the recency check and requery now (used on submenu open).
 */
void SDCardModule::_refreshSdInfoCache(bool force)
{
    if (!isMounted())
    {
        _sdInfoCache.valid = false; // hint state — every Info row falls back to SD_INFO_HINT
        return;
    }

    const uint32_t now = millis();
    const bool generationChanged = (_sdInfoCache.mountGeneration != _sdInfoGeneration);

    // Skip the expensive requery if we already have a valid snapshot for this mount that was
    // refreshed recently and no explicit refresh was requested (typical per-frame redraw).
    if (!force && !generationChanged && _sdInfoCache.valid &&
        (uint32_t)(now - _sdInfoCache.refreshedAt) < SD_INFO_CACHE_MIN_INTERVAL_MS)
        return;

    // formatSize() returns a pointer into a shared static buffer, so each result MUST be
    // copied into its own std::string before the next call overwrites it.
    _sdInfoCache.type = std::string(getCardType(true).c_str());
    _sdInfoCache.fs = std::string(getFsType().c_str());
    _sdInfoCache.label = std::string(getVolumeLabel().c_str());

    const uint64_t total = getSDCardSize();
    _sdInfoCache.capacity = (total == 0) ? std::string(SD_INFO_HINT) : std::string(formatSize(total));

    uint64_t freeBytes = 0, usedBytes = 0;
    if (getSDCardUsage(freeBytes, usedBytes)) // the single expensive query, ≤1× per open
    {
        _sdInfoCache.freeSpace = std::string(formatSize(freeBytes));
        _sdInfoCache.usedSpace = std::string(formatSize(usedBytes));
    }
    else
    {
        _sdInfoCache.freeSpace = SD_INFO_HINT;
        _sdInfoCache.usedSpace = SD_INFO_HINT;
    }

    _sdInfoCache.mountGeneration = _sdInfoGeneration;
    _sdInfoCache.refreshedAt = now;
    _sdInfoCache.valid = true;
}

    #endif

#endif // OPENKNX_SDCARD
