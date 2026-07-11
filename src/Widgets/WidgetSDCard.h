#ifdef DEVICE_DISPLAY_MODULE

    #pragma once
    #include "SDCardModule.h"
    #include "Widget.h"
    #ifdef DEVICE_DISPLAY_MODULE
        #include "DeviceDisplay.h"
    #endif

class WidgetSDCard : public Widget
{
  public:
    const std::string logPrefix() { return "Widget SD-Card"; }
    WidgetSDCard(uint32_t displayTime, WidgetFlags action);

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

  private:
    WidgetState _state;
    uint32_t _displayTime;
    WidgetFlags _action;
    i2cDisplay *_display;
    std::string _name = "SD-Card";
    uint32_t _duration_timerStart = 0;

    // Free/used come from SDCardModule's NON-BLOCKING incremental scan. This widget only (re)triggers
    // a scan (beginUsageScan) on mount and every SDINFO_USAGE_REFRESH_MS, and reads the last finished
    // result (getCachedUsage). Triggering costs nothing (the scan spreads over loop()), so refreshing
    // often no longer causes a loop spike.
    static constexpr uint32_t SDINFO_USAGE_REFRESH_MS = 30000;
    uint32_t _lastUsageQuery = 0; // last time a scan was (re)triggered; 0 = trigger on next draw (mount)
    bool _cachedUsageValid = false;
    uint64_t _cachedTotal = 0, _cachedUsed = 0, _cachedFree = 0;

    void drawSDInfo();
};

#endif // DEVICE_DISPLAY_MODULE