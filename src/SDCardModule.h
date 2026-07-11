#ifdef OPENKNX_SD_CARD_MODULE_ENABLE
    #pragma once
/**
 * @file        SDCardModule.h
 * @brief       This module offers file statistics and filesystem information
 *              for SD cards, designed for seamless integration with OpenKNX.
 *              Supports FAT16, FAT32, and exFAT filesystems.
 *              Supported platforms:
 *              - RP2040/RP2350: Only FAT16 and FAT32 are supported on RP2040. exFAT is not due to PSRAM limitations.
 *              - ESP32: ESP32 supports FAT16, FAT32, and exFAT.
 * @author      Erkan Çolak
 * @version     0.0.1
 * @date        2024-03-25
 * @copyright   Copyright (c) 2025, Érkan Çolak
 *
 */

    #include "OpenKNX.h"
    #include <SPI.h>
    #ifdef ARDUINO_ARCH_RP2040
        #include <SDFS.h>
    #elif (ARDUINO_ARCH_ESP32)
        #define DISABLE_FS_H_WARNING
        #define MAINTAIN_FREE_CLUSTER_COUNT 1
        // Full documentation for the SdFat library configuration can be found at:
        // https://github.com/greiman/SdFat/blob/master/src/SdFatConfig.h
        #include <SdFat.h>
    #elif
        #error "Unsupported architecture"
    #endif

    #ifdef DEVICE_DISPLAY_MODULE
        #include "Menu/MenuConfig.h"
        #include "Widgets/WidgetSDCard.h"
class WidgetFileBrowser;
    #endif // DEVICE_DISPLAY_MODULE

    #define SDFAT_ SdFat

    #ifdef ARDUINO_ARCH_RP2040 // Only FAT16 and FAT32 are supported on RP2040. exFAT is not due to PSRAM limitations
        #if defined(ARDUINO_PICO_VERSION_STR) && defined(ARDUINO_PICO_MAJOR) && \
            defined(ARDUINO_PICO_MINOR) &&                                      \
            defined(ARDUINO_PICO_REVISION)
            #if (ARDUINO_PICO_MAJOR < 4) || (ARDUINO_PICO_MAJOR == 4 && ARDUINO_PICO_MINOR < 4) || \
                (ARDUINO_PICO_MAJOR == 4 && ARDUINO_PICO_MINOR == 4 && ARDUINO_PICO_REVISION < 2)
                #define FSFILE File32
                #define FSVOlUME FatVolume
                #define FS_SUPPORT_FORMATS "FAT16, FAT32"
            #else // RP2040 core 4.4.2 and later supports exFAT
                #define FSFILE FsFile
                #define FSVOlUME FsVolume
                #define FS_SUPPORT_FORMATS "FAT16, FAT32, exFAT"
            #endif
        #else
            #pragme message("RP2040 version not defined. Assuming version 4.4.2 or later.")
            #define FSFILE FsFile
            #define FSVOlUME FsVolume
            #define FS_SUPPORT_FORMATS "FAT16, FAT32, exFAT"
        #endif
    #elif defined(ARDUINO_ARCH_ESP32) // ESP32 supports FAT16, FAT32, and exFAT.
        #define FSFILE FsFile
        #define FSVOlUME FsVolume
        #define FS_SUPPORT_FORMATS "FAT16, FAT32, exFAT"
    #endif

    #define SDCardModule_Display_Name "SDCardModule"
    #define SDCardModule_Display_Version "0.0.1"

struct FileInfo
{
    size_t size;
    size_t blocksize;
    time_t ctime;
    time_t atime;
    bool isDir;
};

struct SdDirEntry
{
    String name;
    bool isDir = false;
    uint64_t size = 0;
};

struct CardInfo
{
    String manufacturer;
    String productName;
    String revision;
    String serialNumber;
    String manufactureDate;
    String oemApplicationID;
    bool isValid = false;
};

struct BootSectorInfo
{
    uint16_t bytesPerSector;
    uint8_t sectorsPerCluster;
    uint16_t reservedSectors;
    uint8_t numberOfFATs;
    uint32_t totalSectors;
    uint32_t fatSize;
    uint32_t rootDirCluster;
    std::string fileSystemType;
    std::string volumeLabel;
    bool isValid = false;
};

enum MountStep
{
    MOUNT_STEP_CARD_CHANGED,  // 0: Card changed
    MOUNT_STEP_CARD_INSERTED, // 1: Card inserted
    MOUNT_STEP_CARD_REMOVED,  // 2: Card removed
    MOUNT_STEP_INIT,          // 3: Initialize SPI
    MOUNT_STEP_DETECT,        // 4: Detect the card
    MOUNT_STEP_CARD_BEGIN,    // 5: Initialize the card
    MOUNT_STEP_VOLUME_BEGIN,  // 6: Initialize the volume
    MOUNT_STEP_MOUNT,         // 7: Mount the volume
    MOUNT_STEP_UNMOUNT,       // 8: Unmount the volume
    // MOUNT_STEP_FORMAT,     // 9: Format the volume
    MOUNT_STEP_ERROR,        // 10: Error state
    MOUNT_STATE_ERROR,       // 11: Error state
    MOUNT_STATE_MOUNTED,     // 12: Mounted state
    MOUNT_STATE_UNMOUNTED,   // 13: Unmounted state
    MOUNT_STATE_CARD_REMOVED // 14: Card removed state
};

class SDCardModule : public OpenKNX::Module
{
  public:
    void init();
    void setup(bool configured) override;
    void loop(bool configured) override;
    // void processInputKo(GroupObject &ko) override;
    void showHelp() override;
    bool processCommand(const std::string command, bool diagnose) override;

    SDCardModule(uint8_t csPin);
    ~SDCardModule();

    bool format();
    bool info();
    bool Statistics(const char *folder, const char *path, FileInfo &info);

    FSFILE open(const char *path, const char *mode);
    bool createFile(const char *path);
    bool remove(const char *path);
    bool exists(const char *path);
    size_t read(const char *path, uint8_t *buffer, size_t size);
    size_t write(const char *path, const uint8_t *buffer, size_t size);
    bool rename(const char *oldPath, const char *newPath);
    size_t append(const char *path, const uint8_t *buffer, size_t size);

    bool mkdir(const char *path);
    bool rmdir(const char *path);
    std::vector<String> getFileList(const char *path);
    size_t listDir(const char *path, std::vector<SdDirEntry> &out, size_t maxEntries = 0);

    inline const std::string name() { return SDCardModule_Display_Name; }
    inline const std::string version() { return SDCardModule_Display_Version; }
    bool Unmount(bool force = false);
    bool Mount();
    void ReMount();
    inline bool isMounted() { return _mountStep == MOUNT_STATE_MOUNTED; }
    inline bool isMounting() { return _inMountingProcess(); }
    inline bool isUnmounting() { return _inUnmountingProcess(); }
    inline bool isError() { return _mountStep == MOUNT_STATE_ERROR; }
    inline bool isUnmounted() { return _mountStep == MOUNT_STATE_UNMOUNTED; }
    inline bool isCardInserted() { return digitalRead(PIN_SDCARD_CD) == LOW; } // Card inserted
    inline bool isCardRemoved() { return !isCardInserted(); }                  // Card removed

    uint64_t getSDCardSize();
    bool getSDCardUsage(uint64_t &freeSpace, uint64_t &usedSpace);

    void beginUsageScan();
    bool tickUsageScan();
    bool getCachedUsage(uint64_t &freeSpace, uint64_t &usedSpace, uint64_t &totalSpace) const;
    inline bool isUsageScanRunning() const { return _usState == UsageScanState::ScanFat || _usState == UsageScanState::ScanBitmap; }

    bool readCardInfo(CardInfo &cardInfo);
    inline void resetCardInfo() { _cardInfo.isValid = false; }
    inline CardInfo getCardInfo() { return _cardInfo; }
    String getCardType(bool shortType = false);
    String getFsType();
    String getVolumeLabel();
    String getPartitionType(uint8_t partitionType);
    const char *formatSize(uint64_t bytes);

  private:
    void _mount(); // No direct call, only for internal use
    bool _inMountingProcess();
    bool _inUnmountingProcess();
    void lowLevelFormat();
    void quickFormat();
    void readPartitionInfo();

    time_t fatDateTimeToUnix(uint16_t fatDate, uint16_t fatTime);
    BootSectorInfo getBootSectorInfo(int fsType);

    #ifdef DEVICE_DISPLAY_MODULE
    MenuConfig::MenuOption buildSdMenu();
    void registerSdMenu();
    struct SdInfoCache
    {
        bool valid = false;
        uint32_t refreshedAt = 0;
        uint32_t mountGeneration = 0;
        std::string type;
        std::string fs;
        std::string label;
        std::string capacity;
        std::string freeSpace;
        std::string usedSpace;
    };
    void _refreshSdInfoCache(bool force = false);
    static constexpr const char *SD_INFO_HINT = "-";
    static constexpr uint32_t SD_INFO_CACHE_MIN_INTERVAL_MS = 2000;
    SdInfoCache _sdInfoCache;

    enum class SdFormatOp
    {
        None,
        Quick,
        ExFat,
        LowLevel
    };
    void _requestSdFormatConfirm(SdFormatOp op);

    void _menuActionSdInfo();
    void _menuActionPartitionInfo();
    void _menuActionSafeEject();
    void _menuActionFileBrowser();
    void _serviceFileBrowser();
    void _hideFileBrowser();

    WidgetFileBrowser *_fileBrowser = nullptr;
    bool _fileBrowserOpen = false;
    bool _fileBrowserActivated = false;
    #endif // DEVICE_DISPLAY_MODULE

    MountStep _mountStep = MOUNT_STEP_INIT; // Default mount step - Initialize SPI
    uint32_t _cardDetectTimer = 0;          // Timer for card detection
    uint32_t _cardMountTimer = 0;           // Delay for card mount
    uint32_t _mountTimer = 0;               // Delay for card mount
    bool _cardChanged = false;              // Card inserted flag
    bool _mountTimerStarted = false;

    CardInfo _cardInfo = {"", "", "", "", "", "", false};
    SDFAT_ _sd;
    uint8_t _chipSelectPin;
    uint32_t _sdInfoGeneration = 0;

    enum class UsageScanState : uint8_t
    {
        Idle, // Scan not started
        ScanFat, // Scan the FAT to count free clusters
        ScanBitmap,  // Scan the bitmap to count free clusters (exFAT only)
        Done // Scan completed, results are cached
    };
    UsageScanState _usState = UsageScanState::Idle;
    uint32_t _usSector = 0;
    uint32_t _usEndSector = 0;
    uint32_t _usCluster = 0;
    uint32_t _usTotalClusters = 0;
    uint32_t _usFreeClusters = 0;
    uint32_t _usClusterSizeBytes = 0;
    uint8_t _usEntryWidth = 0;
    uint64_t _usResFree = 0, _usResUsed = 0, _usResTotal = 0;
    bool _usResValid = false;
    uint8_t _usSectorBuf[512];
    uint32_t _usScanStartMs = 0;
    uint32_t _usWorstTickUs = 0;
};

extern SDCardModule sdCardModule;

    #ifdef DEVICE_DISPLAY_MODULE
        #include "Widgets/WidgetFileBrowser.h"
    #endif

#endif // OPENKNX_SD_CARD_MODULE_ENABLE