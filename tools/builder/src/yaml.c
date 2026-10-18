#include "yaml.h"
#include "util.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char* s;
    size_t n;
    size_t i;
    int failed;
    char* err;
    size_t errlen;
} Y;

static void yfail(Y* y, const char* msg) {
    if (y->failed) return;
    y->failed = 1;
    int line = 1;
    for (size_t k = 0; k < y->i && k < y->n; k++) {
        if (y->s[k] == '\n') line++;
    }
    snprintf(y->err, y->errlen, "line %d: %s", line, msg);
}

static int at_end(const Y* y) { return y->i >= y->n; }
static char cur(const Y* y) { return y->i < y->n ? y->s[y->i] : 0; }
static char peek_at(const Y* y, size_t k) { return y->i + k < y->n ? y->s[y->i + k] : 0; }
static int is_eol(char c) { return c == '\n' || c == '\r' || c == 0; }
static int is_ws(char c) { return c == ' ' || c == '\t'; }

static size_t line_start(const Y* y, size_t pos) {
    while (pos > 0 && y->s[pos - 1] != '\n') pos--;
    return pos;
}

static int column(const Y* y) { return (int)(y->i - line_start(y, y->i)); }

static void skip_eol(Y* y) {
    if (cur(y) == '\r') y->i++;
    if (cur(y) == '\n') y->i++;
}

static void skip_to_eol(Y* y) {
    while (!at_end(y) && !is_eol(cur(y))) y->i++;
}

static void skip_inline_ws(Y* y) {
    while (is_ws(cur(y))) y->i++;
}

static int next_content(Y* y, int* indent) {
    for (;;) {
        if (at_end(y)) return 0;
        size_t ls = line_start(y, y->i);
        if (ls == y->i) {
            size_t p = y->i;
            int ind = 0;
            while (p < y->n && y->s[p] == ' ') {
                p++;
                ind++;
            }
            while (p < y->n && y->s[p] == '\t') p++;
            if (p >= y->n) {
                y->i = p;
                return 0;
            }
            char c = y->s[p];
            if (is_eol(c) || c == '#') {
                y->i = p;
                skip_to_eol(y);
                skip_eol(y);
                continue;
            }
            y->i = p;
            *indent = ind;
            return 1;
        }
        skip_inline_ws(y);
        if (cur(y) == '#' || is_eol(cur(y))) {
            skip_to_eol(y);
            skip_eol(y);
            continue;
        }
        *indent = column(y);
        return 1;
    }
}

static Node* scalar_value(const char* s, size_t n) {
    if (n == 0 || (n == 1 && s[0] == '~') || (n == 4 && (!strncmp(s, "null", 4) || !strncmp(s, "Null", 4) || !strncmp(s, "NULL", 4))))
        return node_null();
    if ((n == 4 && (!strncmp(s, "true", 4) || !strncmp(s, "True", 4) || !strncmp(s, "TRUE", 4)))) return node_bool(1);
    if ((n == 5 && (!strncmp(s, "false", 5) || !strncmp(s, "False", 5) || !strncmp(s, "FALSE", 5)))) return node_bool(0);
    char buf[64];
    if (n < sizeof(buf)) {
        size_t k = 0, j = 0;
        for (; k < n; k++) {
            if (s[k] != '_') buf[j++] = s[k];
        }
        buf[j] = 0;
        const char* p = buf;
        int neg = 0;
        if (*p == '-' || *p == '+') {
            neg = *p == '-';
            p++;
        }
        int is_int = *p != 0;
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X') && p[2]) {
            for (const char* q = p + 2; *q; q++) {
                if (!isxdigit((unsigned char)*q)) is_int = 0;
            }
            if (is_int) return node_num((neg ? -1.0 : 1.0) * (double)strtoll(p + 2, NULL, 16));
        } else if (p[0] == '0' && (p[1] == 'o') && p[2]) {
            for (const char* q = p + 2; *q; q++) {
                if (*q < '0' || *q > '7') is_int = 0;
            }
            if (is_int) return node_num((neg ? -1.0 : 1.0) * (double)strtoll(p + 2, NULL, 8));
        } else if (p[0] == '0' && (p[1] == 'b') && p[2]) {
            for (const char* q = p + 2; *q; q++) {
                if (*q != '0' && *q != '1') is_int = 0;
            }
            if (is_int) return node_num((neg ? -1.0 : 1.0) * (double)strtoll(p + 2, NULL, 2));
        } else {
            for (const char* q = p; *q; q++) {
                if (!isdigit((unsigned char)*q)) is_int = 0;
            }
            if (is_int) {
                if (p[0] == '0' && p[1]) {
                    int octal = 1;
                    for (const char* q = p; *q; q++) {
                        if (*q > '7') octal = 0;
                    }
                    if (octal) return node_num((neg ? -1.0 : 1.0) * (double)strtoll(p, NULL, 8));
                }
                return node_num((neg ? -1.0 : 1.0) * strtod(p, NULL));
            }
        }
        if (!strcmp(p, ".inf") || !strcmp(p, ".Inf") || !strcmp(p, ".INF")) return node_num(neg ? -INFINITY : INFINITY);
        if (!strcmp(buf, ".nan") || !strcmp(buf, ".NaN") || !strcmp(buf, ".NAN")) return node_num(NAN);
        int digits = 0, dot = 0, exp = 0, ok = *p != 0;
        for (const char* q = p; *q && ok; q++) {
            if (isdigit((unsigned char)*q)) digits++;
            else if (*q == '.' && !dot && !exp) dot = 1;
            else if ((*q == 'e' || *q == 'E') && digits && !exp) {
                exp = 1;
                if (q[1] == '+' || q[1] == '-') q++;
                if (!isdigit((unsigned char)q[1])) ok = 0;
            } else ok = 0;
        }
        if (ok && digits && (dot || exp)) return node_num((neg ? -1.0 : 1.0) * strtod(p, NULL));
    }
    return node_strn(s, n);
}

static void put_utf8(Buf* b, uint32_t cp) {
    if (cp < 0x80) buf_u8(b, (uint8_t)cp);
    else if (cp < 0x800) {
        buf_u8(b, (uint8_t)(0xC0 | (cp >> 6)));
        buf_u8(b, (uint8_t)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        buf_u8(b, (uint8_t)(0xE0 | (cp >> 12)));
        buf_u8(b, (uint8_t)(0x80 | ((cp >> 6) & 0x3F)));
        buf_u8(b, (uint8_t)(0x80 | (cp & 0x3F)));
    } else {
        buf_u8(b, (uint8_t)(0xF0 | (cp >> 18)));
        buf_u8(b, (uint8_t)(0x80 | ((cp >> 12) & 0x3F)));
        buf_u8(b, (uint8_t)(0x80 | ((cp >> 6) & 0x3F)));
        buf_u8(b, (uint8_t)(0x80 | (cp & 0x3F)));
    }
}

static void trim_trailing_ws(Buf* b) {
    while (b->len && (b->data[b->len - 1] == ' ' || b->data[b->len - 1] == '\t')) b->len--;
}

static void fold_break(Y* y, Buf* b) {
    trim_trailing_ws(b);
    int breaks = 0;
    while (!at_end(y)) {
        skip_eol(y);
        breaks++;
        size_t p = y->i;
        while (p < y->n && is_ws(y->s[p])) p++;
        if (p < y->n && (y->s[p] == '\n' || y->s[p] == '\r')) {
            y->i = p;
            continue;
        }
        y->i = p;
        break;
    }
    if (breaks == 1) buf_u8(b, ' ');
    else
        for (int k = 1; k < breaks; k++) buf_u8(b, '\n');
}

static Node* parse_single(Y* y) {
    y->i++;
    Buf b = {0};
    for (;;) {
        if (at_end(y)) {
            yfail(y, "unterminated single-quoted string");
            break;
        }
        char c = cur(y);
        if (c == '\'') {
            if (peek_at(y, 1) == '\'') {
                buf_u8(&b, '\'');
                y->i += 2;
                continue;
            }
            y->i++;
            break;
        }
        if (c == '\r' || c == '\n') {
            fold_break(y, &b);
            continue;
        }
        buf_u8(&b, (uint8_t)c);
        y->i++;
    }
    Node* r = node_strn(b.len ? (const char*)b.data : "", b.len);
    buf_free(&b);
    return r;
}

static int hex_digits(Y* y, int n, uint32_t* out) {
    uint32_t v = 0;
    for (int k = 0; k < n; k++) {
        char c = peek_at(y, (size_t)k);
        int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
        if (d < 0) return 0;
        v = v * 16 + (uint32_t)d;
    }
    y->i += (size_t)n;
    *out = v;
    return 1;
}

static Node* parse_double(Y* y) {
    y->i++;
    Buf b = {0};
    for (;;) {
        if (at_end(y)) {
            yfail(y, "unterminated double-quoted string");
            break;
        }
        char c = cur(y);
        if (c == '"') {
            y->i++;
            break;
        }
        if (c == '\\') {
            char e = peek_at(y, 1);
            y->i += 2;
            uint32_t v = 0;
            switch (e) {
            case '0': buf_u8(&b, 0); break;
            case 'a': buf_u8(&b, 7); break;
            case 'b': buf_u8(&b, 8); break;
            case 't':
            case '\t': buf_u8(&b, 9); break;
            case 'n': buf_u8(&b, 10); break;
            case 'v': buf_u8(&b, 11); break;
            case 'f': buf_u8(&b, 12); break;
            case 'r': buf_u8(&b, 13); break;
            case 'e': buf_u8(&b, 27); break;
            case ' ': buf_u8(&b, ' '); break;
            case '"': buf_u8(&b, '"'); break;
            case '/': buf_u8(&b, '/'); break;
            case '\\': buf_u8(&b, '\\'); break;
            case 'N': put_utf8(&b, 0x85); break;
            case '_': put_utf8(&b, 0xA0); break;
            case 'L': put_utf8(&b, 0x2028); break;
            case 'P': put_utf8(&b, 0x2029); break;
            case 'x':
                if (!hex_digits(y, 2, &v)) yfail(y, "bad \\x escape");
                put_utf8(&b, v);
                break;
            case 'u':
                if (!hex_digits(y, 4, &v)) yfail(y, "bad \\u escape");
                put_utf8(&b, v);
                break;
            case 'U':
                if (!hex_digits(y, 8, &v)) yfail(y, "bad \\U escape");
                put_utf8(&b, v);
                break;
            case '\r':
            case '\n':
                y->i--;
                skip_eol(y);
                skip_inline_ws(y);
                break;
            default: yfail(y, "unknown escape in double-quoted string"); break;
            }
            if (y->failed) break;
            continue;
        }
        if (c == '\r' || c == '\n') {
            fold_break(y, &b);
            continue;
        }
        buf_u8(&b, (uint8_t)c);
        y->i++;
    }
    Node* r = node_strn(b.len ? (const char*)b.data : "", b.len);
    buf_free(&b);
    return r;
}

static Node* parse_flow(Y* y);

static void flow_ws(Y* y) {
    for (;;) {
        char c = cur(y);
        if (is_ws(c) || c == '\r' || c == '\n') y->i++;
        else if (c == '#') skip_to_eol(y);
        else break;
    }
}

static Node* flow_plain(Y* y) {
    size_t start = y->i;
    size_t end = start;
    while (!at_end(y)) {
        char c = cur(y);
        if (c == ',' || c == ']' || c == '}' || c == '[' || c == '{') break;
        if (c == ':' && (is_ws(peek_at(y, 1)) || is_eol(peek_at(y, 1)) || peek_at(y, 1) == ',' || peek_at(y, 1) == '}' || peek_at(y, 1) == ']'))
            break;
        if (c == '#' && y->i > start && is_ws(y->s[y->i - 1])) break;
        if (c == '\r' || c == '\n') {
            y->i++;
            continue;
        }
        y->i++;
        if (!is_ws(c)) end = y->i;
    }
    Buf b = {0};
    int space = 0;
    for (size_t k = start; k < end; k++) {
        char c = y->s[k];
        if (c == '\r' || c == '\n' || is_ws(c)) {
            space = 1;
            continue;
        }
        if (space && b.len) buf_u8(&b, ' ');
        space = 0;
        buf_u8(&b, (uint8_t)c);
    }
    Node* r = scalar_value(b.len ? (const char*)b.data : "", b.len);
    buf_free(&b);
    return r;
}

static Node* flow_item(Y* y) {
    flow_ws(y);
    char c = cur(y);
    if (c == '[' || c == '{') return parse_flow(y);
    if (c == '\'') return parse_single(y);
    if (c == '"') return parse_double(y);
    return flow_plain(y);
}

static char* key_string(Node* k) {
    if (k->type == N_STR) return xstrdup(k->str);
    if (k->type == N_NULL) return xstrdup("null");
    if (k->type == N_TRUE) return xstrdup("true");
    if (k->type == N_FALSE) return xstrdup("false");
    if (k->type == N_NUM) {
        char tmp[64];
        if (k->num == floor(k->num) && fabs(k->num) < 1e15) snprintf(tmp, sizeof(tmp), "%.0f", k->num);
        else snprintf(tmp, sizeof(tmp), "%.17g", k->num);
        return xstrdup(tmp);
    }
    return xstrdup("");
}

static void set_key(Y* y, Node* obj, Node* key, Node* val) {
    char* ks = key_string(key);
    node_free(key);
    if (node_get(obj, ks)) {
        yfail(y, "duplicated mapping key");
        node_free(val);
    } else {
        node_set(obj, ks, val);
    }
    free(ks);
}

static Node* parse_flow(Y* y) {
    char open = cur(y);
    char close = open == '[' ? ']' : '}';
    y->i++;
    Node* r = open == '[' ? node_arr() : node_obj();
    for (;;) {
        flow_ws(y);
        if (at_end(y)) {
            yfail(y, "unterminated flow collection");
            return r;
        }
        if (cur(y) == close) {
            y->i++;
            return r;
        }
        Node* item = flow_item(y);
        if (y->failed) {
            node_free(item);
            return r;
        }
        flow_ws(y);
        if (open == '{') {
            Node* val = node_null();
            if (cur(y) == ':') {
                y->i++;
                flow_ws(y);
                if (cur(y) != ',' && cur(y) != '}') {
                    node_free(val);
                    val = flow_item(y);
                }
            }
            set_key(y, r, item, val);
        } else {
            node_push(r, item);
        }
        flow_ws(y);
        if (cur(y) == ',') {
            y->i++;
            continue;
        }
        if (cur(y) != close) {
            yfail(y, "expected , in flow collection");
            return r;
        }
    }
}

static Node* parse_block_scalar(Y* y, int parent_indent) {
    int folding = cur(y) == '>';
    int chomping = 0;
    int detected = 0;
    int text_indent = parent_indent;
    y->i++;
    for (;;) {
        char c = cur(y);
        if (c == '+' || c == '-') {
            if (chomping) {
                yfail(y, "repeat of a chomping mode identifier");
                return node_null();
            }
            chomping = c == '+' ? 1 : -1;
            y->i++;
        } else if (c >= '1' && c <= '9') {
            if (detected) {
                yfail(y, "repeat of an indentation width identifier");
                return node_null();
            }
            text_indent = parent_indent + (c - '0') - 1;
            detected = 1;
            y->i++;
        } else {
            break;
        }
    }
    skip_inline_ws(y);
    if (cur(y) == '#') skip_to_eol(y);
    Buf b = {0};
    int did_read = 0, empty_lines = 0, more_indented = 0;
    while (!at_end(y)) {
        skip_eol(y);
        int line_indent = 0;
        while ((!detected || line_indent < text_indent) && cur(y) == ' ') {
            line_indent++;
            y->i++;
        }
        if (!detected && line_indent > text_indent) text_indent = line_indent;
        char c = cur(y);
        if (c == '\n' || c == '\r') {
            empty_lines++;
            continue;
        }
        if (line_indent < text_indent || at_end(y)) {
            if (chomping == 1) {
                for (int k = 0; k < (did_read ? 1 + empty_lines : empty_lines); k++) buf_u8(&b, '\n');
            } else if (chomping == 0 && did_read) {
                buf_u8(&b, '\n');
            }
            y->i -= (size_t)line_indent;
            break;
        }
        if (folding) {
            if (is_ws(c)) {
                more_indented = 1;
                for (int k = 0; k < (did_read ? 1 + empty_lines : empty_lines); k++) buf_u8(&b, '\n');
            } else if (more_indented) {
                more_indented = 0;
                for (int k = 0; k < empty_lines + 1; k++) buf_u8(&b, '\n');
            } else if (empty_lines == 0) {
                if (did_read) buf_u8(&b, ' ');
            } else {
                for (int k = 0; k < empty_lines; k++) buf_u8(&b, '\n');
            }
        } else {
            for (int k = 0; k < (did_read ? 1 + empty_lines : empty_lines); k++) buf_u8(&b, '\n');
        }
        did_read = 1;
        detected = 1;
        empty_lines = 0;
        size_t start = y->i;
        skip_to_eol(y);
        size_t end = y->i;
        if (end > start && y->s[end - 1] == '\r') end--;
        buf_put(&b, y->s + start, end - start);
    }
    Node* r = node_strn(b.len ? (const char*)b.data : "", b.len);
    buf_free(&b);
    return r;
}

static int ends_value(const Y* y, size_t k) {
    char c = k < y->n ? y->s[k] : 0;
    return is_ws(c) || is_eol(c);
}

static int is_key_line(const Y* y) {
    size_t k = y->i;
    char c = k < y->n ? y->s[k] : 0;
    if (c == '\'' || c == '"') {
        char q = c;
        k++;
        while (k < y->n) {
            if (y->s[k] == q) {
                if (q == '\'' && k + 1 < y->n && y->s[k + 1] == '\'') {
                    k += 2;
                    continue;
                }
                break;
            }
            if (q == '"' && y->s[k] == '\\') k++;
            k++;
        }
        k++;
        while (k < y->n && is_ws(y->s[k])) k++;
        return k < y->n && y->s[k] == ':' && ends_value(y, k + 1);
    }
    if (c == '[' || c == '{' || c == '#' || c == '|' || c == '>') return 0;
    if (c == '-' && ends_value(y, k + 1)) return 0;
    while (k < y->n && !is_eol(y->s[k])) {
        if (y->s[k] == ':' && ends_value(y, k + 1)) return 1;
        if (y->s[k] == '#' && k > y->i && is_ws(y->s[k - 1])) return 0;
        k++;
    }
    return 0;
}

static Node* parse_plain(Y* y, int parent_indent) {
    Buf b = {0};
    int breaks = 0;
    for (;;) {
        size_t start = y->i;
        size_t end = start;
        while (!at_end(y) && !is_eol(cur(y))) {
            char c = cur(y);
            if (c == ':' && ends_value(y, y->i + 1)) break;
            if (c == '#' && y->i > start && is_ws(y->s[y->i - 1])) break;
            y->i++;
            if (!is_ws(c)) end = y->i;
        }
        if (end > start) {
            if (b.len || breaks) {
                if (breaks <= 1) {
                    if (b.len) buf_u8(&b, ' ');
                } else {
                    for (int k = 1; k < breaks; k++) buf_u8(&b, '\n');
                }
            }
            buf_put(&b, y->s + start, end - start);
            breaks = 0;
        }
        if (!is_eol(cur(y)) || at_end(y)) break;
        size_t save = y->i;
        int nb = 0;
        int cont = 0;
        while (!at_end(y)) {
            skip_eol(y);
            nb++;
            size_t p = y->i;
            int ind = 0;
            while (p < y->n && y->s[p] == ' ') {
                p++;
                ind++;
            }
            while (p < y->n && y->s[p] == '\t') p++;
            if (p < y->n && is_eol(y->s[p])) {
                y->i = p;
                continue;
            }
            if (p >= y->n) break;
            if (ind > parent_indent && y->s[p] != '#') {
                y->i = p;
                cont = 1;
            }
            break;
        }
        if (!cont) {
            y->i = save;
            break;
        }
        breaks = nb;
    }
    Node* r = scalar_value(b.len ? (const char*)b.data : "", b.len);
    buf_free(&b);
    return r;
}

static Node* parse_at(Y* y, int ind, int parent_indent);
static Node* parse_seq(Y* y, int ind);

static Node* parse_block(Y* y, int parent_indent) {
    int ind;
    size_t save = y->i;
    if (!next_content(y, &ind)) return node_null();
    if (ind <= parent_indent) {
        (void)save;
        return node_null();
    }
    return parse_at(y, ind, parent_indent);
}

static void finish_line(Y* y) {
    skip_inline_ws(y);
    if (cur(y) == '#') skip_to_eol(y);
    if (!is_eol(cur(y))) yfail(y, "unexpected text after value");
}

static Node* tagged_value(Y* y, int ind);

static Node* inline_value(Y* y, int ind) {
    char c = cur(y);
    Node* v;
    if (c == '!') return tagged_value(y, ind);
    if (c == '|' || c == '>') return parse_block_scalar(y, ind);
    if (c == '[' || c == '{') v = parse_flow(y);
    else if (c == '\'') v = parse_single(y);
    else if (c == '"') v = parse_double(y);
    else return parse_plain(y, ind);
    finish_line(y);
    return v;
}

static Node* read_key(Y* y) {
    char c = cur(y);
    Node* k;
    if (c == '\'') k = parse_single(y);
    else if (c == '"') k = parse_double(y);
    else {
        size_t start = y->i, end = start;
        while (!at_end(y) && !is_eol(cur(y))) {
            if (cur(y) == ':' && ends_value(y, y->i + 1)) break;
            if (!is_ws(cur(y))) end = y->i + 1;
            y->i++;
        }
        k = scalar_value(y->s + start, end - start);
    }
    skip_inline_ws(y);
    if (cur(y) != ':') {
        yfail(y, "expected : after mapping key");
        return k;
    }
    y->i++;
    return k;
}

static Node* parse_map(Y* y, int ind) {
    Node* obj = node_obj();
    for (;;) {
        if (!is_key_line(y)) {
            yfail(y, "expected a mapping key");
            return obj;
        }
        Node* key = read_key(y);
        if (y->failed) {
            node_free(key);
            return obj;
        }
        skip_inline_ws(y);
        Node* val;
        if (cur(y) == '#' || is_eol(cur(y))) {
            skip_to_eol(y);
            int ind2;
            size_t save = y->i;
            if (next_content(y, &ind2)) {
                if (ind2 > ind) val = parse_at(y, ind2, ind);
                else if (ind2 == ind && cur(y) == '-' && ends_value(y, y->i + 1)) val = parse_seq(y, ind);
                else val = node_null();
            } else {
                y->i = save;
                val = node_null();
            }
        } else {
            val = inline_value(y, ind);
        }
        set_key(y, obj, key, val);
        if (y->failed) return obj;
        int ind2;
        if (!next_content(y, &ind2)) return obj;
        if (ind2 < ind) return obj;
        if (ind2 > ind) {
            yfail(y, "bad indentation of a mapping entry");
            return obj;
        }
        if (cur(y) == '-' && ends_value(y, y->i + 1)) return obj;
    }
}

static Node* parse_seq(Y* y, int ind) {
    Node* arr = node_arr();
    for (;;) {
        y->i++;
        skip_inline_ws(y);
        Node* item;
        if (cur(y) == '#' || is_eol(cur(y))) {
            skip_to_eol(y);
            item = parse_block(y, ind);
        } else {
            int col = column(y);
            item = parse_at(y, col, ind);
        }
        node_push(arr, item);
        if (y->failed) return arr;
        int ind2;
        if (!next_content(y, &ind2)) return arr;
        if (ind2 < ind) return arr;
        if (ind2 > ind) {
            yfail(y, "bad indentation of a sequence entry");
            return arr;
        }
        if (!(cur(y) == '-' && ends_value(y, y->i + 1))) return arr;
    }
}

static Node* tagged_value(Y* y, int ind) {
    size_t start = y->i;
    while (!at_end(y) && !is_ws(cur(y)) && !is_eol(cur(y))) y->i++;
    if (y->i - start != 1) {
        yfail(y, "unsupported tag");
        return node_null();
    }
    skip_inline_ws(y);
    if (cur(y) == '#' || is_eol(cur(y))) return node_null();
    Node* v = inline_value(y, ind);
    if (v->type == N_NUM || v->type == N_TRUE || v->type == N_FALSE) {
        size_t end = y->i;
        while (end > start && (is_ws(y->s[end - 1]) || is_eol(y->s[end - 1]))) end--;
        size_t from = start + 1;
        while (from < end && is_ws(y->s[from])) from++;
        node_free(v);
        v = node_strn(y->s + from, end - from);
    }
    return v;
}

static Node* parse_at(Y* y, int ind, int parent_indent) {
    char c = cur(y);
    if (c == '!') return tagged_value(y, parent_indent);
    if (c == '-' && ends_value(y, y->i + 1)) return parse_seq(y, ind);
    if (is_key_line(y)) return parse_map(y, ind);
    if (c == '|' || c == '>') return parse_block_scalar(y, parent_indent);
    Node* v;
    if (c == '[' || c == '{') v = parse_flow(y);
    else if (c == '\'') v = parse_single(y);
    else if (c == '"') v = parse_double(y);
    else return parse_plain(y, parent_indent);
    finish_line(y);
    return v;
}

Node* yaml_parse(const char* text, size_t len, char* err, size_t errlen) {
    char* copy = NULL;
    if (len && text[len - 1] != '\n' && text[len - 1] != '\r') {
        copy = xmalloc(len + 1);
        memcpy(copy, text, len);
        copy[len] = '\n';
        text = copy;
        len++;
    }
    Y y = {text, len, 0, 0, err, errlen};
    if (len >= 3 && (uint8_t)text[0] == 0xEF && (uint8_t)text[1] == 0xBB && (uint8_t)text[2] == 0xBF) y.i = 3;
    int ind;
    if (!next_content(&y, &ind)) return node_null();
    Node* r = parse_at(&y, ind, -1);
    if (!y.failed && next_content(&y, &ind)) yfail(&y, "unexpected content after document");
    free(copy);
    if (y.failed) {
        node_free(r);
        return NULL;
    }
    return r;
}
