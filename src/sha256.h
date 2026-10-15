#ifndef TG_SHA256_H
#define TG_SHA256_H

#include <stddef.h>
#include <stdint.h>

// Computes the SHA-256 digest of data into out (32 bytes).
void tg_sha256(const uint8_t* data, size_t len, uint8_t out[32]);

#endif
