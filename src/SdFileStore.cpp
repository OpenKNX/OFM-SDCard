// Backward-compat: flag renamed OPENKNX_SD_CARD_MODULE_ENABLE -> OPENKNX_SDCARD; old name still works.
#if defined(OPENKNX_SD_CARD_MODULE_ENABLE) && !defined(OPENKNX_SDCARD)
    #define OPENKNX_SDCARD
#endif
#include "SdFileStore.h"
#ifdef OPENKNX_SDCARD
    #include "SDCardModule.h"

namespace sd
{
    IFileStore fileStore;

    static FSFILE _src;
    static FSFILE _sink;
    static FSFILE _dir;

    bool IFileStore::available() { return sdCardModule.isCardInserted() && sdCardModule.isMounted(); }
    uint64_t IFileStore::totalBytes() { return sdCardModule.getSDCardSize(); }
    uint64_t IFileStore::freeBytes()
    {
        uint64_t freeB = 0, usedB = 0, totalB = 0;
        if (sdCardModule.getCachedUsage(freeB, usedB, totalB)) return freeB;
        if (!sdCardModule.isUsageScanRunning()) sdCardModule.beginUsageScan();
        return 0;
    }

    int32_t IFileStore::open(const char *path)
    {
        if (!sdCardModule.isMounted()) return -1;
        _src = sdCardModule.open(path, "r");
        if (!_src) return -1;
        return (int32_t)_src.fileSize();
    }
    // 0 only at true EOF; retry transient reads (never 0 mid-file -> no silent truncation). Bounded.
    uint8_t IFileStore::read(uint32_t offset, uint8_t *buf, uint8_t len)
    {
        if (!_src || buf == nullptr || len == 0) return 0;
        if ((uint64_t)offset >= _src.fileSize()) return 0;
        if (_src.curPosition() != offset && !_src.seekSet(offset)) return 0;
        for (uint8_t attempt = 0; attempt < 4; ++attempt)
        {
            const int r = _src.read(buf, len);
            if (r > 0) return (uint8_t)r;
            if (!_src.seekSet(offset)) break;
        }
        return 0;
    }
    void IFileStore::close() { _src.close(); }

    bool IFileStore::exists(const char *path) { return sdCardModule.exists(path); }

    bool IFileStore::sinkOpen(const char *path, uint32_t offset)
    {
        if (!sdCardModule.isMounted()) return false;
        _sink = sdCardModule.open(path, offset ? "r+" : "w");
        if (!_sink) return false;
        if (offset && !_sink.seekSet(offset))
        {
            _sink.close();
            return false;
        }
        return true;
    }
    int IFileStore::sinkWrite(const uint8_t *buf, uint16_t len)
    {
        if (!_sink || buf == nullptr || len == 0) return -1;
        return (int)_sink.write(buf, len);
    }
    void IFileStore::sinkClose() { _sink.close(); }

    bool IFileStore::dirOpen(const char *path)
    {
        if (!sdCardModule.isCardInserted() || !sdCardModule.isMounted()) return false;
        _dir = sdCardModule.open((path && *path) ? path : "/", "r");
        return (bool)_dir;
    }
    uint8_t IFileStore::dirNext(char *nameOut, uint16_t cap, uint32_t *sizeOut)
    {
        if (!_dir || cap == 0) return 0;
        FSFILE e = _dir.openNextFile();
        if (!e) return 0;
        e.getName(nameOut, cap);
        const bool isDir = e.isDirectory();
        if (sizeOut) *sizeOut = isDir ? 0 : (uint32_t)e.size();
        e.close();
        return isDir ? 2 : 1;
    }
    void IFileStore::dirClose() { _dir.close(); }

    bool IFileStore::remove(const char *path) { return sdCardModule.remove(path); }
    bool IFileStore::mkdir(const char *path) { return sdCardModule.mkdir(path); }
    bool IFileStore::rmdir(const char *path) { return sdCardModule.rmdir(path); }
} // namespace sd
#endif
