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

#include "OpenKNX.h"
#include <SPI.h>
#include <sdfat.h>
#include <SD.h>

#define SDCardModule_Display_Name "SDCardModule"
#define SDCardModule_Display_Version "0.0.1"

class SDCardModule : public OpenKNX::Module {
public:

    
    //SPIClass SPI1(HSPI);
    //SoftSpiDriver<PIN_SDCARD_MISO, PIN_SDCARD_MOSI, PIN_SDCARD_SCK> softSpi;
   // #define SD_CONFIG SdSpiConfig(PIN_SDCARD_CS, SDCARD_SPI_INTERFACE, SD_SCK_MHZ(0), &SPI1)
    void init();
    void setup(bool configured) override;
    void loop(bool configured) override;
    //void processInputKo(GroupObject &ko) override;
    void showHelp() override;
    bool processCommand(const std::string command, bool diagnose) override;

    SDCardModule(uint8_t csPin);
    ~SDCardModule();

    bool isMounted();
    bool format();
    bool info();
    bool Statistics(const String path);

    FsFile open(const char *path, const char *mode);
    bool createFile(const char *path);
    bool remove(const char *path);
    bool exists(const char *path);
    size_t read(const char *path, uint8_t *buffer, size_t size);
    size_t write(const char *path, const uint8_t *buffer, size_t size);
    bool rename(const char *oldPath, const char *newPath);

    bool mkdir(const char *path);
    bool rmdir(const char *path);
    std::vector<String> ls(const char *path);

    inline const std::string name() { return SDCardModule_Display_Name; }
    inline const std::string version() { return SDCardModule_Display_Version; }
private:
   
    SdFs sd;
    //Fsfile file;
    uint8_t chipSelectPin;
    bool mounted;
};

extern SDCardModule sdCardModule;