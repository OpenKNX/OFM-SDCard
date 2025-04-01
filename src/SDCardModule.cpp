#ifdef OPENKNX_SD_CARD_MODULE_ENABLE

    #include "SDCardModule.h"

    #if !defined(SDCARD_SPI_INTERFACE) || \
        !defined(PIN_SDCARD_CS) ||        \
        !defined(PIN_SDCARD_CD) ||        \
        !defined(PIN_SDCARD_SCK) ||       \
        !defined(PIN_SDCARD_MISO) ||      \
        !defined(PIN_SDCARD_MOSI)
        #pragma message("\nSD-Card SPI Interface not defined in HardwareConfig.\nPlease define the following in HardwareConfig:\n  - SDCARD_SPI_INTERFACE \n  - PIN_SDCARD_CS \n  - PIN_SDCARD_CD \n  - PIN_SDCARD_SCK \n  - PIN_SDCARD_MISO \n  - PIN_SDCARD_MOSI \n\n\n\n")
        #pragma GCC error "SD-Card SPI Interface definitions are missing. Compilation aborted."
    #endif

    #ifdef ARDUINO_ARCH_RP2040
        #define SPI_SD SDCARD_SPI_INTERFACE
SdSpiConfig sdConfig(PIN_SDCARD_CS, DEDICATED_SPI, SD_SCK_MHZ(50), &SPI_SD);
    #elif defined(ARDUINO_ARCH_ESP32)
SPIClass SPI_SD(SDCARD_SPI_INTERFACE);
SdSpiConfig sdConfig(PIN_SDCARD_CS, DEDICATED_SPI, SD_SCK_MHZ(50), &SPI_SD);
    #endif

/**
 * @brief Construct a new SDCardModule::SDCardModule object
 *
 * @param csPin
 */
SDCardModule::SDCardModule(uint8_t csPin) : _chipSelectPin(csPin), _sd(SDFAT_()) {}

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
    #ifdef ARDUINO_ARCH_RP2040
    if (SPI_SD.setSCK(PIN_SDCARD_SCK) && SPI_SD.setRX(PIN_SDCARD_MISO) && SPI_SD.setTX(PIN_SDCARD_MOSI) && SPI_SD.setCS(PIN_SDCARD_CS))
    {
        logDebugP("SPI Pins set.");
    }
    else
    {
        logErrorP("Error setting SPI Pins.");
    }
    #endif
}

/**
 * @brief Setup the SD-Card Module
 *
 * @param configured
 */
void SDCardModule::setup(bool configured)
{
    logDebugP("Setup...");
    _cardChanged = isCardInserted();
    if (_cardChanged) // Only initial on bootup!
    {
        logInfoP("A inserted SD-Card is detected. Initializing...");
        _cardMountTimer = millis();
        _mountStep = MOUNT_STEP_CARD_INSERTED;
    }
    else // Only on bootup
    {
        _mountStep = MOUNT_STATE_CARD_REMOVED;
        logDebugP("No SD-Card is inserted.");
    }
}

/**
 * @brief Check if the SD-Card is inserted or removed.
 *        On Insertion, the card will be mounted.
 *        On Removal, the card will be unmounted.
 */
void SDCardModule::loop(bool configured)
{
    if (delayCheck(_cardDetectTimer, 500)) // CHeck every 500ms for card detection
    {
        _cardDetectTimer = millis();
        bool currentCardInserted = isCardInserted();
        if (currentCardInserted != _cardChanged) // Detect card change
        {
            _cardChanged = currentCardInserted;
            _mountStep = MOUNT_STEP_CARD_CHANGED;
        }
        _mount(); // State machine for card operations
    }
}

/**
 * @brief show Help for the SD-Card Module
 *
 */
void SDCardModule::showHelp()
{
    openknx.console.printHelpLine("sdc", "SD Card Control Module. Use 'sdc ?' for more information.");
}

/**
 * @brief Process the help command for the SD-Card Module
 *
 * @param command  The command to process
 * @param diagnose External diagnose flag
 * @return true if the command was processed, false otherwise false
 */
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
            openknx.console.printHelpLine("sdc format", "ATTENTION: Will auto format (exFat|Fat32) the external SD card.");
            openknx.console.printHelpLine("sdc qformat", "ATTENTION: Quick Format the SD-Card");
            openknx.console.printHelpLine("sdc llf", "ATTENTION: Low-Level Format the SD-Card");
            openknx.console.printHelpLine("sdc rpi", "Read Partition Information of the SD-Card GPT or MBR");
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
            if (!isCardInserted())
            {
                logErrorP("No SD card inserted!");
                return false;
            }
            std::string answer = command.substr(10).c_str();
            if (answer.compare(" yes") == 0)
            {
                logInfoP("Ok! You know the consequences. Formatting the SD card...");
                format();
            }
            else
            {
                logInfoP("Formatting the SD card will erase all data on the card. Are you sure you sure really want to do this?");
                logInfoP("Type 'sdc format yes' to do so.");
            }
        }
        else if (command.compare(4, 3, "llf") == 0)
        {
            if (!isCardInserted())
            {
                logErrorP("No SD card inserted!");
                return false;
            }
            std::string answer = command.substr(7);
            if (answer.compare(" yes") == 0)
            {
                logInfoP("Ok! You know the consequences. Low-Level Formatting the SD card...");
                lowLevelFormat();
            }
            else
            {
                logInfoP("Low-Level Formatting the SD card will erase all data on the card. Are you really sure you want to do this?");
                logInfoP("Type 'sdc llf yes' to do so.");
            }
        }
        else if (command.compare(4, 3, "rpi") == 0) // Read partition information
        {
            if (!isCardInserted())
            {
                logErrorP("No SD card inserted!");
                return false;
            }
            readPartitionInfo();
        }
        else if (command.compare(4, 7, "qformat") == 0)
        {
            std::string answer = command.substr(11).c_str();
            if (answer.compare(" yes") == 0)
            {
                logInfoP("Ok! You know the consequences. Quick Formatting the SD card...");
                quickFormat();
            }
            else
            {
                logInfoP("Quick Formatting the SD card will erase all data on the card. Are you sure you want to continue?");
                logInfoP("Type 'sdc qformat yes' to do so.");
            }
        }
        else if (command.compare(4, 4, "add ") == 0)
        {
            if (!isCardInserted() || !isMounted())
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
            if (!isCardInserted() || !isMounted())
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
            if (!isCardInserted() || !isMounted())
            {
                logErrorP("No SD card inserted or mounted!");
                return false;
            }
            String path = command.substr(7).c_str();
            std::vector<String> files = getFileList(path.length() > 0 ? path.c_str() : "/");
            for (String file : files)
            {
                logInfoP("%s", file.c_str());
            }
        }
        else if (command.compare(4, 6, "mkdir ") == 0)
        {
            if (!isCardInserted() || !isMounted())
            {
                logErrorP("No SD card inserted or mounted!");
                return false;
            }
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
            if (!isCardInserted() || !isMounted())
            {
                logErrorP("No SD card inserted or mounted!");
                return false;
            }
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
            if (!isCardInserted() || !isMounted())
            {
                logErrorP("No SD card inserted or mounted!");
                return false;
            }
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
            if (!isCardInserted() || !isMounted())
            {
                logErrorP("No SD card inserted or mounted!");
                return false;
            }
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
            if (!isCardInserted() || !isMounted())
            {
                logErrorP("No SD card inserted or mounted!");
                return false;
            }
            String fileName = command.substr(9, command.find(' ', 9) - 9).c_str();
            String content = command.substr(command.find(' ', 9) + 1).c_str();
            if (fileName.length() > 0 && content.length() > 0)
            {
                FSFILE file = open(fileName.c_str(), "w");
                if (!file)
                {
                    file = open(fileName.c_str(), "w");
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
            if (!isCardInserted() || !isMounted())
            {
                logErrorP("No SD card inserted or mounted!");
                return false;
            }
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

bool SDCardModule::_inMountingProcess()
{
    return ( // Die zwischenschritte dürfen wir hier an der stelle nicht stören
        _mountStep == MOUNT_STEP_CARD_INSERTED ||
        _mountStep == MOUNT_STEP_INIT ||
        _mountStep == MOUNT_STEP_DETECT ||
        _mountStep == MOUNT_STEP_CARD_BEGIN ||
        _mountStep == MOUNT_STEP_VOLUME_BEGIN ||
        _mountStep == MOUNT_STEP_MOUNT);
}
bool SDCardModule::_inUnmountingProcess()
{
    return (_mountStep == MOUNT_STEP_UNMOUNT);
}

/**
 * @brief State machine to mount the SD-Card.
 *
 * This function implements a step-by-step process to initialize and mount the SD-Card.
 * It ensures that the card is properly initialized, detected, and its volume is prepared for use.
 * The function will return `true` once the card is successfully mounted, or `false` if the process is still ongoing or fails.
 *
 * **Important:** Do not call this function directly. Use the `Mount()/Unmount()` functions instead to trigger the (un)mounting processes.
 *
 * ### State Machine Process:
 * 1. **Card Changed**: Detects if the card has been inserted or removed.
 * 2. **Card Inserted**: Waits for a few seconds to ensure the card is stable before proceeding.
 * 3. **Initialize**: Initializes the SPI interface for the SD-Card.
 * 4. **Detect**: Checks if the card is inserted.
 * 5. **Card Begin**: Initializes the SD-Card.
 * 6. **Volume Begin**: Initializes the volume on the SD-Card.
 * 7. **Mount**: Mounts the volume and prepares it for use.
 * 8. **Unmount**: Unmounts the volume and prepares for removal.
 * 9. **Error**: Handles any errors that occur during the mounting process.
 * 10. **Mounted**: Indicates that the card is successfully mounted and ready for use.
 * 11. **Unmounted**: Indicates that the card is unmounted and not ready for use.
 * 12. **Card Removed**: Indicates that the card has been removed.
 *
 * @return true if the SD-Card is successfully mounted.
 * @return false if the SD-Card is not yet mounted or an error occurred.
 */
void SDCardModule::_mount()
{
    if (_mountStep == MOUNT_STATE_ERROR && _cardChanged) return;
    switch (_mountStep)
    {
        case MOUNT_STEP_CARD_CHANGED:
        {
            if (isCardInserted() && _inMountingProcess()) return; // Card Inserted! Do nothing! We are in mount process

            if (isCardRemoved() && _inUnmountingProcess()) return; // Card Removed! Do nothing! We are in unmount process

            if (isCardInserted() && _mountStep != MOUNT_STATE_MOUNTED)
            {
                logInfoP("SD-Card Inserted.");
                _cardMountTimer = millis();
                _mountStep = MOUNT_STEP_CARD_INSERTED;
                return;
            }

            if (isCardRemoved() && (_mountStep != MOUNT_STATE_UNMOUNTED))
            {
                if (_inMountingProcess())
                    logInfoP("SD-Card is removed. Stopping the mounting process...");
                else
                    logInfoP("SD-Card is removed. Unmounting the card...");
                _mountStep = MOUNT_STEP_UNMOUNT;
            }
        }
        break;

        case MOUNT_STEP_CARD_INSERTED:
        {
            if (isCardInserted() &&
                (_cardMountTimer > 0 && delayCheck(_cardMountTimer, 3000)))
            {
                logInfoP("Mounting SD-Card...");
                _cardMountTimer = 0; //?
                _mountStep = MOUNT_STEP_INIT;
            }
        }
        break;

        case MOUNT_STEP_INIT:
        {
            logDebugP("Initializing SPI for SD-Card...");
    #ifdef ARDUINO_ARCH_RP2040
            SPI_SD.begin(true);
    #else
            SPI_SD.begin(PIN_SDCARD_SCK, PIN_SDCARD_MISO, PIN_SDCARD_MOSI, PIN_SDCARD_CS);
    #endif
            _mountStep = MOUNT_STEP_DETECT;
        }
        break;

        case MOUNT_STEP_DETECT:
        {
            if (!isCardInserted())
            {
                logErrorP("SD-Card is not inserted! Please insert the SD-Card and try again.");
                _mountStep = MOUNT_STEP_ERROR;
                return;
            }
            _mountStep = MOUNT_STEP_CARD_BEGIN;
        }
        break;

        case MOUNT_STEP_CARD_BEGIN:
        {
            if (!_sd.cardBegin(sdConfig))
            {
                logErrorP("SD-Card initialization failed!");
                _mountStep = MOUNT_STEP_ERROR;
                return;
            }
            logInfoP("SD-Card initialized.");
            _mountStep = MOUNT_STEP_VOLUME_BEGIN;
        }
        break;

        case MOUNT_STEP_VOLUME_BEGIN:
        {
            if (!_sd.volumeBegin())
            {
                logErrorP("Volume initialization failed! Please check file system format!");
                logErrorP(" -- Supported formats: %s --", FS_SUPPORT_FORMATS);
                _mountStep = MOUNT_STEP_ERROR;
                return;
            }
            logInfoP("Volume initialized.");
            _mountStep = MOUNT_STEP_MOUNT;
        }
        break;

        case MOUNT_STEP_MOUNT:
        {
            _mountStep = MOUNT_STATE_MOUNTED; // Reset the mount step
            logInfoP("SD-Card successfully mounted!");
            // logInfoP("Manufacturer: %s", getManufacturer().c_str());
            // logInfoP("Type: %s", getCardType().c_str());
            // logInfoP("FS Type: %s", getFsType().c_str());
            // logInfoP("Size: %s", formatSize((uint64_t)_sd.card()->sectorCount() * 512));
            info(); // ToDo: info takes sometime to long to show the info!
        }
        break;

        case MOUNT_STEP_UNMOUNT:
        {
            logInfoP("Unmounting SD-Card...");
            Unmount(true);
        }
        break;

        case MOUNT_STEP_ERROR:
        {
            logErrorP("Failed to mount the SD-Card!");
            logErrorP("Error code: %X (Data: %X)", _sd.card()->errorCode(), _sd.card()->errorData());
            logErrorP("Mounting aborted. SD-Card is in an error state. Remove and reinsert the card to retry.");
            _mountStep = MOUNT_STATE_ERROR;
        }
        break;

        // ToDo: Add also the formatting processes here
        //
        case MOUNT_STATE_ERROR:        // Card is in error state
        case MOUNT_STATE_CARD_REMOVED: // Card is removed
        case MOUNT_STATE_UNMOUNTED:    // Card could not be mounted
        case MOUNT_STATE_MOUNTED:      // Card is mounted
            break;

        default:
            logErrorP("Unknown state in SD-Card process!");
            _mountStep = MOUNT_STEP_ERROR;
            break;
    }
    return;
}

/**
 * @brief Unmount the SD-Card
 *
 * @param force will force the unmounting of the SD-Card and ignore the busy state
 * @return true if the SD-Card was unmounted successfully
 */
bool SDCardModule::Unmount(bool force)
{
    if (!force && !isMounted())
    {
        logDebugP("SD-Card is not mounted!");
        return true;
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
    _mountStep = MOUNT_STATE_UNMOUNTED; // Reset the mount step
    return true;
}

/**
 * @brief Mount the SD-Card - Triggers the mounting sequenc, if not mounted!
 *
 * @return true if remounting sequence was triggered
 */
bool SDCardModule::Mount()
{
    if (isMounted())
    {
        logDebugP("SD-Card is already mounted!");
        return true;
    }
    if (!isCardInserted())
    {
        logErrorP("SD-Card is not inserted! Please insert the SD-Card and try again.");
        return false;
    }

    _mountStep = MOUNT_STEP_CARD_INSERTED; // Reset the state machine
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
    if (!isCardInserted())
    {
        logErrorP("No SD card inserted!");
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
    FSFILE file = open("bootsector.bin", "w");
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
    if (!isMounted())
    {
        logErrorP("No SD card mounted!");
        return false;
    }
    return _sd.rename(oldPath, newPath);
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
    if (!Unmount())
    {
        logErrorP("Unmounting the SD card failed! Aborting format.");
        return false;
    }

    logInfoP("Formatting the SD card...");
    logInfoP("The formatting process may take a few time.");
    logInfoP(" --- PLEASE WAIT --- ");
    if (_sd.format())
    {
        logInfoP(" --- FORMATTING SUCCESSFUL --- ");
        logInfoP("Formatting completed successfully!");
        logInfoP("The SD card is now formatted.");
        logInfoP("SD card formatted successfully!");
        logInfoP("Triggering remount...");
        ReMount();
        return true;
    }
    else
    {
        logErrorP("!!! ERROR DURING FORMATTING !!!");
        logErrorP("Failed to format the SD card!");
        logErrorP("Please format the SD card manually!");
        logErrorP("Supported formats: %s", FS_SUPPORT_FORMATS);
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
    if (!Unmount())
    {
        logErrorP("Unmounting the SD card failed! Aborting quick format.");
        return;
    }
    logInfoP("Quick formatting the SD card...");
    logInfoP("The quick formatting process may take a few time.");
    logInfoP(" --- PLEASE WAIT --- ");
    uint8_t emptyMBR[512] = {0};
    if (!_sd.card()->writeSector(0, emptyMBR))
    {
        logInfoP(" --- ERROR DURING QUICK FORMATTING --- ");
        logInfoP("Error writing MBR sector!");
        logInfoP("The SD card is not formatted!");
        logInfoP("Please format the SD card manually!");
        return;
    }
    logInfoP(" --- QUICK FORMATTING SUCCESSFUL --- ");
    logInfoP("Quick formatting completed successfully!");
    logInfoP("The MBR was deleted!");
    logInfoP("You need to format the SD card. Use the 'format' command.");
}

/**
 * @brief Low-level format the SD card
 *        ATTENTION: This will erase all data on the SD card!
 */
void SDCardModule::lowLevelFormat()
{
    if (!isCardInserted())
    {
        logErrorP("No SD card inserted!");
        return;
    }
    if (!Unmount())
    {
        logErrorP("Unmounting the SD card failed! Aborting low-level format.");
        return;
    }
    logInfoP("Low-Level formatting the SD card...");
    logInfoP("!! This will zero-fill the entire SD card !!");
    logInfoP("!! The low-level formatting process take a few time. !! ");
    logInfoP(" --- PLEASE WAIT --- ");
    uint8_t emptySector[512] = {0}; // Leerer Sektor mit 0x00

    for (uint32_t i = 0; i < _sd.card()->sectorCount(); i++)
    {
        if (!_sd.card()->writeSector(i, emptySector))
        {
            logErrorP(" --- ERROR DURING LOW-LEVEL FORMATTING --- ");
            logErrorP("Error writing sector: %lu", i);
            logErrorP("Low-Level formatting failed!");
            logErrorP("Please format the SD card manually!");
            logErrorP("Supported formats: %s", FS_SUPPORT_FORMATS);
            logErrorP("Low-Level formatting aborted!");
            return;
        }

        if (i % 100 == 0)
        {
            logInfoP("Low-Level formatting: %lu%%", i * 100 / _sd.card()->sectorCount());
        }
    }
    logInfoP(" --- LOW-LEVEL FORMATTING SUCCESSFUL --- ");
    logInfoP("Low-Level formatting completed successfully!");
    logInfoP("You need to format the SD card. Use the 'format' command.");
}

/**
 * @brief Returns the manufacturer of the SD card as a string
 *
 * @param partitionType The partition type of the SD card
 *
 */
String SDCardModule::getPartitionType(uint8_t partitionType)
{
    switch (partitionType)
    {
        case 0x07: return "exFAT/NTFS";
        case 0x0B: return "FAT32";
        case 0x0C: return "FAT32 (LBA)";
        case 0x0E: return "FAT16 (LBA)";
        case 0x0F: return "exFAT/NTFS (LBA)";
        case 0x05: return "Extended";
        case 0x83: return "Linux";
        case 0x82: return "Linux Swap";
        case 0x8E: return "Linux LVM";
        case 0xA5: return "FreeBSD";
        case 0xA6: return "OpenBSD";
        case 0xA9: return "NetBSD";
        case 0xAB: return "Mac OS X";
        case 0xAF: return "APFS (Apple)";
        case 0xEE: return "EFI (FAT)";
        default: return "Unknown";
    }
}

/**
 * @brief Read the partition information of the SD card (GPT or MBR)
 *
 * Console output: Displays the partition information of the SD
 *                 card (GPT or MBR) if available and detected.
 * Console command: `sdc rpi`
 */
void SDCardModule::readPartitionInfo()
{
    if (!isCardInserted())
    {
        logErrorP("No SD card inserted!");
        return;
    }

    uint8_t buffer[512];
    // Read sector 0 to determine if MBR or GPT is present
    if (!_sd.card()->readSector(0, buffer))
    {
        logErrorP("Error reading sector 0 (MBR or GPT protection)!");
        return;
    }

    bool readMBR = false;
    bool readGPT = false;

    // Check if MBR is present (signature in sector 0)
    if (buffer[0x1FE] == 0x55 && buffer[0x1FF] == 0xAA)
    {
        if (buffer[0x1BE + 4] == 0xEE)
        {
            logInfoP("GPT partition table detected via protective MBR!");
            readGPT = true;
        }
        else
            readMBR = true;
    }

    // Read MBR partition table
    if (readMBR)
    {
        logInfoP("MBR partition table detected!");
        openknx.logger.begin();
        openknx.logger.color(CONSOLE_HEADLINE_COLOR);
        openknx.logger.log("============================== MBR Partition Table =============================");
        openknx.logger.log("| Partition Count           | 4 (Valids will be listed below)");
        openknx.logger.log("================================================================================");
        openknx.logger.color(0);
        openknx.logger.log("-------------------------------------------------------------------------------");

        for (int i = 0; i < 4; i++) // 4 partition entries
        {
            int offset = 0x1BE + (i * 16); // MBR partition entries start at offset 0x1BE

            uint8_t bootFlag = buffer[offset];          // Boot Flag (0x80 = bootable)
            uint8_t partitionType = buffer[offset + 4]; // Partitionstype (0x0B = FAT32, 0x07 = exFAT/NTFS)
            uint32_t startSector = buffer[offset + 8] | (buffer[offset + 9] << 8) | (buffer[offset + 10] << 16) | (buffer[offset + 11] << 24);
            uint32_t totalSectors = buffer[offset + 12] | (buffer[offset + 13] << 8) | (buffer[offset + 14] << 16) | (buffer[offset + 15] << 24);

            // Calculate the width of the sector numbers
            int sectorWidth = 15;
            if (startSector > 999999) sectorWidth = 18;

            // Calculate the width of the partition type
            String partitionTypeString = getPartitionType(partitionType);
            int typeWidth = std::max(10, (int)partitionTypeString.length() + 5); // Minimum 15 characters for partition type

            openknx.logger.logWithValues("| Partition %-2d           | %-*u - %-*u", i + 1, sectorWidth, startSector, sectorWidth, startSector + totalSectors - 1);
            openknx.logger.logWithValues("| Bootflag               | 0x%-2X", bootFlag);
            openknx.logger.logWithValues("| Type                   | 0x%-2X - %-*s", partitionType, typeWidth, partitionTypeString.c_str());
            openknx.logger.log("-------------------------------------------------------------------------------");
        }
        openknx.logger.color(CONSOLE_HEADLINE_COLOR);
        openknx.logger.log("===============================================================================");
        openknx.logger.color(0);
        openknx.logger.end();
        return;
    }

    // Read GPT partition table ( GPT-Signatur is in sector 1)
    if (readGPT || memcmp(buffer, "EFI PART", 8) == 0)
    {
        // GPT partition table detected
        logInfoP("GPT partition table detected!");
        if (!_sd.card()->readSector(1, buffer))
        {
            logErrorP("Error reading GPT header (sector 1)!");
            return;
        }

        uint32_t partitionEntryLBA;
        memcpy(&partitionEntryLBA, &buffer[72], sizeof(partitionEntryLBA));

        uint32_t partitionCount;
        memcpy(&partitionCount, &buffer[80], sizeof(partitionCount));
        openknx.logger.begin();
        openknx.logger.color(CONSOLE_HEADLINE_COLOR);
        openknx.logger.log("============================== GPT Partition Table =============================");
        openknx.logger.logWithValues("| Partition Entry LBA       | %lu", partitionEntryLBA);
        openknx.logger.logWithValues("| Partition Count           | %u (Valids will be listed below)", partitionCount);
        openknx.logger.log("================================================================================");
        openknx.logger.color(0);
        openknx.logger.log("-------------------------------------------------------------------------------");
        uint32_t actualPartitionCount = 0;
        for (uint32_t i = 0; i < partitionCount; i++)
        {
            uint32_t sector = partitionEntryLBA + (i * 128) / 512;
            if (!_sd.card()->readSector(sector, buffer))
            {
                openknx.logger.logWithValues("Error reading GPT partition entry %d!", i + 1);
                continue;
            }

            uint8_t *entry = buffer + (i * 128) % 512;
            uint64_t firstLBA, lastLBA;
            memcpy(&firstLBA, &entry[32], sizeof(firstLBA));
            memcpy(&lastLBA, &entry[40], sizeof(lastLBA));

            if (firstLBA == 0 && lastLBA == 0)
            {
                continue; // Ungültige Partition ?
            }

            actualPartitionCount++;

            char partitionTypeGUID[37];
            snprintf(partitionTypeGUID, sizeof(partitionTypeGUID),
                     "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                     *(uint32_t *)&entry[0], *(uint16_t *)&entry[4], *(uint16_t *)&entry[6],
                     entry[8], entry[9], entry[10], entry[11], entry[12], entry[13], entry[14], entry[15]);

            // Calculate the width of the sector numbers
            int lbaWidth = 15;
            if (firstLBA > 999999) lbaWidth = 18;

            openknx.logger.logWithValues("| Partition %-2d              | %-*llu - %-*llu", actualPartitionCount, lbaWidth, firstLBA, lbaWidth, lastLBA);
            openknx.logger.logWithValues("| Partition GUID            | %-50s", partitionTypeGUID);

            if (i < partitionCount - 1) openknx.logger.log("-------------------------------------------------------------------------------");
        }
        openknx.logger.color(CONSOLE_HEADLINE_COLOR);
        openknx.logger.log("===============================================================================");
        openknx.logger.color(0);
        openknx.logger.end();
        return;
    }

    logErrorP("No valid partition table (GPT or MBR) found on SD card.");
}

/**
 * @brief Show the SD card information on the console
 * Console output: Displays the SD card information such as manufacturer, product
 *                 name, revision, serial number, etc.
 * COnsole command: `sdc info`
 */
bool SDCardModule::info()
{
    if (!isMounted())
    {
        logErrorP("No SD card mounted!");
        return false;
    }
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
    openknx.logger.logWithValues("| Card Type              | %-50s |", getCardType().c_str());
    openknx.logger.logWithValues("| File System            | %-50s |", getFsType().c_str());
    // #ifdef ARDUINO_ARCH_RP2040
    //  Need a solution to get the SD card size and usage on RP2040 with FAT32 file system!
    // #elif defined(ARDUINO_ARCH_ESP32) // && defined(SNUSNU)
    uint64_t freeBytes = 0, usedBytes = 0, totalBytes = getSDCardSize();
    if (totalBytes > 0)
    {
        getSDCardUsage(freeBytes, usedBytes);

        if (freeBytes == 0 && usedBytes == 0)
        {
            openknx.logger.log("| Error                  | Unable to retrieve SD card usage                 |");
        }
        else
        {
            float usedPercentage = (totalBytes > 0) ? ((float)usedBytes * 100.0f / totalBytes) : 0.0f;
            float freePercentage = 100.0f - usedPercentage;
            int usedBarLength = (int)(usedPercentage * 0.5f);

    #define BAR_LENGTH 48
            int freeBarLength = BAR_LENGTH - usedBarLength;

            char usedBar[BAR_LENGTH + 1] = {0};
            char freeBar[BAR_LENGTH + 1] = {0};

            memset(usedBar, '=', usedBarLength);
            memset(freeBar, '=', freeBarLength);

            openknx.logger.logWithValues("| Card capacity          | %-50s |", formatSize(totalBytes));
            openknx.logger.log("-------------------------------------------------------------------------------");
            openknx.logger.color(CONSOLE_HEADLINE_COLOR);
            openknx.logger.logWithValues("| Used: %-10s [%-*s] %6.2f%% |", formatSize(usedBytes), BAR_LENGTH, usedBar, usedPercentage);
            openknx.logger.logWithValues("| Free: %-10s [%-*s] %6.2f%% |", formatSize(freeBytes), BAR_LENGTH, freeBar, freePercentage);
            openknx.logger.color(0);
        }
    }
    else
    {
        openknx.logger.log("| Error                  | Unable to read SD card size                      |");
    }
    // #endif

    openknx.logger.log("-------------------------------------------------------------------------------");
    openknx.logger.end();
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
FSFILE SDCardModule::open(const char *path, const char *mode)
{
    if (!isMounted()) return FSFILE();
    oflag_t flags = 0;
    if (strcmp(mode, "r") == 0)
    {
        flags = O_RDONLY;
    }
    else if (strcmp(mode, "w") == 0) // FILE_WRITE
    {
        flags = O_WRONLY | O_CREAT | O_TRUNC;
    }
    else if (strcmp(mode, "a") == 0) // FILE_APPEND
    {
        flags = O_WRONLY | O_CREAT | O_APPEND;
    }
    else if (strcmp(mode, "r+") == 0) // FILE_READWRITE
    {
        flags = O_RDWR;
    }
    else if (strcmp(mode, "w+") == 0) // FILE_READWRITE
    {
        flags = O_RDWR | O_CREAT | O_TRUNC;
    }
    else if (strcmp(mode, "a+") == 0) // FILE_READWRITE
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
    if (!isMounted()) return false;
    FSFILE file = open(path, "w");
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
    if (!isMounted()) return false;
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
    if (!isMounted()) return false;
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
    if (!isMounted()) return 0;
    FSFILE file = open(path, "r");
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
    if (!isMounted()) return 0;
    FSFILE file = open(path, "w");
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
    if (!isMounted()) return 0;
    FSFILE file = open(path, "a");
    if (!file) return 0;
    size_t bytesAppended = file.write(buffer, size);
    file.close();
    return bytesAppended;
}

/** @brief Create a directory.
 *
 * This function creates a directory at the specified path.
 *
 * @param path The path to the directory.
 * @return True if the directory is successfully created, false otherwise.
 */
bool SDCardModule::mkdir(const char *path)
{
    if (!isMounted()) return false;
    return _sd.mkdir(path);
}

/** @brief Remove a directory.
 *
 * This function removes a directory at the specified path.
 *
 * @param path The path to the directory.
 * @return True if the directory is successfully removed, false otherwise.
 */
bool SDCardModule::rmdir(const char *path)
{
    if (!isMounted()) return false;
    return _sd.rmdir(path);
}

/**
 * @brief
 *
 *
 * @param path The path to the directory.
 * @return A vector of strings containing the list of files in the directory.
 */
std::vector<String> SDCardModule::getFileList(const char *path)
{
    if (!isMounted()) return std::vector<String>();
    std::vector<String> fileList;
    FSFILE dir = _sd.open(path);
    if (!dir) return fileList;
    char buffer[256];
    while (FSFILE file = dir.openNextFile())
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
    if (!isMounted()) return false;
    FSFILE file;
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
    if (!isCardInserted() || !isMounted() || !_sd.card()) return 0;
    uint32_t sectorCount = _sd.card()->sectorCount(); // Count of sectors
    return (uint64_t)sectorCount * 512;               // Sector size is (alywas?) 512 bytes
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
    FSVOlUME *vol = _sd.vol();
    if (!vol)
    {
        freeSpace = usedSpace = 0;
        return; // No volume ?
    }
    uint32_t freeClusters = vol->freeClusterCount(); // free clusters
    uint32_t totalClusters = vol->clusterCount();    // total clusters
    uint32_t clusterSize = vol->bytesPerCluster();   // bytes per cluster (always 512 bytes)

    if (freeClusters == 0xFFFFFFFF || totalClusters == 0)
    {
        freeSpace = usedSpace = 0;
        return; // Values are invalid ?
    }
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
    if (!isMounted()) return "error";
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

SDCardModule sdCardModule(PIN_SDCARD_CS); // ToDo: CS Pin for SD card module ?, not obtain them from device configuration

#endif // OPENKNX_SD_CARD_MODULE_ENABLE