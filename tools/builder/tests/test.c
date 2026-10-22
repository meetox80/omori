#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "adpcm.h"
#include "crypto.h"
#include "image.h"
#include "jpeg.h"
#include "js.h"
#include "jsops.h"
#include "rx.h"
#include "script.h"
#include "yaml.h"
#include "miniz.h"
#define STBI_NO_STDIO
#include "stb_image.h"
#include "movie.h"
#include "node.h"
#include "ogg.h"
#include "pack.h"
#include "target.h"
#include "webm.h"
#include "util.h"

static int g_fail = 0, g_run = 0;

#define CHECK(cond)                                                      \
    do {                                                                 \
        g_run++;                                                         \
        if (!(cond)) {                                                   \
            g_fail++;                                                    \
            fprintf(stderr, "fail %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                \
    } while (0)

static void to_json(const Node* n, Buf* b) {
    char tmp[64];
    switch (n->type) {
    case N_NULL: buf_put(b, "null", 4); break;
    case N_TRUE: buf_put(b, "true", 4); break;
    case N_FALSE: buf_put(b, "false", 5); break;
    case N_NUM:
        snprintf(tmp, sizeof(tmp), "%g", n->num);
        buf_put(b, tmp, strlen(tmp));
        break;
    case N_STR:
        buf_u8(b, '"');
        buf_put(b, n->str, n->len);
        buf_u8(b, '"');
        break;
    case N_BYTES: buf_put(b, "bytes", 5); break;
    case N_ARR:
        buf_u8(b, '[');
        for (size_t i = 0; i < n->count; i++) {
            if (i) buf_u8(b, ',');
            to_json(n->items[i], b);
        }
        buf_u8(b, ']');
        break;
    case N_OBJ:
        buf_u8(b, '{');
        for (size_t i = 0; i < n->count; i++) {
            if (i) buf_u8(b, ',');
            buf_u8(b, '"');
            buf_put(b, n->keys[i], strlen(n->keys[i]));
            buf_put(b, "\":", 2);
            to_json(n->items[i], b);
        }
        buf_u8(b, '}');
        break;
    }
}

static void unhex(const char* h, uint8_t* out) {
    for (size_t i = 0; h[i * 2]; i++) {
        unsigned v;
        sscanf(h + i * 2, "%2x", &v);
        out[i] = (uint8_t)v;
    }
}

static void test_crypto(void) {
    uint8_t key[32], iv[16], data[64], want[64];
    unhex("603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4", key);
    unhex("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff", iv);
    unhex("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e5130c81c46a35ce411e5fbc1191a0a52eff69f2445df4f9b17ad2b417be66c3710", data);
    unhex("601ec313775789a5b7a7f504bbf3d228f443e3ca4d62b59aca84e990cacaf5c52b0930daa23de94ce87017ba2d84988ddfc9c58db67aada613c2dd08457941a6", want);
    Aes256 a;
    aes256_init(&a, key);
    aes256_ctr(&a, iv, data, 64);
    CHECK(!memcmp(data, want, 64));
    uint8_t file[16 + 5];
    memcpy(file, iv, 16);
    memcpy(file + 16, "hello", 5);
    aes256_ctr(&a, iv, file + 16, 5);
    size_t len = sizeof(file);
    CHECK(aes_file_decrypt(&a, file, &len) && len == 5 && !memcmp(file, "hello", 5));
    uint8_t mv[16 + 20];
    static const uint8_t head[16] = {0x52, 0x50, 0x47, 0x4d, 0x56, 0, 0, 0, 0, 3, 1, 0, 0, 0, 0, 0};
    uint8_t mk[16];
    for (int i = 0; i < 16; i++) mk[i] = (uint8_t)(i * 7 + 3);
    memcpy(mv, head, 16);
    for (int i = 0; i < 20; i++) mv[16 + i] = (uint8_t)(i < 16 ? (0x80 + i) ^ mk[i] : 0x80 + i);
    len = sizeof(mv);
    CHECK(mv_decrypt(mv, &len, mk) && len == 20);
    int ok = 1;
    for (int i = 0; i < 20; i++) ok &= mv[i] == 0x80 + i;
    CHECK(ok);
    uint8_t plain[4] = {'O', 'g', 'g', 'S'};
    len = 4;
    CHECK(!mv_decrypt(plain, &len, mk) && len == 4);
}

static int js_ok(const char* src) {
    char err[200];
    Node* n = js_compile_str(src, err, sizeof(err));
    if (!n) fprintf(stderr, "compile failed: %s (%s)\n", src, err);
    node_free(n);
    return n != NULL;
}

static int js_bad(const char* src) {
    char err[200];
    Node* n = js_compile_str(src, err, sizeof(err));
    node_free(n);
    return n == NULL;
}

static void test_scripts(void) {
    CHECK(js_ok("$gameSelfSwitches.setValue([12, 3, 'A'], true);"));
    CHECK(js_ok("var x = 1, y\nx += 2\nif (x > 1) { y = `a${x}b` } else y = null"));
    CHECK(js_ok("const f = (a, b) => a + b; let g = v => { return v * 2 }; f(1, g(2))"));
    CHECK(js_ok("for (var i = 0; i < 3; i++) { if (i === 1) continue; else break; }"));
    CHECK(js_ok("for (var k in obj) delete obj[k]; for (const v of [1, 2]) v;"));
    CHECK(js_ok("switch (a) { case 1: b(); break; default: c(); }"));
    CHECK(js_ok("try { x() } catch (e) { y() } finally { z() }"));
    CHECK(js_ok("var o = {a: 1, 'b': 2, 3: c, d, e() { return 1 }, [k]: 4}; o?.a; o?.[k]; f?.(1)"));
    CHECK(js_ok("this._calculatedBaseDmg = user.atk * 3 - b.def; value = typeof x === 'undefined' ? -1 : x % 2 ** 3;"));
    CHECK(js_ok("return\n5"));
    CHECK(js_ok("x = /ab+c/i.test(y); y = a / b / c; z = s.replace(/\\d+/g, '')"));
    CHECK(js_bad("UP: 1"));
    CHECK(js_bad("a = (1"));
    char err[200];
    Node* n = js_compile_str("a.b(1)", err, sizeof(err));
    CHECK(n && (int)n->items[0]->num == JS_PROGRAM);
    Node* stmt = n ? n->items[1]->items[0] : NULL;
    CHECK(stmt && (int)stmt->items[0]->num == JS_EXPR && (int)stmt->items[1]->items[0]->num == JS_CALL);
    node_free(n);
    Node* r = script_compile_route("MOVE TO: 12, 7 collide", "t", 0);
    CHECK(r && r->items[0]->num == 0 && !strcmp(r->items[1]->str, "MOVE_TO_POINT") && r->items[2]->num == 12 && r->items[3]->num == 7 &&
          r->items[4]->type == N_TRUE);
    node_free(r);
    r = script_compile_route("this.setCustomFrameXY(0, 1)", "t", 0);
    CHECK(r && (int)r->items[0]->num == JS_PROGRAM);
    node_free(r);
    r = script_compile_route("balloon: question", "t", 0);
    CHECK(r && !strcmp(r->items[1]->str, "BALLOON") && r->items[2]->num == 2);
    node_free(r);
    r = script_compile_route("Move lower right until wall", "t", 0);
    CHECK(r && !strcmp(r->items[1]->str, "MOVE_UNTIL_STOP") && r->items[2]->num == 4 && r->items[3]->type == N_FALSE);
    node_free(r);
    r = script_compile_route("opacity: 50%", "t", 0);
    CHECK(r && !strcmp(r->items[1]->str, "OPACITY_SET") && r->items[2]->num == 128);
    node_free(r);
    r = script_compile_route("index: +2", "t", 0);
    CHECK(r && !strcmp(r->items[1]->str, "INDEX_ADJUST") && r->items[2]->num == 2);
    node_free(r);
    r = script_compile_route("hug left wall touch", "t", 0);
    CHECK(r && !strcmp(r->items[1]->str, "HUG_LEFT") && r->items[2]->type == N_TRUE);
    node_free(r);
}

static void test_rx(void) {
    RxMatch m;
    Rx* rx = rx_compile("(?:SELF VARIABLE)[ ](.*):[ ](.*)", "i");
    const char* s = "self variable 2: v+1: 3";
    CHECK(rx && rx_match_str(rx, s, &m));
    char* g1 = rx_group(&m, s, 1);
    char* g2 = rx_group(&m, s, 2);
    CHECK(!strcmp(g1, "2: v+1") && !strcmp(g2, "3"));
    free(g1);
    free(g2);
    rx_free(rx);
    CHECK(rx_test("\\bfoo\\b", "", "a foo b"));
    CHECK(!rx_test("^b", "", "ab"));
    CHECK(rx_test("^b", "m", "a\nb"));
    rx = rx_compile("(a*)*b", "");
    CHECK(rx_match_str(rx, "aaab", &m) && m.end[1] - m.start[1] == 3);
    rx_free(rx);
    rx = rx_compile("\\x1bV\\[(\\d+)\\]", "gi");
    CHECK(rx_match_str(rx, "say \x1bv[12] now", &m) && m.start[0] == 4 && m.end[1] - m.start[1] == 2);
    CHECK(!rx_match_str(rx, "say V[12] now", &m));
    rx_free(rx);
    CHECK(rx_test("cat|dog", "", "hotdog"));
    CHECK(rx_test("x?y", "", "zzy"));
    CHECK(rx_test("[^a]b", "", "aacb"));
    CHECK(!rx_test("[^a]b", "", "ab"));
    CHECK(rx_test("(?=b)b", "", "aab"));
    CHECK(rx_test("a*", "", ""));
    CHECK(rx_test("$", "", "abc"));
    CHECK(rx_test("(a)\\1", "", "xaa"));
}

static void test_yaml(void) {
    const char* src = "a: 1\nb: 'it''s'\nc: \"x\\ty\"\nd:\n  - one\n  - two: 2\n    three: 3.5\ne: |\n  line1\n  line2\nf: >\n  folded\n  text\n\n  para\ng: {x: 0, y: [1, 2]}\nh: !\ni: ~\nj: plain text\n  continued\n";
    char err[200];
    Node* n = yaml_parse(src, strlen(src), err, sizeof(err));
    CHECK(n != NULL);
    if (!n) return;
    CHECK(node_numv(node_get(n, "a"), 0) == 1);
    CHECK(!strcmp(node_strv(node_get(n, "b")), "it's"));
    CHECK(!strcmp(node_strv(node_get(n, "c")), "x\ty"));
    Node* d = node_get(n, "d");
    CHECK(d && d->count == 2 && !strcmp(node_strv(d->items[0]), "one") && node_numv(node_get(d->items[1], "three"), 0) == 3.5);
    CHECK(!strcmp(node_strv(node_get(n, "e")), "line1\nline2\n"));
    CHECK(!strcmp(node_strv(node_get(n, "f")), "folded text\npara\n"));
    CHECK(node_numv(node_at(node_get(node_get(n, "g"), "y"), 1), 0) == 2);
    CHECK(node_get(n, "h")->type == N_NULL && node_get(n, "i")->type == N_NULL);
    CHECK(!strcmp(node_strv(node_get(n, "j")), "plain text continued"));
    node_free(n);
    CHECK(yaml_parse("a: 1\na: 2\n", 10, err, sizeof(err)) == NULL);
}

static void test_json_vbin(void) {
    const char* src = "{\"a\":[1,-2,3.5,null,true,false,\"t\\u00e9xt\",{\"k\":\"v\"}],\"big\":1099511627776}";
    Node* n = json_parse(src, strlen(src), NULL, 0);
    CHECK(n != NULL);
    CHECK(!strcmp(node_strv(node_at(node_get(n, "a"), 6)), "t\xC3\xA9xt"));
    Buf b = {0};
    vbin_dump(n, &b);
    CHECK(b.len > 12 && !memcmp(b.data, "VBIN", 4));
    static const uint8_t big[] = {4, 0, 0, 0, 0, 0, 0, 0x70, 0x42};
    int found = 0;
    for (size_t i = 0; i + sizeof(big) <= b.len; i++) {
        if (!memcmp(b.data + i, big, sizeof(big))) found = 1;
    }
    CHECK(found);
    static const uint8_t ints[] = {6, 8, 3, 2, 3, 3, 4};
    found = 0;
    for (size_t i = 0; i + sizeof(ints) <= b.len; i++) {
        if (!memcmp(b.data + i, ints, sizeof(ints))) found = 1;
    }
    CHECK(found);
    buf_free(&b);
    node_free(n);
    CHECK(json_parse("{\"a\":", 5, NULL, 0) == NULL);
}

static uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }

static void test_texture(void) {
    Image* img = img_new(70, 37);
    uint32_t s = 1;
    for (int i = 0; i < 70 * 37 * 4; i++) {
        s = s * 1103515245u + 12345u;
        img->px[i] = (uint8_t)(s >> 16);
    }
    for (int i = 0; i < 70 * 37; i++) {
        uint8_t* p = img->px + i * 4;
        for (int c = 0; c < 3; c++) p[c] = (uint8_t)(p[c] * p[3] / 255);
    }
    Buf t = {0};
    CHECK(tex_encode(img, 70, 37, &t));
    CHECK(t.len > 44 && !memcmp(t.data, "TTEX", 4));
    CHECK(t.data[5] == 0 && rd16(t.data + 8) == 70 && rd16(t.data + 10) == 37 && rd16(t.data + 16) == 1);
    const uint8_t* e = t.data + 20;
    int stride = rd16(e + 8), rows = rd16(e + 10);
    uint32_t off = rd32(e + 12), csize = rd32(e + 16), rsize = rd32(e + 20);
    CHECK(stride % 16 == 0 && rows % 8 == 0);
    uint8_t* raw = xmalloc(rsize);
    mz_ulong rl = rsize;
    CHECK(mz_uncompress(raw, &rl, t.data + off, csize) == MZ_OK && rl == rsize);
    int bw = stride * 4, ok = 1;
    for (int y = 0; y < 37 && ok; y++) {
        for (int x = 0; x < 70 * 4 && ok; x++) {
            size_t sw = ((size_t)(y / 8) * (size_t)(bw / 16) + (size_t)(x / 16)) * 128 + (size_t)(y % 8) * 16 + (size_t)(x % 16);
            if (raw[sw] != img->px[y * 70 * 4 + x]) ok = 0;
        }
    }
    CHECK(ok);
    free(raw);
    buf_free(&t);
    img_free(img);
    Image* big = img_new(700, 600);
    Buf tb = {0};
    CHECK(tex_encode(big, 700, 600, &tb));
    CHECK(rd16(tb.data + 16) == 4);
    buf_free(&tb);
    img_free(big);
}

static uint32_t layout_texel(TexLayout layout, const uint8_t* raw, int x, int y, int stride, int rows) {
    const uint8_t* p;
    switch (layout) {
        case TEX_PSP: {
            size_t bw = (size_t)stride * 4, xb = (size_t)x * 4;
            p = raw + ((size_t)(y / 8) * (bw / 16) + xb / 16) * 128 + (size_t)(y % 8) * 16 + xb % 16;
            return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
        }
        case TEX_PICA: {
            int row = rows - 1 - y, m = 0;
            for (int i = 0; i < 3; i++) m |= ((x >> i) & 1) << (2 * i) | ((row >> i) & 1) << (2 * i + 1);
            p = raw + (((size_t)(row >> 3) * (size_t)(stride >> 3) + (size_t)(x >> 3)) * 64 + (size_t)m) * 4;
            return (uint32_t)p[3] | (uint32_t)p[2] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[0] << 24;
        }
        case TEX_GX: {
            const uint8_t* tile = raw + ((size_t)(y >> 2) * (size_t)(stride >> 2) + (size_t)(x >> 2)) * 64;
            size_t at = (size_t)((y & 3) * 4 + (x & 3)) * 2;
            return (uint32_t)tile[at + 1] | (uint32_t)tile[32 + at] << 8 | (uint32_t)tile[32 + at + 1] << 16 | (uint32_t)tile[at] << 24;
        }
        case TEX_ARGB:
            p = raw + ((size_t)y * stride + x) * 4;
            return (uint32_t)p[1] | (uint32_t)p[2] << 8 | (uint32_t)p[3] << 16 | (uint32_t)p[0] << 24;
        case TEX_GS: {
            p = raw + ((size_t)y * stride + x) * 4;
            int a = p[3] >= 128 ? 255 : (p[3] * 255 + 64) / 128;
            return (uint32_t)(p[0] * a / 255) | (uint32_t)(p[1] * a / 255) << 8 | (uint32_t)(p[2] * a / 255) << 16 | (uint32_t)a << 24;
        }
        default:
            p = raw + ((size_t)y * stride + x) * 4;
            return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
    }
}

static void test_layouts(void) {
    Image* img = img_new(70, 37);
    uint32_t s = 7;
    for (int i = 0; i < 70 * 37; i++) {
        uint8_t* p = img->px + i * 4;
        s = s * 1103515245u + 12345u;
        p[3] = (uint8_t)(i % 5 == 0 ? 255 : (s >> 16));
        for (int c = 0; c < 3; c++) {
            s = s * 1103515245u + 12345u;
            p[c] = (uint8_t)(((s >> 16) & 255) * p[3] / 255);
        }
    }
    const Target* keep = g_target;
    for (int id = 0; id < TARGET_COUNT; id++) {
        const Target* t = target_get((TargetId)id);
        target_set(t);
        Buf out = {0};
        CHECK(tex_encode(img, 70, 37, &out));
        CHECK(rd16(out.data + 16) == 1);
        CHECK(out.data[6] == (t->layout == TEX_LINEAR ? 0 : 1));
        const uint8_t* e = out.data + 20;
        int stride = rd16(e + 8), rows = rd16(e + 10);
        uint32_t off = rd32(e + 12), csize = rd32(e + 16), rsize = rd32(e + 20);
        CHECK(rsize == (uint32_t)stride * (uint32_t)rows * 4 && stride >= 70 && rows >= 37);
        if (t->layout == TEX_PICA) CHECK(stride == 128 && rows == 64);
        if (t->layout == TEX_GX) CHECK(stride % 4 == 0 && rows % 4 == 0);
        uint8_t* raw = xmalloc(rsize);
        mz_ulong rl = rsize;
        CHECK(mz_uncompress(raw, &rl, out.data + off, csize) == MZ_OK && rl == rsize);
        int ok = 1;
        for (int y = 0; y < 37 && ok; y++) {
            for (int x = 0; x < 70 && ok; x++) {
                uint32_t got = layout_texel(t->layout, raw, x, y, stride, rows);
                const uint8_t* want = img->px + ((size_t)y * 70 + x) * 4;
                for (int c = 0; c < 4; c++) {
                    int d = (int)((got >> (c * 8)) & 255) - want[c];
                    if (d < 0) d = -d;
                    if (d > (t->layout == TEX_GS ? 4 : 0)) ok = 0;
                }
            }
        }
        CHECK(ok);
        free(raw);
        buf_free(&out);
    }
    target_set(target_get(TARGET_3DS));
    Image* big = img_new(700, 600);
    Buf tb = {0};
    CHECK(tex_encode(big, 700, 600, &tb));
    CHECK(rd16(tb.data + 16) == 6);
    for (int k = 0; k < rd16(tb.data + 16); k++) {
        const uint8_t* c = tb.data + 20 + k * 24;
        int cw = rd16(c + 4), ch = rd16(c + 6), cs = rd16(c + 8), cr = rd16(c + 10);
        CHECK(cs >= cw && cr >= ch && !(cs & (cs - 1)) && !(cr & (cr - 1)) && cs <= 512 && cr <= 512);
    }
    buf_free(&tb);
    img_free(big);
    img_free(img);
    target_set(keep);
}

static void test_face_chunks(void) {
    Image* face = img_new(424, 212);
    for (int i = 0; i < 424 * 212; i++) {
        uint8_t* p = face->px + i * 4;
        p[0] = (uint8_t)(i % 424);
        p[1] = (uint8_t)(i / 424);
        p[2] = 90;
        p[3] = 255;
    }
    const Target* keep = g_target;
    for (int id = 0; id < TARGET_COUNT; id++) {
        const Target* t = target_get((TargetId)id);
        target_set(t);
        Buf out = {0};
        CHECK(tex_from_image(face, 4, 2, 1, &out));
        int n = rd16(out.data + 16);
        int cell = (int)rint(106 * t->tex_scale);
        int split = t->layout != TEX_PICA && t->layout != TEX_PSP;
        CHECK(split ? n == 8 : n < 8);
        for (int k = 0; split && k < n; k++) {
            const uint8_t* c = out.data + 20 + k * 24;
            CHECK(rd16(c) == (k % 4) * cell && rd16(c + 2) == (k / 4) * cell && rd16(c + 4) == cell && rd16(c + 6) == cell);
        }
        buf_free(&out);
        Buf whole = {0};
        CHECK(tex_from_image(face, 4, 2, 0, &whole));
        CHECK(rd16(whole.data + 16) == 1);
        buf_free(&whole);
    }
    target_set(keep);
    img_free(face);
}

static void test_3dsx_pack(void) {
    Buf in = {0};
    buf_put(&in, "3DSX", 4);
    buf_u16(&in, 0x20);
    buf_u16(&in, 8);
    buf_u32(&in, 0);
    buf_u32(&in, 0);
    buf_u32(&in, 4);
    buf_u32(&in, 4);
    buf_u32(&in, 8);
    buf_u32(&in, 4);
    buf_u32(&in, 1);
    buf_u32(&in, 0);
    buf_u32(&in, 0);
    buf_u32(&in, 0);
    buf_u32(&in, 0);
    buf_u32(&in, 1);
    buf_put(&in, "codeconsdata", 12);
    buf_put(&in, "rel1rel2", 8);
    size_t body_len = in.len - 0x20;
    buf_put(&in, "trailing", 8);
    Exe3dsx x;
    CHECK(x3dsx_read(in.data, in.len, &x));
    CHECK(x.body.len == body_len);
    Buf smdh = {0};
    buf_put(&smdh, "SMDH", 4);
    buf_zero(&smdh, 0x36C0 - 4);
    FILE* f = tmpfile();
    CHECK(f != NULL);
    uint64_t pack_at = 0;
    CHECK(x3dsx_write(&x, &smdh, f, &pack_at));
    CHECK(pack_at % 16 == 0 && (uint64_t)ftell(f) == pack_at);
    fseek(f, 0, SEEK_SET);
    uint8_t h[0x2C];
    CHECK(fread(h, 1, sizeof(h), f) == sizeof(h));
    CHECK(!memcmp(h, "3DSX", 4) && rd16(h + 4) == 0x2C && rd32(h + 0x24) == 0x36C0 && rd32(h + 0x28) == pack_at);
    fclose(f);
    x3dsx_free(&x);
    buf_free(&smdh);
    buf_free(&in);
}

static void test_pbp_pack(void) {
    Pbp p;
    memset(&p, 0, sizeof(p));
    sfo_build("Test", &p.part[0]);
    CHECK(!memcmp(p.part[0].data, "\0PSF", 4));
    buf_put(&p.part[6], "elf", 3);
    FILE* f = tmpfile();
    CHECK(f != NULL);
    CHECK(pbp_write_head(&p, f));
    Pack pk = {0};
    CHECK(pack_open(&pk, f));
    PackEntry* first = pack_add(&pk, "maps/Map002.bin");
    buf_put(&first->data, "bb", 2);
    pack_ready(&pk, first);
    buf_put(&pack_add(&pk, "game.bin")->data, "a", 1);
    uint64_t pack_len = 0;
    CHECK(pack_finish(&pk, &pack_len));
    long size = ftell(f);
    rewind(f);
    Buf out = {0};
    buf_zero(&out, (size_t)size);
    CHECK(fread(out.data, 1, (size_t)size, f) == (size_t)size);
    fclose(f);
    Pbp back;
    CHECK(pbp_read(out.data, out.len, &back));
    CHECK(back.part[0].len == p.part[0].len && !memcmp(back.part[0].data, p.part[0].data, p.part[0].len));
    CHECK(back.part[6].len == 3 && !memcmp(back.part[6].data, "elf", 3));
    CHECK(back.part[7].len == pack_len);
    const uint8_t* t = back.part[7].data;
    CHECK(!memcmp(t, "TPAK", 4) && rd32(t + 4) == 2);
    const uint8_t* tail = t + pack_len - 16;
    CHECK(!memcmp(tail, "TPKI", 4) && rd32(tail + 4) == 2);
    const uint8_t* idx = t + rd32(tail + 12);
    const char* names = (const char*)idx + 2 * 12;
    CHECK(rd32(tail + 8) == strlen("game.bin") + 1 + strlen("maps/Map002.bin") + 1);
    CHECK(!strcmp(names + rd32(idx), "game.bin") && !strcmp(names + rd32(idx + 12), "maps/Map002.bin"));
    CHECK(rd32(idx + 4) % 64 == 0 && rd32(idx + 8) == 1 && t[rd32(idx + 4)] == 'a');
    CHECK(rd32(idx + 16) % 64 == 0 && rd32(idx + 20) == 2 && t[rd32(idx + 16)] == 'b');
    pbp_free(&p);
    pbp_free(&back);
    pack_free(&pk);
    buf_free(&out);
}

static void eb_id(Buf* b, uint32_t id) {
    if (id > 0xFFFFFF) buf_u8(b, (uint8_t)(id >> 24));
    if (id > 0xFFFF) buf_u8(b, (uint8_t)(id >> 16));
    if (id > 0xFF) buf_u8(b, (uint8_t)(id >> 8));
    buf_u8(b, (uint8_t)id);
}

static void eb_el(Buf* b, uint32_t id, const void* data, size_t len) {
    eb_id(b, id);
    buf_u8(b, 0x01);
    for (int i = 6; i >= 0; i--) buf_u8(b, (uint8_t)(len >> (i * 8)));
    buf_put(b, data, len);
}

static void eb_uint(Buf* b, uint32_t id, uint64_t v) {
    uint8_t d[8];
    for (int i = 0; i < 8; i++) d[i] = (uint8_t)(v >> ((7 - i) * 8));
    eb_el(b, id, d, 8);
}

static void eb_float(Buf* b, uint32_t id, double v) {
    uint64_t u;
    memcpy(&u, &v, 8);
    eb_uint(b, id, u);
}

static void eb_wrap(Buf* b, uint32_t id, Buf* inner) {
    eb_el(b, id, inner->data, inner->len);
    buf_free(inner);
}

static void eb_unknown(Buf* b, uint32_t id) {
    eb_id(b, id);
    buf_put(b, "\x01\xFF\xFF\xFF\xFF\xFF\xFF\xFF", 8);
}

static void block_head(Buf* b, int track, int rel, uint8_t flags) {
    buf_u8(b, (uint8_t)(0x80 | track));
    buf_u8(b, (uint8_t)((rel >> 8) & 0xFF));
    buf_u8(b, (uint8_t)(rel & 0xFF));
    buf_u8(b, flags);
}

static void fill(Buf* b, uint8_t v, size_t n) {
    for (size_t i = 0; i < n; i++) buf_u8(b, v);
}

static void test_webm(void) {
    Buf f = {0}, t = {0}, x = {0}, y = {0};
    eb_el(&t, 0x4282, "webm", 4);
    eb_wrap(&f, 0x1A45DFA3, &t);
    eb_unknown(&f, 0x18538067);
    eb_uint(&t, 0x2AD7B1, 1000000);
    eb_float(&t, 0x4489, 1500.0);
    eb_wrap(&f, 0x1549A966, &t);
    eb_uint(&x, 0xD7, 1);
    eb_uint(&x, 0x83, 1);
    eb_el(&x, 0x86, "V_VP8", 5);
    eb_uint(&y, 0xB0, 640);
    eb_uint(&y, 0xBA, 480);
    eb_wrap(&x, 0xE0, &y);
    eb_wrap(&t, 0xAE, &x);
    eb_uint(&x, 0xD7, 2);
    eb_uint(&x, 0x83, 2);
    eb_el(&x, 0x86, "A_OPUS", 6);
    eb_el(&x, 0x63A2, "OpusHead", 8);
    eb_uint(&x, 0x56AA, 6500000);
    eb_float(&y, 0xB5, 48000.0);
    eb_uint(&y, 0x9F, 2);
    eb_wrap(&x, 0xE1, &y);
    eb_wrap(&t, 0xAE, &x);
    eb_wrap(&f, 0x1654AE6B, &t);
    eb_unknown(&f, 0x1F43B675);
    eb_uint(&f, 0xE7, 1000);
    block_head(&x, 1, 0, 0x80);
    buf_put(&x, "AAA", 3);
    eb_wrap(&f, 0xA3, &x);
    block_head(&x, 2, 5, 0x82);
    buf_u8(&x, 2);
    buf_put(&x, "\xFF\x2D\x02", 3);
    fill(&x, 1, 300);
    fill(&x, 2, 2);
    fill(&x, 3, 4);
    eb_wrap(&f, 0xA3, &x);
    block_head(&y, 1, 33, 0x00);
    buf_put(&y, "BB", 2);
    eb_wrap(&x, 0xA1, &y);
    eb_uint(&x, 0x9B, 33);
    buf_put(&x, "\xFB\x81\xDF", 3);
    buf_put(&x, "\x75\xA2\x82\xFF\x38", 5);
    eb_wrap(&f, 0xA0, &x);
    eb_id(&f, 0x1F43B675);
    eb_uint(&t, 0xE7, 2000);
    block_head(&x, 2, -2, 0x86);
    buf_u8(&x, 2);
    buf_put(&x, "\x8A\xBC", 2);
    fill(&x, 4, 10);
    fill(&x, 5, 7);
    fill(&x, 6, 5);
    eb_wrap(&t, 0xA3, &x);
    block_head(&x, 2, 40, 0x84);
    buf_u8(&x, 1);
    fill(&x, 7, 8);
    eb_wrap(&t, 0xA3, &x);
    buf_u8(&f, 0x01);
    for (int i = 6; i >= 0; i--) buf_u8(&f, (uint8_t)(t.len >> (i * 8)));
    buf_put(&f, t.data, t.len);
    buf_free(&t);
    Webm w;
    char err[128] = {0};
    CHECK(webm_parse(f.data, f.len, &w, err, sizeof(err)));
    CHECK(w.ntracks == 2 && w.npackets == 10);
    CHECK(w.duration_ns == 1.5e9);
    const WebmTrack* vt = webm_find_track(&w, WEBM_VIDEO);
    const WebmTrack* at = webm_find_track(&w, WEBM_AUDIO);
    CHECK(vt && !strcmp(vt->codec, "V_VP8") && vt->width == 640 && vt->height == 480 && vt->number == 1);
    CHECK(at && !strcmp(at->codec, "A_OPUS") && at->rate == 48000.0 && at->channels == 2 && at->codec_delay_ns == 6500000);
    CHECK(at && at->priv_len == 8 && !memcmp(at->priv, "OpusHead", 8));
    if (w.npackets == 10) {
        WebmPacket* k = w.packets;
        CHECK(k[0].track == 1 && k[0].ts_ns == 1000000000 && k[0].key && k[0].len == 3 && !memcmp(k[0].data, "AAA", 3));
        CHECK(k[1].track == 2 && k[1].ts_ns == 1005000000 && k[1].len == 300 && k[1].data[0] == 1 && k[1].data[299] == 1);
        CHECK(k[2].len == 2 && k[2].data[0] == 2 && k[3].len == 4 && k[3].data[3] == 3);
        CHECK(k[4].track == 1 && k[4].ts_ns == 1033000000 && !k[4].key && k[4].len == 2 && k[4].duration_ns == 33000000);
        CHECK(k[4].discard_ns == -200);
        CHECK(k[5].ts_ns == 1998000000 && k[5].len == 10 && k[6].len == 7 && k[7].len == 5 && k[7].data[0] == 6);
        CHECK(k[8].ts_ns == 2040000000 && k[8].len == 4 && k[9].len == 4 && k[9].data[0] == 7);
    }
    webm_free(&w);
    f.data[0] = 0x1B;
    CHECK(!webm_parse(f.data, f.len, &w, err, sizeof(err)));
    buf_free(&f);
}

typedef struct {
    int flags;
    int64_t granule;
    uint32_t seq;
    int segs;
} PageInfo;

static int parse_pages(const Buf* b, PageInfo* pages, int max, Buf* packets, size_t* lens, int* npk) {
    size_t p = 0;
    int n = 0;
    Buf cur = {0};
    *npk = 0;
    while (p + 27 <= b->len && n < max) {
        const uint8_t* h = b->data + p;
        if (memcmp(h, "OggS", 4)) return -1;
        int segs = h[26];
        size_t body = 0;
        for (int i = 0; i < segs; i++) body += h[27 + i];
        size_t total = 27 + (size_t)segs + body;
        uint8_t* copy = xmalloc(total);
        memcpy(copy, h, total);
        memset(copy + 22, 0, 4);
        uint32_t crc = ogg_crc(copy, total);
        free(copy);
        if (crc != rd32(h + 22)) return -1;
        pages[n].flags = h[5];
        pages[n].granule = (int64_t)((uint64_t)rd32(h + 6) | ((uint64_t)rd32(h + 10) << 32));
        pages[n].seq = rd32(h + 18);
        pages[n].segs = segs;
        const uint8_t* d = h + 27 + segs;
        for (int i = 0; i < segs; i++) {
            buf_put(&cur, d, h[27 + i]);
            d += h[27 + i];
            if (h[27 + i] < 255) {
                buf_put(packets, cur.data, cur.len);
                lens[(*npk)++] = cur.len;
                cur.len = 0;
            }
        }
        p += total;
        n++;
    }
    buf_free(&cur);
    return n;
}

static void bits_put(Buf* b, int* bitpos, uint32_t v, int n) {
    for (int i = 0; i < n; i++) {
        if (*bitpos % 8 == 0) buf_u8(b, 0);
        if ((v >> i) & 1) b->data[b->len - 1] |= (uint8_t)(1 << (*bitpos % 8));
        (*bitpos)++;
    }
}

static void test_ogg(void) {
    Buf out = {0};
    OggWriter w;
    ogg_writer_init(&w, &out, 77);
    size_t sizes[4] = {0, 255, 510, 140000};
    uint8_t* data = xmalloc(140000);
    for (int i = 0; i < 140000; i++) data[i] = (uint8_t)(i * 7);
    for (int i = 0; i < 4; i++) ogg_writer_packet(&w, data, sizes[i], (int64_t)(i + 1) * 1000, i == 3, 0);
    ogg_writer_free(&w);
    PageInfo pages[64];
    Buf pk = {0};
    size_t lens[16];
    int npk = 0;
    int np = parse_pages(&out, pages, 64, &pk, lens, &npk);
    CHECK(np == 3);
    CHECK(npk == 4 && lens[0] == 0 && lens[1] == 255 && lens[2] == 510 && lens[3] == 140000);
    if (npk == 4) CHECK(!memcmp(pk.data + 765, data, 140000) && !memcmp(pk.data + 255, data, 510));
    if (np == 3) {
        CHECK(pages[0].flags & 2);
        CHECK(pages[np - 1].flags & 4);
        CHECK(pages[np - 1].granule == 4000);
        int cont = 0, nogran = 0;
        for (int i = 0; i < np; i++) {
            CHECK(pages[i].seq == (uint32_t)i && pages[i].segs <= 255);
            if (pages[i].flags & 1) cont++;
            if (pages[i].granule == -1) nogran++;
        }
        CHECK(cont == 2 && nogran == 1 && pages[0].granule == 3000 && pages[1].granule == -1);
    }
    buf_free(&pk);
    buf_free(&out);
    free(data);

    uint8_t id[30] = {1, 'v', 'o', 'r', 'b', 'i', 's', 0, 0, 0, 0, 2, 0x80, 0xBB, 0, 0};
    id[28] = 0xB8;
    id[29] = 1;
    uint8_t comment[16] = {3, 'v', 'o', 'r', 'b', 'i', 's', 0, 0, 0, 0, 0, 0, 0, 0, 1};
    Buf setup = {0};
    buf_put(&setup, "\x05vorbis", 7);
    fill(&setup, 0xFF, 8);
    int bit = 0;
    bits_put(&setup, &bit, 1, 6);
    bits_put(&setup, &bit, 0, 1);
    bits_put(&setup, &bit, 0, 16);
    bits_put(&setup, &bit, 0, 16);
    bits_put(&setup, &bit, 0, 8);
    bits_put(&setup, &bit, 1, 1);
    bits_put(&setup, &bit, 0, 16);
    bits_put(&setup, &bit, 0, 16);
    bits_put(&setup, &bit, 0, 8);
    bits_put(&setup, &bit, 1, 1);
    VorbisInfo vi;
    CHECK(vorbis_parse_info(id, 30, setup.data, setup.len, &vi));
    CHECK(vi.channels == 2 && vi.rate == 48000 && vi.bs0 == 256 && vi.bs1 == 2048 && vi.mode_count == 2);
    CHECK(vi.mode_block[0] == 0 && vi.mode_block[1] == 1);
    Buf priv = {0};
    buf_u8(&priv, 2);
    buf_u8(&priv, 30);
    buf_u8(&priv, 16);
    buf_put(&priv, id, 30);
    buf_put(&priv, comment, 16);
    buf_put(&priv, setup.data, setup.len);
    const uint8_t* hdr[3];
    size_t hl[3];
    CHECK(vorbis_split_private(priv.data, priv.len, hdr, hl));
    CHECK(hl[0] == 30 && hl[1] == 16 && hl[2] == setup.len && hdr[2][0] == 5);
    uint8_t a0[3] = {0x00, 9, 9}, a1[3] = {0x02, 9, 9}, a2[2] = {0x02, 8}, a3[4] = {0x00, 1, 2, 3};
    const uint8_t* ap[4] = {a0, a1, a2, a3};
    size_t al[4] = {3, 3, 2, 4};
    CHECK(vorbis_packet_blocksize(&vi, a0, 3) == 256 && vorbis_packet_blocksize(&vi, a1, 3) == 2048);
    Buf ogg = {0};
    int64_t samples = 0;
    char err[128];
    CHECK(ogg_vorbis_remux(priv.data, priv.len, ap, al, 4, 5, &ogg, &samples, err, sizeof(err)));
    CHECK(samples == 2176);
    Buf back = {0};
    np = parse_pages(&ogg, pages, 64, &back, lens, &npk);
    CHECK(np == 3 && npk == 7);
    if (np == 3) {
        CHECK(pages[0].flags == 2 && pages[0].segs == 1 && pages[0].granule == 0);
        CHECK(pages[1].flags == 0 && pages[1].granule == 0);
        CHECK(pages[2].flags == 4 && pages[2].granule == 2176);
    }
    CHECK(npk == 7 && lens[0] == 30 && lens[1] == 16 && lens[2] == setup.len && lens[6] == 4);
    buf_free(&back);
    buf_free(&ogg);
    priv.data[0] = 1;
    CHECK(!ogg_vorbis_remux(priv.data, priv.len, ap, al, 4, 5, &ogg, &samples, err, sizeof(err)));
    buf_free(&ogg);
    buf_free(&priv);
    buf_free(&setup);

    MovieOut mo;
    CHECK(!movie_convert((const uint8_t*)"not a movie", 11, MOVIE_QUALITY, &mo, err, sizeof(err)));
}

static void test_jpeg(void) {
    int w = 368, h = 272;
    uint8_t* rgb = xmalloc((size_t)w * h * 3);
    uint32_t seed = 7;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            seed = seed * 1103515245u + 12345u;
            uint8_t* p = rgb + ((size_t)y * w + x) * 3;
            p[0] = (uint8_t)(x * 255 / w);
            p[1] = (uint8_t)(y * 255 / h);
            p[2] = (uint8_t)((x / 23 + y / 17) % 2 ? 220 : 30);
            if (x > 100 && x < 140 && y > 50 && y < 90) p[1] = (uint8_t)(seed >> 24);
        }
    }
    Buf j = {0};
    CHECK(jpeg_encode_rgb(rgb, w, h, 80, &j));
    CHECK(j.len > 600 && j.data[0] == 0xFF && j.data[1] == 0xD8 && j.data[j.len - 2] == 0xFF && j.data[j.len - 1] == 0xD9);
    int dw = 0, dh = 0, n = 0;
    uint8_t* d = stbi_load_from_memory(j.data, (int)j.len, &dw, &dh, &n, 3);
    CHECK(d && dw == w && dh == h);
    if (d && dw == w && dh == h) {
        double se = 0;
        for (size_t i = 0; i < (size_t)w * h * 3; i++) {
            double e = (double)d[i] - rgb[i];
            se += e * e;
        }
        double psnr = 10.0 * log10(255.0 * 255.0 / (se / ((double)w * h * 3)));
        CHECK(psnr > 21.0);
    }
    free(d);
    for (int i = 0; i < 15; i++) {
        rgb[i * 3] = 200;
        rgb[i * 3 + 1] = 120;
        rgb[i * 3 + 2] = 40;
    }
    Buf small = {0};
    CHECK(jpeg_encode_rgb(rgb, 5, 3, 50, &small));
    d = stbi_load_from_memory(small.data, (int)small.len, &dw, &dh, &n, 3);
    CHECK(d && dw == 5 && dh == 3);
    if (d && dw == 5 && dh == 3) {
        int worst = 0;
        for (int i = 0; i < 45; i++) {
            int e = abs((int)d[i] - (int)rgb[i]);
            if (e > worst) worst = e;
        }
        CHECK(worst <= 3);
    }
    free(d);
    buf_free(&small);
    buf_free(&j);
    free(rgb);
}

static double adpcm_snr(const int16_t* pcm, uint32_t frames, int ch, int* seek_ok, Buf* enc) {
    const uint8_t* h = enc->data;
    uint32_t every = (uint32_t)(h[6] | (h[7] << 8));
    uint32_t data_at = rd32(h + 28);
    uint32_t blocks = (frames + ADPCM_BLOCK_FRAMES - 1) / ADPCM_BLOCK_FRAMES;
    int32_t h1[2] = {0, 0}, h2[2] = {0, 0};
    int16_t out[ADPCM_BLOCK_FRAMES * 2];
    double sig = 0, noise = 0;
    *seek_ok = enc->len == data_at + (size_t)blocks * ADPCM_BLOCK_BYTES * (size_t)ch;
    for (uint32_t b = 0; b < blocks && *seek_ok; b++) {
        if (b % every == 0) {
            const uint8_t* t = h + ADPCM_HEADER_BYTES + (size_t)(b / every) * (size_t)ch * 4;
            for (int c = 0; c < ch; c++) {
                int16_t a = (int16_t)(t[c * 4] | (t[c * 4 + 1] << 8)), z = (int16_t)(t[c * 4 + 2] | (t[c * 4 + 3] << 8));
                if (a != h1[c] || z != h2[c]) *seek_ok = 0;
            }
        }
        for (int c = 0; c < ch; c++) adpcm_decode_block(h + data_at + ((size_t)b * (size_t)ch + (size_t)c) * ADPCM_BLOCK_BYTES, &h1[c], &h2[c], out + c, ch);
        for (int i = 0; i < ADPCM_BLOCK_FRAMES && b * ADPCM_BLOCK_FRAMES + (uint32_t)i < frames; i++) {
            for (int c = 0; c < ch; c++) {
                double x = pcm[(size_t)(b * ADPCM_BLOCK_FRAMES + (uint32_t)i) * (size_t)ch + (size_t)c];
                double d = out[i * ch + c] - x;
                sig += x * x;
                noise += d * d;
            }
        }
    }
    return noise > 0 ? 10 * log10(sig / noise) : 200;
}

static void test_adpcm(void) {
    uint32_t frames = 44100 * 3 + 17;
    int16_t* pcm = xmalloc((size_t)frames * 2 * sizeof(int16_t));
    uint32_t seed = 12345;
    for (uint32_t i = 0; i < frames; i++) {
        double t = i / 44100.0;
        seed = seed * 1664525u + 1013904223u;
        double n = ((int32_t)(seed >> 8) - (1 << 23)) / (double)(1 << 23);
        double l = 9000 * sin(2 * M_PI * 220 * t) + 5000 * sin(2 * M_PI * 1375 * t) + 2500 * sin(2 * M_PI * 6100 * t) + 300 * n;
        double r = 12000 * sin(2 * M_PI * 330 * t + 1) * (0.5 + 0.5 * sin(2 * M_PI * 0.7 * t)) + 300 * n;
        pcm[i * 2] = (int16_t)l;
        pcm[i * 2 + 1] = (int16_t)r;
    }
    Buf enc = {0};
    CHECK(adpcm_encode(pcm, frames, 2, 44100, 1000, 90000, &enc));
    CHECK(enc.len > ADPCM_HEADER_BYTES && !memcmp(enc.data, "OADP", 4) && enc.data[4] == 1 && enc.data[5] == 2);
    CHECK(rd32(enc.data + 8) == 44100 && rd32(enc.data + 12) == frames && rd32(enc.data + 16) == 1000 && rd32(enc.data + 20) == 90000);
    int seek_ok = 0;
    double snr = adpcm_snr(pcm, frames, 2, &seek_ok, &enc);
    CHECK(seek_ok);
    CHECK(snr > 55);
    printf("adpcm tones snr %.1f dB\n", snr);
    for (uint32_t i = 0; i < frames; i++) pcm[i] = (int16_t)((i / 50) % 2 ? 32767 : -32768);
    CHECK(adpcm_encode(pcm, frames, 1, 22050, -1, 0, &enc));
    CHECK((int32_t)rd32(enc.data + 16) == -1 && rd32(enc.data + 20) == 0);
    snr = adpcm_snr(pcm, frames, 1, &seek_ok, &enc);
    CHECK(seek_ok && snr > 40);
    printf("adpcm square snr %.1f dB\n", snr);
    memset(pcm, 0, (size_t)frames * sizeof(int16_t));
    CHECK(adpcm_encode(pcm, frames, 1, 22050, -1, 0, &enc));
    snr = adpcm_snr(pcm, frames, 1, &seek_ok, &enc);
    CHECK(seek_ok && snr >= 200);
    buf_free(&enc);
    free(pcm);
}

int main(void) {
    test_crypto();
    test_layouts();
    test_face_chunks();
    test_3dsx_pack();
    test_json_vbin();
    test_scripts();
    test_rx();
    test_yaml();
    test_texture();
    test_pbp_pack();
    test_webm();
    test_ogg();
    test_adpcm();
    test_jpeg();
    printf("%d checks, %d failed\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
