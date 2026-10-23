#include "hajimu_embed.h"
#include <assert.h>
#include <string.h>
int main(void) {
    assert(hajimu_embed_abi_version()==1);
    char *result=hajimu_embed_analyze("var x =\n","test.haj");
    assert(result && strstr(result,"\"valid\":false"));hajimu_embed_free(result);
    result=hajimu_embed_run("var x = 2\nx + 3\n","test.haj");
    assert(result && strstr(result,"\"ok\":true") && strstr(result,"\"value\":\"5\""));hajimu_embed_free(result);
    result=hajimu_embed_run("var x = 1 / 0\n","test.haj");
    assert(result && strstr(result,"\"ok\":false"));hajimu_embed_free(result);
    return 0;
}
