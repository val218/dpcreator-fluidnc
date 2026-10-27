// Host build: the parts of mbedtls SHA-256 the firmware uses, on FluidNC's own SHA256.
#pragma once
#include "../../src/SHA256.h"
typedef struct {
    SHA256_CTX c;
} mbedtls_sha256_context;
inline void mbedtls_sha256_init(mbedtls_sha256_context*) {}
inline void mbedtls_sha256_free(mbedtls_sha256_context*) {}
inline int  mbedtls_sha256_starts(mbedtls_sha256_context* ctx, int is224) {
    sha256_init(&ctx->c);
    return is224 ? -1 : 0;
}
inline int mbedtls_sha256_update(mbedtls_sha256_context* ctx, const unsigned char* in, size_t n) {
    sha256_update(&ctx->c, in, n);
    return 0;
}
inline int mbedtls_sha256_finish(mbedtls_sha256_context* ctx, unsigned char out[32]) {
    sha256_final(&ctx->c, out);
    return 0;
}
