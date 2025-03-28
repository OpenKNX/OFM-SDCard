#pragma once
/**
 * @file        SDCardModule.h
 * @brief       This module offers file statistics and filesystem information
 *              for SD cards, designed for seamless integration with OpenKNX.
 *              Based on the SdFat library.
 * @author      Erkan Çolak
 * @version     0.0.1
 * @date        2024-03-25
 * @copyright   Copyright (c) 2025, Érkan Çolak

 */

// Full documentation for the SdFat library configuration can be found at:
// https://github.com/greiman/SdFat/blob/master/src/SdFatConfig.h
//
// #define SPI_DRIVER_SELECT 3
// #define SD_FAT_TYPE 3
#define DISABLE_FS_H_WARNING
#define USE_LONG_FILE_NAMES 1
#define USE_UTF8_LONG_NAMES 1

#include "OpenKNX.h"
#include <SD.h>
#include <SPI.h>
#include <SdFat.h>

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
    MOUNT_STEP_INIT,         // 0: Inialize SPI
    MOUNT_STEP_DETECT,       // 1: Get the card object
    MOUNT_STEP_CARD_BEGIN,   // 2: Start the card
    MOUNT_STEP_VOLUME_BEGIN, // 3: Initialize the volume
    MOUNT_STEP_FINISHED,     // 3: Init done
    MOUNT_STEP_ERROR,        // 4: Error during mount
    MOUNT_ERROR_STATE        // 5: Error state

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

    bool isMounted();
    bool format();
    bool info();
    bool Statistics(const char *folder, const char *path, FileInfo &info);

    FsFile open(const char *path, const char *mode);
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

    uint64_t getSDCardSize();
    String getCardType();
    String getFsType();
    String getManufacturer(cid_t cid=cid_t());
    String getVolumeLabel();
    bool isCardInserted();

  private:
    bool _mount(); // No direct call, only for internal use
    void lowLevelFormat();
    void quickFormat();
    void readPartitionTable();
    void readGPT();
    const char *formatSize(uint64_t bytes);
    void getSDCardUsage(uint64_t &freeSpace, uint64_t &usedSpace);

    time_t fatDateTimeToUnix(uint16_t fatDate, uint16_t fatTime);
    BootSectorInfo getBootSectorInfo(int fsType);

    MountStep _mountStep = MOUNT_STEP_INIT; // Default mount step - Initialize SPI
    uint32_t _cardDetectTimer = 0;          // Timer for card detection
    uint32_t _cardMountTimer = 0;           // Delay for card mount
    uint32_t _mountTimer = 0;               // Delay for card mount
    bool _cardInserted = false;             // Card inserted flag
    bool _mountTimerStarted = false;

    SdFs _sd;
    uint8_t _chipSelectPin;
    bool _mounted;
    // SPIClass SPI_SD(HSPI);
};

extern SDCardModule sdCardModule;