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
        // Full documentation for the SdFat library configuration can be found at:
        // https://github.com/greiman/SdFat/blob/master/src/SdFatConfig.h
        #include <SdFat.h>
    #elif
        #error "Unsupported architecture"
    #endif

    #define SDFAT_ SdFat

    #ifdef ARDUINO_ARCH_RP2040 // Only FAT16 and FAT32 are supported on RP2040. exFAT is not due to PSRAM limitations
        #if defined(ARDUINO_PICO_VERSION_STR) && defined(ARDUINO_PICO_MAJOR) && \
            defined(ARDUINO_PICO_MINOR) && \
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
    MOUNT_STEP_CARD_CHANGED, // 0: Card changed
    MOUNT_STEP_CARD_INSERTED, // 1: Card inserted
    MOUNT_STEP_CARD_REMOVED, // 2: Card removed
    MOUNT_STEP_INIT,         // 0: Inialize SPI
    MOUNT_STEP_DETECT,       // 1: Get the card object
    MOUNT_STEP_CARD_BEGIN,   // 2: Start the card
    MOUNT_STEP_VOLUME_BEGIN, // 3: Initialize the volume
    MOUNT_STEP_MOUNT,        // 4: Mount the volume
    MOUNT_STEP_UNMOUNT,      // 5: Unmount the volume
    //MOUNT_STEP_FORMAT,       // 6: Format the volume
    MOUNT_STEP_ERROR,        // 4: Error during mount
    MOUNT_STATE_ERROR,      // 6: Error state
    MOUNT_STATE_MOUNTED,     // 3: Mounted
    MOUNT_STATE_UNMOUNTED,    // 7: Unmounted state
    MOUNT_STATE_CARD_REMOVED
};

class SDCardModule : public OpenKNX::Module
{
  public:
    // SPIClass SPI1(HSPI);
    // SoftSpiDriver<PIN_SDCARD_MISO, PIN_SDCARD_MOSI, PIN_SDCARD_SCK> softSpi;
    // #define SD_CONFIG SdSpiConfig(PIN_SDCARD_CS, SDCARD_SPI_INTERFACE, SD_SCK_MHZ(0), &SPI1)
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

    inline const std::string name() { return SDCardModule_Display_Name; }
    inline const std::string version() { return SDCardModule_Display_Version; }
    bool Unmount(bool force = false);
    bool Mount();
    void ReMount();
    inline bool isMounted() { return _mountStep == MOUNT_STATE_MOUNTED; }
    inline bool isUnmounted() { return _mountStep == MOUNT_STATE_UNMOUNTED; }
    inline bool isCardInserted() { return digitalRead(PIN_SDCARD_CD) == LOW; } // Card inserted
    inline bool isCardRemoved() { return !isCardInserted(); } // Card removed

    uint64_t getSDCardSize();
    String getCardType();
    String getFsType();
    String getManufacturer(cid_t cid = cid_t());
    String getVolumeLabel();
    String getPartitionType(uint8_t partitionType);

  private:
    void _mount(); // No direct call, only for internal use
    void lowLevelFormat();
    void quickFormat();
    void readPartitionInfo();
    const char *formatSize(uint64_t bytes);
    void getSDCardUsage(uint64_t &freeSpace, uint64_t &usedSpace);

    time_t fatDateTimeToUnix(uint16_t fatDate, uint16_t fatTime);
    BootSectorInfo getBootSectorInfo(int fsType);

    MountStep _mountStep = MOUNT_STEP_INIT; // Default mount step - Initialize SPI
    uint32_t _cardDetectTimer = 0;          // Timer for card detection
    uint32_t _cardMountTimer = 0;           // Delay for card mount
    uint32_t _mountTimer = 0;               // Delay for card mount
    bool _cardChanged = false;             // Card inserted flag
    bool _mountTimerStarted = false;

    SDFAT_ _sd;
    uint8_t _chipSelectPin;
};

extern SDCardModule sdCardModule;

#endif // OPENKNX_SD_CARD_MODULE_ENABLE