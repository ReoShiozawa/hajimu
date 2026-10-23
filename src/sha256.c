#include "sha256.h"

#include <stdio.h>
#include <string.h>

#define ROTR(value, bits) (((value) >> (bits)) | ((value) << (32U - (bits))))
#define CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTR((x), 2) ^ ROTR((x), 13) ^ ROTR((x), 22))
#define EP1(x) (ROTR((x), 6) ^ ROTR((x), 11) ^ ROTR((x), 25))
#define SIG0(x) (ROTR((x), 7) ^ ROTR((x), 18) ^ ((x) >> 3))
#define SIG1(x) (ROTR((x), 17) ^ ROTR((x), 19) ^ ((x) >> 10))

static const uint32_t constants[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
};

static void transform(HajimuSha256 *ctx, const uint8_t block[64]) {
    uint32_t words[64];
    for (size_t i = 0; i < 16; i++) {
        size_t offset = i * 4;
        words[i] = ((uint32_t)block[offset] << 24) |
                   ((uint32_t)block[offset + 1] << 16) |
                   ((uint32_t)block[offset + 2] << 8) |
                   (uint32_t)block[offset + 3];
    }
    for (size_t i = 16; i < 64; i++) {
        words[i] = SIG1(words[i - 2]) + words[i - 7] +
                   SIG0(words[i - 15]) + words[i - 16];
    }

    uint32_t a = ctx->state[0];
    uint32_t b = ctx->state[1];
    uint32_t c = ctx->state[2];
    uint32_t d = ctx->state[3];
    uint32_t e = ctx->state[4];
    uint32_t f = ctx->state[5];
    uint32_t g = ctx->state[6];
    uint32_t h = ctx->state[7];

    for (size_t i = 0; i < 64; i++) {
        uint32_t first = h + EP1(e) + CH(e, f, g) + constants[i] + words[i];
        uint32_t second = EP0(a) + MAJ(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + first;
        d = c;
        c = b;
        b = a;
        a = first + second;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

void hajimu_sha256_init(HajimuSha256 *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->state[0] = 0x6a09e667U;
    ctx->state[1] = 0xbb67ae85U;
    ctx->state[2] = 0x3c6ef372U;
    ctx->state[3] = 0xa54ff53aU;
    ctx->state[4] = 0x510e527fU;
    ctx->state[5] = 0x9b05688cU;
    ctx->state[6] = 0x1f83d9abU;
    ctx->state[7] = 0x5be0cd19U;
}

void hajimu_sha256_update(HajimuSha256 *ctx, const void *input, size_t length) {
    const uint8_t *data = input;
    for (size_t i = 0; i < length; i++) {
        ctx->data[ctx->data_length++] = data[i];
        if (ctx->data_length == sizeof(ctx->data)) {
            transform(ctx, ctx->data);
            ctx->bit_length += 512;
            ctx->data_length = 0;
        }
    }
}

void hajimu_sha256_final(HajimuSha256 *ctx, uint8_t digest[32]) {
    size_t i = ctx->data_length;
    ctx->data[i++] = 0x80;

    if (i > 56) {
        while (i < 64) ctx->data[i++] = 0;
        transform(ctx, ctx->data);
        i = 0;
    }
    while (i < 56) ctx->data[i++] = 0;

    ctx->bit_length += (uint64_t)ctx->data_length * 8U;
    for (size_t j = 0; j < 8; j++) {
        ctx->data[63 - j] = (uint8_t)(ctx->bit_length >> (j * 8));
    }
    transform(ctx, ctx->data);

    for (i = 0; i < 8; i++) {
        digest[i * 4] = (uint8_t)(ctx->state[i] >> 24);
        digest[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 16);
        digest[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 8);
        digest[i * 4 + 3] = (uint8_t)ctx->state[i];
    }
}

bool hajimu_sha256_file(const char *path, char hex_digest[65]) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) return false;

    HajimuSha256 ctx;
    hajimu_sha256_init(&ctx);
    uint8_t buffer[8192];
    size_t read_count;
    while ((read_count = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        hajimu_sha256_update(&ctx, buffer, read_count);
    }
    bool ok = !ferror(file);
    fclose(file);
    if (!ok) return false;

    uint8_t digest[32];
    hajimu_sha256_final(&ctx, digest);
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < sizeof(digest); i++) {
        hex_digest[i * 2] = digits[digest[i] >> 4];
        hex_digest[i * 2 + 1] = digits[digest[i] & 0x0f];
    }
    hex_digest[64] = '\0';
    return true;
}
