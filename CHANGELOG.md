# Changes

SD card for OpenKNX devices: a card reader offered to the rest of the firmware as a file-store provider,
with on-device management and a display widget. Entries are grouped by module version; every line traces
to a commit in this repository.


## 0.1.1: 2026-10-23

* Change: the SD card menu, the file browser, the format confirmations and toasts and the partition info are English; comments and log lines too
* Change: the `sdc` console commands are built only with `OPENKNX_CONSOLE_DEV`; without it the module answers no `sdc` command
* Fix: the SD card widget is built only together with the SD card module; a display build without it no longer compiles the widget


## 0.1.0: 2026-09-15

First tagged state.

**File store**
* Change: the module is a pure provider -- it exposes `sd::IFileStore` and no longer reaches into the file transfer client
* Feature: positioned write, rename and a bounded directory listing on `SdFileStore`
* Feature: `isDir()` and a busy guard, so a second operation cannot start while one is running
* Feature: `sinkOpen` preallocates and takes a resume offset; `getFileList` is capped, so a full card cannot stall the loop
* Fix: directories are listed through `getName` and `Statistics`, which avoids an exFAT fault

**On-device management**
* Feature: non-blocking format, volume label and partition information
* Feature: file management from the browser -- info, delete, rename
* Feature: SD backend for the file transfer client, with real file timestamps
* Fix: the SD console commands and `read()` are hardened against short and out-of-bounds input

**Display**
* Feature: an SD-card widget, which shows how long it stays on screen

**Build flags**
* Change: `OPENKNX_SD_CARD_MODULE_ENABLE` is `OPENKNX_SDCARD`; the old name stays as an alias

**Version**
* Change: `version()` is read from `library.json`
