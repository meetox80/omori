#include "../platform.h"

#include <cstdio>

namespace plat {

struct File {
    FILE* f;
};

static bool seek_to(FILE* f, int64_t pos, int whence) {
#if defined(_WIN32)
    return _fseeki64(f, pos, whence) == 0;
#elif defined(PSP)
    return std::fseek(f, (long)pos, whence) == 0;
#else
    return fseeko(f, (off_t)pos, whence) == 0;
#endif
}

static int64_t tell(FILE* f) {
#if defined(_WIN32)
    return _ftelli64(f);
#elif defined(PSP)
    return std::ftell(f);
#else
    return (int64_t)ftello(f);
#endif
}

File* file_open(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return nullptr;
#if defined(__3DS__)
    std::setvbuf(f, nullptr, _IOFBF, 64 * 1024);
#endif
    return new File{f};
}

size_t file_read(File* f, void* dst, size_t size) { return std::fread(dst, 1, size, f->f); }

bool file_seek(File* f, int64_t pos) { return pos >= 0 && seek_to(f->f, pos, SEEK_SET); }

int64_t file_size(File* f) {
    int64_t at = tell(f->f);
    if (!seek_to(f->f, 0, SEEK_END)) return -1;
    int64_t size = tell(f->f);
    seek_to(f->f, at, SEEK_SET);
    return size;
}

void file_close(File* f) {
    if (!f) return;
    std::fclose(f->f);
    delete f;
}

}
