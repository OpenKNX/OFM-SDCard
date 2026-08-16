// Backward-compat: flag renamed OPENKNX_SD_CARD_MODULE_ENABLE -> OPENKNX_SDCARD; old name still works.
#if defined(OPENKNX_SD_CARD_MODULE_ENABLE) && !defined(OPENKNX_SDCARD)
    #define OPENKNX_SDCARD
#endif
#pragma once
// sd::IFileStore — SD-card file store; identical API to efc::IFileStore, no shared base. SdFat-free header.
#ifdef OPENKNX_SDCARD
    #include <cstdint>

namespace sd
{
    class IFileStore
    {
      public:
        bool available();
        bool busy();                   // a read/write transfer handle is open -> serialize one transfer per drive
        uint64_t totalBytes();
        uint64_t freeBytes();
        int32_t open(const char *path);
        uint8_t read(uint32_t offset, uint8_t *buf, uint8_t len);
        void close();
        bool exists(const char *path);
        bool isDir(const char *path);  // stat-based type check (no transfer handle -> never blocked by busy())
        bool sinkOpen(const char *path, uint32_t offset = 0, uint32_t sizeHint = 0); // sizeHint>0 -> preAllocate (fresh only)
        int sinkWrite(const uint8_t *buf, uint16_t len);
        int sinkWriteAt(uint32_t offset, const uint8_t *buf, uint16_t len); // positioned write (FTC chunk)
        void sinkClose();
        bool dirOpen(const char *path);
        uint8_t dirNext(char *nameOut, uint16_t cap, uint32_t *sizeOut = nullptr); // 0 end · 1 file · 2 dir
        void dirClose();
        bool remove(const char *path);
        bool mkdir(const char *path);
        bool rmdir(const char *path);
        bool rename(const char *oldPath, const char *newPath);
    };

    extern IFileStore fileStore;
} // namespace sd
#endif
