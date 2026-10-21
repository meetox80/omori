#include "../platform.h"

#include <sys/file.h>

#include <cstdio>
#include <cstring>

namespace plat {

constexpr size_t READ_AHEAD = 64 * 1024;

struct File {
    s32 fd;
    int64_t pos;
    int64_t buf_at;
    size_t buf_len;
    uint8_t buf[READ_AHEAD];
};

File* file_open(const std::string& path) {
    s32 fd;
    if (sysLv2FsOpen(path.c_str(), SYS_O_RDONLY, &fd, 0, nullptr, 0) != 0) return nullptr;
    File* f = new File;
    f->fd = fd;
    f->pos = 0;
    f->buf_at = 0;
    f->buf_len = 0;
    return f;
}

static size_t read_at(File* f, int64_t at, void* dst, size_t size) {
    u64 where = 0;
    if (sysLv2FsLSeek64(f->fd, (u64)at, SEEK_SET, &where) != 0 || where != (u64)at) return 0;
    u64 got = 0;
    if (sysLv2FsRead(f->fd, dst, size, &got) != 0) return 0;
    return (size_t)got;
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

int64_t file_size(File* f) {
    sysFSStat st;
    if (sysLv2FsFStat(f->fd, &st) != 0) return -1;
    return (int64_t)st.st_size;
}

void file_close(File* f) {
    if (!f) return;
    sysLv2FsClose(f->fd);
    delete f;
}

}
