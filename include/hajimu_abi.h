#ifndef HAJIMU_ABI_H
#define HAJIMU_ABI_H
#include <stdint.h>
#include <stddef.h>
#define HAJIMU_ABI_VERSION 1u
#define HAJIMU_ABI_FEATURE_NUMERIC_BUFFERS UINT64_C(1)
typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t value_size;
    uint32_t plugin_info_size;
    uint32_t runtime_size;
    uint32_t string_length_offset;
    uint64_t required_features;
} HajimuAbiInfo;
typedef const HajimuAbiInfo *(*HajimuPluginAbiFn)(void);

/* エントリポイントを定義する翻訳単位で一度だけ展開する。 */
#define HAJIMU_PLUGIN_ABI_EXPORT() \
    HAJIMU_PLUGIN_EXPORT const HajimuAbiInfo *hajimu_plugin_abi(void) { \
        static const HajimuAbiInfo abi = { HAJIMU_ABI_VERSION, sizeof(HajimuAbiInfo), \
            sizeof(Value), sizeof(HajimuPluginInfo), sizeof(HajimuRuntime), \
            offsetof(Value, string.byte_length), 0 }; \
        return &abi; \
    }
#endif
