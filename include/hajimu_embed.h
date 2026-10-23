#ifndef HAJIMU_EMBED_H
#define HAJIMU_EMBED_H
#ifdef __cplusplus
extern "C" {
#endif
#define HAJIMU_EMBED_ABI_VERSION 1
/* UTF-8ソースを実行せず解析する。返値はJSON。NULLは割当失敗。 */
char *hajimu_embed_analyze(const char *source, const char *filename);
/* 同一プロセス内で直列実行する。プログラムの標準出力はホストのstdoutへ流れる。 */
char *hajimu_embed_run(const char *source, const char *filename);
/* 解析結果の返値を、確保したランタイムと同じアロケーターで解放する。 */
void hajimu_embed_free(char *result);
unsigned int hajimu_embed_abi_version(void);
#ifdef __cplusplus
}
#endif
#endif
