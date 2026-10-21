#ifndef TB_CRYPTO_H
#define TB_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t round_keys[240];
} Aes256;

typedef struct {
    uint32_t h[5];
    uint64_t len;
    uint8_t buf[64];
    size_t fill;
} Sha1;

void sha1_init(Sha1* s);
void sha1_update(Sha1* s, const void* data, size_t len);
void sha1_final(Sha1* s, uint8_t out[20]);
void sha1(const void* data, size_t len, uint8_t out[20]);
void sha1_block64(const uint8_t msg[64], uint8_t out[20]);
void sha256(const void* data, size_t len, uint8_t out[32]);
void aes256_init(Aes256* a, const uint8_t key[32]);
void aes256_ctr(const Aes256* a, const uint8_t iv[16], uint8_t* data, size_t len);
int aes_file_decrypt(const Aes256* a, uint8_t* buf, size_t* len);
int mv_decrypt(uint8_t* buf, size_t* len, const uint8_t key[16]);

#endif
