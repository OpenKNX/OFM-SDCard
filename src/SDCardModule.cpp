#ifdef OPENKNX_SD_CARD_MODULE_ENABLE

    #include "SDCardModule.h"
    #ifdef OPENKNX_FTC
        #include "FileTransferClient.h" // self-register the SD storage backend with the FTC client
    #endif

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
// SdFat calls this on file create/modify to stamp the FAT/exFAT date+time. Pulls the current time
// from the OpenKNX time source (KNX/NTP); falls back to a fixed date until the clock is valid.
static void sdCardDateTimeCallback(uint16_t *date, uint16_t *time)
{
    if (openknx.time.isValid())
    {
        auto t = openknx.time.getLocalTime();
        *date = FS_DATE(t.year, t.month, t.day);
        *time = FS_TIME(t.hour, t.minute, t.second);
    }
    else
    {
        *date = FS_DATE(2026, 1, 1); // clock not set yet -> deterministic placeholder
        *time = FS_TIME(0, 0, 0);
    }
}

    #ifdef OPENKNX_FTC
// --- FTC storage backend: SD ("sd/") ------------------------------------------------------------
// Self-registered with the FTC client (see setup()), so the client needs no SD header. SD is SdFat
// (FSFILE: fileSize()/curPosition()/seekSet()) -- its own handles + method names, NOT the client's fs::File.
static FSFILE _ftcSdSrcFile;
static FSFILE _ftcSdSinkFile;

static int32_t sdOpen(const char *path)
{
    if (!sdCardModule.isMounted()) return -1;
    _ftcSdSrcFile = sdCardModule.open(path, "r");
    if (!_ftcSdSrcFile) return -1;
    return (int32_t)_ftcSdSrcFile.fileSize();
}
static uint8_t sdRead(uint32_t offset, uint8_t *buf, uint8_t len)
{
    if (!_ftcSdSrcFile || buf == nullptr || len == 0) return 0;
    if (_ftcSdSrcFile.curPosition() != offset && !_ftcSdSrcFile.seekSet(offset)) return 0;
    const int r = _ftcSdSrcFile.read(buf, len);
    return (r > 0) ? (uint8_t)r : 0;
}
static void sdClose() { _ftcSdSrcFile.close(); }

static bool sdSinkOpen(const char *path)
{
    if (!sdCardModule.isMounted()) return false;
    _ftcSdSinkFile = sdCardModule.open(path, "w"); // create / truncate
    return (bool)_ftcSdSinkFile;
}
static int sdSinkWrite(const uint8_t *buf, uint16_t len)
{
    if (!_ftcSdSinkFile || buf == nullptr || len == 0) return -1;
    return (int)_ftcSdSinkFile.write(buf, len);
}
static void sdSinkClose() { _ftcSdSinkFile.close(); }

static bool sdAvailable() { return sdCardModule.isCardInserted() && sdCardModule.isMounted(); }
// No cheap free-space API: getSDCardUsage() -> freeClusterCount() is O(FAT) (timed) and would stall the
// transfer loop -> register nullptr (skip the pre-write gate; a full card is still caught per-chunk write).
    #endif // OPENKNX_FTC

void SDCardModule::setup(bool configured)
{
    logDebugP("Setup...");
    // Stamp firmware-created files with the real time (SdFat otherwise uses a fixed default date).
    FsDateTime::setCallback(sdCardDateTimeCallback);
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

    #ifdef DEVICE_DISPLAY_MODULE
    WidgetSDCard *sdCardWidget = new WidgetSDCard(30000, WidgetFlags::DefaultWidget); // Create a new SD Card widget
    openknxDisplayModule.tryAddWidget(sdCardWidget); // Add the widget to the widget manager queue (safe no-op if absent).

    WidgetFileBrowser *fileBrowserWidget = new WidgetFileBrowser(
        4500, static_cast<WidgetFlags>(WidgetFlags::ManagedExternally | WidgetFlags::Background | WidgetFlags::WantsButtonInput));
    if (openknxDisplayModule.tryAddWidget(fileBrowserWidget)) // safe no-op (and delete) if absent.
        _fileBrowser = fileBrowserWidget;
    registerSdMenu();
    #endif // DEVICE_DISPLAY_MODULE

    #ifdef OPENKNX_FTC
    // Self-register the SD backend with the FTC client ("sd/..." paths). freeBytes = nullptr (see note above).
    openknxFileTransferClient.registerFileBackend("sd", {sdOpen, sdRead, sdClose},
                                                  {sdSinkOpen, sdSinkWrite, sdSinkClose}, sdAvailable, nullptr);
    #endif
}

/**
 * @brief Check if the SD-Card is inserted or removed.
 *        On Insertion, the card will be mounted.
 *        On Removal, the card will be unmounted.
 */
void SDCardModule::loop(bool configured)
{
    // Drive the non-blocking format first. While a format runs the card is unmounted and being
    // written sector-by-sector, so skip mount detection / browser / usage scan to avoid racing it
    // (a mid-format remount or scan would fight the sector writes). A write error aborts the format
    // (clears _fmtOp) and the next loop resumes normal handling.
    _serviceFormat();
    if (isFormatting())
        return;

    if (delayCheck(_cardDetectTimer, 500)) // Check every 500ms for card detection
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

    #ifdef DEVICE_DISPLAY_MODULE
    _serviceFileBrowser();
    #endif

    tickUsageScan(); // Non-blocking incremental free-cluster scan (if running)
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
            openknx.console.printHelpLine("sdc setlabel <name>", "Set the SD-Card volume label (relabel in place)");
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
        else if (command.compare(4, 1, "i") == 0 &&
                 (command.length() == 5 || command.compare(4, 4, "info") == 0))
        {
            // "sdc i" and "sdc info" both show the card information.
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
                requestFormat(1); // non-blocking (runs from loop)
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
                requestFormat(2); // non-blocking (runs from loop)
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
                requestFormat(0); // non-blocking (runs from loop)
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
        else if (command.compare(4, 2, "ll") == 0 && (command.length() == 6 || command[6] == ' '))
        {
            if (!isCardInserted() || !isMounted())
            {
                logErrorP("No SD card inserted or mounted!");
                return false;
            }
            logInfoP("SD-Card Files:");
            // "sdc ll", "sdc ll " and "sdc ll /" all list root; guard substr so bare "sdc ll" cannot throw.
            String path = (command.length() > 7) ? String(command.substr(7).c_str()) : String("/");
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
        else if (command.compare(4, 2, "ls") == 0 && (command.length() == 6 || command[6] == ' '))
        {
            if (!isCardInserted() || !isMounted())
            {
                logErrorP("No SD card inserted or mounted!");
                return false;
            }
            // "sdc ls", "sdc ls " and "sdc ls /" all list root; guard substr so bare "sdc ls" cannot throw.
            String path = (command.length() > 7) ? String(command.substr(7).c_str()) : String("/");
            std::vector<String> files = getFileList(path.length() > 0 ? path.c_str() : "/");
            for (String file : files)
            {
                openknx.logger.log(file.c_str());
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
            // "sdc cat /<file>": guard the length before substr() - a too-short command would make
            // std::string::substr(9) throw, and an uncaught exception reboots the ESP.
            if (command.length() <= 9)
            {
                logErrorP("Usage: sdc cat /<file>");
                return false;
            }
            logInfoP("Reading file and will show the first %d bytes of the file content.", OPENKNX_MAX_LOG_MESSAGE_LENGTH);
            String fileName = command.substr(9).c_str();
            uint8_t buffer[OPENKNX_MAX_LOG_MESSAGE_LENGTH];
            size_t bytesRead = read(fileName.c_str(), buffer, sizeof(buffer) - 1); // Reserve space for null terminator
            if (bytesRead >= sizeof(buffer)) bytesRead = sizeof(buffer) - 1;       // defensive: never index past buffer
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
        else if (command.compare(4, 9, "setlabel ") == 0)
        {
            if (!isCardInserted() || !isMounted())
            {
                logErrorP("No SD card inserted or mounted!");
                return false;
            }
            if (command.length() <= 13) // "sdc setlabel " is 13 chars
            {
                logErrorP("Usage: sdc setlabel <name>");
                return false;
            }
            std::string label = command.substr(13);
            if (label.size() >= 2 && label.front() == '"' && label.back() == '"')
                label = label.substr(1, label.size() - 2); // strip optional surrounding quotes
            if (setVolumeLabel(label.c_str()))
                logInfoP("Volume label set to \"%s\"", label.c_str());
            else
                logErrorP("Failed to set volume label");
            return true;
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
                _cardUnformatted = true; // card present but no usable FS -> "Bitte formatieren"
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
            _cardUnformatted = false;         // a valid volume is mounted -> clear the "needs Format" hint
            _sdInfoGeneration++; // Increment the generation counter to indicate that the SD-Card state has changed
            logInfoP("SD-Card successfully mounted!");
            resetCardInfo(); // Reset the card info to be sure we read the info again
            info();
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
    _sdInfoGeneration++; // Increment the generation counter to indicate that the SD-Card state has changed
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

// Request a non-blocking format op (0=Quick MBR wipe, 1=exFAT, 2=Low-Level zero-fill). The work runs
// incrementally from loop() (_serviceFormat), so the loop is never stalled / the watchdog stays fed.
// Ignored while a format is already running.
//
// IMPORTANT: Unmount() ends _sd AND closes the SPI bus (SPI_SD.end()), so raw sector writes and
// _sd.format() would fail ("MBR write error" / "no card"). We therefore fully unmount and then
// RE-INITIALISE the card only (re-open SPI + cardBegin, WITHOUT mounting a volume), giving the format
// a live card to work on.
void SDCardModule::requestFormat(uint8_t op)
{
    if (_fmtOp != FmtOp::None)
    {
        logInfoP("Format: already running (%u%%)", _fmtTotal ? (unsigned)((uint64_t)_fmtSector * 100 / _fmtTotal) : 0);
        return;
    }
    if (!isCardInserted())
    {
        logErrorP("No SD card inserted!");
        return;
    }

    Unmount(true); // close the current volume + SPI (forced: a format proceeds regardless of busy)

    // Re-open SPI + init the card WITHOUT a volume, so writeSector() / _sd.format() have a live card.
    #ifdef ARDUINO_ARCH_RP2040
    SPI_SD.begin(true);
    #else
    SPI_SD.begin(PIN_SDCARD_SCK, PIN_SDCARD_MISO, PIN_SDCARD_MOSI, PIN_SDCARD_CS);
    #endif
    if (!_sd.cardBegin(sdConfig))
    {
        logErrorP("Format: card init failed! Aborting.");
        _closeCardAfterFormat();
        return;
    }

    _fmtSector = 0;
    _fmtTotal = (_sd.card() ? (uint32_t)_sd.card()->sectorCount() : 0); // valid now: card initialised
    switch (op)
    {
        case 0:
            _fmtOp = FmtOp::Quick;
            logInfoP("Quick-Format started (background)...");
            break;
        case 1:
            _fmtOp = FmtOp::ExFat;
            logInfoP("Format started (background)...");
            _formatToast("Formatiere exFAT...");
            break;
        case 2:
            _fmtOp = FmtOp::LowLevel;
            _fmtHeartbeat = millis();
            logInfoP("Low-Level-Format started (background, %lu sectors)...", (unsigned long)_fmtTotal);
            _formatToast("Low-Level laeuft...");
            break;
        default:
            logErrorP("Format: unknown op %u", op);
            _closeCardAfterFormat();
            break;
    }
}

// Park the card after a format (or on an init/error abort): end _sd, close SPI, mark unmounted.
// Quick / Low-Level leave the card WITHOUT a filesystem, so this parks it cleanly (the user then runs
// an exFAT Format to create one). exFAT success remounts via ReMount() instead of this.
void SDCardModule::_closeCardAfterFormat()
{
    _sd.end();
    SPI_SD.end();
    _mountStep = MOUNT_STATE_UNMOUNTED;
    _cardUnformatted = true; // no filesystem left -> SD widget prompts "Bitte formatieren"
    _sdInfoGeneration++;
}

// Brief on-screen message so a format gives display feedback (menu OR console trigger), mirroring
// the console log. No-op when no DeviceDisplay is present.
void SDCardModule::_formatToast(const char *msg)
{
    #ifdef DEVICE_DISPLAY_MODULE
    openknxDisplayModule.showToast(msg);
    #else
    (void)msg;
    #endif
}

// Perform ONE incremental step of the pending format op — called every loop(). Quick finishes in one
// tick; exFAT is a single (unavoidably blocking ~seconds) SdFat call; Low-Level zeroes the card in
// SHORT, TIME-BOUNDED bursts (multi-block writes capped at FMT_TICK_BUDGET_MS) so a loop() tick stays
// short and the router keeps running smoothly (KNX / display unaffected — no loop-time warning).
void SDCardModule::_serviceFormat()
{
    switch (_fmtOp)
    {
        case FmtOp::None:
            return;

        case FmtOp::Quick:
        {
            uint8_t emptyMBR[512] = {0};
            const bool ok = _sd.card() && _sd.card()->writeSector(0, emptyMBR);
            _fmtOp = FmtOp::None;
            if (ok)
            {
                logInfoP("Quick-Format done (MBR cleared). Use 'format' next.");
                _formatToast("Quick-Format fertig.\nBitte formatieren.");
            }
            else
            {
                logErrorP("Quick-Format FAILED (MBR write error).");
                _formatToast("Quick-Format\nfehlgeschlagen!");
            }
            _closeCardAfterFormat(); // MBR gone -> no filesystem; park the card cleanly
            return;
        }

        case FmtOp::ExFat:
        {
            // SdFat's format() is a single monolithic library call (cannot be chunked) -> it blocks
            // for its whole duration (a few seconds). Bounded and one-shot, so it is accepted as-is;
            // the loop-time warning it emits is honest (it really did block).
            const bool ok = _sd.format();
            _fmtOp = FmtOp::None;
            if (!ok)
            {
                logErrorP("Format FAILED. Supported: %s", FS_SUPPORT_FORMATS);
                _formatToast("Format\nfehlgeschlagen!");
                _closeCardAfterFormat();
                return;
            }

            logInfoP("Format done.");
            // Mount the fresh exFAT DIRECTLY: the card is still initialised (from requestFormat's
            // cardBegin), so a volumeBegin() picks up the new filesystem. Retry once with a fresh
            // cardBegin in case the backend needs it post-format.
            bool mounted = _sd.volumeBegin();
            if (!mounted && _sd.cardBegin(sdConfig))
                mounted = _sd.volumeBegin();

            if (mounted)
            {
                _mountStep = MOUNT_STATE_MOUNTED;
                _cardUnformatted = false;
                _sdInfoGeneration++;
                resetCardInfo();
                logInfoP("SD-Card successfully mounted!");
                _formatToast("Karte formatiert\n(exFAT).");
                info();
            }
            else
            {
                logErrorP("Format done, but mounting the fresh volume failed.");
                _formatToast("Formatiert,\nMount-Fehler");
                _closeCardAfterFormat();
            }
            return;
        }

        case FmtOp::LowLevel:
        {
            if (_fmtTotal == 0 || !_sd.card())
            {
                logErrorP("Low-Level-Format: no card.");
                _fmtOp = FmtOp::None;
                _formatToast("Low-Level: keine Karte");
                _closeCardAfterFormat();
                return;
            }

    #ifdef DEVICE_DISPLAY_MODULE
            // While a button is held (gesture/menu interaction), pause the wipe for this tick so the
            // display is fully free and the gesture countdown overlay animates smoothly. Progress is
            // preserved; writing resumes as soon as the button is released.
            if (openknxDisplayModule.isGestureActive())
                return;
    #endif

            // Zero the card in short, time-bounded bursts using MULTI-BLOCK writes (writeSectors is
            // far faster than one-sector-at-a-time). We keep writing FMT_SECTORS_PER_WRITE-sector
            // chunks only until FMT_TICK_BUDGET_MS elapsed, so a single tick stays ~15 ms regardless
            // of card speed -> the router stays responsive.
            static uint8_t zeroBuf[FMT_SECTORS_PER_WRITE * 512] = {0};
            const uint32_t budgetStart = millis();
            while (_fmtSector < _fmtTotal && (uint32_t)(millis() - budgetStart) < FMT_TICK_BUDGET_MS)
            {
                uint32_t chunk = FMT_SECTORS_PER_WRITE;
                if (_fmtSector + chunk > _fmtTotal) chunk = _fmtTotal - _fmtSector;
                if (!_sd.card()->writeSectors(_fmtSector, zeroBuf, chunk))
                {
                    logErrorP("Low-Level-Format: write error at sector %lu. Aborted.", (unsigned long)_fmtSector);
                    _fmtOp = FmtOp::None;
                    _formatToast("Low-Level\nfehlgeschlagen!");
                    _closeCardAfterFormat();
                    return;
                }
                _fmtSector += chunk;
            }

            // Heartbeat every few seconds with FINE progress (0.01 %) + sector counter, so it is
            // obvious the wipe is alive — 1 % of a big card is millions of sectors and a plain
            // integer % would sit on 0 for hours.
            if ((uint32_t)(millis() - _fmtHeartbeat) >= FMT_HEARTBEAT_MS)
            {
                _fmtHeartbeat = millis();
                const uint16_t pm = (uint16_t)((uint64_t)_fmtSector * 10000 / _fmtTotal);
                logInfoP("Low-Level-Format: %u.%02u%% (%lu / %lu sectors)",
                         (unsigned)(pm / 100), (unsigned)(pm % 100),
                         (unsigned long)_fmtSector, (unsigned long)_fmtTotal);
            }
            if (_fmtSector >= _fmtTotal)
            {
                _fmtOp = FmtOp::None;
                logInfoP("Low-Level-Format done. Use 'format' next.");
                _formatToast("Low-Level fertig.\nBitte formatieren.");
                _closeCardAfterFormat(); // whole card zeroed -> no filesystem; park cleanly
            }
            return;
        }
    }
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
    // Card can be ended/unmounted (SPI closed) yet still inserted after a format -- _sd.card() is
    // then null; guard before dereferencing it for the raw sector read.
    if (!isMounted() || !_sd.card())
    {
        logErrorP("SD card not mounted!");
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

// Compact MBR/GPT summary into one short line per row (<=21 chars for the 128px display). Bounded:
// GPT entries are capped so a corrupt header can never spin the loop. Used by the Partition-Info
// submenu (readPartitionInfo() keeps the full console dump).
void SDCardModule::readPartitionInfoLines(std::vector<std::string> &out)
{
    out.clear();
    if (!isCardInserted())
    {
        out.push_back("Keine Karte");
        return;
    }
    // After a format the card is ended/unmounted (SPI closed) yet still physically inserted, so
    // isCardInserted() passes while _sd.card() is null -- guard the deref below.
    if (!isMounted() || !_sd.card())
    {
        out.push_back("Nicht gemountet");
        return;
    }

    uint8_t buffer[512];
    if (!_sd.card()->readSector(0, buffer))
    {
        out.push_back("Sektor 0 Fehler");
        return;
    }

    const bool sig = (buffer[0x1FE] == 0x55 && buffer[0x1FF] == 0xAA);
    const bool protectiveGpt = sig && (buffer[0x1BE + 4] == 0xEE);
    char tmp[24];

    // Plain MBR (not a GPT protective MBR): list the up to 4 non-empty primary entries.
    if (sig && !protectiveGpt)
    {
        out.push_back("MBR");
        uint32_t shown = 0;
        for (int i = 0; i < 4; i++)
        {
            const int off = 0x1BE + i * 16;
            const uint8_t type = buffer[off + 4];
            const uint32_t start = buffer[off + 8] | (buffer[off + 9] << 8) |
                                   (buffer[off + 10] << 16) | ((uint32_t)buffer[off + 11] << 24);
            const uint32_t count = buffer[off + 12] | (buffer[off + 13] << 8) |
                                   (buffer[off + 14] << 16) | ((uint32_t)buffer[off + 15] << 24);
            if (type == 0 || count == 0) continue; // empty slot
            shown++;
            snprintf(tmp, sizeof(tmp), "P%d 0x%02X %s", i + 1, type, getPartitionType(type).c_str());
            out.push_back(tmp);
            snprintf(tmp, sizeof(tmp), " %lu-%lu", (unsigned long)start, (unsigned long)(start + count - 1));
            out.push_back(tmp);
        }
        if (shown == 0) out.push_back("(keine Partition)");
        return;
    }

    // GPT (protective MBR or an "EFI PART" signature in sector 0).
    if (protectiveGpt || memcmp(buffer, "EFI PART", 8) == 0)
    {
        out.push_back("GPT");
        if (!_sd.card()->readSector(1, buffer))
        {
            out.push_back("GPT-Header Fehler");
            return;
        }
        uint32_t entryLBA;
        memcpy(&entryLBA, &buffer[72], sizeof(entryLBA));
        uint32_t pcount;
        memcpy(&pcount, &buffer[80], sizeof(pcount));
        if (pcount > 128) pcount = 128; // safety cap against a corrupt header

        uint32_t shown = 0;
        for (uint32_t i = 0; i < pcount; i++)
        {
            const uint32_t sector = entryLBA + (i * 128) / 512;
            if (!_sd.card()->readSector(sector, buffer)) break;
            const uint8_t *e = buffer + (i * 128) % 512;
            uint64_t firstLBA, lastLBA;
            memcpy(&firstLBA, &e[32], sizeof(firstLBA));
            memcpy(&lastLBA, &e[40], sizeof(lastLBA));
            if (firstLBA == 0 && lastLBA == 0) continue; // unused entry
            shown++;
            snprintf(tmp, sizeof(tmp), "P%lu %llu-%llu", (unsigned long)shown,
                     (unsigned long long)firstLBA, (unsigned long long)lastLBA);
            out.push_back(tmp);
            if (shown >= 8) break; // display cap
        }
        if (shown == 0) out.push_back("(keine Partition)");
        return;
    }

    out.push_back("Keine Part.-Tabelle");
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
    if (!_cardInfo.isValid)
    {
        logDebugP("No valid card info available. Reading card info...");
        readCardInfo(_cardInfo); // Seems that we need to read it for the first time! Once is enough!
    }

    if (_cardInfo.isValid)
    {
        openknx.logger.begin();
        openknx.logger.log(""); // Empty line for spacing
        openknx.logger.color(CONSOLE_HEADLINE_COLOR);
        openknx.logger.log("============================= SD Card Information =============================");
        openknx.logger.color(0);
        openknx.logger.logWithValues("| Manufacturer Name      | %-50s |", _cardInfo.manufacturer.c_str());
        openknx.logger.logWithValues("| Product Name           | %-50s |", _cardInfo.productName.c_str());
        openknx.logger.logWithValues("| Product Revision       | %-50s |", _cardInfo.revision.c_str());
        if (_cardInfo.oemApplicationID.length() > 0)
        {
            openknx.logger.logWithValues("| OEM Application ID     | %-50s |", _cardInfo.oemApplicationID.c_str());
        }
        openknx.logger.logWithValues("| OEM ID                 | %-50s |", _cardInfo.oemApplicationID.c_str());
        openknx.logger.logWithValues("| Serial Number          | %-50s |", _cardInfo.serialNumber.c_str());
        openknx.logger.logWithValues("| Manufacture Date       | %-50s |", _cardInfo.manufactureDate.c_str());
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
    openknx.logger.logWithValues("| Volume Label           | %-50s |", getVolumeLabel().c_str());

    uint64_t freeBytes = 0, usedBytes = 0, totalBytes = getSDCardSize();
    if (totalBytes > 0 && getSDCardUsage(freeBytes, usedBytes))
    {
        if (freeBytes == 0 && usedBytes == 0)
        {
            openknx.logger.log("| Error                  | Unable to retrieve SD card usage                 |");
        }
        else
        {
            float usedPercentage = (totalBytes > 0) ? ((float)usedBytes * 100.0f / totalBytes) : 0.0f;
            float freePercentage = 100.0f - usedPercentage;
            int usedBarLength = (int)(usedPercentage * 0.5f);

            // #define BAR_LENGTH 48
            int freeBarLength = 48 - usedBarLength;

            char usedBar[48 + 1] = {0};
            char freeBar[48 + 1] = {0};

            memset(usedBar, '=', usedBarLength);
            memset(freeBar, '=', freeBarLength);

            openknx.logger.logWithValues("| Card capacity          | %-50s |", formatSize(totalBytes));
            openknx.logger.log("-------------------------------------------------------------------------------");
            openknx.logger.color(CONSOLE_HEADLINE_COLOR);
            openknx.logger.logWithValues("| Used: %-10s [%-*s] %6.2f%% |", formatSize(usedBytes), 48, usedBar, usedPercentage);
            openknx.logger.logWithValues("| Free: %-10s [%-*s] %6.2f%% |", formatSize(freeBytes), 48, freeBar, freePercentage);
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
    if (!isMounted() || buffer == nullptr || size == 0) return 0;
    FSFILE file = open(path, "r");
    if (!file) return 0;
    const int bytesRead = file.read(buffer, size); // int: -1 on error
    file.close();
    // Never leak a -1 into size_t (would become SIZE_MAX and overflow callers, e.g. buffer[bytesRead]).
    return (bytesRead > 0) ? static_cast<size_t>(bytesRead) : 0;
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
 * @brief Single-pass directory listing for the device-display file browser.
 *
 * Collects name, directory flag and size for every child of @p path in ONE
 * openNextFile() loop, reusing the already-open FSFILE for isDirectory()/size()
 * instead of re-opening each entry (as Statistics() would). The public signature
 * exposes only SdDirEntry, keeping it free of any SdFat/FSFILE type.
 *
 * Nothing is listed unless the card isMounted(); in that case @p out is left
 * untouched (i.e. empty when the caller passes an empty vector). @p maxEntries
 * bounds the number of appended entries to limit RAM use and loop time; a value
 * of 0 means "no cap".
 *
 * @param path        The directory to list.
 * @param out         Vector the entries are appended to (not cleared here).
 * @param maxEntries  Maximum entries to append (0 = unlimited).
 * @return The number of entries appended to @p out.
 */
size_t SDCardModule::listDir(const char *path, std::vector<SdDirEntry> &out, size_t maxEntries)
{
    if (!isMounted()) return 0;
    FSFILE dir = _sd.open(path);
    if (!dir) return 0;

    size_t count = 0;
    char buffer[256];
    while (FSFILE file = dir.openNextFile())
    {
        if (maxEntries != 0 && count >= maxEntries)
        {
            file.close();
            break;
        }
        SdDirEntry entry;
        file.getName(buffer, sizeof(buffer));
        entry.name = buffer;
        entry.isDir = file.isDirectory();
        entry.size = entry.isDir ? 0 : (uint64_t)file.size();
        file.close();
        out.push_back(entry);
        count++;
    }
    dir.close();
    return count;
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
bool SDCardModule::getSDCardUsage(uint64_t &freeSpace, uint64_t &usedSpace)
{
    FSVOlUME *vol = _sd.vol();
    if (!vol)
    {
        freeSpace = usedSpace = 0;
        return false; // No volume ?
    }
    uint32_t freeClusters = 0;  // vol->freeClusterCount(); // free clusters
    uint32_t totalClusters = 0; // vol->clusterCount();    // total clusters
    uint32_t clusterSize = 0;   // vol->bytesPerCluster();   // bytes per cluster (always 512 bytes)

    uint32_t startTime, endTime;
    startTime = micros();
    freeClusters = vol->freeClusterCount();
    endTime = micros();
    logDebugP("freeClusterCount() dauert: %lu µs", endTime - startTime);

    startTime = micros();
    totalClusters = vol->clusterCount();
    endTime = micros();
    logDebugP("clusterCount() dauert: %lu µs", endTime - startTime);

    startTime = micros();
    clusterSize = vol->bytesPerCluster();
    endTime = micros();
    logDebugP("bytesPerCluster() dauert: %lu µs", endTime - startTime);

    if (freeClusters == 0xFFFFFFFF || totalClusters == 0)
    {
        freeSpace = usedSpace = 0;
        return false; // Values are invalid ?
    }
    freeSpace = (uint64_t)freeClusters * clusterSize;                // Freier Speicher in Bytes
    usedSpace = ((uint64_t)totalClusters * clusterSize) - freeSpace; // Belegter Speicher
    return true;
}

/**
 * @brief  Start an incremental scan of the SD card's usage (free/used clusters).
 * 
 * This function initiates a scan of the SD card's file system to determine the number of free and used clusters.
 * It supports FAT16, FAT32, and exFAT file systems. The scan is performed incrementally to avoid
 * blocking the main loop, and it can be ticked in the main loop
 */
void SDCardModule::beginUsageScan()
{
    if (isUsageScanRunning()) return; // one pass at a time
    if (!isMounted()) return;

    FSVOlUME *vol = _sd.vol();
    if (!vol) return;

    const uint8_t ft = _sd.fatType();
    _usClusterSizeBytes = vol->bytesPerCluster();
    _usTotalClusters = vol->clusterCount();
    _usFreeClusters = 0;
    _usCluster = 0;
    if (_usTotalClusters == 0 || _usClusterSizeBytes == 0) return;

    if (ft == FAT_TYPE_FAT32 || ft == FAT_TYPE_FAT16)
    {
        _usEntryWidth = (ft == FAT_TYPE_FAT32) ? 4 : 2;
        const uint32_t fatStart = vol->fatStartSector();
        const uint32_t entries = _usTotalClusters + 2; // FAT covers clusters 0..(total+1)
        const uint32_t fatSectors = ((entries * _usEntryWidth) + 511u) / 512u;
        _usSector = fatStart;
        _usEndSector = fatStart + fatSectors;
        _usState = UsageScanState::ScanFat;
    }
    else if (ft == FAT_TYPE_EXFAT)
    {
        // exFAT allocation bitmap: 1 bit per cluster (0 = free), bit i => cluster (i+2). Standard
        // layout places the bitmap on cluster 2, i.e. starting at dataStartSector().
        const uint32_t bmpStart = vol->dataStartSector();
        const uint32_t bmpSectors = (((_usTotalClusters + 7u) / 8u) + 511u) / 512u;
        _usSector = bmpStart;
        _usEndSector = bmpStart + bmpSectors;
        _usState = UsageScanState::ScanBitmap;
    }
    else
    {
        _usState = UsageScanState::Idle; // FAT12 / unknown -> not supported by the incremental path
        return;
    }
    _usScanStartMs = millis();
    _usWorstTickUs = 0;
    logDebugP("Usage scan START (fatType=%u, clusters=%lu, sectors=%lu)", ft,
              (unsigned long)_usTotalClusters, (unsigned long)(_usEndSector - _usSector));
}

/**
 * @brief  Advance the incremental scan of the SD card's usage by a bounded number of sectors.
 * 
 * This function should be called repeatedly in the main loop to continue the scan. It processes
 * a limited number of sectors per call to avoid blocking the main loop. Returns true while a scan
 * is still running.
 */
bool SDCardModule::tickUsageScan()
{
    if (_usState != UsageScanState::ScanFat && _usState != UsageScanState::ScanBitmap)
        return false;

    auto *card = _sd.card();
    if (!card)
    {
        _usState = UsageScanState::Idle;
        return false;
    }

    const uint32_t tickStartUs = micros();  // diagnostics: measure this tick's duration
    constexpr uint8_t SECTORS_PER_TICK = 8; // ~4 KB/loop -> a few ms, keeps the loop responsive
    for (uint8_t n = 0; n < SECTORS_PER_TICK && _usSector < _usEndSector; ++n)
    {
        if (!card->readSector(_usSector, _usSectorBuf))
        {
            _usState = UsageScanState::Idle; // read error -> abort, keep the last valid result
            return false;
        }
        _usSector++;

        if (_usState == UsageScanState::ScanFat)
        {
            const uint16_t perSector = (uint16_t)(512u / _usEntryWidth);
            for (uint16_t i = 0; i < perSector; ++i)
            {
                const uint32_t cl = _usCluster++;
                if (cl >= _usTotalClusters + 2) break;
                if (cl < 2) continue; // clusters 0,1 are reserved, never "free"
                uint32_t entry;
                if (_usEntryWidth == 4)
                {
                    const uint8_t *p = &_usSectorBuf[i * 4];
                    entry = ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
                    entry &= 0x0FFFFFFFu;
                }
                else
                {
                    const uint8_t *p = &_usSectorBuf[i * 2];
                    entry = ((uint16_t)p[0]) | ((uint16_t)p[1] << 8);
                }
                if (entry == 0) _usFreeClusters++;
            }
        }
        else // ScanBitmap (exFAT)
        {
            for (uint16_t b = 0; b < 512 && _usCluster < _usTotalClusters; ++b)
            {
                const uint8_t byte = _usSectorBuf[b];
                for (uint8_t bit = 0; bit < 8; ++bit)
                {
                    const uint32_t cl = _usCluster++;
                    if (cl >= _usTotalClusters) break;
                    if ((byte & (1u << bit)) == 0) _usFreeClusters++;
                }
            }
        }
    }

    const uint32_t tickUs = micros() - tickStartUs; // diagnostics: track the worst single tick
    if (tickUs > _usWorstTickUs) _usWorstTickUs = tickUs;

    const bool done = (_usSector >= _usEndSector) ||
                      (_usState == UsageScanState::ScanFat && _usCluster >= _usTotalClusters + 2) ||
                      (_usState == UsageScanState::ScanBitmap && _usCluster >= _usTotalClusters);
    if (done)
    {
        [[maybe_unused]] const uint32_t wallMs = millis() - _usScanStartMs; // only used by logDebugP (compiled out in release)
        // Sanity guard: free must not exceed total (catches a wrong exFAT bitmap-start assumption).
        if (_usFreeClusters <= _usTotalClusters)
        {
            _usResTotal = (uint64_t)_usTotalClusters * _usClusterSizeBytes;
            _usResFree = (uint64_t)_usFreeClusters * _usClusterSizeBytes;
            _usResUsed = _usResTotal - _usResFree;
            _usResValid = true;
            logDebugP("Usage scan DONE: %lu/%lu clusters free; wall=%lu ms, worstTick=%lu us",
                      (unsigned long)_usFreeClusters, (unsigned long)_usTotalClusters, (unsigned long)wallMs, (unsigned long)_usWorstTickUs);
        }
        else
        {
            _usResValid = false;
            logDebugP("Usage scan DISCARDED (free>total; wall=%lu ms, worstTick=%lu us)", (unsigned long)wallMs, (unsigned long)_usWorstTickUs);
        }
        _usState = UsageScanState::Done;
        return false;
    }
    return true;
}

// Read the last completed incremental scan result. False until the first scan has finished.
bool SDCardModule::getCachedUsage(uint64_t &freeSpace, uint64_t &usedSpace, uint64_t &totalSpace) const
{
    if (!_usResValid) return false;
    freeSpace = _usResFree;
    usedSpace = _usResUsed;
    totalSpace = _usResTotal;
    return true;
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
String SDCardModule::getCardType(bool shortType)
{
    const char *cardTypeStr[] = {shortType ? "?" : "Unknown", shortType ? "SD" : "SD (Standard)",
                                 shortType ? "SDHC" : "SDHC (High Capacity)", shortType ? "SDXC" : "SDXC (Extended Capacity)"};
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
 * @brief Get the volume label of the SD card.
 *
 * @return The volume label of the SD card, or an empty String if unavailable.
 */
namespace
{
    inline uint32_t rd32le(const uint8_t *p)
    {
        return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
               (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
    }

    // Absolute LBA of the mounted volume's boot sector: 0 for a superfloppy (no MBR), otherwise the
    // MBR partition-1 start LBA. Reads sector 0 to decide.
    uint32_t volumeBootSector(SDFAT_ &sd)
    {
        uint8_t s0[512];
        if (!sd.card()->readSector(0, s0)) return 0;
        if (memcmp(&s0[3], "EXFAT   ", 8) == 0) return 0; // exFAT superfloppy
        if ((s0[0] == 0xEB || s0[0] == 0xE9) &&
            (memcmp(&s0[0x36], "FAT", 3) == 0 || memcmp(&s0[0x52], "FAT", 3) == 0))
            return 0;                           // FAT superfloppy
        if (s0[510] == 0x55 && s0[511] == 0xAA) // MBR -> partition 1 start LBA
        {
            const uint32_t lba = rd32le(&s0[0x1C6]);
            if (lba != 0) return lba;
        }
        return 0;
    }

    // Absolute sector of the exFAT root directory's first cluster (holds the volume-label entry).
    bool exfatRootSector(SDFAT_ &sd, uint32_t &outSector)
    {
        if (sd.fatType() != FAT_TYPE_EXFAT) return false;
        const uint32_t bs = volumeBootSector(sd);
        uint8_t sec[512];
        if (!sd.card()->readSector(bs, sec)) return false;
        if (memcmp(&sec[3], "EXFAT   ", 8) != 0) return false;
        const uint32_t clusterHeapOffset = rd32le(&sec[0x58]);
        const uint32_t rootDirCluster = rd32le(&sec[0x60]);
        const uint8_t spcShift = sec[0x6D];
        if (rootDirCluster < 2) return false;
        outSector = bs + clusterHeapOffset + ((rootDirCluster - 2) << spcShift);
        return true;
    }
} // namespace

String SDCardModule::getVolumeLabel()
{
    if (!isMounted()) return String();

    uint8_t buffer[512];
    if (!_sd.card()->readSector(0, buffer)) return String();

    char raw[33] = {0};
    size_t rawLen = 0;

    if (_sd.fatType() == FAT_TYPE_EXFAT)
    {
        // exFAT: the label is a root-directory entry (type 0x83), not in the boot sector.
        uint32_t rootSector;
        if (!exfatRootSector(_sd, rootSector)) return String();
        uint8_t rd[512];
        if (!_sd.card()->readSector(rootSector, rd)) return String();
        if (rd[0] != 0x83) return String(); // no label set
        const uint8_t cnt = rd[1] <= 11 ? rd[1] : 11;
        char label[12] = {0};
        for (uint8_t i = 0; i < cnt; ++i)
            label[i] = static_cast<char>(rd[2 + i * 2]); // UTF-16LE low byte
        return String(label);
    }
    else
    {
        // FAT16/FAT32: 11-byte space-padded volume label in the boot sector (offset 0x2B/0x47)
        const size_t off = (_sd.fatType() == FAT_TYPE_FAT32) ? 0x47 : 0x2B;
        rawLen = 11;
        memcpy(raw, &buffer[off], rawLen);
    }

    // Trim trailing spaces / NULs.
    while (rawLen > 0 && (raw[rawLen - 1] == ' ' || raw[rawLen - 1] == '\0'))
        raw[--rawLen] = '\0';

    return String(raw);
}

// Set the volume label in place via a raw sector write. exFAT: the root-dir 0x83 label entry.
// FAT16/32: the 11-byte boot-sector label. Verifies the target holds a plausible label entry before
// writing so a wrong geometry can never scribble over data. Returns false on any inconsistency.
// A volume-label character is valid if it is printable and not filesystem-reserved. exFAT is
// permissive; FAT16/32 additionally forbids . , ; + = [ ] (and is uppercase-only).
static bool validLabelChar(char c, bool exfat)
{
    const uint8_t u = static_cast<uint8_t>(c);
    if (u < 0x20 || u == 0x7F) return false; // control characters
    switch (c)
    {
        case '"':
        case '*':
        case '/':
        case ':':
        case '<':
        case '>':
        case '?':
        case '\\':
        case '|':
            return false;
        default:
            break;
    }
    if (!exfat)
    {
        switch (c)
        {
            case '.':
            case ',':
            case ';':
            case '+':
            case '=':
            case '[':
            case ']':
                return false;
            default:
                break;
        }
    }
    return true;
}

bool SDCardModule::setVolumeLabel(const char *name)
{
    if (!isMounted() || name == nullptr) return false;

    const bool exfat = (_sd.fatType() == FAT_TYPE_EXFAT);

    // Validate length + character set HERE, in the API, so every caller (console, menu, future web)
    // is checked the same way and an invalid label can never reach the card.
    size_t len = 0;
    while (len <= 11 && name[len] != '\0')
        ++len;
    if (len > 11)
    {
        logErrorP("setVolumeLabel: name too long (max 11 characters)");
        return false;
    }
    for (size_t i = 0; i < len; ++i)
    {
        if (!validLabelChar(name[i], exfat))
        {
            const char shown = (name[i] >= 0x20 && name[i] < 0x7F) ? name[i] : '?';
            logErrorP("setVolumeLabel: invalid character '%c' (0x%02X) for a %s label",
                      shown, static_cast<uint8_t>(name[i]), exfat ? "exFAT" : "FAT");
            return false;
        }
    }

    if (exfat)
    {
        uint32_t rootSector;
        if (!exfatRootSector(_sd, rootSector)) return false;

        uint8_t rd[512];
        if (!_sd.card()->readSector(rootSector, rd)) return false;

        // Safety: the FIRST root entry must be a set (0x83) or empty (0x03) volume-label entry.
        if (rd[0] != 0x83 && rd[0] != 0x03)
        {
            logErrorP("setVolumeLabel: unexpected exFAT root entry 0x%02X - aborting", rd[0]);
            return false;
        }

        uint8_t count = 0;
        for (uint8_t i = 0; i < 11 && name[i] != '\0'; ++i)
        {
            rd[2 + i * 2] = static_cast<uint8_t>(name[i]); // ASCII -> UTF-16LE low byte
            rd[3 + i * 2] = 0x00;
            ++count;
        }
        for (uint8_t i = count; i < 11; ++i)
        {
            rd[2 + i * 2] = 0x00;
            rd[3 + i * 2] = 0x00;
        }
        rd[0] = count > 0 ? 0x83 : 0x03; // present / empty
        rd[1] = count;

        if (!_sd.card()->writeSector(rootSector, rd)) return false;
        _sd.card()->syncDevice();
        logInfoP("SD volume label set (exFAT): \"%s\"", name);
        return true;
    }

    // FAT16 / FAT32: 11-byte space-padded, uppercase label in the boot sector.
    const uint32_t bs = volumeBootSector(_sd);
    uint8_t sec[512];
    if (!_sd.card()->readSector(bs, sec)) return false;
    if (sec[0] != 0xEB && sec[0] != 0xE9)
    {
        logErrorP("setVolumeLabel: not a FAT boot sector - aborting");
        return false;
    }

    size_t n = 0;
    while (n < 11 && name[n] != '\0')
        ++n; // length, capped at 11 (no read past the NUL)
    const size_t off = (_sd.fatType() == FAT_TYPE_FAT32) ? 0x47 : 0x2B;
    for (uint8_t i = 0; i < 11; ++i)
    {
        char c = (i < n) ? name[i] : ' ';
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A'); // FAT labels are uppercase
        sec[off + i] = static_cast<uint8_t>(c);
    }

    if (!_sd.card()->writeSector(bs, sec)) return false;
    _sd.card()->syncDevice();
    logInfoP("SD volume label set (FAT): \"%s\"", name);
    return true;
}

/**
 *
 * @brief Get the SD card informations such as manufacturer, product name, revision, serial number, etc.
 * @param info The structure to store the SD card information.
 *
 * @return True if the information is successfully retrieved, false otherwise.
 */
bool SDCardModule::readCardInfo(CardInfo &info)
{
    if (!isMounted())
    {
        logErrorP("No SD card mounted!");
        return false;
    }

    cid_t cid;
    if (!_sd.card()->readCID(&cid))
    {
        logErrorP("Failed to read CID from SD card.");
        return false;
    }

    static const std::unordered_map<uint8_t, const char *> manufacturers = {
        {0x01, "Panasonic"}, {0x41, "ADATA"}, {0x75, "Cactus"}, {0x95, "Extrememory"}, {0x02, "Toshiba"}, {0x45, "Patriot"}, {0x80, "OCZ"}, {0x96, "Dane-Elec"}, {0x03, "SanDisk"}, {0x50, "PNY"}, {0x85, "Kingmax"}, {0x97, "Jenoptik"}, {0x1B, "Samsung"}, {0x55, "Verbatim"}, {0x90, "Apacer"}, {0x98, "Platinum"}, {0x1D, "Kingston"}, {0x5A, "Integral"}, {0x91, "Hama"}, {0x99, "Navigon"}, {0x1E, "Transcend"}, {0x60, "Emtec"}, {0x92, "TakeMS"}, {0xA1, "CDA GmbH"}, {0x28, "Lexar"}, {0x65, "GoodRAM"}, {0x93, "Infineon"}, {0xA2, "ATP Electronics"}, {0x31, "Sony"}, {0x70, "Silicon Power"}, {0x94, "Mustang"}, {0xA3, "Delkin Devices"}};

    info.manufacturer = manufacturers.count(cid.mid) ? manufacturers.at(cid.mid) : "Unknown";
    logDebugP("Manufacturer: %02X", cid.mid);

    info.productName = String(cid.pnm, 5);

    info.oemApplicationID = String(1, cid.oid[0]) + String(1, cid.oid[1]);

    info.revision = String(cid.prv);

    info.serialNumber = String(cid.psn());

    char manufactureDateBuffer[50];
    snprintf(manufactureDateBuffer, sizeof(manufactureDateBuffer), "%02u/%04u", cid.mdtMonth(), cid.mdtYear());
    info.manufactureDate = manufactureDateBuffer;

    return info.isValid = true;
}

SDCardModule sdCardModule(PIN_SDCARD_CS); // ToDo: CS Pin for SD card module ?, not obtain them from device configuration

#endif // OPENKNX_SD_CARD_MODULE_ENABLE
