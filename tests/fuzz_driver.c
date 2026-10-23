#include <stdint.h>
#include <stdio.h>
#include <string.h>
int LLVMFuzzerTestOneInput(const uint8_t *data,size_t size);
int main(void) {
    const char *seeds[]={"var x = 1\nprint(x)\n", "function f(a):\n    return a + 1\nend\n", "変数 値 = [1,2,3]\n表示(値[0])\n"};
    uint32_t state=42;
    for(int i=0;i<1000;i++) {
        char text[128]; const char *seed=seeds[i%3]; size_t size=strlen(seed);
        memcpy(text,seed,size);
        state=state*1664525u+1013904223u;
        text[state%size]=(char)(32+(state>>16)%95);
        LLVMFuzzerTestOneInput((const uint8_t *)text,size);
    }
    for (int i=0;i<10000;i++) {
        uint8_t bytes[32];
        size_t size=(size_t)i%sizeof(bytes);
        for (size_t j=0;j<size;j++) {
            state=state*1664525u+1013904223u;
            bytes[j]=(uint8_t)(state>>16);
        }
        LLVMFuzzerTestOneInput(bytes,size);
    }
    puts("11000 sanitizer parser mutations passed");
    return 0;
}
