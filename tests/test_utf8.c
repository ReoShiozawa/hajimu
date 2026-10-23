#include "lexer.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check(const unsigned char *bytes, size_t size, uint32_t expected, int consumed) {
    // 入力と終端だけを確保し、サニタイザーで境界外読み取りを検知する。
    char *text = malloc(size + 1);
    assert(text);
    memcpy(text, bytes, size);
    text[size] = '\0';
    int length = 0;
    assert(utf8_decode(text, &length) == expected);
    assert(length == consumed);
    free(text);
}

int main(void) {
    for (unsigned int first = 0x80; first <= 0xFF; first++) {
        unsigned char bytes[] = {(unsigned char)first, 0x80, 0x80};
        check(bytes, 1, 0xFFFD, 1);
        if (first >= 0xE0) check(bytes, 2, 0xFFFD, 1);
        if (first >= 0xF0) check(bytes, 3, 0xFFFD, 1);
    }
    check((const unsigned char *)"", 0, 0, 1);
    check((const unsigned char *)"a", 1, 'a', 1);
    check((const unsigned char *)"\xCE\xF5", 2, 0xFFFD, 1);
    check((const unsigned char *)"\xE0\x80\x80", 3, 0xFFFD, 1);
    check((const unsigned char *)"\xED\xA0\x80", 3, 0xFFFD, 1);
    check((const unsigned char *)"\xF0\x80\x80\x80", 4, 0xFFFD, 1);
    check((const unsigned char *)"\xF4\x90\x80\x80", 4, 0xFFFD, 1);
    check((const unsigned char *)"\xC2\xA2", 2, 0xA2, 2);
    check((const unsigned char *)"\xE3\x81\x82", 3, 0x3042, 3);
    check((const unsigned char *)"\xF0\x9F\x9A\x80", 4, 0x1F680, 4);
    check((const unsigned char *)"\xF4\x8F\xBF\xBF", 4, 0x10FFFF, 4);
    puts("UTF-8 boundaries and invalid encodings passed");
    return 0;
}
