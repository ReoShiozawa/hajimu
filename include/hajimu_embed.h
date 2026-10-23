#ifndef HAJIMU_EMBED_H
#define HAJIMU_EMBED_H
#ifdef __cplusplus
extern "C" {
#endif
#define HAJIMU_EMBED_ABI_VERSION 1
/* UTF-8ソースを実行せず解析する。返値はJSON。NULLは不正引数・割当失敗・使用中。 */
char *hajimu_embed_analyze(const char *source, const char *filename);
/* 解析・実行は同時呼び出しを拒否してNULLを返す。標準出力はホストのstdoutへ流れる。 */
char *hajimu_embed_run(const char *source, const char *filename);
/* 解析結果の返値を、確保したランタイムと同じアロケーターで解放する。 */
void hajimu_embed_free(char *result);
unsigned int hajimu_embed_abi_version(void);
#ifdef __cplusplus
}
#endif
#endif
