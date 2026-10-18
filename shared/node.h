#ifndef TB_NODE_H
#define TB_NODE_H

#include <stddef.h>
#include <stdint.h>

#include "buf.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { N_NULL, N_FALSE, N_TRUE, N_NUM, N_STR, N_ARR, N_OBJ, N_BYTES } NodeType;

typedef struct Node Node;
struct Node {
    NodeType type;
    double num;
    char* str;
    size_t len;
    Node** items;
    char** keys;
    size_t count;
    size_t cap;
};

Node* node_null(void);
Node* node_bool(int v);
Node* node_num(double v);
Node* node_str(const char* s);
Node* node_strn(const char* s, size_t n);
Node* node_bytes(const uint8_t* p, size_t n);
Node* node_arr(void);
Node* node_obj(void);
void node_push(Node* arr, Node* v);
void node_set(Node* obj, const char* key, Node* v);
Node* node_get(const Node* obj, const char* key);
Node* node_at(const Node* arr, size_t i);
Node* node_take(Node* obj, const char* key);
int node_remove(Node* obj, const char* key);
void node_free(Node* n);
Node* node_clone(const Node* n);
double node_numv(const Node* n, double def);
const char* node_strv(const Node* n);

Node* json_parse(const char* text, size_t len, char* err, size_t errlen);

void vbin_dump(const Node* root, Buf* out);

#ifdef __cplusplus
}
#endif

#endif
