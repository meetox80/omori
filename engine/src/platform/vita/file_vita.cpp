#include "../platform.h"

#include <psp2/io/fcntl.h>

#include <cstring>

namespace plat {

constexpr size_t READ_AHEAD = 64 * 1024;

struct File {
    SceUID fd;
    int64_t pos;
    int64_t buf_at;
    size_t buf_len;
    uint8_t buf[READ_AHEAD];
};

File* file_open(const std::string& path) {
    SceUID fd = sceIoOpen(path.c_str(), SCE_O_RDONLY, 0);
    if (fd < 0) return nullptr;
    File* f = new File;
    f->fd = fd;
    f->pos = 0;
    f->buf_at = 0;
    f->buf_len = 0;
    return f;
}

static bool fill(File* f) {
    if (sceIoLseek(f->fd, f->pos, SCE_SEEK_SET) != f->pos) return false;
    int got = sceIoRead(f->fd, f->buf, READ_AHEAD);
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
        if (want >= READ_AHEAD) {
            if (sceIoLseek(f->fd, f->pos, SCE_SEEK_SET) != f->pos) break;
            int got = sceIoRead(f->fd, out + done, (SceSize)want);
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

int64_t file_size(File* f) { return sceIoLseek(f->fd, 0, SCE_SEEK_END); }

void file_close(File* f) {
    if (!f) return;
    sceIoClose(f->fd);
    delete f;
}

}
