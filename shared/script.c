#include "script.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "js.h"
#include "rx.h"

static ProblemFn g_problem;

void script_set_problem_handler(ProblemFn fn) { g_problem = fn; }

static void problem(const char* where, int idx, int code, const char* msg) {
    if (g_problem) {
        char* m = str_fmt("%s[%d] code %d: %s", where, idx, code, msg);
        g_problem(m);
        free(m);
    }
}

Node* script_compile(const char* src, const char* where, int idx, int code) {
    char err[200];
    Node* r = js_compile_str(src ? src : "", err, sizeof(err));
    if (!r) problem(where, idx, code, err);
    return r;
}

typedef struct {
    const char* pattern;
    const char* kind;
    int argc;
} RouteRule;

static const RouteRule ROUTE_RULES[] = {
    {"EVAL:[ ](.*)", "EVAL", -1},
    {"(?:ANIMATION|REQUEST ANIMATION):[ ](\\d+)", "ANIMATION", 1},
    {"(?:ICON BALLOON|REQUEST ICON BALLOON):[ ](.*)", "ICON_BALLOON", 0},
    {"(?:BALLOON|REQUEST BALLOON):[ ](.*)", "BALLOON", 0},
    {"(?:JUMP FORWARD|JUMP FORWARDS):[ ](\\d+)", "JUMP_FORWARD", 1},
    {"JUMP[ ](?:TO|TOWARD|TOWARDS):[ ](\\d+),[ ](\\d+)", "JUMP_TO_POINT", 2},
    {"JUMP[ ](?:TO|TOWARD|TOWARDS):[ ]EVENT[ ](\\d+)", "JUMP_TO_EVENT", 1},
    {"JUMP[ ](?:TO|TOWARD|TOWARDS):[ ]PLAYER", "JUMP_TO_PLAYER", 0},
    {"MOVE[ ](?:TO|TOWARD|TOWARDS):[ ](\\d+),[ ](\\d+)", "MOVE_TO_POINT", 2},
    {"MOVE[ ](?:TO|TOWARD|TOWARDS):[ ]EVENT[ ](\\d+)", "MOVE_TO_EVENT", 1},
    {"MOVE[ ](?:TO|TOWARD|TOWARDS):[ ]PLAYER", "MOVE_TO_PLAYER", 0},
    {"(?:PATTERN LOCK):[ ](\\d+)", "PATTERN_LOCK", 1},
    {"(?:PATTERN UNLOCK)", "PATTERN_UNLOCK", 0},
    {"(?:SELF SWITCH)[ ](.*):[ ]ON", "SELF_SWITCH_ON", 0},
    {"(?:SELF SWITCH)[ ](.*):[ ]OFF", "SELF_SWITCH_OFF", 0},
    {"(?:SELF SWITCH)[ ](.*):[ ]TOGGLE", "SELF_SWITCH_TOGGLE", 0},
    {"(?:SELF VARIABLE)[ ](.*):[ ](.*)", "SELF_VARIABLE", -2},
    {"(?:STEP AWAY|STEP AWAY FROM):[ ](\\d+),[ ](\\d+)", "STEP_AWAY_POINT", 2},
    {"(?:STEP AWAY|STEP AWAY FROM):[ ]EVENT[ ](\\d+)", "STEP_AWAY_EVENT", 1},
    {"(?:STEP AWAY|STEP AWAY FROM):[ ]PLAYER", "STEP_AWAY_PLAYER", 0},
    {"(?:STEP TOWARD|STEP TOWARDS):[ ](\\d+),[ ](\\d+)", "STEP_TOWARD_POINT", 2},
    {"(?:STEP TOWARD|STEP TOWARDS):[ ]EVENT[ ](\\d+)", "STEP_TOWARD_EVENT", 1},
    {"(?:STEP TOWARD|STEP TOWARDS):[ ]PLAYER", "STEP_TOWARD_PLAYER", 0},
    {"(?:TELEPORT|TELEPORT TO):[ ](\\d+),[ ](\\d+)", "TELEPORT_POINT", 2},
    {"(?:TELEPORT):[ ]EVENT[ ](\\d+)", "TELEPORT_EVENT", 1},
    {"(?:TELEPORT):[ ]PLAYER", "TELEPORT_PLAYER", 0},
    {"(?:TURN AWAY FROM|TURN AWAY):[ ](\\d+),[ ](\\d+)", "TURN_AWAY_POINT", 2},
    {"(?:TURN AWAY FROM|TURN AWAY):[ ]EVENT[ ](\\d+)", "TURN_AWAY_EVENT", 1},
    {"(?:TURN AWAY FROM|TURN AWAY):[ ]PLAYER", "TURN_AWAY_PLAYER", 0},
    {"(?:TURN TOWARD|TURN TOWARDS):[ ](\\d+),[ ](\\d+)", "TURN_TOWARD_POINT", 2},
    {"(?:TURN TOWARD|TURN TOWARDS):[ ]EVENT[ ](\\d+)", "TURN_TOWARD_EVENT", 1},
    {"(?:TURN TOWARD|TURN TOWARDS):[ ]PLAYER", "TURN_TOWARD_PLAYER", 0},
    {"(?:MOVE LOWER LEFT|LOWER LEFT):[ ](\\d+)", "MOVE_1", 1},
    {"(?:MOVE LOWER RIGHT|LOWER RIGHT):[ ](\\d+)", "MOVE_3", 1},
    {"(?:MOVE UPPER LEFT|UPPER LEFT):[ ](\\d+)", "MOVE_7", 1},
    {"(?:MOVE UPPER RIGHT|UPPER RIGHT):[ ](\\d+)", "MOVE_9", 1},
    {"(?:MOVE UP|UP):[ ](\\d+)", "MOVE_8", 1},
    {"(?:MOVE DOWN|DOWN):[ ](\\d+)", "MOVE_2", 1},
    {"(?:MOVE LEFT|LEFT):[ ](\\d+)", "MOVE_4", 1},
    {"(?:MOVE RIGHT|RIGHT):[ ](\\d+)", "MOVE_6", 1},
    {NULL, NULL, 0}};

static Rx* g_route_rx[64];
static Rx* g_collide_rx;

static void init_route_rx(void) {
    if (g_route_rx[0]) return;
    for (int i = 0; ROUTE_RULES[i].pattern; i++) g_route_rx[i] = rx_compile(ROUTE_RULES[i].pattern, "i");
    g_collide_rx = rx_compile("(?:CRASH|COLLIDE|COLLISION|ENCOUNTER|TOUCH)", "i");
}

static int balloon_id(const char* str) {
    static const struct {
        const char* p;
        int id;
    } B[] = {{"(?:EXCLAMATION|\\!)", 1}, {"(?:QUESTION|\\?)", 2}, {"(?:MUSIC NOTE|MUSIC|NOTE)", 3}, {"(?:HEART|LOVE)", 4},
             {"(?:ANGER)", 5},           {"(?:SWEAT)", 6},        {"(?:COBWEB)", 7},                {"(?:SILENCE|\\.\\.\\.)", 8},
             {"(?:LIGHT BULB|LIGHT|BULB)", 9}, {"(?:ZZZ|ZZ|Z)", 10}, {NULL, 0}};
    for (int i = 0; B[i].p; i++) {
        if (rx_test(B[i].p, "i", str)) return B[i].id;
    }
    Rx* rx = rx_compile("(?:USER|USER-DEFINED|USER DEFINED)[ ](\\d+)", "i");
    RxMatch m;
    int id = 0;
    if (rx_match_str(rx, str, &m)) {
        char* g = rx_group(&m, str, 1);
        id = 10 + atoi(g);
        free(g);
    }
    rx_free(rx);
    return id;
}

static Node* switch_key(const char* str) {
    char* up = xstrdup(str);
    for (char* c = up; *c; c++) {
        if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 32);
    }
    Node* r = node_str(up);
    free(up);
    return r;
}

static Node* route_cmd(const char* kind) {
    Node* cmd = node_arr();
    node_push(cmd, node_num(0));
    node_push(cmd, node_str(kind));
    return cmd;
}

static int rx_num(const char* pattern, const char* line, int group, int* out) {
    Rx* rx = rx_compile(pattern, "i");
    RxMatch m;
    int ok = rx && rx_match_str(rx, line, &m);
    if (ok) {
        char* g = rx_group(&m, line, group);
        *out = atoi(g);
        free(g);
    }
    rx_free(rx);
    return ok;
}

static Node* ext_move_pack(const char* line) {
    int v;
    if (!strcmp(line, "EXTENDED MOVE PACK 1")) return route_cmd("NOOP");
    int collide = rx_test("(?:CRASH|COLLIDE|COLLISION|ENCOUNTER|TOUCH)", "i", line);
    if (rx_test("(?:HUG LEFT|HUG LEFT WALL)", "i", line)) {
        Node* c = route_cmd("HUG_LEFT");
        node_push(c, node_bool(collide));
        return c;
    }
    if (rx_test("(?:HUG RIGHT|HUG RIGHT WALL)", "i", line)) {
        Node* c = route_cmd("HUG_RIGHT");
        node_push(c, node_bool(collide));
        return c;
    }
    if (rx_num("INDEX:[ ](\\d+)", line, 1, &v)) {
        Node* c = route_cmd("INDEX_SET");
        node_push(c, node_num(v));
        return c;
    }
    if (rx_num("INDEX:[ ]([\\+\\-]\\d+)", line, 1, &v)) {
        Node* c = route_cmd("INDEX_ADJUST");
        node_push(c, node_num(v));
        return c;
    }
    Rx* until = rx_compile("MOVE[ ](.*)[ ](?:UNTIL WALL|UNTIL STOP)", "i");
    RxMatch m;
    if (until && rx_match_str(until, line, &m)) {
        char* text = rx_group(&m, line, 1);
        int dir = -2;
        if (rx_test("UPPER RIGHT", "i", text)) dir = 9;
        else if (rx_test("UPPER LEFT", "i", text)) dir = 7;
        else if (rx_test("LOWER RIGHT", "i", text)) dir = 4;
        else if (rx_test("LOWER LEFT", "i", text)) dir = 1;
        else if (rx_test("UP", "i", text)) dir = 8;
        else if (rx_test("RIGHT", "i", text)) dir = 6;
        else if (rx_test("LEFT", "i", text)) dir = 4;
        else if (rx_test("DOWN", "i", text)) dir = 2;
        else if (rx_test("FORWARD", "i", text)) dir = -1;
        free(text);
        rx_free(until);
        if (dir == -2) return route_cmd("NOOP");
        Node* c = route_cmd("MOVE_UNTIL_STOP");
        node_push(c, node_num(dir));
        node_push(c, node_bool(collide));
        return c;
    }
    rx_free(until);
    if (rx_num("OPACITY:[ ](\\d+)([%\xef\xbc\x85])", line, 1, &v)) {
        Node* c = route_cmd("OPACITY_SET");
        node_push(c, node_num(floor(255 * (v * 0.01) + 0.5)));
        return c;
    }
    if (rx_num("OPACITY:[ ]([\\+\\-]\\d+)([%\xef\xbc\x85])", line, 1, &v)) {
        Node* c = route_cmd("OPACITY_ADJUST");
        node_push(c, node_num(floor(255 * (v * 0.01) + 0.5)));
        return c;
    }
    if (rx_num("OPACITY:[ ]([\\+\\-]\\d+)", line, 1, &v)) {
        Node* c = route_cmd("OPACITY_ADJUST");
        node_push(c, node_num(v));
        return c;
    }
    return NULL;
}

Node* script_compile_route(const char* line, const char* where, int idx) {
    init_route_rx();
    if (!line) line = "";
    Node* ext = ext_move_pack(line);
    if (ext) return ext;
    for (int i = 0; ROUTE_RULES[i].pattern; i++) {
        RxMatch m;
        if (!g_route_rx[i] || !rx_match_str(g_route_rx[i], line, &m)) continue;
        const RouteRule* rule = &ROUTE_RULES[i];
        if (!strcmp(rule->kind, "EVAL")) {
            char* code = rx_group(&m, line, 1);
            Node* ast = script_compile(code, where, idx, 45);
            free(code);
            return ast;
        }
        Node* cmd = node_arr();
        node_push(cmd, node_num(0));
        node_push(cmd, node_str(rule->kind));
        if (rule->argc > 0) {
            for (int g = 1; g <= rule->argc; g++) {
                char* v = rx_group(&m, line, g);
                node_push(cmd, node_num(atoi(v)));
                free(v);
            }
            if (!strncmp(rule->kind, "MOVE_TO_", 8)) node_push(cmd, node_bool(rx_match_str(g_collide_rx, line, &m)));
        } else if (!strcmp(rule->kind, "BALLOON")) {
            char* s = rx_group(&m, line, 1);
            node_push(cmd, node_num(balloon_id(s)));
            free(s);
        } else if (!strcmp(rule->kind, "ICON_BALLOON")) {
            char* s = rx_group(&m, line, 1);
            RxMatch m2;
            Rx* two = rx_compile("(\\d+)[ ]TO[ ](\\d+)", "i");
            Rx* one = rx_compile("(\\d+)", "i");
            if (rx_match_str(two, s, &m2)) {
                char* a = rx_group(&m2, s, 1);
                char* b = rx_group(&m2, s, 2);
                node_push(cmd, node_num(atoi(a)));
                node_push(cmd, node_num(atoi(b)));
                free(a);
                free(b);
            } else if (rx_match_str(one, s, &m2)) {
                char* a = rx_group(&m2, s, 1);
                node_push(cmd, node_num(atoi(a)));
                node_push(cmd, node_num(atoi(a)));
                free(a);
            }
            rx_free(two);
            rx_free(one);
            free(s);
        } else if (!strncmp(rule->kind, "SELF_SWITCH", 11)) {
            char* s = rx_group(&m, line, 1);
            node_push(cmd, switch_key(s));
            free(s);
        } else if (!strcmp(rule->kind, "SELF_VARIABLE")) {
            node_free(cmd);
            return route_cmd("NOOP");
        }
        return cmd;
    }
    return script_compile(line, where, idx, 45);
}

static int is_code(const Node* cmd, int code) { return node_numv(node_get(cmd, "code"), -1) == code; }

static Node* params_of(Node* cmd) {
    Node* p = node_get(cmd, "parameters");
    return p && p->type == N_ARR ? p : NULL;
}

void script_compile_route_list(Node* route_list, const char* where, int idx) {
    if (!route_list || route_list->type != N_ARR) return;
    for (size_t s = 0; s < route_list->count; s++) {
        Node* step = route_list->items[s];
        if (!is_code(step, 45)) continue;
        Node* sp = params_of(step);
        const char* src = sp && sp->count ? node_strv(sp->items[0]) : "";
        Node* compiled = script_compile_route(src ? src : "", where, idx);
        Node* np = node_arr();
        node_push(np, node_str(src ? src : ""));
        node_push(np, compiled ? compiled : node_null());
        node_set(step, "parameters", np);
    }
}

static int starts_with_ci(const char* s, const char* prefix) {
    for (; *prefix; s++, prefix++) {
        char a = *s, b = *prefix;
        if (a >= 'a' && a <= 'z') a = (char)(a - 32);
        if (b >= 'a' && b <= 'z') b = (char)(b - 32);
        if (a != b) return 0;
    }
    return 1;
}

void script_compile_list(Node* lst, const char* where) {
    if (!lst || lst->type != N_ARR) return;
    for (size_t i = 0; i < lst->count; i++) {
        Node* cmd = lst->items[i];
        int code = (int)node_numv(node_get(cmd, "code"), -1);
        Node* p = params_of(cmd);
        if (code == 355) {
            const char* first = p && p->count ? node_strv(p->items[0]) : NULL;
            if (!first) continue;
            Buf body = {0};
            buf_put(&body, first, strlen(first));
            for (size_t j = i + 1; j < lst->count && is_code(lst->items[j], 655); j++) {
                Node* pj = params_of(lst->items[j]);
                const char* s = pj && pj->count ? node_strv(pj->items[0]) : "";
                buf_u8(&body, '\n');
                if (s) buf_put(&body, s, strlen(s));
            }
            buf_u8(&body, 0);
            Node* compiled = script_compile((const char*)body.data, where, (int)i, code);
            buf_free(&body);
            Node* np = node_arr();
            node_push(np, node_clone(p->items[0]));
            node_push(np, compiled ? compiled : node_null());
            node_set(cmd, "parameters", np);
        } else if (code == 111 && p && p->count > 1 && node_numv(p->items[0], -1) == 12) {
            const char* src = node_strv(p->items[1]);
            Node* compiled = script_compile(src, where, (int)i, code);
            Node* np = node_arr();
            node_push(np, node_num(12));
            node_push(np, node_clone(p->items[1]));
            node_push(np, compiled ? compiled : node_null());
            node_set(cmd, "parameters", np);
        } else if (code == 122 && p && p->count > 4 && node_numv(p->items[3], -1) == 4) {
            const char* src = node_strv(p->items[4]);
            Node* compiled = script_compile(src, where, (int)i, code);
            Node* np = node_arr();
            for (int k = 0; k < 5; k++) node_push(np, node_clone(p->items[k]));
            node_push(np, compiled ? compiled : node_null());
            node_set(cmd, "parameters", np);
        } else if (code == 205 && p && p->count > 1) {
            script_compile_route_list(node_get(p->items[1], "list"), where, (int)i);
        } else if (code == 356 && p && p->count) {
            const char* line = node_strv(p->items[0]);
            if (line && starts_with_ci(line, "Wait For Condition ")) {
                Node* compiled = script_compile(line + 19, where, (int)i, code);
                Node* np = node_arr();
                node_push(np, node_str(line));
                node_push(np, compiled ? compiled : node_null());
                node_set(cmd, "parameters", np);
            }
        }
    }
}
