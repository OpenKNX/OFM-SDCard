#include "SDCardModule.h"

SPIClass SPI_SD(HSPI);
SdSpiConfig sdConfig(PIN_SDCARD_CS, DEDICATED_SPI, SD_SCK_MHZ(25), &SPI_SD);

SDCardModule::SDCardModule(uint8_t csPin) : chipSelectPin(csPin), mounted(false), sd(SdFat()) {}

SDCardModule::~SDCardModule() {}

void SDCardModule::init()
{
    // Serial.println("[SDCardModule] Initializing...");
    logInfoP("Initializing...");
    _cardDetectTimer = millis();
    // ToDo: set pinmode for card detect, so we can check if card is inserted or removed
    pinMode(PIN_SDCARD_CD, INPUT);
}

void SDCardModule::setup(bool configured)
{
    logDebugP("Setting up...");
    mounted = false;
    // Read the card detect pin
    _cardInserted = digitalRead(PIN_SDCARD_CD) == LOW; // LOW means card is inserted. Since we are using internal pull-up resistor, we need to check for LOW
    if (!_cardInserted)
    {
        logInfoP("No SD card Inserted!");
        return;
    }
    else
    {
        SD_Mount();
    }
}

void SDCardModule::SD_Mount()
{
    // SdSpiConfig sdConfig(PIN_SDCARD_CS, DEDICATED_SPI, SD_SCK_MHZ(25), &SPI_SD);
    SPI_SD.begin(PIN_SDCARD_SCK, PIN_SDCARD_MISO, PIN_SDCARD_MOSI, PIN_SDCARD_CS);
    // if (!SD.begin(PIN_SDCARD_CS, SPI_SD, SPI_FULL_SPEED)) { // SD.h
    if (!sd.begin(sdConfig))
    {
        logInfoP("Unable to detect the inserted SD card!");
        logDebugP("Error code: %X (Data: %X)", sd.card()->errorCode(), sd.card()->errorData());
        logInfoP("Please check the Format of the SD card! It must be FAT16, FAT32 or exFAT! formated!");
        return;
    }
    else
    {
        logInfoP("Successfully mounted the SD card!");
        mounted = true;
        info();

// Debug: Create a file
// #define SD_TEST_WRITE
#ifdef SD_TEST_WRITE
        FsFile file;
        if (!file.open("SDcard.txt", O_RDWR | O_CREAT))
        {
            sd.errorHalt(F("open failed"));
        }
        else
        {
            file.println("Hello OpenKNX World!");
            file.close();
            logDebugP("File: SDcard.txt created! And content: 'Hello OpenKNX World!' written!");
            logDebugP("Use sdc cat /SDcard.txt to read the file content!");
            logDebugP("");
        }
#endif // SD_TEST_WRITE
    }
}

void SDCardModule::loop(bool configured)
{
    // Check every 500ms if the card is inserted or removed
    if (_cardDetectTimer > 0 && delayCheck(_cardDetectTimer, 500))
    {
        _cardDetectTimer = millis();
        bool currentCardInserted = digitalRead(PIN_SDCARD_CD) == LOW;
        if (currentCardInserted != _cardInserted)
        {
            _cardInserted = currentCardInserted;
            if (_cardInserted)
            {
                logInfoP("SD card inserted. Starting initialization...");
                // Wait for 1 second before initializing the SD card
                delay(1000);
                SD_Mount();
            }
            else
            {
                logInfoP("SD card removed.");
                // prevent the SD card from being initialized again.
                // ToDo: Unmount the SD card (cancel read/write operations)
                // SD_Unmount();
                mounted = false;
            }
        }
    }

    // if (mounted) {
    //     Serial.println("SD-Karte ist eingesteckt!");
    // } else {
    //     Serial.println("SD-Karte ist nicht eingesteckt!");
    // }
    // if (sd.exists("SDcard.txt")) {
    //     Serial.println("Datei SDcard.txt existiert!");
    // } else {
    //     Serial.println("Datei SDcard.txt existiert nicht!");
    // }
    // Serial.println("Datei SDcard.txt wird gelesen:");
    // FsFile file = sd.open("SDcard.txt", O_READ);
    // if (!file) {
    //     Serial.println("Fehler beim Öffnen der Datei SDcard.txt!");
    // } else {
    //     while (file.available()) {
    //         Serial.write(file.read());
    //     }
    //     file.close();
    // }
    // Serial.println("Datei SDcard.txt wurde gelesen!");
}

void SDCardModule::showHelp()
{
    Serial.println("============================= Help: SD Card Module =============================");
    Serial.println("Command(s)               Description");
    Serial.println("sdc info                 Get information about the SD card");
    Serial.println("sdc format               Format the SD card");
    Serial.println("sdc ls /<path>           List files in a directory");
    Serial.println("sdc mkdir /<name>        Create a directory");
    Serial.println("sdc rmdir /<name>        Remove a directory");
    Serial.println("sdc rm /<file>           Remove a file");
    Serial.println("sdc cat /<file>          Read a file");
    Serial.println("sdc echo /<file> <text>  Append content to a file");
    Serial.println("================================================================================");
}

bool SDCardModule::processCommand(const std::string command, bool diagnose)
{
    if (diagnose) return false;
    if (command == "sdc info")
    {
        return info();
    }
    else if (command == "sdc format")
    {
        return format();
    }
    else if (command.compare(0, 7, "sdc ls ") == 0)
    {
        String path = command.substr(7).c_str();
        std::vector<String> files = ls(path.c_str());
        for (String file : files)
        {
            Serial.println(file);
        }
        return true;
    }
    else if (command.compare(0, 10, "sdc mkdir ") == 0)
    {
        String dirName = command.substr(10).c_str();
        return mkdir(dirName.c_str());
    }
    else if (command.compare(0, 10, "sdc rmdir ") == 0)
    {
        String dirName = command.substr(10).c_str();
        return rmdir(dirName.c_str());
    }
    else if (command.compare(0, 7, "sdc rm ") == 0)
    {
        String fileName = command.substr(7).c_str();
        return remove(fileName.c_str());
    }
    else if (command.compare(0, 8, "sdc cat ") == 0)
    {
        String fileName = command.substr(8).c_str();
        uint8_t buffer[256];
        size_t bytesRead = read(fileName.c_str(), buffer, sizeof(buffer));
        if (bytesRead > 0)
        {
            Serial.write(buffer, bytesRead);
            return true;
        }
        else
        {
            Serial.println("[SDCardModule] Failed to read file");
            return false;
        }
    }
    else if (command.compare(0, 9, "sdc echo ") == 0)
    {
        size_t spacePos = command.find(' ', 9);
        if (spacePos == std::string::npos)
        {
            Serial.println("[SDCardModule] Invalid command format");
            return false;
        }
        String fileName = command.substr(9, spacePos - 9).c_str();
        String content = command.substr(spacePos + 1).c_str();
        FsFile file = open(fileName.c_str(), FILE_WRITE);
        if (file)
        {
            file.println(content);
            file.close();
            return true;
        }
        else
        {
            Serial.println("[SDCardModule] Failed to write to file");
            return false;
        }
    }
    else if (command.compare(0, 7, "sdc mv ") == 0)
    {
        size_t spacePos = command.find(' ', 7);
        if (spacePos == std::string::npos)
        {
            Serial.println("[SDCardModule] Invalid command format");
            return false;
        }
        String oldName = command.substr(7, spacePos - 7).c_str();
        String newName = command.substr(spacePos + 1).c_str();
        return rename(oldName.c_str(), newName.c_str());
    }
    else
    {
        Serial.println("[SDCardModule] Unknown command");
        return false;
    }
}

bool SDCardModule::rename(const char *oldPath, const char *newPath)
{
    return sd.rename(oldPath, newPath);
}

bool SDCardModule::isMounted()
{
    return mounted;
}

bool SDCardModule::format()
{
    return sd.format();
}

bool SDCardModule::info()
{
    if (!mounted) return false;

    uint32_t cardSize = sd.card()->sectorCount();
    openknx.logger.begin();
    openknx.logger.log(""); // Empty line for spacing
    openknx.logger.color(CONSOLE_HEADLINE_COLOR);
    openknx.logger.log("============================= SD Card Information ==============================");
    openknx.logger.color(0);
    cid_t cid;
    if (sd.card()->readCID(&cid))
    {
        const char* manufacturers[] = {"Unknown", "SanDisk", "Panasonic", "TDK", "SanDisk", "Samsung", "Kingston", "Transcend"};
        openknx.logger.logWithValues("| Manufacturer ID        | %-50s |", 
        manufacturers[(cid.mid == 0x1B) ? 1 : (cid.mid == 0x01) ? 1 : (cid.mid == 0x02) ? 2 : (cid.mid == 0x03) ? 3 : (cid.mid == 0x3F) ? 4 : (cid.mid == 0x4B) ? 5 : (cid.mid == 0x5B) ? 6 : 0]);
        std::string productName = "";
        for (int i = 0; i < 5; i++)
        {
            char tempStr[2] = {(char)cid.pnm[i], '\0'};
            productName += tempStr;
        }
        openknx.logger.logWithValues("| Product Name           | %-50s |", productName.c_str());
        openknx.logger.logWithValues("| Product Revision       | %-50s |", String((unsigned int)cid.prv, HEX).c_str());
        openknx.logger.logWithValues("| Serial Number          | %-50s |", String((unsigned long)cid.psn(), HEX).c_str());
        openknx.logger.logWithValues("| Manufacture Date       | %-2s/%4s                                            |", String(cid.mdtMonth()).c_str(), String(cid.mdtYear()).c_str());
    }
    else
    {
        openknx.logger.log("| Error                  | Unable to read CID data                          |");
    }
    const char *cardTypeStr[] = {"Unknown", "SD (Standard)", "SDHC (High Capacity)", "SDXC (Extended Capacity)"};
    openknx.logger.log("--------------------------------------------------------------------------------");
    openknx.logger.logWithValues("| Card Capacity          | %-50s |", String(((uint64_t)cardSize * 512 / (1024 * 1024))+String(" MB")).c_str());
    openknx.logger.logWithValues("| Max Speed              | %-50s |", String(SPI_FULL_SPEED).c_str());
    openknx.logger.logWithValues("| Card Type              | %-50s |", cardTypeStr[sd.card()->type()] ? cardTypeStr[sd.card()->type()] : "Unknown Type");
    openknx.logger.logWithValues("| File System            | %-50s |", String(String("FAT")+String(sd.fatType())).c_str());
   // openknx.logger.log("--------------------------------------------------------------------------------");
    openknx.logger.color(CONSOLE_HEADLINE_COLOR);
    // uint64_t usedBytes = (uint64_t)sd.vol()->clusterCount() * sd.vol()->sectorsPerCluster() * 512 -
    //                      sd.vol()->freeClusterCount() * sd.vol()->sectorsPerCluster() * 512;
    // uint64_t totalBytes = (uint64_t)sd.vol()->clusterCount() * sd.vol()->sectorsPerCluster() * 512;
    // float usedPercentage = (float)usedBytes / totalBytes * 100.0f;
    //openknx.logger.logWithValues("Used: %-20s [%-50s] %.1f%%",
    //                             String((unsigned long)usedBytes, DEC).c_str(),
    //                             String("==============================================").substring(0, (int)(usedPercentage / 2)).c_str(),
    //                             usedPercentage);
    //openknx.logger.logWithValues("Free: %-20s [%-50s] %.1f%%",
    //                             String((unsigned long)(totalBytes - usedBytes), DEC).c_str(),
    //                             String("==============================================").substring(0, (int)((100 - usedPercentage) / 2)).c_str(),
    //                             100 - usedPercentage);
    //openknx.logger.logWithValues("Total: %-20s", String((unsigned long)totalBytes, DEC).c_str());
    openknx.logger.log("--------------------------------------------------------------------------------");
    openknx.logger.color(0);

    return true;
}

FsFile SDCardModule::open(const char *path, const char *mode)
{
    oflag_t flags = 0;
    if (strcmp(mode, "r") == 0)
    {
        flags = O_RDONLY;
    }
    else if (strcmp(mode, "w") == 0)
    {
        flags = O_WRONLY | O_CREAT | O_TRUNC;
    }
    else if (strcmp(mode, "a") == 0)
    {
        flags = O_WRONLY | O_CREAT | O_APPEND;
    }
    else if (strcmp(mode, "r+") == 0)
    {
        flags = O_RDWR;
    }
    else if (strcmp(mode, "w+") == 0)
    {
        flags = O_RDWR | O_CREAT | O_TRUNC;
    }
    else if (strcmp(mode, "a+") == 0)
    {
        flags = O_RDWR | O_CREAT | O_APPEND;
    }
    return sd.open(path, flags);
}

bool SDCardModule::createFile(const char *path)
{
    FsFile file = open(path, FILE_WRITE);
    if (!file) return false;
    file.close();
    return true;
}

bool SDCardModule::remove(const char *path)
{
    return sd.remove(path);
}

bool SDCardModule::exists(const char *path)
{
    return sd.exists(path);
}

size_t SDCardModule::read(const char *path, uint8_t *buffer, size_t size)
{
    FsFile file = open(path, FILE_READ);
    if (!file) return 0;
    size_t bytesRead = file.read(buffer, size);
    file.close();
    return bytesRead;
}

size_t SDCardModule::write(const char *path, const uint8_t *buffer, size_t size)
{
    FsFile file = open(path, FILE_WRITE);
    if (!file) return 0;
    size_t bytesWritten = file.write(buffer, size);
    file.close();
    return bytesWritten;
}

bool SDCardModule::mkdir(const char *path)
{
    return sd.mkdir(path);
}

bool SDCardModule::rmdir(const char *path)
{
    return sd.rmdir(path);
}

std::vector<String> SDCardModule::ls(const char *path)
{
    std::vector<String> fileList;
    FsFile dir = sd.open(path);
    if (!dir) return fileList;
    char buffer[256];
    while (FsFile file = dir.openNextFile())
    {
        file.getName(buffer, sizeof(buffer));
        file.close();
        fileList.push_back(buffer);
    }
    dir.close();
    return fileList;
}

SDCardModule sdCardModule(PIN_SDCARD_CS); // ToDo: CS Pin for SD card module ?, not obtain them from device configuration
