#ifndef TB_RX_H
#define TB_RX_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RX_MAX_GROUPS 16

typedef struct Rx Rx;

typedef struct {
    size_t start[RX_MAX_GROUPS + 1];
    size_t end[RX_MAX_GROUPS + 1];
} RxMatch;

Rx* rx_compile(const char* pattern, const char* flags);
void rx_free(Rx* rx);
int rx_groups(const Rx* rx);
int rx_search(const Rx* rx, const char* s, size_t n, size_t from, RxMatch* m);
int rx_match_str(const Rx* rx, const char* s, RxMatch* m);
char* rx_group(const RxMatch* m, const char* s, int g);
int rx_test(const char* pattern, const char* flags, const char* s);

#ifdef __cplusplus
}
#endif

#endif
