#pragma once
/**
 * @file        SDCardModule.h
 * @brief       This module offers file statistics and filesystem information
 *              for SD cards, designed for seamless integration with OpenKNX.
 * @author      Erkan Colak
 * @version     0.0.1
 * @date        2024-03-25
 * @copyright   Copyright (c) 2024, Erkan Colak

 */

// #define SPI_DRIVER_SELECT 3
// #define SD_FAT_TYPE 3
#define DISABLE_FS_H_WARNING

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
    bool Statistics(const char *path, FileInfo &info);

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
    std::vector<String> ls(const char *path);

    inline const std::string name() { return SDCardModule_Display_Name; }
    inline const std::string version() { return SDCardModule_Display_Version; }
    void Mount();
    bool Unmount(bool force = false);

  private:
    void lowLevelFormat();
    void quickFormat();
    void readPartitionTable();
    void readGPT();
    const char* formatSize(uint64_t bytes);
    void getSDCardUsage(uint64_t &freeSpace, uint64_t &usedSpace);
    uint64_t getSDCardSize();

    time_t fatDateTimeToUnix(uint16_t fatDate, uint16_t fatTime);
    BootSectorInfo getBootSectorInfo(int fsType);
    // SPIClass SPI_SD(HSPI);
    uint32_t _cardDetectTimer = 0; // Timer for card detection
    uint32_t _cardMountTimer = 0; // Delay for card mount
    bool _cardInserted = false;    // Card inserted flag
    bool _mountTimerStarted = false;
    SdFs _sd;
    uint8_t _chipSelectPin;
    bool _mounted;
};

extern SDCardModule sdCardModule;