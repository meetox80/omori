#include "vpk.h"

#include <stdlib.h>
#include <string.h>

#include "../third_party/miniz.h"

static const char TEMPLATE[] = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                               "<livearea style=\"a1\" format-ver=\"01.00\" content-rev=\"1\">\n"
                               "  <livearea-background>\n"
                               "    <image>bg.png</image>\n"
                               "  </livearea-background>\n"
                               "  <gate>\n"
                               "    <startup-image>startup.png</startup-image>\n"
                               "  </gate>\n"
                               "</livearea>\n";

static int finish(mz_zip_archive* zip, const char* path) {
    void* data = NULL;
    size_t size = 0;
    int ok = mz_zip_writer_finalize_heap_archive(zip, &data, &size);
    mz_zip_writer_end(zip);
    if (!ok) return 0;
    char* tmp = str_fmt("%s.tmp", path);
    ok = file_write(tmp, data, size) && file_replace(tmp, path);
    if (!ok) remove(tmp);
    free(tmp);
    mz_free(data);
    return ok;
}

static int add(mz_zip_archive* zip, const char* name, const void* data, size_t len) {
    return mz_zip_writer_add_mem(zip, name, data, len, MZ_BEST_COMPRESSION);
}

int vpk_write(const char* path, const Vpk* v) {
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!mz_zip_writer_init_heap(&zip, 0, 1 << 20)) return 0;
    int ok = add(&zip, "eboot.bin", v->eboot, v->eboot_len) && add(&zip, "sce_sys/param.sfo", v->sfo, v->sfo_len) &&
             add(&zip, "sce_sys/icon0.png", v->icon0.data, v->icon0.len) && add(&zip, "sce_sys/livearea/contents/bg.png", v->bg.data, v->bg.len) &&
             add(&zip, "sce_sys/livearea/contents/startup.png", v->startup.data, v->startup.len) &&
             add(&zip, "sce_sys/livearea/contents/template.xml", TEMPLATE, sizeof(TEMPLATE) - 1);
    if (!ok) {
        mz_zip_writer_end(&zip);
        return 0;
    }
    return finish(&zip, path);
}

int vpk_is_vpk(const char* path) {
    FILE* f = file_open(path, "rb");
    uint8_t h[4];
    int ok = f && fread(h, 1, 4, f) == 4 && h[0] == 'P' && h[1] == 'K' && h[2] == 3 && h[3] == 4;
    if (f) fclose(f);
    return ok;
}

int vpk_replace_eboot(const char* path, const uint8_t* eboot, size_t len) {
    Buf old = {0};
    if (!file_read(path, &old)) return 0;
    mz_zip_archive in, out;
    memset(&in, 0, sizeof(in));
    memset(&out, 0, sizeof(out));
    int ok = mz_zip_reader_init_mem(&in, old.data, old.len, 0);
    if (ok && !mz_zip_writer_init_heap(&out, 0, 1 << 20)) {
        mz_zip_reader_end(&in);
        ok = 0;
    }
    if (!ok) {
        buf_free(&old);
        return 0;
    }
    int found = 0;
    mz_uint n = mz_zip_reader_get_num_files(&in);
    for (mz_uint i = 0; i < n && ok; i++) {
        char name[512];
        mz_zip_reader_get_filename(&in, i, name, sizeof(name));
        if (!strcmp(name, "eboot.bin")) {
            found = 1;
            continue;
        }
        ok = mz_zip_writer_add_from_zip_reader(&out, &in, i);
    }
    ok = ok && found && add(&out, "eboot.bin", eboot, len);
    mz_zip_reader_end(&in);
    buf_free(&old);
    if (!ok) {
        mz_zip_writer_end(&out);
        return 0;
    }
    return finish(&out, path);
}
