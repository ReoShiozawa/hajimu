#include "hajimu_embed.h"
#include <assert.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>

static atomic_int start = 0;
static atomic_int completed = 0;

static void *exercise(void *argument) {
    (void)argument;
    while (!atomic_load(&start)) {}
    for (int i = 0; i < 100; i++) {
        char *result = i % 2
            ? hajimu_embed_analyze("var x = 2\n", "thread.haj")
            : hajimu_embed_run("2 + 3\n", "thread.haj");
        // 他の呼び出しが使用中ならNULL。完了した呼び出しは正しいJSONを返す。
        if (result) {
            assert(strstr(result, i % 2 ? "\"valid\":true" : "\"ok\":true"));
            hajimu_embed_free(result);
            atomic_fetch_add(&completed, 1);
        }
    }
    return NULL;
}

int main(void) {
    assert(hajimu_embed_abi_version()==1);
    char *result=hajimu_embed_analyze("var x =\n","test.haj");
    assert(result && strstr(result,"\"valid\":false"));hajimu_embed_free(result);
    result=hajimu_embed_run("var x = 2\nx + 3\n","test.haj");
    assert(result && strstr(result,"\"ok\":true") && strstr(result,"\"value\":\"5\""));hajimu_embed_free(result);
    result=hajimu_embed_run("var x = 1 / 0\n","test.haj");
    assert(result && strstr(result,"\"ok\":false"));hajimu_embed_free(result);
    pthread_t threads[4];
    for (int i = 0; i < 4; i++) assert(!pthread_create(&threads[i], NULL, exercise, NULL));
    atomic_store(&start, 1);
    for (int i = 0; i < 4; i++) assert(!pthread_join(threads[i], NULL));
    assert(atomic_load(&completed) > 0);
    return 0;
}
