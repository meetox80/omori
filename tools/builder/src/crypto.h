#ifndef TB_CRYPTO_H
#define TB_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t round_keys[240];
} Aes256;

void sha256(const void* data, size_t len, uint8_t out[32]);
void aes256_init(Aes256* a, const uint8_t key[32]);
void aes256_ctr(const Aes256* a, const uint8_t iv[16], uint8_t* data, size_t len);
int aes_file_decrypt(const Aes256* a, uint8_t* buf, size_t* len);
int mv_decrypt(uint8_t* buf, size_t* len, const uint8_t key[16]);

#endif
