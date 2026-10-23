#include "sha256.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    HajimuSha256 ctx;
    unsigned char digest[32];
    char actual[65];
    static const char digits[] = "0123456789abcdef";
    const char *expected = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";

    hajimu_sha256_init(&ctx);
    hajimu_sha256_update(&ctx, "abc", 3);
    hajimu_sha256_final(&ctx, digest);
    for (size_t i = 0; i < sizeof(digest); i++) {
        actual[i * 2] = digits[digest[i] >> 4];
        actual[i * 2 + 1] = digits[digest[i] & 0x0f];
    }
    actual[64] = '\0';

    if (strcmp(actual, expected) != 0) {
        fprintf(stderr, "SHA-256 mismatch: %s\n", actual);
        return 1;
    }
    puts("SHA-256 checks passed");
    return 0;
}
