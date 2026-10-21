#include "../platform.h"

#include <fcntl.h>
#include <malloc.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>

namespace plat {

constexpr size_t READ_AHEAD = 512 * 1024;

struct File {
    int fd;
    int64_t pos;
    int64_t buf_at;
    size_t buf_len;
    uint8_t* buf;
};

File* file_open(const std::string& path) {
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) return nullptr;
    uint8_t* buf = (uint8_t*)memalign(64, READ_AHEAD);
    if (!buf) {
        close(fd);
        return nullptr;
    }
    File* f = new File;
    f->fd = fd;
    f->pos = 0;
    f->buf_at = 0;
    f->buf_len = 0;
    f->buf = buf;
    return f;
}

static bool seek_to(File* f, int64_t pos) { return lseek(f->fd, (off_t)pos, SEEK_SET) == (off_t)pos; }

static bool fill(File* f) {
    if (!seek_to(f, f->pos)) return false;
    ssize_t got = read(f->fd, f->buf, READ_AHEAD);
    if (got <= 0) return false;
    f->buf_at = f->pos;
    f->buf_len = (size_t)got;
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
        if (want >= READ_AHEAD && ((uintptr_t)(out + done) & 63) == 0) {
            want &= ~(size_t)63;
            if (!seek_to(f, f->pos)) break;
            ssize_t got = read(f->fd, out + done, want);
            if (got <= 0) break;
            done += (size_t)got;
            f->pos += got;
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

int64_t file_size(File* f) { return (int64_t)lseek(f->fd, 0, SEEK_END); }

void file_close(File* f) {
    if (!f) return;
    close(f->fd);
    std::free(f->buf);
    delete f;
}

}
