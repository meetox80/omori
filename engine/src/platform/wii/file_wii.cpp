#include "../platform.h"
#include "wii.h"

#include <dvm.h>
#include <malloc.h>
#include <ogc/usbstorage.h>
#include <sdcard/gcsd.h>
#include <sdcard/wiisd_io.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace plat {

constexpr unsigned CACHE_PAGES = 4;
constexpr unsigned PAGE_SECTORS = 64;
constexpr uint32_t WINDOW_BYTES = 32 * 1024;
constexpr size_t MAX_RUNS = 16384;
constexpr int64_t MAP_MIN_BYTES = 16 << 20;

struct Volume {
    std::string name;
    DvmDisc* disc;
    sec_t start;
};

struct Run {
    uint32_t first;
    uint32_t count;
    sec_t sector;
};

struct Map {
    std::string path;
    DvmDisc* disc = nullptr;
    int64_t size = 0;
    uint32_t sector_bytes = 0;
    uint32_t cluster_sectors = 0;
    uint32_t window = 0;
    std::vector<Run> runs;
};

struct File {
    FILE* f = nullptr;
    Map* map = nullptr;
    int64_t pos = 0;
    uint8_t* buf = nullptr;
    int64_t buf_at = 0;
    size_t buf_len = 0;
};

static std::vector<Volume> g_volumes;
static std::vector<Map*> g_maps;
static std::vector<std::string> g_unmapped;
static Mutex* g_lock = nullptr;

static uint32_t rd16(const uint8_t* p) { return p[0] | (p[1] << 8); }
static uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

static unsigned mount_disc(const char* base, const DISC_INTERFACE* iface) {
    DvmDisc* disc = dvmDiscCreate(iface);
    if (!disc) return 0;
    disc = dvmDiscCacheCreate(disc, CACHE_PAGES, PAGE_SECTORS);
    DvmPartInfo parts[4];
    unsigned count = dvmReadPartitionTable(disc, parts, 4, DVM_IDENT_FSTYPE);
    unsigned mounted = 0;
    for (unsigned i = 0; i < count; i++) {
        if (!parts[i].fstype) continue;
        std::string name = base;
        if (parts[i].index) name += (char)('1' + parts[i].index);
        if (!dvmMountVolume(name.c_str(), disc, parts[i].start_sector, parts[i].fstype)) continue;
        mounted++;
        if (std::strcmp(parts[i].fstype, "vfat") == 0) g_volumes.push_back({name, disc, parts[i].start_sector});
    }
    if (mounted) dvmDiscAddUser(disc);
    else disc->vt->destroy(disc);
    return mounted;
}

static Map* build_map(const std::string& path) {
    size_t colon = path.find(':');
    if (colon == std::string::npos) return nullptr;
    const Volume* vol = nullptr;
    for (const Volume& v : g_volumes) {
        if (path.compare(0, colon, v.name) == 0 && v.name.size() == colon) vol = &v;
    }
    struct stat st;
    if (!vol || stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < MAP_MIN_BYTES || st.st_ino < 2) return nullptr;
    uint32_t bps = 1u << vol->disc->sector_shift;
    uint32_t window = std::max<uint32_t>(1, std::min<uint32_t>(PAGE_SECTORS, WINDOW_BYTES / bps));
    uint8_t* buf = (uint8_t*)memalign(32, window * bps);
    if (!buf) return nullptr;
    Map* m = new Map();
    bool ok = dvmDiscReadSectors(vol->disc, buf, vol->start, 1);
    uint32_t spc = buf[0x0D], rsvd = rd16(buf + 0x0E), nfats = buf[0x10], roots = rd16(buf + 0x11);
    uint32_t total = rd16(buf + 0x13) ? rd16(buf + 0x13) : rd32(buf + 0x20);
    uint32_t fatsz = rd16(buf + 0x16) ? rd16(buf + 0x16) : rd32(buf + 0x24);
    uint32_t data = rsvd + nfats * fatsz + (roots * 32 + bps - 1) / bps;
    ok = ok && rd16(buf + 0x0B) == bps && spc && !(spc & (spc - 1)) && rsvd && nfats && fatsz && total > data;
    uint32_t clusters = ok ? (total - data) / spc : 0;
    bool fat32 = clusters > 0xFFF5;
    ok = ok && clusters > 0xFF5;
    uint64_t cluster_bytes = (uint64_t)spc * bps;
    uint64_t need = ok ? ((uint64_t)st.st_size + cluster_bytes - 1) / cluster_bytes : 0;
    sec_t fat_start = vol->start + rsvd, fat_end = fat_start + fatsz;
    sec_t loaded = 0, loaded_end = 0;
    uint32_t cl = (uint32_t)st.st_ino;
    for (uint64_t i = 0; ok && i < need; i++) {
        if (cl < 2 || cl >= clusters + 2) {
            ok = false;
            break;
        }
        sec_t sector = vol->start + data + (sec_t)(cl - 2) * spc;
        if (!m->runs.empty() && m->runs.back().sector + m->runs.back().count * spc == sector) {
            m->runs.back().count++;
        } else if (m->runs.size() < MAX_RUNS) {
            m->runs.push_back({(uint32_t)i, 1, sector});
        } else {
            ok = false;
            break;
        }
        if (i + 1 == need) break;
        uint64_t off = (uint64_t)cl * (fat32 ? 4 : 2);
        sec_t s = fat_start + (sec_t)(off / bps);
        if (s < loaded || s >= loaded_end) {
            sec_t w = std::max(s & ~(sec_t)(window - 1), fat_start);
            sec_t end = std::min((s & ~(sec_t)(window - 1)) + window, fat_end);
            if (s >= fat_end || !dvmDiscReadSectors(vol->disc, buf, w, end - w)) {
                ok = false;
                break;
            }
            loaded = w;
            loaded_end = end;
        }
        const uint8_t* e = buf + (size_t)(s - loaded) * bps + off % bps;
        cl = fat32 ? rd32(e) & 0x0FFFFFFF : rd16(e);
    }
    std::free(buf);
    if (!ok) {
        delete m;
        return nullptr;
    }
    m->path = path;
    m->disc = vol->disc;
    m->size = st.st_size;
    m->sector_bytes = bps;
    m->cluster_sectors = spc;
    m->window = window;
    return m;
}

static bool fill(File* f, int64_t at) {
    const Map* m = f->map;
    uint32_t ci = (uint32_t)(at / ((int64_t)m->cluster_sectors * m->sector_bytes));
    auto it = std::upper_bound(m->runs.begin(), m->runs.end(), ci, [](uint32_t v, const Run& r) { return v < r.first; });
    if (it == m->runs.begin()) return false;
    --it;
    if (ci >= it->first + it->count) return false;
    int64_t run_at = (int64_t)it->first * m->cluster_sectors * m->sector_bytes;
    sec_t run_end = it->sector + it->count * m->cluster_sectors;
    sec_t s = it->sector + (sec_t)((at - run_at) / m->sector_bytes);
    sec_t w = std::max(s & ~(sec_t)(m->window - 1), it->sector);
    sec_t end = std::min((s & ~(sec_t)(m->window - 1)) + m->window, run_end);
    if (!f->buf) f->buf = (uint8_t*)memalign(32, m->window * m->sector_bytes);
    if (!f->buf || !dvmDiscReadSectors(m->disc, f->buf, w, end - w)) return false;
    f->buf_at = run_at + (int64_t)(w - it->sector) * m->sector_bytes;
    f->buf_len = (size_t)(end - w) * m->sector_bytes;
    return true;
}

static bool same_bytes(Map* m, FILE* fp, int64_t at, size_t n) {
    std::vector<uint8_t> a(n), b(n);
    File probe;
    probe.map = m;
    bool ok = true;
    for (size_t done = 0; ok && done < n;) {
        int64_t pos = at + (int64_t)done;
        if (pos < probe.buf_at || pos >= probe.buf_at + (int64_t)probe.buf_len) ok = fill(&probe, pos);
        if (!ok) break;
        size_t c = (size_t)std::min<int64_t>((int64_t)(n - done), probe.buf_at + (int64_t)probe.buf_len - pos);
        std::memcpy(a.data() + done, probe.buf + (pos - probe.buf_at), c);
        done += c;
    }
    std::free(probe.buf);
    ok = ok && fseeko(fp, (off_t)at, SEEK_SET) == 0 && std::fread(b.data(), 1, n, fp) == n;
    return ok && a == b;
}

static Map* find_map(const std::string& path) {
    if (!g_lock) g_lock = mutex_create();
    mutex_lock(g_lock);
    Map* found = nullptr;
    for (Map* m : g_maps) {
        if (m->path == path) found = m;
    }
    bool skip = std::find(g_unmapped.begin(), g_unmapped.end(), path) != g_unmapped.end();
    if (!found && !skip) {
        found = build_map(path);
        FILE* fp = found ? std::fopen(path.c_str(), "rb") : nullptr;
        size_t n = found ? (size_t)std::min<int64_t>(found->size, 4096) : 0;
        if (found && !(fp && same_bytes(found, fp, 0, n) && same_bytes(found, fp, found->size - (int64_t)n, n))) {
            delete found;
            found = nullptr;
        }
        if (fp) std::fclose(fp);
        if (found) {
            g_maps.push_back(found);
            log("direct reads for %s: %u runs, %u KB clusters", path.c_str(), (unsigned)found->runs.size(),
                (unsigned)(found->cluster_sectors * found->sector_bytes / 1024));
        } else {
            g_unmapped.push_back(path);
        }
    }
    mutex_unlock(g_lock);
    return found;
}

File* file_open(const std::string& path) {
    if (Map* m = find_map(path)) {
        File* f = new File();
        f->map = m;
        return f;
    }
    FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) return nullptr;
    File* f = new File();
    f->f = fp;
    return f;
}

size_t file_read(File* f, void* dst, size_t size) {
    if (!f->map) return std::fread(dst, 1, size, f->f);
    int64_t left = f->map->size - f->pos;
    if (left <= 0) return 0;
    if ((int64_t)size > left) size = (size_t)left;
    uint8_t* out = (uint8_t*)dst;
    size_t done = 0;
    while (done < size) {
        if ((f->pos < f->buf_at || f->pos >= f->buf_at + (int64_t)f->buf_len) && !fill(f, f->pos)) break;
        size_t n = (size_t)std::min<int64_t>((int64_t)(size - done), f->buf_at + (int64_t)f->buf_len - f->pos);
        std::memcpy(out + done, f->buf + (f->pos - f->buf_at), n);
        done += n;
        f->pos += (int64_t)n;
    }
    return done;
}

bool file_seek(File* f, int64_t pos) {
    if (pos < 0) return false;
    if (!f->map) return fseeko(f->f, (off_t)pos, SEEK_SET) == 0;
    if (pos > f->map->size) return false;
    f->pos = pos;
    return true;
}

int64_t file_size(File* f) {
    if (f->map) return f->map->size;
    off_t at = ftello(f->f);
    if (fseeko(f->f, 0, SEEK_END) != 0) return -1;
    int64_t size = (int64_t)ftello(f->f);
    fseeko(f->f, at, SEEK_SET);
    return size;
}

void file_close(File* f) {
    if (!f) return;
    if (f->f) std::fclose(f->f);
    std::free(f->buf);
    delete f;
}

}

namespace wii {

bool mount_storage() {
    unsigned mounted = plat::mount_disc("sd", &__io_wiisd);
    mounted += plat::mount_disc("usb", &__io_usbstorage);
    mounted += plat::mount_disc("carda", &__io_gcsda);
    mounted += plat::mount_disc("cardb", &__io_gcsdb);
    if (mounted && __system_argv->argvMagic == ARGV_MAGIC && __system_argv->argc >= 1) {
        const char* argv0 = __system_argv->argv[0];
        const char* slash = std::strrchr(argv0, '/');
        if (slash) chdir(std::string(argv0, slash + 1).c_str());
    }
    return mounted != 0;
}

}
