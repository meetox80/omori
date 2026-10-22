#include "rx.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <stdio.h>

static void* rx_oom(void) {
    fputs("out of memory\n", stderr);
    abort();
    return NULL;
}

static void* xmalloc(size_t n) {
    void* p = malloc(n ? n : 1);
    return p ? p : rx_oom();
}

static void* xcalloc(size_t n, size_t m) {
    void* p = calloc(n ? n : 1, m ? m : 1);
    return p ? p : rx_oom();
}

static void* xrealloc(void* p, size_t n) {
    void* q = realloc(p, n ? n : 1);
    return q ? q : rx_oom();
}

static char* xstrndup(const char* s, size_t n) {
    char* d = (char*)xmalloc(n + 1);
    memcpy(d, s, n);
    d[n] = 0;
    return d;
}

static char* xstrdup(const char* s) {
    size_t n = strlen(s) + 1;
    char* d = (char*)xmalloc(n);
    memcpy(d, s, n);
    return d;
}

enum { R_CHAR, R_ANY, R_CLASS, R_GROUP, R_SEQ, R_ALT, R_BOL, R_EOL, R_WORDB, R_NWORDB, R_BACKREF, R_LOOK };

typedef struct RNode RNode;

struct RNode {
    int kind;
    int ch;
    uint8_t set[32];
    int negate;
    int capture;
    int min, max;
    int lazy;
    RNode** seq;
    int nseq;
    RNode** alts;
    int nalts;
};

struct Rx {
    RNode* root;
    int ngroups;
    int icase;
    int multiline;
    int nullable;
    uint8_t first[32];
};

typedef struct {
    const char* p;
    int failed;
    int ngroups;
    int icase;
} RP;

static RNode* rnew(int kind) {
    RNode* n = xcalloc(1, sizeof(RNode));
    n->kind = kind;
    n->min = n->max = 1;
    return n;
}

static void rfree(RNode* n) {
    if (!n) return;
    for (int i = 0; i < n->nseq; i++) rfree(n->seq[i]);
    free(n->seq);
    for (int i = 0; i < n->nalts; i++) rfree(n->alts[i]);
    free(n->alts);
    free(n);
}

static void set_add(uint8_t* set, int c) { set[(c & 255) >> 3] |= (uint8_t)(1 << (c & 7)); }
static int set_has(const uint8_t* set, int c) { return (set[(c & 255) >> 3] >> (c & 7)) & 1; }

static void add_class_escape(uint8_t* set, char e) {
    for (int c = 0; c < 256; c++) {
        int in = 0;
        switch (e) {
        case 'd': in = c >= '0' && c <= '9'; break;
        case 'D': in = !(c >= '0' && c <= '9'); break;
        case 'w': in = isalnum(c) || c == '_'; break;
        case 'W': in = !(isalnum(c) || c == '_'); break;
        case 's': in = c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; break;
        case 'S': in = !(c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'); break;
        default: break;
        }
        if (in) set_add(set, c);
    }
}

static int escape_char(char e) {
    switch (e) {
    case 'n': return '\n';
    case 'r': return '\r';
    case 't': return '\t';
    case 'f': return '\f';
    case 'v': return '\v';
    case '0': return 0;
    default: return (unsigned char)e;
    }
}

static RNode* parse_alt(RP* r);

static int hexd(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int read_code_escape(RP* r, char e, int* out) {
    int digits = e == 'x' ? 2 : e == 'u' ? 4 : 0;
    if (!digits) return 0;
    int v = 0;
    for (int k = 0; k < digits; k++) {
        int h = hexd(r->p[k]);
        if (h < 0) return 0;
        v = v * 16 + h;
    }
    r->p += digits;
    *out = v;
    return 1;
}

static RNode* utf8_node(int cp) {
    unsigned char b[4];
    int n = 0;
    if (cp < 0x80) b[n++] = (unsigned char)cp;
    else if (cp < 0x800) {
        b[n++] = (unsigned char)(0xC0 | (cp >> 6));
        b[n++] = (unsigned char)(0x80 | (cp & 0x3F));
    } else {
        b[n++] = (unsigned char)(0xE0 | (cp >> 12));
        b[n++] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        b[n++] = (unsigned char)(0x80 | (cp & 0x3F));
    }
    if (n == 1) {
        RNode* c = rnew(R_CHAR);
        c->ch = b[0];
        return c;
    }
    RNode* s = rnew(R_SEQ);
    s->seq = xmalloc(sizeof(RNode*) * (size_t)n);
    for (int i = 0; i < n; i++) {
        RNode* c = rnew(R_CHAR);
        c->ch = b[i];
        s->seq[i] = c;
    }
    s->nseq = n;
    return s;
}

static RNode* parse_class(RP* r) {
    RNode* n = rnew(R_CLASS);
    r->p++;
    if (*r->p == '^') {
        n->negate = 1;
        r->p++;
    }
    int first = 1;
    while (*r->p && (*r->p != ']' || first)) {
        first = 0;
        int lo;
        if (*r->p == '\\') {
            char e = r->p[1];
            r->p += 2;
            if (strchr("dDwWsS", e)) {
                add_class_escape(n->set, e);
                continue;
            }
            if (!read_code_escape(r, e, &lo)) lo = escape_char(e);
        } else {
            lo = (unsigned char)*r->p++;
        }
        int hi = lo;
        if (*r->p == '-' && r->p[1] && r->p[1] != ']') {
            r->p++;
            if (*r->p == '\\') {
                char he = r->p[1];
                r->p += 2;
                if (!read_code_escape(r, he, &hi)) hi = escape_char(he);
            } else {
                hi = (unsigned char)*r->p++;
            }
        }
        for (int c = lo; c <= hi; c++) {
            set_add(n->set, c);
            if (r->icase) {
                set_add(n->set, tolower(c));
                set_add(n->set, toupper(c));
            }
        }
    }
    if (*r->p != ']') r->failed = 1;
    else r->p++;
    return n;
}

static RNode* parse_atom(RP* r) {
    char c = *r->p;
    if (c == '(') {
        r->p++;
        RNode* g = rnew(R_GROUP);
        if (r->p[0] == '?' && (r->p[1] == '=' || r->p[1] == '!')) {
            g->kind = R_LOOK;
            g->negate = r->p[1] == '!';
            r->p += 2;
            g->capture = 0;
        } else if (r->p[0] == '?' && r->p[1] == ':') {
            r->p += 2;
            g->capture = 0;
        } else {
            g->capture = ++r->ngroups;
        }
        RNode* inner = parse_alt(r);
        g->seq = xmalloc(sizeof(RNode*));
        g->seq[0] = inner;
        g->nseq = 1;
        if (*r->p != ')') r->failed = 1;
        else r->p++;
        return g;
    }
    if (c == '[') return parse_class(r);
    if (c == '.') {
        r->p++;
        return rnew(R_ANY);
    }
    if (c == '^') {
        r->p++;
        return rnew(R_BOL);
    }
    if (c == '$') {
        r->p++;
        return rnew(R_EOL);
    }
    if (c == '\\') {
        char e = r->p[1];
        r->p += 2;
        if (strchr("dDwWsS", e)) {
            RNode* n = rnew(R_CLASS);
            add_class_escape(n->set, e);
            return n;
        }
        if (e == 'b') return rnew(R_WORDB);
        if (e == 'B') return rnew(R_NWORDB);
        if (e >= '1' && e <= '9') {
            RNode* n = rnew(R_BACKREF);
            n->ch = e - '0';
            return n;
        }
        int cp;
        if (read_code_escape(r, e, &cp)) return utf8_node(cp);
        RNode* n = rnew(R_CHAR);
        n->ch = escape_char(e);
        return n;
    }
    RNode* n = rnew(R_CHAR);
    n->ch = (unsigned char)c;
    r->p++;
    return n;
}

static RNode* parse_seq(RP* r) {
    RNode* s = rnew(R_SEQ);
    while (*r->p && *r->p != '|' && *r->p != ')' && !r->failed) {
        RNode* a = parse_atom(r);
        char q = *r->p;
        if (q == '*' || q == '+' || q == '?') {
            a->min = q == '+' ? 1 : 0;
            a->max = q == '?' ? 1 : -1;
            r->p++;
            if (*r->p == '?') {
                a->lazy = 1;
                r->p++;
            }
        } else if (q == '{' && isdigit((unsigned char)r->p[1])) {
            r->p++;
            int lo = (int)strtol(r->p, (char**)&r->p, 10), hi = lo;
            if (*r->p == ',') {
                r->p++;
                hi = isdigit((unsigned char)*r->p) ? (int)strtol(r->p, (char**)&r->p, 10) : -1;
            }
            if (*r->p != '}') r->failed = 1;
            else r->p++;
            a->min = lo;
            a->max = hi;
            if (*r->p == '?') {
                a->lazy = 1;
                r->p++;
            }
        }
        s->seq = xrealloc(s->seq, sizeof(RNode*) * (size_t)(s->nseq + 1));
        s->seq[s->nseq++] = a;
    }
    return s;
}

static RNode* parse_alt(RP* r) {
    RNode* first = parse_seq(r);
    if (*r->p != '|') return first;
    RNode* alt = rnew(R_ALT);
    alt->alts = xmalloc(sizeof(RNode*));
    alt->alts[0] = first;
    alt->nalts = 1;
    while (*r->p == '|') {
        r->p++;
        alt->alts = xrealloc(alt->alts, sizeof(RNode*) * (size_t)(alt->nalts + 1));
        alt->alts[alt->nalts++] = parse_seq(r);
    }
    return alt;
}

static int byte_matches(const RNode* n, int c, int icase) {
    switch (n->kind) {
    case R_CHAR: return icase ? tolower(c) == tolower(n->ch) : c == n->ch;
    case R_ANY: return c != '\n' && c != '\r';
    case R_CLASS: {
        int in = set_has(n->set, c);
        if (!in && icase) in = set_has(n->set, tolower(c)) || set_has(n->set, toupper(c));
        return n->negate ? !in : in;
    }
    default: return 0;
    }
}

static int first_bytes(const RNode* n, uint8_t* set, int icase) {
    int nullable = 0;
    switch (n->kind) {
    case R_CHAR:
    case R_ANY:
    case R_CLASS:
        for (int c = 0; c < 256; c++) {
            if (byte_matches(n, c, icase)) set_add(set, c);
        }
        break;
    case R_GROUP: nullable = first_bytes(n->seq[0], set, icase); break;
    case R_SEQ:
        nullable = 1;
        for (int i = 0; i < n->nseq && nullable; i++) nullable = first_bytes(n->seq[i], set, icase);
        break;
    case R_ALT:
        for (int i = 0; i < n->nalts; i++) nullable |= first_bytes(n->alts[i], set, icase);
        break;
    case R_BACKREF: memset(set, 0xFF, 32); return 1;
    default: return 1;
    }
    return nullable || n->min == 0;
}

Rx* rx_compile(const char* pattern, const char* flags) {
    RP r = {pattern, 0, 0, 0};
    int icase = flags && strchr(flags, 'i');
    int multiline = flags && strchr(flags, 'm');
    r.icase = icase;
    RNode* root = parse_alt(&r);
    if (r.failed || *r.p) {
        rfree(root);
        return NULL;
    }
    Rx* rx = xcalloc(1, sizeof(Rx));
    rx->root = root;
    rx->ngroups = r.ngroups;
    rx->icase = icase;
    rx->multiline = multiline;
    rx->nullable = first_bytes(root, rx->first, icase);
    return rx;
}

void rx_free(Rx* rx) {
    if (!rx) return;
    rfree(rx->root);
    free(rx);
}

int rx_groups(const Rx* rx) { return rx->ngroups; }

typedef struct {
    const Rx* rx;
    const char* s;
    size_t n;
    RxMatch* m;
    long steps;
} MS;

typedef struct Cont Cont;
struct Cont {
    int (*fn)(MS* ms, size_t pos, const Cont* k);
    const RNode* node;
    int index;
    int count;
    size_t start;
    const Cont* next;
};

static int run_cont(MS* ms, size_t pos, const Cont* k) {
    if (!k) return 1;
    return k->fn(ms, pos, k);
}

static int match_node(MS* ms, const RNode* n, size_t pos, const Cont* k);

static int is_word(int c) { return isalnum(c) || c == '_'; }

static int single(MS* ms, const RNode* n, size_t pos) {
    if (pos >= ms->n) return 0;
    int c = (unsigned char)ms->s[pos];
    switch (n->kind) {
    case R_CHAR:
        if (ms->rx->icase) return tolower(c) == tolower(n->ch);
        return c == n->ch;
    case R_ANY: return c != '\n' && c != '\r';
    case R_CLASS: {
        int in = set_has(n->set, c);
        if (!in && ms->rx->icase) in = set_has(n->set, tolower(c)) || set_has(n->set, toupper(c));
        return n->negate ? !in : in;
    }
    default: return 0;
    }
}

static int seq_step(MS* ms, size_t pos, const Cont* k);

static int match_seq(MS* ms, const RNode* seq, int index, size_t pos, const Cont* k) {
    if (index >= seq->nseq) return run_cont(ms, pos, k);
    Cont c = {seq_step, seq, index + 1, 0, 0, k};
    return match_node(ms, seq->seq[index], pos, &c);
}

static int seq_step(MS* ms, size_t pos, const Cont* k) { return match_seq(ms, k->node, k->index, pos, k->next); }

static int group_close(MS* ms, size_t pos, const Cont* k) {
    const RNode* g = k->node;
    size_t old_s = 0, old_e = 0;
    int had = 0;
    if (g->capture) {
        had = 1;
        old_s = ms->m->start[g->capture];
        old_e = ms->m->end[g->capture];
        ms->m->start[g->capture] = k->start;
        ms->m->end[g->capture] = pos;
    }
    int r = run_cont(ms, pos, k->next);
    if (!r && had) {
        ms->m->start[g->capture] = old_s;
        ms->m->end[g->capture] = old_e;
    }
    return r;
}

static int match_group_once(MS* ms, const RNode* g, size_t pos, const Cont* k) {
    Cont c = {group_close, g, 0, 0, pos, k};
    const RNode* inner = g->seq[0];
    if (inner->kind == R_ALT) {
        for (int i = 0; i < inner->nalts; i++) {
            if (match_seq(ms, inner->alts[i], 0, pos, &c)) return 1;
        }
        return 0;
    }
    return match_seq(ms, inner, 0, pos, &c);
}

static int repeat_step(MS* ms, size_t pos, const Cont* k);

static int match_repeat(MS* ms, const RNode* n, int count, size_t pos, size_t last, const Cont* k) {
    if (++ms->steps > 2000000) return 0;
    int can_more = n->max < 0 || count < n->max;
    int can_stop = count >= n->min;
    (void)last;
    Cont c = {repeat_step, n, count + 1, 0, pos, k};
    if (n->lazy) {
        if (can_stop && run_cont(ms, pos, k)) return 1;
        if (can_more) {
            if (n->kind == R_GROUP) return match_group_once(ms, n, pos, &c);
            if (single(ms, n, pos)) return repeat_step(ms, pos + 1, &c);
        }
        return 0;
    }
    if (can_more) {
        if (n->kind == R_GROUP) {
            if (match_group_once(ms, n, pos, &c)) return 1;
        } else if (single(ms, n, pos)) {
            if (repeat_step(ms, pos + 1, &c)) return 1;
        }
    }
    return can_stop && run_cont(ms, pos, k);
}

static int repeat_step(MS* ms, size_t pos, const Cont* k) {
    if (k->index - 1 >= k->node->min && pos == k->start) return 0;
    return match_repeat(ms, k->node, k->index, pos, k->start, k->next);
}

static int accept_cont(MS* ms, size_t pos, const Cont* k) {
    (void)ms;
    (void)pos;
    (void)k;
    return 1;
}

static int match_node(MS* ms, const RNode* n, size_t pos, const Cont* k) {
    if (n->kind != R_LOOK && (n->min != 1 || n->max != 1)) return match_repeat(ms, n, 0, pos, (size_t)-1, k);
    switch (n->kind) {
    case R_LOOK: {
        Cont acc = {accept_cont, n, 0, 0, pos, NULL};
        const RNode* inner = n->seq[0];
        int found = 0;
        if (inner->kind == R_ALT) {
            for (int i = 0; i < inner->nalts && !found; i++) found = match_seq(ms, inner->alts[i], 0, pos, &acc);
        } else {
            found = match_seq(ms, inner, 0, pos, &acc);
        }
        if (found != n->negate) return run_cont(ms, pos, k);
        return 0;
    }
    case R_GROUP: return match_group_once(ms, n, pos, k);
    case R_SEQ: return match_seq(ms, n, 0, pos, k);
    case R_ALT:
        for (int i = 0; i < n->nalts; i++) {
            if (match_seq(ms, n->alts[i], 0, pos, k)) return 1;
        }
        return 0;
    case R_BOL:
        if (pos == 0 || (ms->rx->multiline && (ms->s[pos - 1] == '\n' || ms->s[pos - 1] == '\r'))) return run_cont(ms, pos, k);
        return 0;
    case R_EOL:
        if (pos == ms->n || (ms->rx->multiline && (ms->s[pos] == '\n' || ms->s[pos] == '\r'))) return run_cont(ms, pos, k);
        return 0;
    case R_WORDB:
    case R_NWORDB: {
        int a = pos > 0 && is_word((unsigned char)ms->s[pos - 1]);
        int b = pos < ms->n && is_word((unsigned char)ms->s[pos]);
        int at = a != b;
        if ((n->kind == R_WORDB) == at) return run_cont(ms, pos, k);
        return 0;
    }
    case R_BACKREF: {
        int g = n->ch;
        if (g > ms->rx->ngroups || ms->m->start[g] == (size_t)-1) return run_cont(ms, pos, k);
        size_t len = ms->m->end[g] - ms->m->start[g];
        if (pos + len > ms->n) return 0;
        for (size_t i = 0; i < len; i++) {
            int a = (unsigned char)ms->s[ms->m->start[g] + i], b = (unsigned char)ms->s[pos + i];
            if (ms->rx->icase ? tolower(a) != tolower(b) : a != b) return 0;
        }
        return run_cont(ms, pos + len, k);
    }
    default:
        if (!single(ms, n, pos)) return 0;
        return run_cont(ms, pos + 1, k);
    }
}

static int final_cont(MS* ms, size_t pos, const Cont* k) {
    (void)k;
    ms->m->end[0] = pos;
    return 1;
}

int rx_search(const Rx* rx, const char* s, size_t n, size_t from, RxMatch* m) {
    MS ms = {rx, s, n, m, 0};
    for (size_t start = from; start <= n; start++) {
        if (!rx->nullable && (start == n || !set_has(rx->first, (unsigned char)s[start]))) continue;
        for (int g = 0; g <= RX_MAX_GROUPS; g++) m->start[g] = m->end[g] = (size_t)-1;
        m->start[0] = start;
        Cont fin = {final_cont, NULL, 0, 0, 0, NULL};
        ms.steps = 0;
        if (match_node(&ms, rx->root, start, &fin)) return 1;
    }
    return 0;
}

int rx_match_str(const Rx* rx, const char* s, RxMatch* m) { return rx_search(rx, s, strlen(s), 0, m); }

char* rx_group(const RxMatch* m, const char* s, int g) {
    if (g > RX_MAX_GROUPS || m->start[g] == (size_t)-1) return xstrdup("");
    return xstrndup(s + m->start[g], m->end[g] - m->start[g]);
}

int rx_test(const char* pattern, const char* flags, const char* s) {
    Rx* rx = rx_compile(pattern, flags);
    if (!rx) return 0;
    RxMatch m;
    int r = rx_match_str(rx, s, &m);
    rx_free(rx);
    return r;
}
