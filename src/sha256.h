#ifndef HAJIMU_SHA256_H
#define HAJIMU_SHA256_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t data[64];
    uint32_t state[8];
    uint64_t bit_length;
    size_t data_length;
} HajimuSha256;

void hajimu_sha256_init(HajimuSha256 *ctx);
void hajimu_sha256_update(HajimuSha256 *ctx, const void *data, size_t length);
void hajimu_sha256_final(HajimuSha256 *ctx, uint8_t digest[32]);
bool hajimu_sha256_file(const char *path, char hex_digest[65]);

#endif
