#ifndef TB_SCRIPT_H
#define TB_SCRIPT_H

#include "node.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ProblemFn)(const char* msg);

void script_set_problem_handler(ProblemFn fn);
Node* script_compile(const char* src, const char* where, int idx, int code);
Node* script_compile_route(const char* line, const char* where, int idx);
void script_compile_route_list(Node* route_list, const char* where, int idx);
void script_compile_list(Node* lst, const char* where);

#ifdef __cplusplus
}
#endif

#endif
