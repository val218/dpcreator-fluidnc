// Host build: mbedtls_md_hmac for SHA-256 only (RFC 2104), on FluidNC's SHA256.
#pragma once
#include "sha256.h"
#include <cstring>
typedef enum { MBEDTLS_MD_NONE = 0, MBEDTLS_MD_SHA256 = 6 } mbedtls_md_type_t;
typedef struct {
    mbedtls_md_type_t type;
} mbedtls_md_info_t;
inline const mbedtls_md_info_t* mbedtls_md_info_from_type(mbedtls_md_type_t t) {
    static const mbedtls_md_info_t sha256 = { MBEDTLS_MD_SHA256 };
    return t == MBEDTLS_MD_SHA256 ? &sha256 : nullptr;
}
inline int mbedtls_md_hmac(const mbedtls_md_info_t* info, const unsigned char* key, size_t keylen, const unsigned char* in, size_t ilen,
                           unsigned char* out) {
    if (!info || info->type != MBEDTLS_MD_SHA256) return -1;
    unsigned char k[64] = {}, pad[64], inner[32];
    SHA256_CTX    c;
    if (keylen > 64) {
        sha256_init(&c);
        sha256_update(&c, key, keylen);
        sha256_final(&c, k);
    } else {
        memcpy(k, key, keylen);
    }
    for (int i = 0; i < 64; ++i) pad[i] = k[i] ^ 0x36;
    sha256_init(&c);
    sha256_update(&c, pad, 64);
    sha256_update(&c, in, ilen);
    sha256_final(&c, inner);
    for (int i = 0; i < 64; ++i) pad[i] = k[i] ^ 0x5c;
    sha256_init(&c);
    sha256_update(&c, pad, 64);
    sha256_update(&c, inner, 32);
    sha256_final(&c, out);
    return 0;
}
