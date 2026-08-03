// Backward-compat: flag renamed OPENKNX_SD_CARD_MODULE_ENABLE -> OPENKNX_SDCARD; old name still works.
#if defined(OPENKNX_SD_CARD_MODULE_ENABLE) && !defined(OPENKNX_SDCARD)
    #define OPENKNX_SDCARD
#endif
#include "SdFileStore.h"
#ifdef OPENKNX_SDCARD
    #include <vector>
    #include "SDCardModule.h"

namespace sd
{
    IFileStore fileStore;

    static FSFILE _src;
    static FSFILE _sink;
    static std::vector<SdDirEntry> _dirEntries; // one snapshot per listing (capped); FsFile stays inside the module
    static size_t _dirIdx = 0;

    bool IFileStore::available() { return sdCardModule.isCardInserted() && sdCardModule.isMounted(); }
    // A read (_src) or write (_sink) transfer is live. Both share the single SPI bus + a single static
    // handle each, so we serialize: one transfer per drive at a time (guards open()/sinkOpen()).
    bool IFileStore::busy() { return (bool)_src || (bool)_sink; }
    uint64_t IFileStore::totalBytes() { return sdCardModule.getSDCardSize(); }
    uint64_t IFileStore::freeBytes()
    {
        // Only the cached result; do NOT kick beginUsageScan() here. The module's incremental free-cluster
        // scan has proven to reboot on some large exFAT cards, and `df` must never crash -> total-only is fine
        // until the SD module's scan is fixed. 0 = unknown (the caller shows "total" without a free figure).
        uint64_t freeB = 0, usedB = 0, totalB = 0;
        return sdCardModule.getCachedUsage(freeB, usedB, totalB) ? freeB : 0;
    }

    int32_t IFileStore::open(const char *path)
    {
        if (busy()) return -1; // another transfer holds a handle -> refuse (no cursor hijack)
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

    // Statistics() opens a module-local handle and stats it -> no FsFile crosses the module boundary
    // (the old dir-iteration corruption) and no transfer handle is touched. Statistics prepends "/".
    bool IFileStore::isDir(const char *path)
    {
        if (path == nullptr || *path == '\0') return false;
        FileInfo info;
        const char *rel = (path[0] == '/') ? path + 1 : path;
        return sdCardModule.Statistics("", rel, info) && info.isDir;
    }

    bool IFileStore::sinkOpen(const char *path, uint32_t offset)
    {
        if (busy()) return false; // another transfer holds a handle -> refuse (no concurrent write on one drive)
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
    int IFileStore::sinkWriteAt(uint32_t offset, const uint8_t *buf, uint16_t len)
    {
        if (!_sink || buf == nullptr || len == 0) return -1;
        if (_sink.curPosition() != offset && !_sink.seekSet(offset)) return -1;
        return (int)_sink.write(buf, len);
    }
    void IFileStore::sinkClose() { _sink.close(); }

    bool IFileStore::dirOpen(const char *path)
    {
        _dirEntries.clear();
        _dirIdx = 0;
        if (!sdCardModule.isCardInserted() || !sdCardModule.isMounted()) return false;
        // Snapshot the directory once (capped at 512) so dirNext() serves one entry per FTC round-trip
        // without holding an FsFile open across ticks. listDir() uses the module's proven getName()+
        // Statistics() mechanism -> no FsFile crosses the module boundary, no stat on an openNextFile handle.
        sdCardModule.listDir((path && *path) ? path : "/", _dirEntries, 512);
        return true;
    }
    uint8_t IFileStore::dirNext(char *nameOut, uint16_t cap, uint32_t *sizeOut)
    {
        if (cap == 0 || _dirIdx >= _dirEntries.size()) return 0;
        const SdDirEntry &e = _dirEntries[_dirIdx++];
        strncpy(nameOut, e.name.c_str(), cap - 1);
        nameOut[cap - 1] = '\0';
        if (sizeOut) *sizeOut = e.isDir ? 0 : (uint32_t)e.size;
        return e.isDir ? 2 : 1;
    }
    void IFileStore::dirClose()
    {
        _dirEntries.clear();
        _dirIdx = 0;
    }

    bool IFileStore::remove(const char *path) { return sdCardModule.remove(path); }
    bool IFileStore::mkdir(const char *path) { return sdCardModule.mkdir(path); }
    bool IFileStore::rmdir(const char *path) { return sdCardModule.rmdir(path); }
    bool IFileStore::rename(const char *oldPath, const char *newPath) { return sdCardModule.rename(oldPath, newPath); }
} // namespace sd
#endif
