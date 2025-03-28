# OFM-SDCard

## Description

The `OFM-SDCard` module is a library for managing and utilizing SD cards. It provides a straightforward interface for reading, writing, and managing data stored on SD cards. The library supports both ESP32 and RP2040/2350 platforms. While both platforms support FAT16 and FAT32 filesystems, only ESP32 offers additional support for the exFAT filesystem.
### Supported Filesystems and Limitations

The `OFM-SDCard` library supports SD cards formatted with the following filesystems:

- **FAT16**
- **FAT32**
- **exFAT** (only supported on ESP32)

Other filesystems, such as NTFS or ext4, are not supported. Additionally, SD cards formatted with GUID Partition Table (GPT) or other advanced partitioning schemes are not compatible. The library requires SD cards to use the Master Boot Record (MBR) partitioning scheme.

#### Tested SD Card Sizes
The library has been successfully tested with SD cards up to **256 GB** in size. While larger SD cards must also work, they have not been extensively tested.

#### Platform-Specific Notes
- On the **ESP32** platform, FAT16, FAT32, and exFAT filesystems are supported.
- On the **RP2040/2350** platform, only FAT16 and FAT32 filesystems are supported. exFAT is not available.

For best results, ensure your SD card is formatted with one of the supported filesystems and uses the MBR partitioning scheme.


## Features

- **Read Data**: Enables reading data from the SD card.
- **Write Data**: Supports writing data to the SD card.
- **File Management**: Provides functions for managing files and directories on the SD card.
- **Initialization**: Includes functions for initializing and configuring the SD card.

## Usage

To use the `OFM-SDCard` library, include it in your project and utilize the provided functions to interact with the SD card.

## Important Notes

### Hardware Configuration Requirements

To ensure proper functionality of the `OFM-SDCard` module, specific hardware configuration definitions must be set in your `HardwareConfig` file. These definitions are mandatory for the SPI interface and pin assignments required to communicate with the SD card.

The following definitions must be included in your `HardwareConfig`:

- `SDCARD_SPI_INTERFACE`: Specifies the SPI interface used for the SD card.
- `PIN_SDCARD_CS`: Chip Select (CS) pin for the SD card.
- `PIN_SDCARD_CD`: Card Detect (CD) pin for the SD card.
- `PIN_SDCARD_SCK`: Serial Clock (SCK) pin for the SPI interface.
- `PIN_SDCARD_MISO`: Master In Slave Out (MISO) pin for the SPI interface.
- `PIN_SDCARD_MOSI`: Master Out Slave In (MOSI) pin for the SPI interface.

If any of these definitions are missing, the compilation process will fail with an error message indicating the missing definitions. Ensure that all required pins are properly defined in your `HardwareConfig` file to avoid compilation errors.

#### Example Configuration for RP2040/2350
```cpp
#define SDCARD_SPI_INTERFACE SPI
#define PIN_SDCARD_CS        5
#define PIN_SDCARD_CD        4
#define PIN_SDCARD_SCK       18
#define PIN_SDCARD_MISO      19
#define PIN_SDCARD_MOSI      23
```

Failure to define these pins will result in the following error during compilation:
```
SD-Card SPI Interface not defined in HardwareConfig.
Compilation aborted.
```
Ensure that your hardware configuration is complete and accurate to enable seamless integration of the `OFM-SDCard` module.

### Enviroment Define 
To ensure the `OFM-SDCard` module functions correctly, you must define `OPENKNX_SD_CARD_MODULE_ENABLE`. The recommended approach is to add this definition in your `platformio.ini` file. For example:

```ini
[env:your_environment]
build_flags = -D OPENKNX_SD_CARD_MODULE_ENABLE
```

Replace `your_environment` with the appropriate environment name for your project. This definition enables the SD card module and ensures proper integration with the OpenKNX framework.

## Example

```cpp
#include "SDCardModule.h"

#ifdef SDCARD_MODULE
  openknx.addModule(13, sdCardModule);
#endif

if (sdCardModule.isMounted()) {
  openknx.logger.log("SD Card mounted. Successfully initialized SD card filesystem.");
} else {
  openknx.logger.log("Failed to initialize SD card filesystem.");
}

// Check the SDCard class for detailed functions like read, write, and file management.
// You can use the console to test the functions. See the Command Overview.
```

### File Operations Example

Here is an example of opening, creating, and writing to a file on the SD card:

```cpp
#include "SDCardModule.h"

File file;

// Open an existing file for reading
file = sdCardModule.open("/example.txt", FILE_READ);
if (file) {
  openknx.logger.log("File opened successfully.");
  while (file.available()) {
    char c = file.read();
    openknx.logger.log(String(c));
  }
  file.close();
} else {
  openknx.logger.log("Failed to open file.");
}

// Create a new file and write data to it
file = sdCardModule.open("/newfile.txt", FILE_WRITE);
if (file) {
  file.println("This is a new file created on the SD card.");
  file.println("You can write multiple lines of text.");
  file.close();
  openknx.logger.log("File written successfully.");
} else {
  openknx.logger.log("Failed to create or write to file.");
}
```

### Features

- **Command Processing**: Execute commands for file and directory management, including:
  - Displaying filesystem information (`sdc info`)
  - Adding files or folders to the SD card (`sdc add /<f>`)
  - Removing files or directories (`sdc rm /<f>`, `sdc rmdir /<name>`)
  - Reading file contents (`sdc cat /<f>`)
  - Appending content to files (`sdc echo /<file> <text>`)
  - Renaming or moving files and folders (`sdc mv /<src> /<targt>`)
  - Copying files (`sdc cp /<src> /<targt>`)
  - Creating directories (`sdc mkdir /<name>`)
  - Listing files and directories (`sdc ls /<path>`, `sdc ll /<path>`)

- **Filesystem Management**: Includes commands for formatting and analyzing the SD card:
  - Formatting the SD card (`sdc format`, `sdc qformat`, `sdc llf`)
  - Reading partition information (`sdc rpi`)

### Command Overview

| Command          | Description                                      |
|-------------------|--------------------------------------------------|
| `sdc info`        | Get information about the SD card                |
| `sdc add /<f>`    | Add a folder or file to the SD card              |
| `sdc rm /<f>`     | Remove a file from the SD card                   |
| `sdc cat /<f>`    | Read a file from the SD card                     |
| `sdc echo /<file> <text>` | Append content to a file in the SD card  |
| `sdc mv /<src> /<targt>` | Rename or move a file or folder           |
| `sdc cp /<src> /<targt>` | Copy a file in the SD card                |
| `sdc mkdir /<name>` | Create a directory in the SD card              |
| `sdc rmdir /<name>` | Remove a directory from the SD card            |
| `sdc ls /<path>`  | Short list files in a directory                  |
| `sdc ll /<path>`  | List files in a directory with details           |


### Example Commad
#### 'sdc info'
The `sdc info` command provides detailed information about the SD card, including its manufacturer, product details, capacity, and filesystem type. This command is useful for verifying the SD card's specifications and ensuring compatibility with the `OFM-SDCard` library.

##### Example Output:
```plaintext
============================= SD Card Information =============================
| Manufacturer ID        | SanDisk                                            |
| Product Name           | SN64G                                              |
| Product Revision       | 80                                                 |
| Serial Number          | 12345678                                           |
| Manufacture Date       | 7 / 2020                                           |
-------------------------------------------------------------------------------
| Card Type              | SDXC (Extended Capacity)                           |
| File System            | FAT32                                              |
| Card capacity          | 59.48 GB                                           |
-------------------------------------------------------------------------------
| Used: 39.13 GB   [================================                 ] 65.79% |
| Free: 20.32 GB   [==================                               ] 34.21% |
```

This output includes:
- **Manufacturer ID**: Identifies the SD card's manufacturer.
- **Product Name**: The name or model of the SD card.
- **Product Revision**: Indicates the revision or version of the product.
- **Serial Number**: A unique identifier for the SD card.
- **Manufacture Date**: The date the SD card was manufactured.
- **Card Type**: Specifies the type of SD card (e.g., SDHC, SDXC).
- **File System**: The filesystem format used on the SD card (e.g., FAT32, exFAT).
- **Card Capacity**: The total storage capacity of the SD card.
- **Used/Free Space**: A visual representation of the used and available storage space.

Use this command to quickly assess the SD card's properties and ensure it meets the requirements for your application.


#### 'sdc ll \'
The `sdc ll \` command provides a detailed listing of files and directories within a specified path on the SD card. This command is particularly useful for inspecting the contents of a directory, as it displays additional information such as file size, type, and creation date.

##### Example Output:
```plaintext
======================== SD-Card Control - File List Long (ll) =========================
----------------------------------------------------------------------------------------
Name                                      | Size (bytes) | Type   | Created             
----------------------------------------------------------------------------------------
[.Spotlight-V100]                         |              | Dir    | 18:19:18 01.07.23   
[.fseventsd]                              |              | Dir    | 18:19:18 01.07.23   
[timelapse]                               |              | Dir    | 15:42:58 04.03.25   
[cache]                                   |              | Dir    | 11:17:20 04.03.25   
[ipcam]                                   |              | Dir    | 15:41:24 04.03.25   
FSCK0000.REC                              | 32.00 KB     | File   | 00:00:00 30.11.79   
----------------------------------------------------------------------------------------
Folders: 5           Files: 1             | Size: 32.00 KB
----------------------------------------------------------------------------------------
```

This output includes:
- **Name**: The name of the file or directory.
- **Size**: The size of the file in bytes (directories do not display a size).
- **Type**: Indicates whether the entry is a file or directory.
- **Created**: The timestamp of when the file or directory was created.

Use this command to gain a comprehensive overview of the directory structure and file details on your SD card.

### Special Commands

There are also special commands available that can be executed even if the SD card is not successfully mounted. However, the SD card must be inserted for these commands to work. Please note that these commands should be used at your own risk, as they can modify or erase data on the SD card.

| Command       | Description                                                                 |
|---------------|-----------------------------------------------------------------------------|
| `sdc format`  | **ATTENTION**: Will format (Auto: exFAT/FAT32) the external SD card.        |
| `sdc qformat` | **ATTENTION**: Quick format (FAT32) the external SD card.(MBR Delete)       |
| `sdc llf`     | **ATTENTION**: Low-level format the external SD card.                       |

### Formatting Options

The `OFM-SDCard` library provides several formatting options for SD cards. These commands allow you to prepare the SD card for use by erasing existing data and setting up the desired filesystem.

- **Format**: Performs a full format of the SD card. This process erases all data and sets up the filesystem (Auto: exFAT/FAT32).
- **Quick Format**: Deletes the Master Boot Record (MBR) and performs a quick format of the SD card, setting up the FAT32 filesystem.
- **Low-Level Format**: Executes a low-level format of the SD card, which resets the card to its factory state. Use this option with caution as it may permanently erase all data.

These commands should be used carefully, as they can result in data loss. To prevent accidental execution, a confirmation prompt will appear. You will need to re-enter the same command followed by `yes` to proceed it again. This ensures an additional layer of safety, even if the command is typed accidentally.

### Special Command for Partition Table Analysis

| Command   | Description                                                                 |
|-----------|-----------------------------------------------------------------------------|
| `sdc rpi` | Reads partition information of the SD card (GPT or MBR).                   |

This special command is useful for diagnosing issues when an SD card is not recognized. It allows you to inspect the partition table (MBR) and GUID partition table (GPT) of the SD card, providing detailed information about partition types, GUIDs, and sizes.

Key features of this command:
- **Partition Table Analysis**: Displays detailed information about the MBR or GPT, including partition count, types, and sizes.
- **Troubleshooting**: Helps identify potential issues with the SD card's structure, such as corrupted or unsupported partitions.

This command can be executed even if the SD card is not successfully mounted, as long as the card is inserted. It is a valuable tool for diagnosing and resolving SD card recognition problems.

#### Example: Reading the Partition MBR of a SUPPORTED SD-Card (exFAT)
```plaintext
============================== MBR Partition Table =============================
| Partition Count           | 4 (Valids will be listed below)
================================================================================
-------------------------------------------------------------------------------
| Partition 1            | 32768           - 250347519      
| Bootflag               | 0x0 
| Type                   | 0x7  - exFAT/NTFS     
-------------------------------------------------------------------------------
| Partition 2            | 0               - 4294967295     
| Bootflag               | 0x0 
| Type                   | 0x0  - Unknown     
-------------------------------------------------------------------------------
| Partition 3            | 0               - 4294967295     
| Bootflag               | 0x0 
| Type                   | 0x0  - Unknown     
-------------------------------------------------------------------------------
| Partition 4            | 0               - 4294967295     
| Bootflag               | 0x0 
| Type                   | 0x0  - Unknown     
-------------------------------------------------------------------------------
===============================================================================
```

#### Example: Reading the Partition MBR of a not Supported SD-Card
```plaintext
============================== MBR Partition Table =============================
| Partition Count           | 4 (Valids will be listed below)
================================================================================
-------------------------------------------------------------------------------
| Partition 1            | 8192            - 532480         
| Bootflag               | 0x0 
| Type                   | 0xC  - FAT32 (LBA)     
-------------------------------------------------------------------------------
| Partition 2            | 540672          - 62521343       
| Bootflag               | 0x0 
| Type                   | 0x83 - Linux     
-------------------------------------------------------------------------------
| Partition 3            | 0               - 4294967295     
| Bootflag               | 0x0 
| Type                   | 0x0  - Unknown     
-------------------------------------------------------------------------------
| Partition 4            | 0               - 4294967295     
| Bootflag               | 0x0 
| Type                   | 0x0  - Unknown     
-------------------------------------------------------------------------------
==============================================================================
```

#### Example: Reading the GUID Partition Table of a not Supported SD-Card

```plaintext
> sdc rgpt
============================== GPT Partition Table =============================
| Partition Entry LBA       | 2
| Partition Count           | 128 (Valids will be listed below)
================================================================================
-------------------------------------------------------------------------------
| Partition 1               | 40              - 409639         
| Partition GUID            | c12a7328-f81f-11d2-ba4b-00a0c93ec93b              
-------------------------------------------------------------------------------
| Partition 2               | 411648          - 49238015       
| Partition GUID            | ebd0a0a2-b9e5-4433-87c0-68b6b72699c7              
-------------------------------------------------------------------------------
| Partition 3               | 49238016           - 98064383          
| Partition GUID            | ebd0a0a2-b9e5-4433-87c0-68b6b72699c7              
-------------------------------------------------------------------------------
| Partition 4               | 98064384           - 146890751         
| Partition GUID            | ebd0a0a2-b9e5-4433-87c0-68b6b72699c7              
-------------------------------------------------------------------------------
| Partition 5               | 146890752          - 195717119         
| Partition GUID            | ebd0a0a2-b9e5-4433-87c0-68b6b72699c7              
-------------------------------------------------------------------------------
| Partition 6               | 195717120          - 250345471         
| Partition GUID            | ebd0a0a2-b9e5-4433-87c0-68b6b72699c7              
-------------------------------------------------------------------------------
===============================================================================
```

On the RP2040/2350 platform, only SD cards formatted with FAT16 and FAT32 filesystems are currently supported. In contrast, the ESP32 platform supports FAT16, FAT32, and additionally exFAT filesystems. Other filesystems are not supported at this time. The size of the SD card does not appear to be a limiting factor, although this has not been extensively tested.

If you need guidance on formatting an SD card, there are numerous tutorials and resources available online. For the best results, consider searching for instructions in English, as they often provide detailed and comprehensive steps for formatting SD cards using various tools and operating systems.

