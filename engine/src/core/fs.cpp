#include "fs.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

#include "../platform/platform.h"

namespace fs {

bool read_file(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size < 0) {
        std::fclose(f);
        return false;
    }
    out.resize((size_t)size);
    size_t got = size ? std::fread(out.data(), 1, (size_t)size, f) : 0;
    std::fclose(f);
    return got == (size_t)size;
}

bool write_file(const std::string& path, const void* data, size_t size) {
    std::string tmp = path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(data, 1, size, f) == size;
    ok = (std::fclose(f) == 0) && ok;
    if (!ok) return false;
    std::remove(path.c_str());
    return std::rename(tmp.c_str(), path.c_str()) == 0;
}

bool exists(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

bool remove_file(const std::string& path) { return std::remove(path.c_str()) == 0; }

std::string save_path(const std::string& rel) { return plat::save_root() + "/" + rel; }

struct Entry {
    uint32_t name;
    uint32_t offset;
    uint32_t size;
};

struct DataFile {
    FILE* f = nullptr;
    int64_t base = 0;
    int64_t size = 0;
    int64_t pos = 0;
    std::vector<uint8_t> mem;
    bool memory = false;
    bool pooled = false;
};

static std::string g_source;
static std::string g_dir;
static std::vector<FILE*> g_idle;
static plat::Mutex* g_idle_lock = nullptr;
constexpr size_t MAX_IDLE = 4;

static FILE* take_pack_handle() {
    if (!g_idle_lock) g_idle_lock = plat::mutex_create();
    FILE* f = nullptr;
    plat::mutex_lock(g_idle_lock);
    if (!g_idle.empty()) {
        f = g_idle.back();
        g_idle.pop_back();
    }
    plat::mutex_unlock(g_idle_lock);
    return f ? f : std::fopen(g_source.c_str(), "rb");
}

static void give_pack_handle(FILE* f) {
    plat::mutex_lock(g_idle_lock);
    bool keep = g_idle.size() < MAX_IDLE;
    if (keep) g_idle.push_back(f);
    plat::mutex_unlock(g_idle_lock);
    if (!keep) std::fclose(f);
}

void drop_idle_handles() {
    if (!g_idle_lock) return;
    plat::mutex_lock(g_idle_lock);
    std::vector<FILE*> old;
    old.swap(g_idle);
    plat::mutex_unlock(g_idle_lock);
    for (FILE* f : old) std::fclose(f);
}
static bool g_packed = false;
static int64_t g_base = 0;
static std::vector<Entry> g_entries;
static std::vector<char> g_names;

static uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

static int64_t pack_base(FILE* f) {
    uint8_t h[40];
    if (std::fseek(f, 0, SEEK_SET) != 0 || std::fread(h, 1, 8, f) != 8) return -1;
    if (std::memcmp(h, "TPAK", 4) == 0) return 0;
    if (std::memcmp(h, "\0PBP", 4) != 0 || std::fread(h + 8, 1, 32, f) != 32) return -1;
    int64_t base = rd32(h + 36);
    uint8_t m[4];
    if (std::fseek(f, (long)base, SEEK_SET) != 0 || std::fread(m, 1, 4, f) != 4 || std::memcmp(m, "TPAK", 4) != 0) return -1;
    return base;
}

bool has_pack(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    bool ok = pack_base(f) >= 0;
    std::fclose(f);
    return ok;
}

static bool load_index(FILE* f, int64_t base) {
    uint8_t h[16];
    if (std::fseek(f, (long)base, SEEK_SET) != 0 || std::fread(h, 1, 16, f) != 16) return false;
    if (std::memcmp(h, "TPAK", 4) != 0) return false;
    uint32_t version = rd32(h + 4);
    uint32_t count = 0, names = 0;
    if (version == 1) {
        count = rd32(h + 8);
        names = rd32(h + 12);
    } else if (version == 2) {
        uint8_t t[16];
        if (std::fseek(f, -16, SEEK_END) != 0 || std::fread(t, 1, 16, f) != 16 || std::memcmp(t, "TPKI", 4) != 0) return false;
        count = rd32(t + 4);
        names = rd32(t + 8);
        if (std::fseek(f, (long)(base + rd32(t + 12)), SEEK_SET) != 0) return false;
    } else {
        return false;
    }
    std::vector<uint8_t> raw((size_t)count * 12);
    if (count && std::fread(raw.data(), 1, raw.size(), f) != raw.size()) return false;
    g_names.assign(names + 1, 0);
    if (names && std::fread(g_names.data(), 1, names, f) != names) return false;
    g_entries.resize(count);
    for (uint32_t i = 0; i < count; i++) {
        g_entries[i] = {rd32(&raw[i * 12]), rd32(&raw[i * 12 + 4]), rd32(&raw[i * 12 + 8])};
        if (g_entries[i].name >= names) return false;
    }
    return true;
}

bool init_data(const std::string& source) {
    g_source = source;
    g_packed = false;
    g_entries.clear();
    g_names.clear();
    struct stat st;
    bool is_dir = stat(source.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
    FILE* f = is_dir ? nullptr : std::fopen(source.c_str(), "rb");
    if (f) {
        int64_t base = pack_base(f);
        bool ok = base >= 0 && load_index(f, base);
        std::fclose(f);
        if (ok) {
            g_packed = true;
            g_base = base;
            plat::log("data: pack %s (%d files)", source.c_str(), (int)g_entries.size());
            return true;
        }
        g_entries.clear();
        size_t slash = source.find_last_of('/');
        g_dir = (slash == std::string::npos ? std::string(".") : source.substr(0, slash)) + "/data";
        plat::log("data: no pack in %s, using %s", source.c_str(), g_dir.c_str());
        return true;
    }
    g_dir = source;
    plat::log("data: folder %s", g_dir.c_str());
    return true;
}

bool data_packed() { return g_packed; }

static const Entry* find(const std::string& rel) {
    const char* key = rel.c_str();
    auto it = std::lower_bound(g_entries.begin(), g_entries.end(), key, [](const Entry& e, const char* k) {
        return std::strcmp(&g_names[e.name], k) < 0;
    });
    if (it == g_entries.end() || std::strcmp(&g_names[it->name], key) != 0) return nullptr;
    return &*it;
}

DataFile* open_data(const std::string& rel) {
    if (g_packed) {
        const Entry* e = find(rel);
        if (!e) return nullptr;
        FILE* f = take_pack_handle();
        if (!f) return nullptr;
        DataFile* d = new DataFile();
        d->f = f;
        d->pooled = true;
        d->base = g_base + e->offset;
        d->size = e->size;
        if (std::fseek(f, (long)d->base, SEEK_SET) != 0) {
            data_close(d);
            return nullptr;
        }
        return d;
    }
    FILE* f = std::fopen((g_dir + "/" + rel).c_str(), "rb");
    if (!f) return nullptr;
    DataFile* d = new DataFile();
    d->f = f;
    std::fseek(f, 0, SEEK_END);
    d->size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (d->size < 0) {
        data_close(d);
        return nullptr;
    }
    return d;
}

DataFile* open_data_memory(const std::string& rel, size_t max_bytes) {
    DataFile* d = open_data(rel);
    if (!d || d->size < 0 || (uint64_t)d->size > max_bytes) return d;
    d->mem.resize((size_t)d->size);
    size_t got = d->size ? std::fread(d->mem.data(), 1, d->mem.size(), d->f) : 0;
    if (got != d->mem.size()) {
        d->mem.clear();
        std::fseek(d->f, (long)d->base, SEEK_SET);
        return d;
    }
    if (d->pooled) give_pack_handle(d->f);
    else std::fclose(d->f);
    d->f = nullptr;
    d->memory = true;
    d->pos = 0;
    return d;
}

size_t data_read(DataFile* d, void* dst, size_t size) {
    int64_t left = d->size - d->pos;
    if (left <= 0) return 0;
    if ((int64_t)size > left) size = (size_t)left;
    if (d->memory) {
        std::memcpy(dst, d->mem.data() + d->pos, size);
        d->pos += (int64_t)size;
        return size;
    }
    size_t got = std::fread(dst, 1, size, d->f);
    d->pos += (int64_t)got;
    return got;
}

int data_seek(DataFile* d, int64_t off, int whence) {
    int64_t p = whence == SEEK_SET ? off : whence == SEEK_CUR ? d->pos + off : d->size + off;
    if (p < 0 || p > d->size) return -1;
    if (d->memory) {
        d->pos = p;
        return 0;
    }
    if (std::fseek(d->f, (long)(d->base + p), SEEK_SET) != 0) return -1;
    d->pos = p;
    return 0;
}

int64_t data_tell(DataFile* d) { return d->pos; }

int64_t data_size(DataFile* d) { return d ? d->size : 0; }

void data_close(DataFile* d) {
    if (!d) return;
    if (d->f) {
        if (d->pooled) give_pack_handle(d->f);
        else std::fclose(d->f);
    }
    delete d;
}

bool read_data(const std::string& rel, std::vector<uint8_t>& out) {
    DataFile* d = open_data(rel);
    if (!d) return false;
    out.resize((size_t)d->size);
    size_t got = d->size ? data_read(d, out.data(), out.size()) : 0;
    bool ok = got == out.size();
    data_close(d);
    return ok;
}

bool data_exists(const std::string& rel) {
    if (g_packed) return find(rel) != nullptr;
    DataFile* d = open_data(rel);
    if (!d) return false;
    data_close(d);
    return true;
}

}
