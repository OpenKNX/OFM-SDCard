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
    logDebugP("Setup...");
    _mounted = false;
    _cardInserted = false;
    _mountStep = MOUNT_STEP_INIT;
}

/** */
void SDCardModule::loop(bool configured)
{
    if (delayCheck(_cardDetectTimer, 500)) // CHeck every 500ms for card detection
    {
        _cardDetectTimer = millis();
        bool currentCardInserted = isCardInserted();
        if (currentCardInserted != _cardInserted)
        {
            _cardInserted = currentCardInserted;
            if (_cardInserted)
            {
                logInfoP("SD card inserted. Starting initialization...");
                _cardMountTimer = millis();
                _mountStep = MOUNT_STEP_INIT; // Reset the state machine
            }
            else
            {
                logInfoP("SD card removed.");
                Unmount(true); // Force unmount the card
                _cardMountTimer = 0;
                _mountStep = MOUNT_STEP_INIT; // Reset the state machine
            }
        }
    }
    if (_cardInserted && _cardMountTimer > 0 && delayCheck(_cardMountTimer, 3000))
    {
        if (!_mounted && delayCheck(_mountTimer, 1000)) // Check every 1s for mounting
        {
            if (_mount())
            {
                _cardMountTimer = 0; // Mount-Timer zurücksetzen
            }
            _mountTimer = millis(); // Reset timer for next check
        }
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
            if (!isCardInserted() || !_mounted)
            {
                logErrorP("No SD card inserted or mounted!");
                return false;
            }
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
            if (!isCardInserted() || !_mounted)
            {
                logErrorP("No SD card inserted or mounted!");
                return false;
            }
            logInfoP("SD-Card Files:");
            String path = command.substr(7).c_str();
            path = path.length() == 0 ? "/" : path;
            std::vector<String> files = getFileList(path.c_str());
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
                if (Statistics(path.c_str(), file.c_str(), stat))
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
                if (Statistics(path.c_str(), dir.c_str(), stat))
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
                if (Statistics(path.c_str(), file.c_str(), stat))
                {
                    const String type = "File";
                    char formattedTime[25];
                    strftime(formattedTime, sizeof(formattedTime), "%H:%M:%S %d.%m.", localtime(&stat.ctime));
                    sprintf(formattedTime + strlen(formattedTime), "%02d", (localtime(&stat.ctime)->tm_year + 1900) % 100);
                    totalSize += stat.size;
                    openknx.logger.logWithValues("%-41s | %-12s | %-6s | %-20s",
                                                 // file.c_str(),
                                                 String((file.length() > 41) ? file.substring(0, 38) + "..." : file).c_str(),
                                                 String(formatSize((uint64_t)stat.size)).c_str(), type.c_str(), formattedTime);
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
                                         String("Size: " + String(formatSize((uint64_t)totalSize))).c_str());
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
            std::vector<String> files = getFileList(path.length() > 0 ? path.c_str() : "/");
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
} // end of processCommand

/**
 * @brief State machine to mount the SD-Card.
 *
 * This function implements a step-by-step process to initialize and mount the SD-Card.
 * It ensures that the card is properly initialized, detected, and its volume is prepared for use.
 * The function will return `true` once the card is successfully mounted, or `false` if the process is still ongoing or fails.
 *
 * **Important:** Do not call this function directly. Use the `Mount()` function instead to trigger the mounting process.
 *
 * ### State Machine Process:
 * 1. **MOUNT_STEP_INIT**: Initialize the SPI interface for the SD-Card.
 * 2. **MOUNT_STEP_DETECT**: Detect the presence of the SD-Card.
 * 3. **MOUNT_STEP_CARD_BEGIN**: Start the SD-Card initialization process.
 * 4. **MOUNT_STEP_VOLUME_BEGIN**: Initialize the volume (file system) on the SD-Card.
 * 5. **MOUNT_STEP_FINISHED**: Successfully mount the SD-Card and reset the state machine.
 * 6. **MOUNT_STEP_ERROR**: Handle errors during the mounting process.
 *
 * @return true if the SD-Card is successfully mounted.
 * @return false if the SD-Card is not yet mounted or an error occurred.
 */
bool SDCardModule::_mount()
{
    if (_mountStep == MOUNT_ERROR_STATE && _cardInserted) return false;
    switch (_mountStep)
    {
        case MOUNT_STEP_INIT:
            logInfoP("Initializing SPI for SD-Card...");
            SPI_SD.begin(PIN_SDCARD_SCK, PIN_SDCARD_MISO, PIN_SDCARD_MOSI, PIN_SDCARD_CS);
            _mountStep = MOUNT_STEP_DETECT;
            return false;

        case MOUNT_STEP_DETECT:
            if (!isCardInserted())
            {
                logErrorP("SD-Card is not inserted! Please insert the SD-Card and try again.");
                _mountStep = MOUNT_STEP_ERROR;
                return false;
            }
            _mountStep = MOUNT_STEP_CARD_BEGIN;
            return false;

        case MOUNT_STEP_CARD_BEGIN:
            if (!_sd.cardBegin(sdConfig))
            {
                logErrorP("SD-Card initialization failed!");
                _mountStep = MOUNT_STEP_ERROR;
                return false;
            }
            logInfoP("SD-Card initialized.");
            _mountStep = MOUNT_STEP_VOLUME_BEGIN;
            return false;

        case MOUNT_STEP_VOLUME_BEGIN:
            if (!_sd.volumeBegin())
            {
                logErrorP("Volume initialization failed! Please check format (FAT16, FAT32, exFAT).");
                _mountStep = MOUNT_STEP_ERROR;
                return false;
            }
            logInfoP("Volume initialized.");
            _mountStep = MOUNT_STEP_FINISHED;
            return false;

        case MOUNT_STEP_FINISHED:
            _mounted = true;
            _mountStep = MOUNT_STEP_INIT; // Reset the mount step
            logInfoP("SD-Card successfully mounted!");
            //logInfoP("Manufacturer: %s", getManufacturer().c_str());
            //logInfoP("Type: %s", getCardType().c_str());
            //logInfoP("FS Type: %s", getFsType().c_str());
            //logInfoP("Size: %s", formatSize((uint64_t)_sd.card()->sectorCount() * 512));
            info();
            return true;

        case MOUNT_STEP_ERROR:
            logErrorP("Failed to mount the SD-Card!");
            logErrorP("Error code: %X (Data: %X)", _sd.card()->errorCode(), _sd.card()->errorData());
            logErrorP("Mounting aborted. SD-Card is in an error state. Remove and reinsert the card to retry.");
            _mountStep = MOUNT_ERROR_STATE;
            return true;

        case MOUNT_ERROR_STATE:
            return false;
    }
    return false;
}

/**
 * @brief Unmount the SD-Card
 *
 * @param force will force the unmounting of the SD-Card and ignore the busy state
 * @return true if the SD-Card was unmounted successfully
 */
bool SDCardModule::Unmount(bool force)
{
    if (!force && !_mounted)
    {
        logDebugP("SD-Card is not mounted!");
        return false;
    }

    if (!force && _sd.card()->isBusy())
    {
        logErrorP("SD-Card is busy. Cannot unmount the card! Please try again later.");
        return false;
    }

    _sd.end();
    logInfoP("SD-Card unmounted!");
    SPI_SD.end();
    logDebugP("SPI for SD-Card closed!");
    _mounted = false;
    _mountStep = MOUNT_STEP_INIT; // Reset the mount step
    _cardInserted = false;
    return true;
}

/**
 * @brief Mount the SD-Card - Triggers the mounting sequenc, if not mounted!
 *
 * @return true if remounting sequence was triggered
 */
bool SDCardModule::Mount()
{
    if (_mounted)
    {
        logDebugP("SD-Card is already mounted!");
        return true;
    }

    // Reset settings to remount the card
    _mounted = false;
    _mountStep = MOUNT_STEP_INIT;
    _cardInserted = false;
    logInfoP("Remounting triggered...");
    return true;
}

/**
 * @brief Remount the SD-Card, which will force the card to be unmounted and mounted again
 *
 */
void SDCardModule::ReMount()
{
    Unmount(true);
    Mount();
}

BootSectorInfo SDCardModule::getBootSectorInfo(int fsType)
{
    if (!isCardInserted() || !_mounted)
    {
        logErrorP("No SD card inserted or mounted!");
        return BootSectorInfo();
    }
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
    if (!isCardInserted())
    {
        logErrorP("No SD card inserted!");
        return false;
    }
    logInfoP("Formatting the SD card...");
    if (_sd.format())
    {
        logInfoP("SD card formatted successfully!");
        Unmount();
        return true;
    }
    else
    {
        logErrorP("Failed to format the SD card!");
        return false;
    }
}

/**
 * @brief Quick format the SD card with deleting the MBR
 *
 */
void SDCardModule::quickFormat()
{
    if (!isCardInserted())
    {
        logErrorP("No SD card inserted!");
        return;
    }
    uint8_t emptyMBR[512] = {0};
    if (!_sd.card()->writeSector(0, emptyMBR))
    {
        logErrorP("Error writing MBR sector!");
        logErrorP("Quick formatting failed!");
        return;
    }
    logInfoP("MBR deleted. Card must be repartitioned.");
}

/**
 * @brief Low-level format the SD card
 *        ATTENTION: This will erase all data on the SD card!
 */
void SDCardModule::lowLevelFormat()
{
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

/**
 * @brief Show the SD card information on the console
 *
 */
bool SDCardModule::info()
{
    if (!_mounted) return false;

    openknx.logger.begin();
    openknx.logger.log(""); // Empty line for spacing
    openknx.logger.color(CONSOLE_HEADLINE_COLOR);
    openknx.logger.log("============================= SD Card Information =============================");
    openknx.logger.color(0);
    cid_t cid;
    if (_sd.card()->readCID(&cid))
    {
        openknx.logger.logWithValues("| Manufacturer ID        | %-50s |", getManufacturer(cid).c_str());

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
    openknx.logger.log("-------------------------------------------------------------------------------");

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
    //     openknx.logger.log("-------------------------------------------------------------------------------");
    // }

    uint64_t freeBytes, usedBytes, totalBytes = getSDCardSize();
    if (totalBytes > 0)
    {
        getSDCardUsage(freeBytes, usedBytes);
        float usedPercentage = (float)usedBytes * 100.0f / totalBytes;
        float freePercentage = 100.0f - usedPercentage;
        int usedBarLength = (int)(usedPercentage * 0.5f);
        int freeBarLength = 50 - usedBarLength;
        char usedBar[51] = {0};
        char freeBar[51] = {0};

        memset(usedBar, '=', usedBarLength);
        memset(freeBar, '=', freeBarLength);

        openknx.logger.logWithValues("| Card Type              | %-50s |", getCardType().c_str());
        openknx.logger.logWithValues("| File System            | %-50s |", getFsType().c_str());
        openknx.logger.logWithValues("| Card capacity          | %-50s |", formatSize(totalBytes));
        openknx.logger.log("-------------------------------------------------------------------------------");
        openknx.logger.color(CONSOLE_HEADLINE_COLOR);
        openknx.logger.logWithValues("| Used: %-10s [%-50s] %.2f%%", formatSize(usedBytes), usedBar, usedPercentage);
        openknx.logger.logWithValues("| Free: %-10s [%-50s] %.2f%%", formatSize(freeBytes), freeBar, freePercentage);

        openknx.logger.color(0);
        openknx.logger.log("-------------------------------------------------------------------------------");
    }
    else
    {
        openknx.logger.log("| Error                  | Unable to read SD card size                      |");
    }
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

/** 
 * @brief Create a directory.
 *
 * This function creates a directory at the specified path.
 *
 * @param path The path to the directory.
 * @return True if the directory is successfully created, false otherwise.
 */
bool SDCardModule::mkdir(const char *path)
{
    return _sd.mkdir(path);
}

/** 
 * @brief Remove a directory.
 *
 * This function removes a directory at the specified path.
 *
 * @param path The path to the directory.
 * @return True if the directory is successfully removed, false otherwise.
 */
bool SDCardModule::rmdir(const char *path)
{
    return _sd.rmdir(path);
}

/** 
 * @brief Get the list of files in a directory.
 *
 * @param path The path to the directory.
 * @return A vector of strings containing the list of files in the directory.
 */
std::vector<String> SDCardModule::getFileList(const char *path)
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

/**
 * @brief Convert FAT date and time to Unix timestamp.
 *
 * This function converts a FAT date and time to a Unix timestamp.
 *
 * @param fatDate The FAT date.
 * @param fatTime The FAT time.
 * @return The Unix timestamp.
 */
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
bool SDCardModule::Statistics(const char *folder, const char *path, FileInfo &info)
{
    FsFile file;
    String fullPath = String(folder) + "/" + String(path);
    if (!file.open(fullPath.c_str(), O_RDONLY))
    {
        logErrorP("Failed to open the file: %s", path);
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

/**
 * @brief Get the size of the SD card.
 *
 * This function retrieves the size of the SD card in bytes.
 *
 * @return The size of the SD card in bytes.
 */
uint64_t SDCardModule::getSDCardSize()
{
    uint32_t sectorCount = _sd.card()->sectorCount(); // Anzahl der Sektoren
    uint16_t sectorSize = 512;                        // Standard bei SD-Karten (kann aber variieren)
    return (uint64_t)sectorCount * sectorSize;        // Größe in Bytes
}

/**
 * @brief Get the usage of the SD card.
 *
 * This function retrieves the usage of the SD card in terms of free and used space.
 *
 * @param freeSpace The variable to store the free space on the SD card.
 * @param usedSpace The variable to store the used space on the SD card.
 */
void SDCardModule::getSDCardUsage(uint64_t &freeSpace, uint64_t &usedSpace)
{
    FsVolume *vol = _sd.vol();
    uint32_t freeClusters = vol->freeClusterCount(); // Anzahl freier Cluster
    uint32_t totalClusters = vol->clusterCount();    // Gesamtanzahl Cluster
    uint32_t clusterSize = vol->bytesPerCluster();   // Größe eines Clusters

    freeSpace = (uint64_t)freeClusters * clusterSize;                // Freier Speicher in Bytes
    usedSpace = ((uint64_t)totalClusters * clusterSize) - freeSpace; // Belegter Speicher
}

/**
 * @brief Format a size in bytes to a human-readable format.
 *
 * This function formats a size in bytes to a human-readable format (e.g., KB, MB, GB, etc.).
 *
 * @param bytes The size in bytes.
 * @return A string representing the formatted size.
 */
const char *SDCardModule::formatSize(uint64_t bytes)
{
    static char output[20];
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    int unitIndex = 0;

    double size = bytes;
    for (; size >= 1024.0 && unitIndex < 4; size /= 1024.0, ++unitIndex)
        ;

    snprintf(output, sizeof(output), "%.2f %s", size, units[unitIndex]);
    return output;
}

/**
 * @brief Get the type of the SD card.
 *
 * @return The type of the SD card.
 */
String SDCardModule::getCardType()
{
    const char *cardTypeStr[] = {"Unknown", "SD (Standard)", "SDHC (High Capacity)", "SDXC (Extended Capacity)"};
    return cardTypeStr[_sd.card()->type()] ? cardTypeStr[_sd.card()->type()] : "Unknown Type";
}

/**
 * @brief Get the file system type of the SD card.
 *
 * @return The file system type of the SD card.
 */
String SDCardModule::getFsType()
{
    if (!_mounted) return "error";
    return _sd.fatType() == FAT_TYPE_EXFAT   ? "exFAT"
           : _sd.fatType() == FAT_TYPE_FAT32 ? "FAT32"
           : _sd.fatType() == FAT_TYPE_FAT16 ? "FAT16"
                                             : "Unknown";
}

/**
 * @brief Get the manufacturer of the SD card.
 *
 * @return The manufacturer of the SD card.
 */
String SDCardModule::getManufacturer(cid_t cid)
{
    static const std::unordered_map<uint8_t, std::string> manufacturers = {
        {0x01, "Panasonic"}, {0x41, "ADATA"}, {0x75, "Cactus"}, {0x95, "Extrememory"}, {0x02, "Toshiba"}, {0x45, "Patriot"}, {0x80, "OCZ"}, {0x96, "Dane-Elec"}, {0x03, "SanDisk"}, {0x50, "PNY"}, {0x85, "Kingmax"}, {0x97, "Jenoptik"}, {0x1B, "Samsung"}, {0x55, "Verbatim"}, {0x90, "Apacer"}, {0x98, "Platinum"}, {0x1D, "Kingston"}, {0x5A, "Integral"}, {0x91, "Hama"}, {0x99, "Navigon"}, {0x1E, "Transcend"}, {0x60, "Emtec"}, {0x92, "TakeMS"}, {0xA1, "CDA GmbH"}, {0x28, "Lexar"}, {0x65, "GoodRAM"}, {0x93, "Infineon"}, {0xA2, "ATP Electronics"}, {0x31, "Sony"}, {0x70, "Silicon Power"}, {0x94, "Mustang"}, {0xA3, "Delkin Devices"}};
    cid_t _cid = cid;
    if (_cid.mid == 0 && !_sd.card()->readCID(&_cid)) return "Unknown";
    auto it = manufacturers.find(_cid.mid);
    return (it != manufacturers.end()) ? String(it->second.c_str()) : "Unknown";
}

/**
 * @brief Check if the SD card is inserted.
 *
 * @return True if the SD card is inserted, false otherwise.
 */
bool SDCardModule::isCardInserted()
{
    // ToDo Hardware Config for CD Pin
    return digitalRead(PIN_SDCARD_CD) == LOW;
}

SDCardModule sdCardModule(PIN_SDCARD_CS); // ToDo: CS Pin for SD card module ?, not obtain them from device configuration
