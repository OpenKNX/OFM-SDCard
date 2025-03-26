#include "SDCardModule.h"

// ToDo: Dynamic HardwareConfig for the SD-Card

SPIClass SPI_SD(HSPI);
SdSpiConfig sdConfig(PIN_SDCARD_CS, DEDICATED_SPI, SD_SCK_MHZ(50), &SPI_SD);

/**
 * @brief Construct a new SDCardModule::SDCardModule object
 *
 * @param csPin
 */
SDCardModule::SDCardModule(uint8_t csPin) : _chipSelectPin(csPin), _mounted(false), _sd(SdFat()) {}

/**
 * @brief Destroy the SDCardModule::SDCardModule object
 *
 */
SDCardModule::~SDCardModule() {}

/**
 * @brief Initialize the SD-Card Module
 *
 */
void SDCardModule::init()
{
    logInfoP("Initializing...");
    _cardDetectTimer = millis();
    pinMode(PIN_SDCARD_CD, INPUT); // Set the card detect pin as input
}

/**
 * @brief Setup the SD-Card Module
 *
 * @param configured
 */
void SDCardModule::setup(bool configured)
{
    logDebugP("Setting up...");
    _mounted = false;
    // Read the card detect pin
    _cardInserted = digitalRead(PIN_SDCARD_CD) == LOW; // LOW means card is inserted. Since we are using internal pull-up resistor, we need to check for LOW
    if (!_cardInserted)
    {
        logInfoP("No SD card inserted.");
        return;
    }
    else
    {
        logInfoP("SD card inserted. Mounting the SD card...");
        Mount();
    }
}

/**
 * @brief Mount the SD-Card
 *
 */
void SDCardModule::Mount()
{
    // ToDo: HardwareConfig for the SD-Card
    SPI_SD.begin(PIN_SDCARD_SCK, PIN_SDCARD_MISO, PIN_SDCARD_MOSI, PIN_SDCARD_CS);
    if (!_sd.begin(sdConfig))
    {
        logErrorP("Unable to detect the inserted SD card!");
        logDebugP("Error code: %X (Data: %X)", _sd.card()->errorCode(), _sd.card()->errorData());
        logErrorP("Please check the Format of the SD card! It must be FAT16, FAT32 or exFAT! formated!");
        return;
    }
    else
    {
        logInfoP("Successfully mounted the SD card!");
        _mounted = true;
        info();
    }
}

/**
 * @brief Unmount the SD-Card
 *
 * @param force will force the unmounting of the SD-Card and ignore the busy state
 * @return true if the SD-Card was unmounted successfully
 */
bool SDCardModule::Unmount(bool force)
{
    if (!force && _sd.card()->isBusy())
    {
        logErrorP("SD-Card is busy. Cannot unmount the card! Please try again later.");
        return false;
    }
    else
    {
        _sd.end();
        logInfoP("SD-Card unmounted!");
        SPI_SD.end();
        logDebugP("SPI for SD-Card closed!");
        _mounted = false;
        return true;
    }
}

void SDCardModule::loop(bool configured)
{
    if (delayCheck(_cardDetectTimer, 500)) // CHeck every 500ms for card detection
    {
        _cardDetectTimer = millis();
        bool currentCardInserted = digitalRead(PIN_SDCARD_CD) == LOW; // ToDo: HardwareConfig for the SD-Card

        if (currentCardInserted != _cardInserted)
        {
            _cardInserted = currentCardInserted;

            if (_cardInserted)
            {
                logInfoP("SD card inserted. Starting initialization...");
                _cardMountTimer = millis();
            }
            else
            {
                logInfoP("SD card removed.");
                Unmount(true); // Unmount the card and force it, since it is removed!
                _cardMountTimer = 0;
            }
        }
    }

    if (_cardInserted && _cardMountTimer > 0 && delayCheck(_cardMountTimer, 3000))
    {
        Mount();
        _cardMountTimer = 0;
    }
}

void SDCardModule::showHelp()
{
    openknx.console.printHelpLine("sdc", "SD Card Control Module. Use 'sdc ?' for more information.");
}

bool SDCardModule::processCommand(const std::string command, bool diagnose)
{
    if (diagnose) return false;
    bool bRet = true;

    if (command.compare(0, 4, "sdc ") == 0)
    {
        if (command.compare(4, 1, "?") == 0 || command.compare(4, 4, "help") == 0 || command.length() == 4)
        {
            openknx.logger.begin();
            openknx.logger.log("");
            openknx.logger.color(CONSOLE_HEADLINE_COLOR);
            openknx.logger.log("============================= Help: SD Card Control Module ============================="); // 80
            openknx.logger.color(0);
            openknx.logger.log("Command(s)               Description");
            openknx.console.printHelpLine("sdc info", "Get information about the SD-Card");
            openknx.console.printHelpLine("sdc add /<f>", "Add a folder/file to the SD-Card");
            openknx.console.printHelpLine("sdc rm /<f>", "Remove a file from the SD-Card");
            openknx.console.printHelpLine("sdc cat /<f>", "Read a file from the SD-Card");
            openknx.console.printHelpLine("sdc echo /<file> <text>", "Append content to a file in the SD-Card");
            openknx.console.printHelpLine("sdc mv /<src> /<targt>", "Rename/ or Move a file or folder");
            openknx.console.printHelpLine("sdc cp /<src> /<targt>", "Copy a file in the SD-Card");
            openknx.console.printHelpLine("sdc mkdir /<name>", "Create a directory in the SD-Card");
            openknx.console.printHelpLine("sdc rmdir /<name>", "Remove a directory from the SD-Card");
            openknx.console.printHelpLine("sdc ls /<path>", "Short list files in a directory in the SD-Card");
            openknx.console.printHelpLine("sdc ll /<path>", "List files in a directory in the SD-Card with details");
            openknx.console.printHelpLine("sdc format", "ATTENTION: Will Format (exFAT) the external SD-Card");
            // openknx.console.printHelpLine("efc test", "Creating files, folders, writing and reading files");
            openknx.logger.color(CONSOLE_HEADLINE_COLOR);
            openknx.logger.log("========================================================================================"); // 80
            openknx.logger.color(0);
            openknx.logger.end();
        }
        else if (command.compare(4, 4, "info") == 0)
        {
            info();
        }
        else if (command.compare(4, 6, "format") == 0)
        {
            std::string answer = command.substr(10).c_str();
            if (answer.compare(" yes") == 0)
            {
                logInfoP("Ok! You know the consequences. Formatting the SD card...");
                format();
            }
            else
            {
                logInfoP("Formatting the SD card will erase all data on the card. Are you sure you want to continue?");
                logInfoP("Type 'sdc format yes' to do so.");
            }
        }
        else if (command.compare(4, 3, "llf") == 0)
        {
            lowLevelFormat();
        }
        else if (command.compare(4, 3, "rpt") == 0)
        {
            readPartitionTable();
        }
        else if (command.compare(4, 4, "rgpt") == 0)
        {
            readGPT();
        }
        else if (command.compare(4, 4, "qformat") == 0)
        {
            quickFormat();
        }

        else if (command.compare(4, 4, "add ") == 0)
        {
            String fileName = command.substr(8).c_str();
            if (fileName[0] != '/')
            {
                fileName = "/" + fileName;
            }

            if (fileName.length() > 0 && fileName.length() <= 255 && fileName[0] == '/' && createFile(fileName.c_str()))
            {
                logInfoP(String("File created: " + fileName).c_str());
            }
            else
            {
                logErrorP("File name is invalid");
                bRet = false;
            }
        }
        else if (command.compare(4, 3, "ll ") == 0)
        {
            logInfoP("SD-Card Files:");
            String path = command.substr(7).c_str();
            std::vector<String> files = ls(path.length() == 0 ? "/" : path.c_str());
            openknx.logger.begin();
            openknx.logger.log("");
            openknx.logger.color(CONSOLE_HEADLINE_COLOR);
            openknx.logger.log("======================== SD-Card Control - File List Long (ll) ========================="); // 80 characters
            openknx.logger.log("----------------------------------------------------------------------------------------");
            openknx.logger.log("Name                                      | Size (bytes) | Type   | Created             ");
            openknx.logger.log("----------------------------------------------------------------------------------------"); // 88 characters
            openknx.logger.color(0);
            if (files.size() == 0)
            {
                openknx.logger.log("..(empty)");
            }
            uint64_t totalSize = 0;
            u_int64_t CountedFoders = 0;
            // Separate files and directories
            std::vector<String> directories, regularFiles;
            for (String file : files)
            {
                FileInfo stat;
                if (Statistics(file.c_str(), stat))
                {
                    if (stat.isDir)
                    {
                        directories.push_back(file);
                        CountedFoders++;
                    }
                    else
                    {
                        regularFiles.push_back(file);
                    }
                }
                else
                {
                    logErrorP(String("Failed to get stats for: " + file).c_str());
                }
            }

            // Process directories first
            for (String dir : directories)
            {
                FileInfo stat;
                if (Statistics(dir.c_str(), stat))
                {
                    const String type = "Dir";
                    char formattedTime[25];
                    strftime(formattedTime, sizeof(formattedTime), "%H:%M:%S %d.%m.", localtime(&stat.ctime));
                    sprintf(formattedTime + strlen(formattedTime), "%02d", (localtime(&stat.ctime)->tm_year + 1900) % 100);

                    const String name = "[" + ((dir.length() > 37) ? dir.substring(0, 36) + "..." : dir) + "]";
                    totalSize += stat.size;
                    openknx.logger.logWithValues("%-41s | %-12s | %-6s | %-20s",
                                                 name.c_str(),
                                                 String(/*getSize(dir.c_str())*/ "").c_str(), type.c_str(), formattedTime);
                }
                else
                {
                    // Stat failed. Show only the directory name
                    openknx.logger.logWithValues("%-41s | %-12s | %-6s | %-20s",
                                                 String((dir.length() > 41) ? dir.substring(0, 38) + "..." : dir).c_str(),
                                                 "N/A", "Dir", "N/A");
                }
            }

            // Process regular files
            for (String file : regularFiles)
            {
                FileInfo stat;
                if (Statistics(file.c_str(), stat))
                {
                    const String type = "File";
                    char formattedTime[25];
                    strftime(formattedTime, sizeof(formattedTime), "%H:%M:%S %d.%m.", localtime(&stat.ctime));
                    sprintf(formattedTime + strlen(formattedTime), "%02d", (localtime(&stat.ctime)->tm_year + 1900) % 100);
                    totalSize += stat.size;
                    openknx.logger.logWithValues("%-41s | %-12s | %-6s | %-20s",
                                                 // file.c_str(),
                                                 String((file.length() > 41) ? file.substring(0, 38) + "..." : file).c_str(),
                                                 String(stat.size).c_str(), type.c_str(), formattedTime);
                }
                else
                {
                    // Stat failed. Show only the file name
                    openknx.logger.logWithValues("%-41s | %-12s | %-6s | %-20s",
                                                 String((file.length() > 41) ? file.substring(0, 38) + "..." : file).c_str(),
                                                 "N/A", "File", "N/A");
                }
            }
            openknx.logger.color(CONSOLE_HEADLINE_COLOR);
            openknx.logger.log("----------------------------------------------------------------------------------------"); // 88 characters
            // const String totalFiles = String("Total files: " + String((unsigned long)files.size(), DEC) + " | Total size: " + String((unsigned long)totalSize, DEC) + " bytes");
            openknx.logger.logWithValues("%-20s %-20s | %-12s",
                                         String("Folders: " + String((unsigned long)CountedFoders, DEC)).c_str(),
                                         String("Files: " + String((unsigned long)(files.size() - CountedFoders), DEC)).c_str(),
                                         String("Size: " + String((unsigned long)totalSize, DEC) + " bytes").c_str());
            openknx.logger.log("----------------------------------------------------------------------------------------"); // 88 characters

            // FSInfo info;
            // if (_extFlashLfs.info(info))
            //{
            //     const float usedPercentage = (float)info.usedBytes / info.totalBytes * 100.0f;
            //     openknx.logger.log("Total Storage extFlash: ");
            //     openknx.logger.logWithValues("Used: %-20s [%-50s] %.1f%%", // Used space
            //                                  String((unsigned long)info.usedBytes, DEC).c_str(),
            //                                  String("==============================================").substring(0, (int)(usedPercentage / 2)).c_str(), // Bar length
            //                                  usedPercentage);
            //     openknx.logger.logWithValues("Free: %-20s [%-50s] %.1f%%", // Free space
            //                                  String((unsigned long)(info.totalBytes - info.usedBytes), DEC).c_str(),
            //                                  String("==============================================").substring(0, (50 - (int)(usedPercentage / 2))).c_str(), // Bar length
            //                                  100.0f - usedPercentage);
            // }
            // openknx.logger.log("----------------------------------------------------------------------------------------"); // 88 characters

            openknx.logger.color(0);
            openknx.logger.end();
        }
        else if (command.compare(4, 3, "ls ") == 0)
        {
            String path = command.substr(7).c_str();
            std::vector<String> files = ls(path.length() > 0 ? path.c_str() : "/");
            for (String file : files)
            {
                logInfoP("%s", file.c_str());
            }
        }
        else if (command.compare(4, 6, "mkdir ") == 0)
        {
            String dirName = command.substr(11).c_str();
            if (dirName[0] != '/')
            {
                dirName = "/" + dirName;
            }
            if (dirName.length() > 0 && mkdir(dirName.c_str()))
            {
                logInfoP(String("Directory created: " + dirName).c_str());
            }
            else
            {
                logErrorP("Failed to create directory");
                bRet = false;
            }
        }
        else if (command.compare(4, 6, "rmdir ") == 0)
        {
            String dirName = command.substr(11).c_str();
            if (dirName[0] != '/')
            {
                dirName = "/" + dirName;
            }
            if (dirName.length() > 0 && rmdir(dirName.c_str()))
            {
                logInfoP(String("Directory removed: " + dirName).c_str());
            }
            else
            {
                logErrorP("Failed to remove directory");
                bRet = false;
            }
        }
        else if (command.compare(4, 3, "rm ") == 0)
        {
            String fileName = command.substr(7).c_str();
            if (fileName[0] != '/')
            {
                fileName = "/" + fileName;
            }
            if (fileName.length() > 0 && remove(fileName.c_str()))
            {
                logInfoP(String("File removed: " + fileName).c_str());
            }
            else
            {
                logErrorP("Failed to remove file");
                bRet = false;
            }
        }
        else if (command.compare(4, 4, "cat ") == 0)
        {
            logInfoP("Reading file and will show the first %d bytes of the file content.", OPENKNX_MAX_LOG_MESSAGE_LENGTH);
            String fileName = command.substr(9).c_str();
            uint8_t buffer[OPENKNX_MAX_LOG_MESSAGE_LENGTH];
            size_t bytesRead = read(fileName.c_str(), buffer, sizeof(buffer) - 1); // Reserve space for null terminator
            if (bytesRead > 0)
            {
                buffer[bytesRead] = '\0'; // Null-terminate the buffer for printing as a string in log message
                logInfoP("File %s content:", fileName.c_str());
                openknx.logger.color(CONSOLE_HEADLINE_COLOR);
                logInfoP("%.*s", OPENKNX_MAX_LOG_MESSAGE_LENGTH, buffer); // Ensure no more than max log length is written
                openknx.logger.color(0);
                logInfoP("End of file content.");
                return true;
            }
            else
            {
                logErrorP("Failed to read file");
                return false;
            }
        }
        else if (command.compare(4, 5, "echo ") == 0)
        {
            // Get the file name which begins with / and ends with space
            String fileName = command.substr(9, command.find(' ', 9) - 9).c_str();

            // After the file name, get the content to write to the file it will begin after the space of the file name
            String content = command.substr(command.find(' ', 9) + 1).c_str();
            if (fileName.length() > 0 && content.length() > 0)
            {
                FsFile file = _sd.open(fileName.c_str(), O_RDWR | O_CREAT);
                if (!file)
                {
                    file = _sd.open(fileName.c_str(), O_RDWR | O_CREAT);
                    if (!file)
                    {
                        logErrorP("Failed to create file");
                        bRet = false;
                    }
                    else
                    {
                        logInfoP(String("File created: " + fileName).c_str());
                        file.println(content);
                        file.close();
                        logInfoP(String("Appended to file: " + fileName).c_str());
                    }
                }
                else
                {
                    file.println(content);
                    file.close();
                    logInfoP(String("Appended to file: " + fileName).c_str());
                }
            }
            else
            {
                logErrorP("Invalid file name or content");
                bRet = false;
            }
        }
        else if (command.compare(4, 3, "mv ") == 0)
        {
            String oldName = command.substr(7, command.find(' ', 7) - 7).c_str();
            if (oldName[0] != '/')
            {
                oldName = "/" + oldName;
            }
            String newName = command.substr(command.find(' ', 7) + 1).c_str();
            if (newName[0] != '/')
            {
                newName = "/" + newName;
            }
            if (oldName.length() > 0 && newName.length() > 0 && rename(oldName.c_str(), newName.c_str()))
            {
                logInfoP(String("Renamed from " + oldName + " to " + newName).c_str());
            }
            else
            {
                logErrorP("Failed to rename %s to %s", oldName.c_str(), newName.c_str());
                bRet = false;
            }
        }
        else
        {
            logErrorP("Invalid command. Use 'sdc ?' for help.");
            return false;
        }
    } // end of sdc command

    else
    {
        bRet = false;
    }
    return bRet;
}

BootSectorInfo SDCardModule::getBootSectorInfo(int fsType)
{
    uint8_t buffer[512];
    BootSectorInfo info;

    if (!_sd.card()->readSector(0, buffer))
    {
        logErrorP("The boot sector could not be read!");
        return info;
    }

    // Write the buffer to a file
    FsFile file = open("bootsector.bin", FILE_WRITE);
    file.write(buffer, 512);
    file.close(); // Close the file

    switch (fsType)
    {
        case 16: // FAT16
            info.fileSystemType = "FAT16";
            info.bytesPerSector = buffer[0x0B] | (buffer[0x0C] << 8);
            info.sectorsPerCluster = buffer[0x0D];
            info.reservedSectors = buffer[0x0E] | (buffer[0x0F] << 8);
            info.numberOfFATs = buffer[0x10];
            info.totalSectors = buffer[0x13] | (buffer[0x14] << 8); // FAT16
            info.fatSize = buffer[0x16] | (buffer[0x17] << 8);
            info.rootDirCluster = 0;
            info.volumeLabel.assign(reinterpret_cast<const char *>(&buffer[0x47]), 11);
            info.isValid = true;
            break;

        case 32: // FAT32
            info.fileSystemType = "FAT32";
            info.bytesPerSector = buffer[0x0B] | (buffer[0x0C] << 8);
            info.sectorsPerCluster = buffer[0x0D];
            info.reservedSectors = buffer[0x0E] | (buffer[0x0F] << 8);
            info.numberOfFATs = buffer[0x10];
            info.totalSectors = buffer[0x20] | (buffer[0x21] << 8) | (buffer[0x22] << 16) | (buffer[0x23] << 24);
            info.fatSize = buffer[0x24] | (buffer[0x25] << 8) | (buffer[0x26] << 16) | (buffer[0x27] << 24);
            info.rootDirCluster = buffer[0x2C] | (buffer[0x2D] << 8) | (buffer[0x2E] << 16) | (buffer[0x2F] << 24);
            info.volumeLabel.assign(reinterpret_cast<const char *>(&buffer[0x47]), 11);
            info.isValid = true;
            break;

        case 64: // exFAT
            info.fileSystemType = "exFAT";
            info.bytesPerSector = 1 << buffer[0x0C];
            info.sectorsPerCluster = 1 << buffer[0x0D];
            info.totalSectors = buffer[0x20] | (buffer[0x21] << 8) | (buffer[0x22] << 16) | (buffer[0x23] << 24);
            info.volumeLabel.assign(reinterpret_cast<const char *>(&buffer[0x52]), 0x20);
            info.isValid = true;
            break;

        default:
            info.fileSystemType = "Unknown";
            info.isValid = false;
            logErrorP("The file system type is unknown!");
            break;
    }

    logDebugP("Bootsektor erfolgreich gelesen!");
    return info;
}

/**
 * @brief Rename or move a file or folder
 *
 * @param oldPath Path to the file or folder to be renamed or moved
 * @param newPath New path of the file or folder
 * @return true for success or false for failure
 *
 * */
bool SDCardModule::rename(const char *oldPath, const char *newPath)
{
    return _sd.rename(oldPath, newPath);
}

/**
 * @brief Returns the mount status of the SD card
 *
 * @return true if the SD card is mounted, false otherwise
 *
 */
bool SDCardModule::isMounted()
{
    return _mounted;
}

/**
 * @brief Format the SD card
 *
 * @return true for success or false for failure
 *
 */
bool SDCardModule::format()
{
    // if (!_mounted) {
    //    return false;
    //    logInfoP("SD card is not _mounted! Only _mounted SD card can be formatted!");
    // }
    logInfoP("Formatting the SD card...");
    if (_sd.format())
    {
        logInfoP("SD card formatted successfully!");
        Mount();
        return true;
    }
    else
    {
        logErrorP("Failed to format the SD card!");
        return false;
    }
}

void SDCardModule::quickFormat()
{
    uint8_t emptyMBR[512] = {0};
    if (!_sd.card()->writeSector(0, emptyMBR))
    {
        logErrorP("Error writing MBR sector!");
        logErrorP("Quick formatting failed!");
        return;
    }
    logInfoP("MBR deleted. Card must be repartitioned.");
}

void SDCardModule::lowLevelFormat()
{
    // Serial.println("WARNUNG: Low-Level-Formatierung wird gestartet!");
    logInfoP("WARNING: A low-level format will erase all data on the SD card!");
    uint8_t emptySector[512] = {0}; // Leerer Sektor mit 0x00

    for (uint32_t i = 0; i < _sd.card()->sectorCount(); i++)
    {
        if (!_sd.card()->writeSector(i, emptySector))
        {
            logErrorP("Error writing sector %lu", i);
            logErrorP("Low-Level formatting failed!");
            return;
        }

        if (i % 100 == 0)
        {
            logInfoP("Low-Level formatting: %lu%%", i * 100 / _sd.card()->sectorCount());
        }
    }
    logInfoP("Low-Level formatting completed successfully!");
}

void SDCardModule::readPartitionTable()
{
    uint8_t buffer[512];
    if (!_sd.card()->readSector(0, buffer))
    {
        logErrorP("The boot sector could not be read!");
        return;
    }
    logInfoP("Reading partition table...");

    for (int i = 0; i < 4; i++)
    {
        int offset = 0x1BE + (i * 16);

        uint8_t bootFlag = buffer[offset];          // Boot-Flag (80h = aktiv)
        uint8_t partitionType = buffer[offset + 4]; // Partitionstyp (z.B. 0x0B = FAT32, 0x07 = exFAT/NTFS)
        uint32_t startSector = buffer[offset + 8] | (buffer[offset + 9] << 8) | (buffer[offset + 10] << 16) | (buffer[offset + 11] << 24);
        uint32_t totalSectors = buffer[offset + 12] | (buffer[offset + 13] << 8) | (buffer[offset + 14] << 16) | (buffer[offset + 15] << 24);

        logInfoP("Partition %d:", i + 1);
        logInfoP("  Bootflag: 0x%X", bootFlag);
        logInfoP("  Type: 0x%X (%s)", partitionType,
                 (partitionType == 0x0B) ? "FAT32" : (partitionType == 0x07) ? "exFAT/NTFS"
                                                 : (partitionType == 0x0C)   ? "FAT32 LBA"
                                                 : (partitionType == 0x0E)   ? "FAT16 LBA"
                                                 : (partitionType == 0x0F)   ? "exFAT/NTFS LBA"
                                                 : (partitionType == 0x05)   ? "Extended"
                                                 : (partitionType == 0x83)   ? "Linux"
                                                 : (partitionType == 0x82)   ? "Linux Swap"
                                                 : (partitionType == 0x8E)   ? "Linux LVM"
                                                                             : "Unknown");
        logInfoP("  Start sector: %lu", startSector);
        logInfoP("  Total sectors: %lu", totalSectors);
    }
}

void SDCardModule::readGPT()
{
    uint8_t buffer[512];
    if (!_sd.card()->readSector(1, buffer))
    {
        logErrorP("Error reading GPT header!");
        return;
    }

    logInfoP("Reading GPT header...");

    uint32_t partitionEntryLBA = buffer[72] | (buffer[73] << 8) | (buffer[74] << 16) | (buffer[75] << 24);

    logInfoP("Partition table starts at LBA: %lu", partitionEntryLBA);

    for (int i = 0; i < 4; i++) // Iterate through the first 4 GPT partitions
    {                           // Wir lesen nur die ersten 4 Partitionen
        if (!_sd.card()->readSector(partitionEntryLBA + i, buffer))
        {
            logErrorP("Error reading GPT partition %d!", i + 1);
            continue;
        }

        uint64_t firstLBA = *(uint64_t *)&buffer[32];
        uint64_t lastLBA = *(uint64_t *)&buffer[40];

        logInfoP("GPT-Partition %d:", i + 1);
        logInfoP("  Start-LBA: %llu", firstLBA);
        logInfoP("  End-LBA: %llu", lastLBA);
    }
}

bool SDCardModule::info()
{
    if (!_mounted) return false;

    openknx.logger.begin();
    openknx.logger.log(""); // Empty line for spacing
    openknx.logger.color(CONSOLE_HEADLINE_COLOR);
    openknx.logger.log("============================= SD Card Information ==============================");
    openknx.logger.color(0);
    cid_t cid;
    if (_sd.card()->readCID(&cid))
    {
        const char *manufacturers[] = {"Unknown", "SanDisk", "Panasonic", "TDK", "SanDisk", "Samsung", "Kingston", "Transcend"};
        openknx.logger.logWithValues("| Manufacturer ID        | %-50s |",
                                     manufacturers[(cid.mid == 0x1B) ? 1 : (cid.mid == 0x01) ? 1
                                                                       : (cid.mid == 0x02)   ? 2
                                                                       : (cid.mid == 0x03)   ? 3
                                                                       : (cid.mid == 0x3F)   ? 4
                                                                       : (cid.mid == 0x4B)   ? 5
                                                                       : (cid.mid == 0x5B)   ? 6
                                                                                             : 0]);
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

    // BootSectorInfo info = getBootSectorInfo(_sd.fatType());
    // if (info.isValid)
    //{
    //     openknx.logger.logWithValues("| Bytes per Sector        | %-50s |", String(info.bytesPerSector).c_str());
    //     openknx.logger.logWithValues("| Sectors per Cluster     | %-50s |", String((unsigned int)info.sectorsPerCluster).c_str());
    //     openknx.logger.logWithValues("| Reserved Sectors        | %-50s |", String((unsigned int)info.reservedSectors).c_str());
    //     openknx.logger.logWithValues("| Number of FATs          | %-50s |", String((unsigned int)info.numberOfFATs).c_str());
    //     openknx.logger.logWithValues("| Total Sectors           | %-50s |", String((unsigned long)info.totalSectors).c_str());
    //     openknx.logger.logWithValues("| FAT Size                | %-50s |", String((unsigned long)info.fatSize).c_str());
    //     openknx.logger.logWithValues("| Root Directory Cluster  | %-50s |", String((unsigned long)info.rootDirCluster).c_str());
    //     openknx.logger.logWithValues("| File System Type        | %-50s |", info.fileSystemType.c_str());
    //     openknx.logger.logWithValues("| Volume Label            | %-50s |", info.volumeLabel.c_str());
    //     openknx.logger.log("--------------------------------------------------------------------------------");
    // }
    //  **MB / GB Automatisch setzen**

    //
    openknx.logger.logWithValues("| Card Size              | %-50s |", formatSize((uint64_t)_sd.card()->sectorCount() * 512));
    openknx.logger.logWithValues("| Max Speed              | %-50s |", String(SPI_FULL_SPEED).c_str());
    openknx.logger.logWithValues("| Card Type              | %-50s |", cardTypeStr[_sd.card()->type()] ? cardTypeStr[_sd.card()->type()] : "Unknown Type");
    openknx.logger.logWithValues("| File System            | %-50s |", _sd.fatType() == FAT_TYPE_EXFAT ? "exFAT" : _sd.fatType() == FAT_TYPE_FAT32 ? "FAT32"
                                                                                                               : _sd.fatType() == FAT_TYPE_FAT16   ? "FAT16"
                                                                                                                                                   : "Unknown");
    // openknx.logger.log("--------------------------------------------------------------------------------");
    openknx.logger.color(CONSOLE_HEADLINE_COLOR);
    // uint64_t freeBytes, usedBytes;
    //// EINMALIG SD-INFO LADEN (um CPU-Blockaden zu vermeiden!)
    // uint64_t totalBytes = getSDCardSize();
    // yield(); // Ermöglicht anderen Tasks weiterzulaufen (Watchdog-Reset)
    // getSDCardUsage(freeBytes, usedBytes);
    // yield(); // Wieder CPU freigeben
    // float usedPercentage = (float)usedBytes * 100.0f / totalBytes;
    // float freePercentage = 100.0f - usedPercentage;
    // int usedBarLength = (int)(usedPercentage * 0.5f);
    // int freeBarLength = 50 - usedBarLength;
    // char usedBar[51] = {0}; // 50 Zeichen + Nullterminierung
    // char freeBar[51] = {0};
    // memset(usedBar, '=', usedBarLength);
    // memset(freeBar, '=', freeBarLength);
    // openknx.logger.logWithValues("Used: %-20s [%-50s] %.1f%%", formatSize(usedBytes), usedBar, usedPercentage);
    // openknx.logger.logWithValues("Free: %-20s [%-50s] %.1f%%", formatSize(freeBytes), freeBar, freePercentage);
    // openknx.logger.logWithValues("Total: %-20s", formatSize(totalBytes));
    openknx.logger.log("--------------------------------------------------------------------------------");
    openknx.logger.color(0);

    return true;
}

/*
 *
 * This function allows opening a file on the SD card with various modes such as read, write, append, etc.
 * The mode is specified as a string similar to standard file I/O operations (e.g., "r", "w", "a").
 *
 * @param path The path to the file to be opened.
 * @param mode The mode in which the file should be opened. Supported modes:
 *             - "r"  : Open for reading.
 *             - "w"  : Open for writing (truncates the file if it exists).
 *             - "a"  : Open for appending (creates the file if it doesn't exist).
 *             - "r+" : Open for reading and writing.
 *             - "w+" : Open for reading and writing (truncates the file if it exists).
 *             - "a+" : Open for reading and writing (appends to the file if it exists).
 * @return A `FsFile` object representing the opened file. If the file cannot be opened, the returned object will be invalid.
 */
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
    return _sd.open(path, flags);
}

/**
 * @brief Create a file.
 *
 * This function creates a file at the specified path.
 *
 * @param path The path to the file.
 * @return True if the file is successfully created, false otherwise.
 */
bool SDCardModule::createFile(const char *path)
{
    FsFile file = open(path, FILE_WRITE);
    if (!file)
    {
        logErrorP("Failed to create file");
        return false;
    }
    file.close();
    return true;
}

/**
 * @brief Remove a file.
 *
 * This function removes a file at the specified path.
 *
 * @param path The path to the file.
 * @return True if the file is successfully removed, false otherwise.
 */
bool SDCardModule::remove(const char *path)
{
    if (!_sd.remove(path))
    {
        logErrorP("Failed to remove file");
        return false;
    }
    return true;
}

/**
 * @brief Check if a file exists.
 *
 * This function checks if a file exists at the specified path.
 *
 * @param path The path to the file.
 * @return True if the file exists, false otherwise.
 */
bool SDCardModule::exists(const char *path)
{
    return _sd.exists(path);
}

/**
 * @brief Read data from a file.
 *
 * @param path The path to the file.
 * @param buffer The buffer to store the data read from the file.
 * @param size The size of the buffer.
 * @return The number of bytes read from the file.
 */
size_t SDCardModule::read(const char *path, uint8_t *buffer, size_t size)
{
    FsFile file = open(path, FILE_READ);
    if (!file) return 0;
    size_t bytesRead = file.read(buffer, size);
    file.close();
    return bytesRead;
}

/**
 * @brief Write data to a file.
 *
 * @param path The path to the file.
 * @param buffer The buffer containing the data to write.
 * @param size The size of the data to write.
 * @return The number of bytes written to the file.
 */
size_t SDCardModule::write(const char *path, const uint8_t *buffer, size_t size)
{
    FsFile file = open(path, FILE_WRITE);
    if (!file) return 0;
    size_t bytesWritten = file.write(buffer, size);
    file.close();
    return bytesWritten;
}

/**
 * @brief Append data to a file.
 *
 * @param path The path to the file.
 * @param buffer The buffer containing the data to append.
 * @param size The size of the data to append.
 * @return The number of bytes appended to the file.
 */
size_t SDCardModule::append(const char *path, const uint8_t *buffer, size_t size)
{
    FsFile file = open(path, FILE_APPEND);
    if (!file) return 0;
    size_t bytesAppended = file.write(buffer, size);
    file.close();
    return bytesAppended;
}

bool SDCardModule::mkdir(const char *path)
{
    return _sd.mkdir(path);
}

bool SDCardModule::rmdir(const char *path)
{
    return _sd.rmdir(path);
}

std::vector<String> SDCardModule::ls(const char *path)
{
    std::vector<String> fileList;
    FsFile dir = _sd.open(path);
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

// **Funktion zur Umwandlung von FAT-Datum/Zeit in Unix-Zeit (time_t)**
time_t SDCardModule::fatDateTimeToUnix(uint16_t fatDate, uint16_t fatTime)
{
    struct tm t = {};
    t.tm_sec = (fatTime & 0x1F) * 2;          // Sekunden (×2, da FAT nur 2-Sek-Schritte speichert)
    t.tm_min = (fatTime >> 5) & 0x3F;         // Minuten
    t.tm_hour = (fatTime >> 11) & 0x1F;       // Stunden
    t.tm_mday = fatDate & 0x1F;               // Tag des Monats
    t.tm_mon = ((fatDate >> 5) & 0x0F) - 1;   // Monat (0-basiert)
    t.tm_year = ((fatDate >> 9) & 0x7F) + 80; // Jahre seit 1900 (FAT beginnt 1980, Unix 1970)

    return mktime(&t); // Unix-Timestamp erstellen
}
/**
 * @brief Retrieves file statistics.
 *
 * This function retrieves statistics about a file at the specified path.
 *
 * @param path The path to the file.
 * @param info The structure to store the file statistics.
 * @return True if the statistics are successfully retrieved, false otherwise.
 */

bool SDCardModule::Statistics(const char *path, FileInfo &info)
{
    FsFile file;
    if (!file.open(path, O_RDONLY))
    {
        Serial.println("Fehler: Datei konnte nicht geöffnet werden!");
        return false;
    }
    uint16_t createDate, createTime, accessDate, accessTime;
    info.size = file.size();
    file.getCreateDateTime(&createDate, &createTime);
    file.getAccessDateTime(&accessDate, &accessTime);

    info.ctime = fatDateTimeToUnix(createDate, createTime);
    info.atime = fatDateTimeToUnix(accessDate, accessTime);
    info.isDir = file.isDirectory();
    info.blocksize = 512; // SD-Blockgröße fix 512 Bytes

    file.close();
    return true;
}

uint64_t SDCardModule::getSDCardSize()
{
    uint32_t sectorCount = _sd.card()->sectorCount(); // Anzahl der Sektoren
    uint16_t sectorSize = 512;                        // Standard bei SD-Karten (kann aber variieren)
    return (uint64_t)sectorCount * sectorSize;        // Größe in Bytes
}

void SDCardModule::getSDCardUsage(uint64_t &freeSpace, uint64_t &usedSpace)
{
    FsVolume *vol = _sd.vol();
    uint32_t freeClusters = vol->freeClusterCount(); // Anzahl freier Cluster
    uint32_t totalClusters = vol->clusterCount();    // Gesamtanzahl Cluster
    uint32_t clusterSize = vol->bytesPerCluster();   // Größe eines Clusters

    freeSpace = (uint64_t)freeClusters * clusterSize;                // Freier Speicher in Bytes
    usedSpace = ((uint64_t)totalClusters * clusterSize) - freeSpace; // Belegter Speicher
}

const char *SDCardModule::formatSize(uint64_t bytes)
{
    static char output[20];
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    float size = bytes;
    int unitIndex = 0;

    while (size >= 1024.0f && unitIndex < 4)
    {
        size /= 1024.0f;
        unitIndex++;
    }

    snprintf(output, sizeof(output), "%.1f %s", size, units[unitIndex]);
    return output;
}

SDCardModule sdCardModule(PIN_SDCARD_CS); // ToDo: CS Pin for SD card module ?, not obtain them from device configuration
