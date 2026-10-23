#ifndef HAJIMU_TOOLING_H
#define HAJIMU_TOOLING_H
#include "value.h"
/* ソースを実行せず、ランタイムと同じパーサーで解析する。返値は呼出元が解放する。 */
Value hajimu_analyze_source(const char *source, const char *filename);
int hajimu_analyze_stdin(const char *filename);
#endif
