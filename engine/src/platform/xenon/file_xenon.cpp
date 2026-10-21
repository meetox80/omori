#include "../platform.h"

#include <dirent.h>
#include <fcntl.h>
#include <ppc/register.h>
#include <ppc/xenonsprs.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstring>
#include <string>
#include <vector>

#include "io_xenon.h"

namespace xenon_io {

alignas(128) static volatile unsigned int g_word = 0;
static volatile int g_owner = -1;
static int g_depth = 0;

int thread_id() { return (int)mfspr(pir); }

void lock() {
    int id = thread_id();
    if (g_owner == id) {
        g_depth++;
        return;
    }
    while (__atomic_exchange_n(&g_word, 1u, __ATOMIC_ACQUIRE)) {
        asm volatile("or 1,1,1");
        while (g_word) {
        }
        asm volatile("or 2,2,2");
    }
    g_owner = id;
    g_depth = 1;
}

void unlock() {
    if (--g_depth > 0) return;
    g_owner = -1;
    __atomic_store_n(&g_word, 0u, __ATOMIC_RELEASE);
}

}

extern "C" {

int __real_open(const char* path, int flags, int mode);
ssize_t __real_read(int fd, void* dst, size_t len);
ssize_t __real_write(int fd, const void* src, size_t len);
off_t __real_lseek(int fd, off_t pos, int whence);
int __real_close(int fd);
int __real_fstat(int fd, struct stat* st);
int __real_stat(const char* path, struct stat* st);
int __real_mkdir(const char* path, mode_t mode);
int __real_unlink(const char* path);
int __real_rename(const char* from, const char* to);
int __real_ftruncate(int fd, off_t len);
int __real_fsync(int fd);
DIR* __real_opendir(const char* path);
struct dirent* __real_readdir(DIR* dir);
int __real_closedir(DIR* dir);

int __wrap_open(const char* path, int flags, int mode) {
    xenon_io::Guard g;
    return __real_open(path, flags, mode);
}

ssize_t __wrap_read(int fd, void* dst, size_t len) {
    xenon_io::Guard g;
    return __real_read(fd, dst, len);
}

ssize_t __wrap_write(int fd, const void* src, size_t len) {
    xenon_io::Guard g;
    return __real_write(fd, src, len);
}

off_t __wrap_lseek(int fd, off_t pos, int whence) {
    xenon_io::Guard g;
    return __real_lseek(fd, pos, whence);
}

int __wrap_close(int fd) {
    xenon_io::Guard g;
    return __real_close(fd);
}

int __wrap_fstat(int fd, struct stat* st) {
    xenon_io::Guard g;
    return __real_fstat(fd, st);
}

int __wrap_stat(const char* path, struct stat* st) {
    xenon_io::Guard g;
    return __real_stat(path, st);
}

int __wrap_mkdir(const char* path, mode_t mode) {
    xenon_io::Guard g;
    return __real_mkdir(path, mode);
}

int __wrap_unlink(const char* path) {
    xenon_io::Guard g;
    return __real_unlink(path);
}

int __wrap_rename(const char* from, const char* to) {
    xenon_io::Guard g;
    return __real_rename(from, to);
}

int __wrap_ftruncate(int fd, off_t len) {
    xenon_io::Guard g;
    return __real_ftruncate(fd, len);
}

int __wrap_fsync(int fd) {
    xenon_io::Guard g;
    return __real_fsync(fd);
}

DIR* __wrap_opendir(const char* path) {
    xenon_io::Guard g;
    return __real_opendir(path);
}

struct dirent* __wrap_readdir(DIR* dir) {
    xenon_io::Guard g;
    return __real_readdir(dir);
}

int __wrap_closedir(DIR* dir) {
    xenon_io::Guard g;
    return __real_closedir(dir);
}

}

namespace plat {

constexpr size_t READ_AHEAD = 64 * 1024;

struct Part {
    int fd;
    int64_t start;
    int64_t size;
    int64_t pos;
};

struct Source {
    std::string path;
    std::vector<Part> parts;
    int64_t size = 0;
};

struct File {
    Source* src;
    int64_t pos;
    int64_t buf_at;
    size_t buf_len;
    uint8_t buf[READ_AHEAD];
};

static std::vector<Source*> g_sources;

static bool add_part(Source* s, const std::string& path) {
    int fd = open(path.c_str(), O_RDONLY, 0);
    if (fd < 0) return false;
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size <= 0) {
        close(fd);
        return false;
    }
    s->parts.push_back({fd, s->size, (int64_t)st.st_size, 0});
    s->size += (int64_t)st.st_size;
    return true;
}

static Source* source_for(const std::string& path) {
    for (Source* s : g_sources) {
        if (s->path == path) return s;
    }
    Source* s = new Source();
    s->path = path;
    if (!add_part(s, path)) {
        delete s;
        return nullptr;
    }
    for (int i = 1; add_part(s, path + "." + std::to_string(i)); i++) {
    }
    g_sources.push_back(s);
    return s;
}

File* file_open(const std::string& path) {
    xenon_io::Guard g;
    Source* s = source_for(path);
    if (!s) return nullptr;
    File* f = new File;
    f->src = s;
    f->pos = 0;
    f->buf_at = 0;
    f->buf_len = 0;
    return f;
}

static size_t read_at(Source* s, int64_t at, void* dst, size_t size) {
    xenon_io::Guard g;
    uint8_t* out = (uint8_t*)dst;
    size_t done = 0;
    for (Part& p : s->parts) {
        if (done == size) break;
        int64_t end = p.start + p.size;
        if (at >= end) continue;
        if (at < p.start) break;
        int64_t local = at - p.start;
        if (p.pos != local) {
            if (lseek(p.fd, (off_t)local, SEEK_SET) != (off_t)local) {
                p.pos = -1;
                break;
            }
            p.pos = local;
        }
        size_t want = size - done;
        if ((int64_t)want > end - at) want = (size_t)(end - at);
        while (want > 0) {
            ssize_t got = read(p.fd, out + done, want);
            if (got <= 0) {
                p.pos = -1;
                return done;
            }
            done += (size_t)got;
            want -= (size_t)got;
            at += got;
            p.pos += got;
        }
    }
    return done;
}

static bool fill(File* f) {
    size_t got = read_at(f->src, f->pos, f->buf, READ_AHEAD);
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
            size_t got = read_at(f->src, f->pos, out + done, want);
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

void file_close(File* f) { delete f; }

}
