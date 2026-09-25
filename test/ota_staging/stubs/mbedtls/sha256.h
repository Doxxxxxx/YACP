#pragma once
#include <cstddef>
#include <cstdint>
// Deliberate crypto double: these tests exercise OTA ordering/error handling,
// not the SHA algorithm. The digest is supplied by the test fixture.
struct mbedtls_sha256_context {};
void mbedtls_sha256_init(mbedtls_sha256_context*);
void mbedtls_sha256_free(mbedtls_sha256_context*);
int mbedtls_sha256_starts(mbedtls_sha256_context*, int);
int mbedtls_sha256_update(mbedtls_sha256_context*, const uint8_t*, size_t);
int mbedtls_sha256_finish(mbedtls_sha256_context*, uint8_t*);
