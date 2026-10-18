#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: embed <input> <output.c> <symbol>\n");
        return 2;
    }
    FILE* in = fopen(argv[1], "rb");
    if (!in) {
        fprintf(stderr, "embed: cannot read %s\n", argv[1]);
        return 1;
    }
    FILE* out = fopen(argv[2], "wb");
    if (!out) {
        fprintf(stderr, "embed: cannot write %s\n", argv[2]);
        return 1;
    }
    fprintf(out, "#include <stddef.h>\nconst unsigned char %s[] = {\n", argv[3]);
    unsigned char buf[65536];
    size_t n, total = 0;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        for (size_t i = 0; i < n; i++) {
            fprintf(out, "%u,", buf[i]);
            if (++total % 32 == 0) fputc('\n', out);
        }
    }
    if (!total) fprintf(out, "0");
    fprintf(out, "};\nconst size_t %s_size = %lu;\n", argv[3], (unsigned long)total);
    fclose(in);
    return fclose(out) == 0 ? 0 : 1;
}
