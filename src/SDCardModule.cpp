#include "SDCardModule.h"

SDCardModule::SDCardModule(uint8_t csPin) : chipSelectPin(csPin), mounted(false), sd(SdFat()) {}

SDCardModule::~SDCardModule() {}

void SDCardModule::init() {
    Serial.println("[SDCardModule] Initializing...");
}

//SPIClass SPI_SD(HSPI);
SPIClass SPI_SD(1);

void SDCardModule::setup(bool configured) {
  Serial.println("[SDCardModule] Setting up...");
  mounted= false;

  SdSpiConfig sdConfig(PIN_SDCARD_CS, DEDICATED_SPI, SD_SCK_MHZ(25), &SPI_SD);
  SPI_SD.begin(PIN_SDCARD_SCK, PIN_SDCARD_MISO, PIN_SDCARD_MOSI, PIN_SDCARD_CS);
    //if (!SD.begin(PIN_SDCARD_CS, SPI_SD, SPI_FULL_SPEED)) { // SD.h 
      if (!sd.begin(sdConfig)) {
        Serial.println("SD-Karten-Fehler!");
        Serial.print("Fehlercode: ");
        Serial.println(sd.card()->errorCode(), HEX);
        Serial.print("Fehlerdaten: ");
        Serial.println(sd.card()->errorData(), HEX);
    } else {
        Serial.println("SD-Karte erfolgreich erkannt!");
        mounted = true;
        Serial.println("========================================");
        Serial.println("         SD-Karten Informationen        ");
        Serial.println("========================================");
        Serial.print("Hersteller: ");
        cid_t cid;
        if (sd.card()->readCID(&cid)) {
            Serial.print("Hersteller ID: ");
            Serial.println(cid.mid, HEX);
            Serial.print("Produktname: ");
            for (int i = 0; i < 5; i++) {
          Serial.print((char)cid.pnm[i]);
            }
            Serial.println();
            Serial.print("Seriennummer: ");
            Serial.println(cid.psn(), HEX);
            Serial.print("Herstellungsdatum: ");
            Serial.print(cid.mdtMonth());
            Serial.print("/");
            uint16_t mdt = (cid.mdt[0] << 8) | cid.mdt[1]; // Combine the two bytes of mdt
            Serial.println(2000 + (mdt >> 4)); // Try to extract year from mdt
        } else {
            Serial.println("Fehler beim Lesen der CID-Daten.");
        }

        Serial.println("----------------------------------------");
        Serial.print("Speicherkapazität: ");
        uint32_t cardSize = sd.card()->sectorCount();
        Serial.print(cardSize);
        Serial.print(" Sektoren, Kapazität: ");
        Serial.print((uint64_t)cardSize * 512 / (1024 * 1024));
        Serial.print(" MB (");
        Serial.print((uint64_t)cardSize * 512 / (1024 * 1024 * 1024));
        Serial.println(" GB)");
        Serial.print(" Blöcke (");
        Serial.print(cardSize * 512 / 1024 / 1024);
        Serial.println(" MB)");
        Serial.println("----------------------------------------");
        Serial.print("Maximale Geschwindigkeit: ");
        Serial.print(SD_SCK_MHZ(25));
        Serial.println(" MHz");
        Serial.println(" MHz");
        Serial.print("Kartentyp: ");
        Serial.println(sd.card()->type());
        Serial.print("Dateisystem: FAT");
        Serial.println(sd.fatType());
        Serial.println("========================================");
    }

  Serial.println("SD-Karte erfolgreich initialisiert!");
  FsFile file;
  if (!file.open("SDcard.txt", O_RDWR | O_CREAT)) {
    sd.errorHalt(F("open failed"));
  } else {
    file.println("Hello OpenKNX World!");
    file.close();
    Serial.println("Datei SDcard.txt erfolgreich erstellt!");
  }
}

void SDCardModule::loop(bool configured) {
    // Event loop for SD card operations
    // ToDo: CD cahnge detection and show SD info and file list...
}

void SDCardModule::showHelp() {
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

bool SDCardModule::processCommand(const std::string command, bool diagnose) {
  if (diagnose) return false;
  if (command == "sdc info") {
      return info();
  } else if (command == "sdc format") {
      return format();
  } else if (command.compare(0, 7, "sdc ls ") == 0) {
      String path = command.substr(7).c_str();
      std::vector<String> files = ls(path.c_str());
      for (String file : files) {
          Serial.println(file);
      }
      return true;
  } else if (command.compare(0, 10, "sdc mkdir ") == 0) {
      String dirName = command.substr(10).c_str();
      return mkdir(dirName.c_str());
  } else if (command.compare(0, 10, "sdc rmdir ") == 0) {
      String dirName = command.substr(10).c_str();
      return rmdir(dirName.c_str());
  } else if (command.compare(0, 7, "sdc rm ") == 0) {
      String fileName = command.substr(7).c_str();
      return remove(fileName.c_str());
  } else if (command.compare(0, 8, "sdc cat ") == 0) {
      String fileName = command.substr(8).c_str();
      uint8_t buffer[256];
      size_t bytesRead = read(fileName.c_str(), buffer, sizeof(buffer));
      if (bytesRead > 0) {
          Serial.write(buffer, bytesRead);
          return true;
      } else {
          Serial.println("[SDCardModule] Failed to read file");
          return false;
      }
  } else if (command.compare(0, 9, "sdc echo ") == 0) {
      size_t spacePos = command.find(' ', 9);
      if (spacePos == std::string::npos) {
          Serial.println("[SDCardModule] Invalid command format");
          return false;
      }
      String fileName = command.substr(9, spacePos - 9).c_str();
      String content = command.substr(spacePos + 1).c_str();
      FsFile file = open(fileName.c_str(), FILE_WRITE);
      if (file) {
          file.println(content);
          file.close();
          return true;
      } else {
          Serial.println("[SDCardModule] Failed to write to file");
          return false;
      }
  } else if (command.compare(0, 7, "sdc mv ") == 0) {
      size_t spacePos = command.find(' ', 7);
      if (spacePos == std::string::npos) {
          Serial.println("[SDCardModule] Invalid command format");
          return false;
      }
      String oldName = command.substr(7, spacePos - 7).c_str();
      String newName = command.substr(spacePos + 1).c_str();
      return rename(oldName.c_str(), newName.c_str());
  } else {
      Serial.println("[SDCardModule] Unknown command");
      return false;
  }
}

bool SDCardModule::rename(const char *oldPath, const char *newPath) {
    return sd.rename(oldPath, newPath);
}

bool SDCardModule::isMounted() {
    return mounted;
}

bool SDCardModule::format() {
    return sd.format();
}

bool SDCardModule::info() {
    if (!mounted) return false;
    Serial.println("[SDCardModule] SD Card Info:");
    Serial.print("Total Blocks: "); Serial.println(sd.card()->sectorCount());
    return true;
}

FsFile SDCardModule::open(const char *path, const char *mode) {
    oflag_t flags = 0;
    if (strcmp(mode, "r") == 0) {
        flags = O_RDONLY;
    } else if (strcmp(mode, "w") == 0) {
        flags = O_WRONLY | O_CREAT | O_TRUNC;
    } else if (strcmp(mode, "a") == 0) {
        flags = O_WRONLY | O_CREAT | O_APPEND;
    } else if (strcmp(mode, "r+") == 0) {
        flags = O_RDWR;
    } else if (strcmp(mode, "w+") == 0) {
        flags = O_RDWR | O_CREAT | O_TRUNC;
    } else if (strcmp(mode, "a+") == 0) {
        flags = O_RDWR | O_CREAT | O_APPEND;
    }
   return sd.open(path, flags);
}

bool SDCardModule::createFile(const char *path) {
    FsFile file = open(path, FILE_WRITE);
    if (!file) return false;
    file.close();
    return true;
}

bool SDCardModule::remove(const char *path) {
    return sd.remove(path);
}

bool SDCardModule::exists(const char *path) {
    return sd.exists(path);
}

size_t SDCardModule::read(const char *path, uint8_t *buffer, size_t size) {
    FsFile file = open(path, FILE_READ);
    if (!file) return 0;
    size_t bytesRead = file.read(buffer, size);
    file.close();
    return bytesRead;
}

size_t SDCardModule::write(const char *path, const uint8_t *buffer, size_t size) {
    FsFile file = open(path, FILE_WRITE);
    if (!file) return 0;
    size_t bytesWritten = file.write(buffer, size);
    file.close();
    return bytesWritten;
}

bool SDCardModule::mkdir(const char *path) {
    return sd.mkdir(path);
}

bool SDCardModule::rmdir(const char *path) {
    return sd.rmdir(path);
}

std::vector<String> SDCardModule::ls(const char *path) {
    std::vector<String> fileList;
    FsFile dir = sd.open(path);
    if (!dir) return fileList;
    char buffer[256];
    while (FsFile file = dir.openNextFile()) {
        file.getName(buffer, sizeof(buffer));
        file.close();
        fileList.push_back(buffer);
    }
    dir.close();
    return fileList;
}

SDCardModule sdCardModule(PIN_SDCARD_CS); // ToDo: CS Pin for SD card module ?, not obtain them from device configuration
