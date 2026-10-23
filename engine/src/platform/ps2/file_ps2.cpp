#include "../platform.h"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace plat {

constexpr size_t READ_AHEAD = 32 * 1024;
constexpr size_t PIECE = 128 * 1024;

struct Part {
    int fd;
    int64_t at;
    int64_t size;
};

struct Source {
    std::string path;
    int refs = 0;
    int64_t size = 0;
    std::vector<Part> parts;
};

struct File {
    Source* src;
    int64_t pos;
    int64_t buf_at;
    size_t buf_len;
    uint8_t buf[READ_AHEAD];
};

static Mutex* g_lock = nullptr;
static std::vector<Source*> g_sources;

static void ensure_lock() {
    if (!g_lock) g_lock = mutex_create();
}

static size_t read_at(File* f, int64_t at, void* dst, size_t size) {
    Source* s = f->src;
    if (at < 0 || at >= s->size) return 0;
    if ((int64_t)size > s->size - at) size = (size_t)(s->size - at);
    uint8_t* out = (uint8_t*)dst;
    size_t done = 0;
    size_t part = 0;
    while (done < size) {
        int64_t pos = at + (int64_t)done;
        while (pos >= s->parts[part].at + s->parts[part].size) part++;
        const Part& p = s->parts[part];
        size_t piece = (size_t)std::min<int64_t>((int64_t)std::min(PIECE, size - done), p.at + p.size - pos);
        mutex_lock(g_lock);
        size_t got = 0;
        if (lseek(p.fd, (off_t)(pos - p.at), SEEK_SET) == (off_t)(pos - p.at)) {
            while (got < piece) {
                ssize_t n = read(p.fd, out + done + got, piece - got);
                if (n <= 0) break;
                got += (size_t)n;
            }
        }
        mutex_unlock(g_lock);
        done += got;
        if (got < piece) break;
    }
    return done;
}

static size_t part_ext(const std::string& path) {
    size_t dot = path.rfind('.');
    if (dot == std::string::npos || path.size() < dot + 4) return std::string::npos;
    const char* e = path.c_str() + dot + 1;
    bool pak = (e[0] == 'P' || e[0] == 'p') && (e[1] == 'A' || e[1] == 'a') && (e[2] == 'K' || e[2] == 'k');
    bool end = path.size() == dot + 4 || path[dot + 4] == ';';
    return pak && end ? dot + 1 : std::string::npos;
}

static Source* open_source(const std::string& path) {
    for (Source* s : g_sources) {
        if (s->path == path) {
            s->refs++;
            return s;
        }
    }
    Source* s = new Source();
    s->path = path;
    s->refs = 1;
    size_t ext = part_ext(path);
    for (int i = 0; i < 10; i++) {
        std::string name = path;
        if (i) name.replace(ext, 3, std::string(name[ext] == 'p' ? "pk" : "PK") + (char)('0' + i));
        int fd = open(name.c_str(), O_RDONLY);
        off_t end = fd >= 0 ? lseek(fd, 0, SEEK_END) : -1;
        if (fd >= 0 && end < 0) close(fd);
        if (end < 0) break;
        s->parts.push_back({fd, s->size, (int64_t)end});
        s->size += s->parts.back().size;
        if (ext == std::string::npos) break;
    }
    if (s->parts.empty()) {
        delete s;
        return nullptr;
    }
    g_sources.push_back(s);
    return s;
}

File* file_open(const std::string& path) {
    ensure_lock();
    mutex_lock(g_lock);
    Source* s = open_source(path);
    mutex_unlock(g_lock);
    if (!s) return nullptr;
    File* f = new File;
    f->src = s;
    f->pos = 0;
    f->buf_at = 0;
    f->buf_len = 0;
    return f;
}

static bool fill(File* f) {
    size_t got = read_at(f, f->pos, f->buf, READ_AHEAD);
    if (got == 0) return false;
    f->buf_at = f->pos;
    f->buf_len = got;
    return true;
}

size_t file_read(File* f, void* dst, size_t size) {
    uint8_t* out = (uint8_t*)dst;
    size_t done = 0;
    while (done < size) {
        if (f->pos >= f->buf_at && f->pos < f->buf_at + (int64_t)f->buf_len) {
            size_t at = (size_t)(f->pos - f->buf_at);
            size_t n = f->buf_len - at;
            if (n > size - done) n = size - done;
            std::memcpy(out + done, f->buf + at, n);
            done += n;
            f->pos += (int64_t)n;
            continue;
        }
        size_t want = size - done;
        if (want >= READ_AHEAD) {
            size_t got = read_at(f, f->pos, out + done, want);
            if (got == 0) break;
            done += got;
            f->pos += (int64_t)got;
            continue;
        }
        if (!fill(f)) break;
    }
    return done;
}

bool file_seek(File* f, int64_t pos) {
    if (pos < 0) return false;
    f->pos = pos;
    return true;
}

int64_t file_size(File* f) { return f->src->size; }

void file_close(File* f) {
    if (!f) return;
    mutex_lock(g_lock);
    Source* s = f->src;
    if (--s->refs == 0) {
        for (size_t i = 0; i < g_sources.size(); i++) {
            if (g_sources[i] == s) {
                g_sources.erase(g_sources.begin() + (long)i);
                break;
            }
        }
        for (Part& p : s->parts) close(p.fd);
        delete s;
    }
    mutex_unlock(g_lock);
    delete f;
}

}
