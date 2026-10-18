#include "js.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jsops.h"

enum { T_NUM, T_STR, T_TMPL, T_NAME, T_OP, T_EOF, T_REGEX };

typedef struct {
    int kind;
    char* text;
    size_t len;
    double num;
    int nl;
    size_t pos;
    char* flags;
} Tok;

typedef struct {
    const char* src;
    size_t n;
    Tok* toks;
    int count;
    int cap;
    int i;
    int failed;
    char* err;
    size_t errlen;
    int no_in;
    int temp_id;
} P;

static void fail_at(P* p, size_t pos, const char* msg) {
    if (p->failed) return;
    p->failed = 1;
    int line = 1, col = 1;
    for (size_t k = 0; k < pos && k < p->n; k++) {
        if (p->src[k] == '\n') {
            line++;
            col = 1;
        } else {
            col++;
        }
    }
    snprintf(p->err, p->errlen, "%d:%d: %s", line, col, msg);
}

static Tok* peek(P* p, int k) {
    int i = p->i + k;
    if (i >= p->count) i = p->count - 1;
    if (i < 0) i = 0;
    return &p->toks[i];
}

static void fail(P* p, const char* msg) { fail_at(p, peek(p, 0)->pos, msg); }

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

static int hexv(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int read_escape(P* p, const char* s, size_t n, size_t* i, Buf* out) {
    if (*i >= n) return 0;
    char e = s[*i];
    (*i)++;
    switch (e) {
    case '\r':
        if (*i < n && s[*i] == '\n') (*i)++;
        return 1;
    case '\n': return 1;
    case 'b': buf_u8(out, 8); return 1;
    case 'f': buf_u8(out, 12); return 1;
    case 'n': buf_u8(out, 10); return 1;
    case 'r': buf_u8(out, 13); return 1;
    case 't': buf_u8(out, 9); return 1;
    case 'v': buf_u8(out, 11); return 1;
    case '0':
        if (*i < n && isdigit((unsigned char)s[*i])) break;
        buf_u8(out, 0);
        return 1;
    case 'x': {
        if (*i + 2 > n || hexv(s[*i]) < 0 || hexv(s[*i + 1]) < 0) return 0;
        put_utf8(out, (uint32_t)(hexv(s[*i]) * 16 + hexv(s[*i + 1])));
        *i += 2;
        return 1;
    }
    case 'u': {
        uint32_t v = 0;
        if (*i < n && s[*i] == '{') {
            (*i)++;
            while (*i < n && s[*i] != '}') {
                int h = hexv(s[*i]);
                if (h < 0) return 0;
                v = v * 16 + (uint32_t)h;
                (*i)++;
            }
            if (*i >= n) return 0;
            (*i)++;
        } else {
            if (*i + 4 > n) return 0;
            for (int k = 0; k < 4; k++) {
                int h = hexv(s[*i + (size_t)k]);
                if (h < 0) return 0;
                v = v * 16 + (uint32_t)h;
            }
            *i += 4;
            if (v >= 0xD800 && v < 0xDC00 && *i + 6 <= n && s[*i] == '\\' && s[*i + 1] == 'u') {
                uint32_t lo = 0;
                int ok = 1;
                for (int k = 0; k < 4; k++) {
                    int h = hexv(s[*i + 2 + (size_t)k]);
                    if (h < 0) ok = 0;
                    else lo = lo * 16 + (uint32_t)h;
                }
                if (ok && lo >= 0xDC00 && lo < 0xE000) {
                    v = 0x10000 + ((v - 0xD800) << 10) + (lo - 0xDC00);
                    *i += 6;
                }
            }
        }
        put_utf8(out, v);
        return 1;
    }
    default: break;
    }
    (void)p;
    buf_u8(out, (uint8_t)e);
    return 1;
}

static int is_name_start(unsigned char c) { return isalpha(c) || c == '_' || c == '$' || c >= 0x80; }
static int is_name_char(unsigned char c) { return isalnum(c) || c == '_' || c == '$' || c >= 0x80; }

static Tok* add_tok(P* p, int kind, size_t pos, int nl) {
    if (p->count == p->cap) {
        p->cap = p->cap ? p->cap * 2 : 64;
        p->toks = xrealloc(p->toks, sizeof(Tok) * (size_t)p->cap);
    }
    Tok* t = &p->toks[p->count++];
    memset(t, 0, sizeof(*t));
    t->kind = kind;
    t->pos = pos;
    t->nl = nl;
    return t;
}

static const char* OPS[] = {">>>=", "...", "===", "!==", "**=", "<<=", ">>=", ">>>", "&&=", "||=", "?\?=", "=>", "==", "!=", "<=", ">=", "&&",
                            "||",   "??",  "?.",  "++",  "--",  "+=",  "-=",  "*=",  "/=",  "%=",  "&=",  "|=",  "^=", "<<", ">>", "**",
                            "{",    "}",   "(",   ")",   "[",   "]",   ";",   ",",   "<",   ">",   "+",   "-",   "*",  "/",  "%",  "&",
                            "|",    "^",   "!",   "~",   "?",   ":",   "=",   ".",   NULL};

static void tokenize_range(P* p, size_t i, size_t end, int stop_brace);

static void read_template(P* p, size_t* pi, int nl) {
    const char* s = p->src;
    size_t i = *pi + 1;
    Tok* head = add_tok(p, T_TMPL, *pi, nl);
    (void)head;
    Buf b = {0};
    for (;;) {
        if (i >= p->n) {
            fail_at(p, *pi, "unterminated template literal");
            buf_free(&b);
            return;
        }
        char c = s[i];
        if (c == '`') {
            i++;
            break;
        }
        if (c == '\\') {
            i++;
            if (!read_escape(p, s, p->n, &i, &b)) {
                fail_at(p, i, "bad escape");
                buf_free(&b);
                return;
            }
            continue;
        }
        if (c == '$' && i + 1 < p->n && s[i + 1] == '{') {
            Tok* st = add_tok(p, T_STR, i, 0);
            st->text = xstrndup(b.data ? (const char*)b.data : "", b.len);
            st->len = b.len;
            b.len = 0;
            add_tok(p, T_OP, i, 0)->text = xstrdup("${");
            i += 2;
            size_t depth = 1, j = i;
            int q = 0;
            while (j < p->n && depth) {
                char d = s[j];
                if (q) {
                    if (d == '\\') j++;
                    else if (d == q) q = 0;
                } else if (d == '\'' || d == '"' || d == '`') q = d;
                else if (d == '{') depth++;
                else if (d == '}') depth--;
                if (depth) j++;
            }
            if (j >= p->n) {
                fail_at(p, i, "unterminated template expression");
                buf_free(&b);
                return;
            }
            tokenize_range(p, i, j, 0);
            add_tok(p, T_OP, j, 0)->text = xstrdup("}$");
            i = j + 1;
            continue;
        }
        buf_u8(&b, (uint8_t)c);
        i++;
    }
    Tok* st = add_tok(p, T_STR, i, 0);
    st->text = xstrndup(b.data ? (const char*)b.data : "", b.len);
    st->len = b.len;
    add_tok(p, T_OP, i, 0)->text = xstrdup("`end");
    buf_free(&b);
    *pi = i;
}

static int regex_allowed(P* p) {
    if (p->count == 0) return 1;
    Tok* t = &p->toks[p->count - 1];
    if (t->kind == T_NUM || t->kind == T_STR || t->kind == T_REGEX) return 0;
    if (t->kind == T_NAME) {
        static const char* KW[] = {"return", "typeof", "case", "do", "else", "in", "of", "new", "delete", "void", "throw", "instanceof", "yield", "await", NULL};
        for (int k = 0; KW[k]; k++) {
            if (!strcmp(t->text, KW[k])) return 1;
        }
        return 0;
    }
    if (t->kind == T_OP) {
        if (!strcmp(t->text, ")") || !strcmp(t->text, "]") || !strcmp(t->text, "}") || !strcmp(t->text, "}$") || !strcmp(t->text, "`end") ||
            !strcmp(t->text, "++") || !strcmp(t->text, "--"))
            return 0;
        return 1;
    }
    return 1;
}

static void tokenize_range(P* p, size_t i, size_t end, int stop_brace) {
    const char* s = p->src;
    int nl = 0;
    (void)stop_brace;
    while (i < end && !p->failed) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\n') {
            nl = 1;
            i++;
            continue;
        }
        if (isspace(c) || c == 0xA0) {
            i++;
            continue;
        }
        if (c == 0xEF && i + 2 < end && (unsigned char)s[i + 1] == 0xBB && (unsigned char)s[i + 2] == 0xBF) {
            i += 3;
            continue;
        }
        if (c == '/' && i + 1 < end && s[i + 1] == '/') {
            while (i < end && s[i] != '\n') i++;
            continue;
        }
        if (c == '/' && !(i + 1 < end && s[i + 1] == '*') && regex_allowed(p)) {
            size_t j = i + 1;
            int in_class = 0;
            while (j < end && s[j] != '\n') {
                if (s[j] == '\\') {
                    j += 2;
                    continue;
                }
                if (s[j] == '[') in_class = 1;
                else if (s[j] == ']') in_class = 0;
                else if (s[j] == '/' && !in_class) break;
                j++;
            }
            if (j >= end || s[j] != '/') {
                fail_at(p, i, "unterminated regex");
                return;
            }
            size_t k = j + 1;
            while (k < end && is_name_char((unsigned char)s[k])) k++;
            Tok* t = add_tok(p, T_REGEX, i, nl);
            t->text = xstrndup(s + i + 1, j - i - 1);
            t->len = j - i - 1;
            t->flags = xstrndup(s + j + 1, k - j - 1);
            nl = 0;
            i = k;
            continue;
        }
        if (c == '/' && i + 1 < end && s[i + 1] == '*') {
            size_t j = i + 2;
            while (j + 1 < end && !(s[j] == '*' && s[j + 1] == '/')) {
                if (s[j] == '\n') nl = 1;
                j++;
            }
            if (j + 1 >= end) {
                fail_at(p, i, "unterminated comment");
                return;
            }
            i = j + 2;
            continue;
        }
        if (isdigit(c) || (c == '.' && i + 1 < end && isdigit((unsigned char)s[i + 1]))) {
            size_t j = i;
            double v = 0;
            if (c == '0' && j + 1 < end && (s[j + 1] == 'x' || s[j + 1] == 'X')) {
                j += 2;
                while (j < end && hexv(s[j]) >= 0) v = v * 16 + hexv(s[j++]);
            } else if (c == '0' && j + 1 < end && (s[j + 1] == 'b' || s[j + 1] == 'B')) {
                j += 2;
                while (j < end && (s[j] == '0' || s[j] == '1')) v = v * 2 + (s[j++] - '0');
            } else if (c == '0' && j + 1 < end && (s[j + 1] == 'o' || s[j + 1] == 'O')) {
                j += 2;
                while (j < end && s[j] >= '0' && s[j] <= '7') v = v * 8 + (s[j++] - '0');
            } else {
                while (j < end && isdigit((unsigned char)s[j])) j++;
                if (j < end && s[j] == '.') {
                    j++;
                    while (j < end && isdigit((unsigned char)s[j])) j++;
                }
                if (j < end && (s[j] == 'e' || s[j] == 'E')) {
                    size_t k = j + 1;
                    if (k < end && (s[k] == '+' || s[k] == '-')) k++;
                    if (k < end && isdigit((unsigned char)s[k])) {
                        j = k;
                        while (j < end && isdigit((unsigned char)s[j])) j++;
                    }
                }
                char* t = xstrndup(s + i, j - i);
                v = strtod(t, NULL);
                free(t);
            }
            if (j < end && is_name_start((unsigned char)s[j])) {
                fail_at(p, j, "bad number");
                return;
            }
            Tok* t = add_tok(p, T_NUM, i, nl);
            t->num = v;
            nl = 0;
            i = j;
            continue;
        }
        if (c == '\'' || c == '"') {
            size_t j = i + 1;
            Buf b = {0};
            while (j < end && (unsigned char)s[j] != c) {
                if (s[j] == '\n') {
                    fail_at(p, j, "newline in string");
                    buf_free(&b);
                    return;
                }
                if (s[j] == '\\') {
                    j++;
                    if (!read_escape(p, s, end, &j, &b)) {
                        fail_at(p, j, "bad escape");
                        buf_free(&b);
                        return;
                    }
                    continue;
                }
                buf_u8(&b, (uint8_t)s[j]);
                j++;
            }
            if (j >= end) {
                fail_at(p, i, "unterminated string");
                buf_free(&b);
                return;
            }
            Tok* t = add_tok(p, T_STR, i, nl);
            t->text = xstrndup(b.data ? (const char*)b.data : "", b.len);
            t->len = b.len;
            buf_free(&b);
            nl = 0;
            i = j + 1;
            continue;
        }
        if (c == '`') {
            read_template(p, &i, nl);
            nl = 0;
            continue;
        }
        if (is_name_start(c)) {
            size_t j = i + 1;
            while (j < end && is_name_char((unsigned char)s[j])) j++;
            Tok* t = add_tok(p, T_NAME, i, nl);
            t->text = xstrndup(s + i, j - i);
            t->len = j - i;
            nl = 0;
            i = j;
            continue;
        }
        int matched = 0;
        for (int k = 0; OPS[k]; k++) {
            size_t l = strlen(OPS[k]);
            if (i + l <= end && !strncmp(s + i, OPS[k], l)) {
                if (!strcmp(OPS[k], "?.") && i + 2 < end && isdigit((unsigned char)s[i + 2])) continue;
                Tok* t = add_tok(p, T_OP, i, nl);
                t->text = xstrdup(OPS[k]);
                t->len = l;
                nl = 0;
                i += l;
                matched = 1;
                break;
            }
        }
        if (!matched) {
            char msg[64];
            snprintf(msg, sizeof(msg), "unexpected character '%c'", c);
            fail_at(p, i, msg);
            return;
        }
    }
}

static int is_op(const Tok* t, const char* v) { return t->kind == T_OP && !strcmp(t->text, v); }
static int is_name(const Tok* t, const char* v) { return t->kind == T_NAME && !strcmp(t->text, v); }

static Tok* take(P* p) {
    Tok* t = peek(p, 0);
    if (p->i < p->count - 1) p->i++;
    return t;
}

static int accept(P* p, const char* v) {
    if (is_op(peek(p, 0), v)) {
        take(p);
        return 1;
    }
    return 0;
}

static void expect(P* p, const char* v) {
    if (!accept(p, v)) {
        char msg[64];
        Tok* t = peek(p, 0);
        snprintf(msg, sizeof(msg), "expected '%s' but got '%s'", v, t->kind == T_EOF ? "end" : t->text ? t->text : "literal");
        fail(p, msg);
    }
}

static Node* op(int code) {
    Node* a = node_arr();
    node_push(a, node_num(code));
    return a;
}

static Node* op1(int code, Node* x) {
    Node* a = op(code);
    node_push(a, x);
    return a;
}

static Node* op2(int code, Node* x, Node* y) {
    Node* a = op1(code, x);
    node_push(a, y);
    return a;
}

static Node* op3(int code, Node* x, Node* y, Node* z) {
    Node* a = op2(code, x, y);
    node_push(a, z);
    return a;
}

static int code_of(const Node* n) { return n && n->type == N_ARR && n->count ? (int)n->items[0]->num : 0; }

static const char* RESERVED[] = {"break",  "case",   "catch", "class",  "const",  "continue", "debugger", "default", "delete",
                                 "do",     "else",   "export", "extends", "finally", "for",     "function", "if",      "import",
                                 "in",     "instanceof", "new", "return", "super",  "switch",   "this",     "throw",   "try",
                                 "typeof", "var",    "void",  "while",  "with",   NULL};

static int is_reserved(const char* s) {
    for (int i = 0; RESERVED[i]; i++) {
        if (!strcmp(RESERVED[i], s)) return 1;
    }
    return 0;
}

static Node* expression(P* p);
static Node* assignment(P* p);
static Node* statement(P* p);
static Node* function_rest(P* p, const char* name, int arrow);
static Node* block_body(P* p);

static Node* template_expr(P* p) {
    Node* acc = op1(JS_STR, node_str(""));
    for (;;) {
        Tok* t = take(p);
        if (t->kind != T_STR) {
            fail(p, "bad template");
            return acc;
        }
        if (t->len) acc = op3(JS_BIN, node_num(JB_ADD), acc, op1(JS_STR, node_strn(t->text, t->len)));
        Tok* sep = take(p);
        if (is_op(sep, "`end")) return acc;
        if (!is_op(sep, "${")) {
            fail(p, "bad template");
            return acc;
        }
        Node* e = expression(p);
        expect(p, "}$");
        acc = op3(JS_BIN, node_num(JB_ADD), acc, e);
        if (p->failed) return acc;
    }
}

static Node* property_key(P* p, int* computed) {
    Tok* t = take(p);
    *computed = 0;
    if (t->kind == T_NAME) return node_str(t->text);
    if (t->kind == T_STR) return node_strn(t->text, t->len);
    if (t->kind == T_NUM) {
        char tmp[64];
        if (t->num == (double)(long long)t->num) snprintf(tmp, sizeof(tmp), "%lld", (long long)t->num);
        else snprintf(tmp, sizeof(tmp), "%.17g", t->num);
        return node_str(tmp);
    }
    if (is_op(t, "[")) {
        Node* e = assignment(p);
        expect(p, "]");
        *computed = 1;
        return e;
    }
    fail_at(p, t->pos, "bad property key");
    return node_str("");
}

static Node* object_literal(P* p) {
    Node* props = node_arr();
    while (!p->failed && !accept(p, "}")) {
        if (accept(p, "...")) {
            node_push(props, op(JS_SPREAD));
            node_push(props, assignment(p));
        } else {
            Tok* kt = peek(p, 0);
            int is_getset = kt->kind == T_NAME && (!strcmp(kt->text, "get") || !strcmp(kt->text, "set")) && peek(p, 1)->kind != T_OP;
            if (is_getset) {
                fail(p, "object getters and setters are not supported");
                break;
            }
            int computed;
            Node* key = property_key(p, &computed);
            Node* val;
            if (accept(p, ":")) {
                val = assignment(p);
            } else if (is_op(peek(p, 0), "(")) {
                val = function_rest(p, computed ? "" : key->str, 0);
            } else if (!computed && kt->kind == T_NAME) {
                val = op1(JS_NAME, node_str(key->str));
            } else {
                fail(p, "expected ':' in object literal");
                node_free(key);
                break;
            }
            node_push(props, key);
            node_push(props, val);
        }
        if (!accept(p, ",")) {
            expect(p, "}");
            break;
        }
    }
    return op1(JS_OBJ, props);
}

static Node* args_list(P* p, const char* close) {
    Node* args = node_arr();
    while (!p->failed && !accept(p, close)) {
        if (accept(p, "...")) node_push(args, op1(JS_SPREAD, assignment(p)));
        else node_push(args, assignment(p));
        if (!accept(p, ",")) {
            expect(p, close);
            break;
        }
    }
    return args;
}

static Node* array_literal(P* p) {
    Node* items = node_arr();
    while (!p->failed && !accept(p, "]")) {
        if (is_op(peek(p, 0), ",")) {
            take(p);
            node_push(items, op(JS_UNDEF));
            continue;
        }
        if (accept(p, "...")) node_push(items, op1(JS_SPREAD, assignment(p)));
        else node_push(items, assignment(p));
        if (!accept(p, ",")) {
            expect(p, "]");
            break;
        }
    }
    return op1(JS_ARR, items);
}

static Node* primary(P* p) {
    Tok* t = take(p);
    switch (t->kind) {
    case T_NUM: return op1(JS_NUM, node_num(t->num));
    case T_STR: return op1(JS_STR, node_strn(t->text, t->len));
    case T_REGEX: return op2(JS_REGEX, node_strn(t->text, t->len), node_str(t->flags ? t->flags : ""));
    case T_TMPL: return template_expr(p);
    case T_NAME:
        if (!strcmp(t->text, "true")) return op(JS_TRUE);
        if (!strcmp(t->text, "false")) return op(JS_FALSE);
        if (!strcmp(t->text, "null")) return op(JS_NULL);
        if (!strcmp(t->text, "undefined")) return op(JS_UNDEF);
        if (!strcmp(t->text, "this")) return op(JS_THIS);
        if (!strcmp(t->text, "function")) {
            const char* name = "";
            if (peek(p, 0)->kind == T_NAME) name = take(p)->text;
            return function_rest(p, name, 0);
        }
        if (!strcmp(t->text, "new")) {
            Node* callee = primary(p);
            for (;;) {
                if (accept(p, ".")) {
                    Tok* n = take(p);
                    if (n->kind != T_NAME) {
                        fail(p, "expected property name");
                        break;
                    }
                    callee = op2(JS_GET, callee, node_str(n->text));
                } else if (accept(p, "[")) {
                    Node* e = expression(p);
                    expect(p, "]");
                    callee = op2(JS_INDEX, callee, e);
                } else {
                    break;
                }
            }
            Node* args = accept(p, "(") ? args_list(p, ")") : node_arr();
            return op2(JS_NEW, callee, args);
        }
        if (is_reserved(t->text)) {
            char msg[64];
            snprintf(msg, sizeof(msg), "unexpected keyword '%s'", t->text);
            fail_at(p, t->pos, msg);
            return op(JS_UNDEF);
        }
        return op1(JS_NAME, node_str(t->text));
    case T_OP:
        if (!strcmp(t->text, "(")) {
            Node* e = expression(p);
            expect(p, ")");
            return e;
        }
        if (!strcmp(t->text, "[")) return array_literal(p);
        if (!strcmp(t->text, "{")) return object_literal(p);
        if (!strcmp(t->text, "/")) {
            fail_at(p, t->pos, "regular expressions are not supported");
            return op(JS_UNDEF);
        }
        break;
    default: break;
    }
    fail_at(p, t->pos, t->kind == T_EOF ? "unexpected end of script" : "unexpected token");
    return op(JS_UNDEF);
}

static Node* call_tail(P* p, Node* e) {
    for (;;) {
        if (p->failed) return e;
        if (accept(p, ".")) {
            Tok* n = take(p);
            if (n->kind != T_NAME) {
                fail_at(p, n->pos, "expected property name");
                return e;
            }
            e = op2(JS_GET, e, node_str(n->text));
        } else if (accept(p, "?.")) {
            if (accept(p, "(")) {
                e = op2(JS_OPTCALL, e, args_list(p, ")"));
            } else if (accept(p, "[")) {
                Node* ix = expression(p);
                expect(p, "]");
                e = op3(JS_OPTGET, e, ix, node_num(1));
            } else {
                Tok* n = take(p);
                if (n->kind != T_NAME) {
                    fail_at(p, n->pos, "expected property name");
                    return e;
                }
                e = op3(JS_OPTGET, e, op1(JS_STR, node_str(n->text)), node_num(0));
            }
        } else if (is_op(peek(p, 0), "[")) {
            take(p);
            Node* ix = expression(p);
            expect(p, "]");
            e = op2(JS_INDEX, e, ix);
        } else if (is_op(peek(p, 0), "(")) {
            take(p);
            e = op2(JS_CALL, e, args_list(p, ")"));
        } else if (peek(p, 0)->kind == T_TMPL && !peek(p, 0)->nl) {
            fail(p, "tagged templates are not supported");
            return e;
        } else {
            return e;
        }
    }
}

static int is_target(const Node* n) {
    int c = code_of(n);
    return c == JS_NAME || c == JS_GET || c == JS_INDEX;
}

static Node* postfix(P* p) {
    Node* e = call_tail(p, primary(p));
    Tok* t = peek(p, 0);
    if (!t->nl && (is_op(t, "++") || is_op(t, "--"))) {
        take(p);
        if (!is_target(e)) {
            fail(p, "invalid increment target");
            return e;
        }
        return op2(JS_INC, node_num(t->text[0] == '+' ? JI_POSTINC : JI_POSTDEC), e);
    }
    return e;
}

static Node* unary(P* p) {
    Tok* t = peek(p, 0);
    if (t->kind == T_OP) {
        int u = 0;
        if (!strcmp(t->text, "!")) u = JU_NOT;
        else if (!strcmp(t->text, "-")) u = JU_NEG;
        else if (!strcmp(t->text, "+")) u = JU_PLUS;
        else if (!strcmp(t->text, "~")) u = JU_BNOT;
        if (u) {
            take(p);
            return op2(JS_UNARY, node_num(u), unary(p));
        }
        if (!strcmp(t->text, "++") || !strcmp(t->text, "--")) {
            take(p);
            Node* e = unary(p);
            if (!is_target(e)) fail(p, "invalid increment target");
            return op2(JS_INC, node_num(t->text[0] == '+' ? JI_PREINC : JI_PREDEC), e);
        }
    } else if (t->kind == T_NAME) {
        if (!strcmp(t->text, "typeof")) {
            take(p);
            return op2(JS_UNARY, node_num(JU_TYPEOF), unary(p));
        }
        if (!strcmp(t->text, "void")) {
            take(p);
            return op2(JS_UNARY, node_num(JU_VOID), unary(p));
        }
        if (!strcmp(t->text, "delete")) {
            take(p);
            return op1(JS_DELETE, unary(p));
        }
    }
    return postfix(p);
}

typedef struct {
    const char* text;
    int prec;
    int code;
    int kind;
} BinInfo;

static const BinInfo BINS[] = {{"??", 1, 0, JS_NULLISH}, {"||", 2, 0, JS_OR},         {"&&", 3, 0, JS_AND},          {"|", 4, JB_BOR, JS_BIN},
                               {"^", 5, JB_BXOR, JS_BIN}, {"&", 6, JB_BAND, JS_BIN},     {"==", 7, JB_EQ, JS_BIN},      {"!=", 7, JB_NE, JS_BIN},
                               {"===", 7, JB_SEQ, JS_BIN}, {"!==", 7, JB_SNE, JS_BIN},   {"<", 8, JB_LT, JS_BIN},       {">", 8, JB_GT, JS_BIN},
                               {"<=", 8, JB_LE, JS_BIN},  {">=", 8, JB_GE, JS_BIN},      {"<<", 9, JB_SHL, JS_BIN},     {">>", 9, JB_SHR, JS_BIN},
                               {">>>", 9, JB_USHR, JS_BIN}, {"+", 10, JB_ADD, JS_BIN},   {"-", 10, JB_SUB, JS_BIN},     {"*", 11, JB_MUL, JS_BIN},
                               {"/", 11, JB_DIV, JS_BIN}, {"%", 11, JB_MOD, JS_BIN},     {"**", 12, JB_POW, JS_BIN},    {NULL, 0, 0, 0}};

static const BinInfo* bin_info(P* p, const Tok* t) {
    if (t->kind == T_NAME) {
        static const BinInfo in_op = {"in", 8, JB_IN, JS_BIN};
        static const BinInfo inst = {"instanceof", 8, JB_INSTANCEOF, JS_BIN};
        if (!strcmp(t->text, "in") && !p->no_in) return &in_op;
        if (!strcmp(t->text, "instanceof")) return &inst;
        return NULL;
    }
    if (t->kind != T_OP) return NULL;
    for (int i = 0; BINS[i].text; i++) {
        if (!strcmp(BINS[i].text, t->text)) return &BINS[i];
    }
    return NULL;
}

static Node* binary(P* p, int min_prec) {
    Node* left = unary(p);
    for (;;) {
        if (p->failed) return left;
        const BinInfo* b = bin_info(p, peek(p, 0));
        if (!b || b->prec < min_prec) return left;
        take(p);
        Node* right = binary(p, b->code == JB_POW ? b->prec : b->prec + 1);
        if (b->kind == JS_BIN) left = op3(JS_BIN, node_num(b->code), left, right);
        else left = op2(b->kind, left, right);
    }
}

static Node* conditional(P* p) {
    Node* c = binary(p, 1);
    if (!accept(p, "?")) return c;
    int saved = p->no_in;
    p->no_in = 0;
    Node* a = assignment(p);
    p->no_in = saved;
    expect(p, ":");
    Node* b = assignment(p);
    return op3(JS_COND, c, a, b);
}

static int arrow_ahead(P* p) {
    Tok* t = peek(p, 0);
    if (t->kind == T_NAME && !is_reserved(t->text) && is_op(peek(p, 1), "=>")) return 1;
    if (!is_op(t, "(")) return 0;
    int depth = 0;
    for (int k = 0; p->i + k < p->count; k++) {
        Tok* u = peek(p, k);
        if (u->kind == T_EOF) return 0;
        if (is_op(u, "(") || is_op(u, "[") || is_op(u, "{")) depth++;
        else if (is_op(u, ")") || is_op(u, "]") || is_op(u, "}")) {
            depth--;
            if (depth == 0) return is_op(peek(p, k + 1), "=>") && !peek(p, k + 1)->nl;
        }
    }
    return 0;
}

static Node* default_param_stmt(const char* name, Node* def) {
    Node* test = op3(JS_BIN, node_num(JB_SEQ), op1(JS_NAME, node_str(name)), op(JS_UNDEF));
    Node* set = op1(JS_EXPR, op3(JS_ASSIGN, node_num(0), op1(JS_NAME, node_str(name)), def));
    return op3(JS_IF, test, set, node_null());
}

static void prepend_defaults(Node* body, Node* defaults) {
    if (!defaults->count) return;
    Node* merged = node_arr();
    for (size_t i = 0; i < defaults->count; i++) node_push(merged, defaults->items[i]);
    for (size_t i = 0; i < body->count; i++) node_push(merged, body->items[i]);
    free(body->items);
    body->items = merged->items;
    body->count = merged->count;
    body->cap = merged->cap;
    merged->items = NULL;
    merged->count = merged->cap = 0;
    node_free(merged);
    defaults->count = 0;
}

static Node* arrow_function(P* p) {
    Node* params = node_arr();
    Node* defaults = node_arr();
    if (peek(p, 0)->kind == T_NAME) {
        node_push(params, node_str(take(p)->text));
    } else {
        expect(p, "(");
        while (!p->failed && !accept(p, ")")) {
            Tok* n = take(p);
            if (n->kind != T_NAME) {
                fail_at(p, n->pos, "only simple arrow parameters are supported");
                break;
            }
            Node* pn = node_str(n->text);
            if (accept(p, "=")) node_push(defaults, default_param_stmt(n->text, assignment(p)));
            node_push(params, pn);
            if (!accept(p, ",")) {
                expect(p, ")");
                break;
            }
        }
    }
    expect(p, "=>");
    Node* body;
    if (is_op(peek(p, 0), "{")) {
        body = block_body(p);
    } else {
        int saved = p->no_in;
        p->no_in = 0;
        Node* e = assignment(p);
        p->no_in = saved;
        body = node_arr();
        node_push(body, op1(JS_RETURN, e));
    }
    prepend_defaults(body, defaults);
    node_free(defaults);
    Node* f = op(JS_FUNC);
    node_push(f, params);
    node_push(f, body);
    node_push(f, node_num(1));
    node_push(f, node_str(""));
    return f;
}

static int assign_code(const Tok* t) {
    if (t->kind != T_OP) return -1;
    static const struct {
        const char* t;
        int c;
    } A[] = {{"=", 0},        {"+=", JB_ADD},   {"-=", JB_SUB},    {"*=", JB_MUL},     {"/=", JB_DIV},    {"%=", JB_MOD},
             {"**=", JB_POW}, {"<<=", JB_SHL},  {">>=", JB_SHR},   {">>>=", JB_USHR},  {"&=", JB_BAND},   {"|=", JB_BOR},
             {"^=", JB_BXOR}, {"&&=", -2},      {"||=", -3},       {"?\?=", -4},       {NULL, 0}};
    for (int i = 0; A[i].t; i++) {
        if (!strcmp(A[i].t, t->text)) return A[i].c;
    }
    return -1;
}

static Node* assignment(P* p) {
    if (arrow_ahead(p)) return arrow_function(p);
    Node* left = conditional(p);
    int ac = assign_code(peek(p, 0));
    if (ac == -1) return left;
    take(p);
    if (!is_target(left)) {
        fail(p, "invalid assignment target");
        return left;
    }
    Node* right = assignment(p);
    if (ac < -1) {
        Node* copy = node_clone(left);
        Node* set = op3(JS_ASSIGN, node_num(0), copy, right);
        int k = ac == -2 ? JS_AND : ac == -3 ? JS_OR : JS_NULLISH;
        return op2(k, left, set);
    }
    return op3(JS_ASSIGN, node_num(ac), left, right);
}

static Node* expression(P* p) {
    Node* e = assignment(p);
    if (!is_op(peek(p, 0), ",")) return e;
    Node* list = node_arr();
    node_push(list, e);
    while (accept(p, ",")) node_push(list, assignment(p));
    return op1(JS_SEQ, list);
}

static void end_statement(P* p) {
    if (accept(p, ";")) return;
    Tok* t = peek(p, 0);
    if (t->kind == T_EOF || is_op(t, "}") || t->nl) return;
    fail(p, "expected ';'");
}

static Node* block_body(P* p) {
    expect(p, "{");
    Node* stmts = node_arr();
    while (!p->failed && !accept(p, "}")) {
        if (peek(p, 0)->kind == T_EOF) {
            fail(p, "expected '}'");
            break;
        }
        node_push(stmts, statement(p));
    }
    return stmts;
}

static Node* function_rest(P* p, const char* name, int arrow) {
    expect(p, "(");
    Node* params = node_arr();
    Node* defaults = node_arr();
    while (!p->failed && !accept(p, ")")) {
        Tok* n = take(p);
        if (n->kind != T_NAME) {
            fail_at(p, n->pos, "only simple parameters are supported");
            break;
        }
        if (accept(p, "=")) node_push(defaults, default_param_stmt(n->text, assignment(p)));
        node_push(params, node_str(n->text));
        if (!accept(p, ",")) {
            expect(p, ")");
            break;
        }
    }
    Node* body = block_body(p);
    prepend_defaults(body, defaults);
    node_free(defaults);
    Node* f = op(JS_FUNC);
    node_push(f, params);
    node_push(f, body);
    node_push(f, node_num(arrow));
    node_push(f, node_str(name ? name : ""));
    return f;
}

static Node* parse_pattern(P* p) {
    Node* pat = node_arr();
    if (accept(p, "[")) {
        node_push(pat, node_str("A"));
        Node* names = node_arr();
        while (!p->failed && !accept(p, "]")) {
            if (is_op(peek(p, 0), ",")) {
                take(p);
                node_push(names, node_null());
                continue;
            }
            Tok* n = take(p);
            if (n->kind != T_NAME || is_reserved(n->text)) {
                fail_at(p, n->pos, "only flat destructuring patterns are supported");
                break;
            }
            node_push(names, node_str(n->text));
            if (!accept(p, ",")) {
                expect(p, "]");
                break;
            }
        }
        node_push(pat, names);
        return pat;
    }
    expect(p, "{");
    node_push(pat, node_str("O"));
    Node* pairs = node_arr();
    while (!p->failed && !accept(p, "}")) {
        Tok* k = take(p);
        if (k->kind != T_NAME && k->kind != T_STR) {
            fail_at(p, k->pos, "only flat destructuring patterns are supported");
            break;
        }
        Node* pair = node_arr();
        node_push(pair, node_str(k->text));
        if (accept(p, ":")) {
            Tok* n = take(p);
            if (n->kind != T_NAME) {
                fail_at(p, n->pos, "only flat destructuring patterns are supported");
                node_free(pair);
                break;
            }
            node_push(pair, node_str(n->text));
        } else {
            node_push(pair, node_str(k->text));
        }
        node_push(pairs, pair);
        if (!accept(p, ",")) {
            expect(p, "}");
            break;
        }
    }
    node_push(pat, pairs);
    return pat;
}

static void push_pattern_bindings(Node* list, Node* pat, const char* temp) {
    const char* kind = node_strv(pat->items[0]);
    Node* items = pat->items[1];
    for (size_t i = 0; i < items->count; i++) {
        if (kind[0] == 'A') {
            const char* name = node_strv(items->items[i]);
            if (!name) continue;
            node_push(list, node_str(name));
            node_push(list, op2(JS_INDEX, op1(JS_NAME, node_str(temp)), op1(JS_NUM, node_num((double)i))));
        } else {
            Node* pair = items->items[i];
            node_push(list, node_str(node_strv(pair->items[1])));
            node_push(list, op2(JS_GET, op1(JS_NAME, node_str(temp)), node_str(node_strv(pair->items[0]))));
        }
    }
}

static char* temp_name(P* p) {
    char buf[32];
    snprintf(buf, sizeof buf, "__d%d", p->temp_id++);
    return xstrdup(buf);
}

static Node* var_decl(P* p, int kind) {
    Node* list = node_arr();
    for (;;) {
        if (is_op(peek(p, 0), "[") || is_op(peek(p, 0), "{")) {
            Node* pat = parse_pattern(p);
            if (p->failed) {
                node_free(pat);
                break;
            }
            expect(p, "=");
            char* temp = temp_name(p);
            node_push(list, node_str(temp));
            node_push(list, assignment(p));
            push_pattern_bindings(list, pat, temp);
            free(temp);
            node_free(pat);
            if (!accept(p, ",")) break;
            continue;
        }
        Tok* n = take(p);
        if (n->kind != T_NAME || is_reserved(n->text)) {
            fail_at(p, n->pos, "destructuring declarations are not supported");
            break;
        }
        node_push(list, node_str(n->text));
        if (accept(p, "=")) node_push(list, assignment(p));
        else node_push(list, node_null());
        if (!accept(p, ",")) break;
    }
    return op2(JS_VAR, node_num(kind), list);
}

static int decl_kind(const Tok* t) {
    if (t->kind != T_NAME) return -1;
    if (!strcmp(t->text, "var")) return 0;
    if (!strcmp(t->text, "let")) return 1;
    if (!strcmp(t->text, "const")) return 2;
    return -1;
}

static Node* statement_block(P* p) {
    if (is_op(peek(p, 0), "{")) return op1(JS_BLOCK, block_body(p));
    return statement(p);
}

static Node* for_statement(P* p) {
    expect(p, "(");
    Node* init = node_null();
    int dk = decl_kind(peek(p, 0));
    if (dk >= 0) {
        take(p);
        Tok* n = peek(p, 0);
        Tok* after = peek(p, 1);
        if (is_op(n, "[") || is_op(n, "{")) {
            int save = p->i;
            Node* pat = parse_pattern(p);
            if (!p->failed && (is_name(peek(p, 0), "in") || is_name(peek(p, 0), "of"))) {
                int of = is_name(take(p), "of");
                Node* obj = expression(p);
                expect(p, ")");
                Node* inner = statement_block(p);
                char* temp = temp_name(p);
                Node* decl_list = node_arr();
                push_pattern_bindings(decl_list, pat, temp);
                Node* stmts = node_arr();
                node_push(stmts, op2(JS_VAR, node_num(dk), decl_list));
                node_push(stmts, inner);
                Node* r = op(of ? JS_FOROF : JS_FORIN);
                node_push(r, node_num(dk));
                node_push(r, op1(JS_NAME, node_str(temp)));
                node_push(r, obj);
                node_push(r, op1(JS_BLOCK, stmts));
                free(temp);
                node_free(pat);
                node_free(init);
                return r;
            }
            node_free(pat);
            if (!p->failed) p->i = save;
        }
        if (n->kind == T_NAME && (is_name(after, "in") || is_name(after, "of"))) {
            take(p);
            int of = is_name(take(p), "of");
            Node* obj = expression(p);
            expect(p, ")");
            Node* body = statement_block(p);
            Node* r = op(of ? JS_FOROF : JS_FORIN);
            node_push(r, node_num(dk));
            node_push(r, op1(JS_NAME, node_str(n->text)));
            node_push(r, obj);
            node_push(r, body);
            node_free(init);
            return r;
        }
        p->no_in = 1;
        node_free(init);
        init = var_decl(p, dk);
        p->no_in = 0;
    } else if (!is_op(peek(p, 0), ";")) {
        p->no_in = 1;
        Node* e = expression(p);
        p->no_in = 0;
        if (is_name(peek(p, 0), "in") || is_name(peek(p, 0), "of")) {
            int of = is_name(take(p), "of");
            if (!is_target(e)) fail(p, "invalid for-in target");
            Node* obj = expression(p);
            expect(p, ")");
            Node* body = statement_block(p);
            Node* r = op(of ? JS_FOROF : JS_FORIN);
            node_push(r, node_num(-1));
            node_push(r, e);
            node_push(r, obj);
            node_push(r, body);
            node_free(init);
            return r;
        }
        node_free(init);
        init = op1(JS_EXPR, e);
    }
    expect(p, ";");
    Node* cond = is_op(peek(p, 0), ";") ? node_null() : expression(p);
    expect(p, ";");
    Node* update = is_op(peek(p, 0), ")") ? node_null() : expression(p);
    expect(p, ")");
    Node* body = statement_block(p);
    Node* r = op(JS_FOR);
    node_push(r, init);
    node_push(r, cond);
    node_push(r, update);
    node_push(r, body);
    return r;
}

static Node* switch_statement(P* p) {
    expect(p, "(");
    Node* disc = expression(p);
    expect(p, ")");
    expect(p, "{");
    Node* cases = node_arr();
    while (!p->failed && !accept(p, "}")) {
        Node* test;
        if (is_name(peek(p, 0), "case")) {
            take(p);
            test = expression(p);
        } else if (is_name(peek(p, 0), "default")) {
            take(p);
            test = node_null();
        } else {
            fail(p, "expected case");
            break;
        }
        expect(p, ":");
        Node* body = node_arr();
        while (!p->failed && !is_name(peek(p, 0), "case") && !is_name(peek(p, 0), "default") && !is_op(peek(p, 0), "}")) {
            if (peek(p, 0)->kind == T_EOF) {
                fail(p, "expected '}'");
                break;
            }
            node_push(body, statement(p));
        }
        Node* c = node_arr();
        node_push(c, test);
        node_push(c, body);
        node_push(cases, c);
    }
    return op2(JS_SWITCH, disc, cases);
}

static Node* statement(P* p) {
    Tok* t = peek(p, 0);
    if (is_op(t, "{")) return op1(JS_BLOCK, block_body(p));
    if (is_op(t, ";")) {
        take(p);
        return op(JS_EMPTY);
    }
    if (t->kind == T_NAME) {
        int dk = decl_kind(t);
        if (dk >= 0 && (dk == 0 || peek(p, 1)->kind == T_NAME || (dk == 2 && (is_op(peek(p, 1), "[") || is_op(peek(p, 1), "{"))) ||
                        (dk == 1 && is_op(peek(p, 1), "{")))) {
            take(p);
            Node* d = var_decl(p, dk);
            end_statement(p);
            return d;
        }
        if (!strcmp(t->text, "if")) {
            take(p);
            expect(p, "(");
            Node* c = expression(p);
            expect(p, ")");
            Node* a = statement_block(p);
            Node* b = node_null();
            if (is_name(peek(p, 0), "else")) {
                take(p);
                node_free(b);
                b = statement_block(p);
            }
            return op3(JS_IF, c, a, b);
        }
        if (!strcmp(t->text, "for")) {
            take(p);
            return for_statement(p);
        }
        if (!strcmp(t->text, "while")) {
            take(p);
            expect(p, "(");
            Node* c = expression(p);
            expect(p, ")");
            return op2(JS_WHILE, c, statement_block(p));
        }
        if (!strcmp(t->text, "do")) {
            take(p);
            Node* body = statement_block(p);
            if (!is_name(take(p), "while")) fail(p, "expected while");
            expect(p, "(");
            Node* c = expression(p);
            expect(p, ")");
            accept(p, ";");
            return op2(JS_DOWHILE, body, c);
        }
        if (!strcmp(t->text, "break") || !strcmp(t->text, "continue")) {
            take(p);
            if (peek(p, 0)->kind == T_NAME && !peek(p, 0)->nl) {
                fail(p, "labels are not supported");
                return op(JS_EMPTY);
            }
            end_statement(p);
            return op(t->text[0] == 'b' ? JS_BREAK : JS_CONTINUE);
        }
        if (!strcmp(t->text, "return")) {
            take(p);
            Tok* n = peek(p, 0);
            if (n->nl || n->kind == T_EOF || is_op(n, ";") || is_op(n, "}")) {
                accept(p, ";");
                return op1(JS_RETURN, node_null());
            }
            Node* e = expression(p);
            end_statement(p);
            return op1(JS_RETURN, e);
        }
        if (!strcmp(t->text, "throw")) {
            take(p);
            Node* e = expression(p);
            end_statement(p);
            return op1(JS_THROW, e);
        }
        if (!strcmp(t->text, "switch")) {
            take(p);
            return switch_statement(p);
        }
        if (!strcmp(t->text, "try")) {
            take(p);
            Node* body = block_body(p);
            Node* cname = node_null();
            Node* cbody = node_null();
            Node* fbody = node_null();
            if (is_name(peek(p, 0), "catch")) {
                take(p);
                if (accept(p, "(")) {
                    Tok* n = take(p);
                    if (n->kind != T_NAME) fail(p, "expected catch name");
                    else {
                        node_free(cname);
                        cname = node_str(n->text);
                    }
                    expect(p, ")");
                }
                node_free(cbody);
                cbody = block_body(p);
            }
            if (is_name(peek(p, 0), "finally")) {
                take(p);
                node_free(fbody);
                fbody = block_body(p);
            }
            Node* r = op(JS_TRY);
            node_push(r, body);
            node_push(r, cname);
            node_push(r, cbody);
            node_push(r, fbody);
            return r;
        }
        if (!strcmp(t->text, "function") && peek(p, 1)->kind == T_NAME) {
            take(p);
            const char* name = take(p)->text;
            Node* f = function_rest(p, name, 0);
            return op2(JS_FUNCDECL, node_str(name), f);
        }
        if (peek(p, 1)->kind == T_OP && !strcmp(peek(p, 1)->text, ":") && !is_reserved(t->text) && strcmp(t->text, "default")) {
            fail(p, "labels are not supported");
            return op(JS_EMPTY);
        }
        if (!strcmp(t->text, "class")) {
            fail(p, "classes are not supported");
            return op(JS_EMPTY);
        }
    }
    Node* e = expression(p);
    end_statement(p);
    return op1(JS_EXPR, e);
}

static void cleanup(P* p) {
    for (int i = 0; i < p->count; i++) {
        free(p->toks[i].text);
        free(p->toks[i].flags);
    }
    free(p->toks);
}

static Node* compile_impl(const char* src, size_t len, char* err, size_t errlen, Node* ranges) {
    P p;
    memset(&p, 0, sizeof(p));
    p.src = src;
    p.n = len;
    p.err = err;
    p.errlen = errlen;
    tokenize_range(&p, 0, len, 0);
    add_tok(&p, T_EOF, len, 1);
    Node* stmts = node_arr();
    while (!p.failed && peek(&p, 0)->kind != T_EOF) {
        size_t start = peek(&p, 0)->pos;
        node_push(stmts, statement(&p));
        if (ranges) {
            Node* r = node_arr();
            node_push(r, node_num((double)start));
            node_push(r, node_num((double)peek(&p, 0)->pos));
            node_push(ranges, r);
        }
    }
    cleanup(&p);
    if (p.failed) {
        node_free(stmts);
        return NULL;
    }
    return op1(JS_PROGRAM, stmts);
}

Node* js_compile(const char* src, size_t len, char* err, size_t errlen) { return compile_impl(src, len, err, errlen, NULL); }

Node* js_compile_ranges(const char* src, size_t len, char* err, size_t errlen, Node* ranges) { return compile_impl(src, len, err, errlen, ranges); }

Node* js_compile_str(const char* src, char* err, size_t errlen) { return js_compile(src, strlen(src), err, errlen); }
