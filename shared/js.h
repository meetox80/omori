#ifndef TB_JS_H
#define TB_JS_H

#include <stddef.h>

#include "node.h"

#ifdef __cplusplus
extern "C" {
#endif

Node* js_compile(const char* src, size_t len, char* err, size_t errlen);
Node* js_compile_str(const char* src, char* err, size_t errlen);
Node* js_compile_ranges(const char* src, size_t len, char* err, size_t errlen, Node* ranges);

#ifdef __cplusplus
}
#endif

#endif
