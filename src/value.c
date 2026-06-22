/**
 * 日本語プログラミング言語 - 値システム実装
 */

#include "value.h"
#include "array_grow.h"
#include "environment.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <limits.h>

#define VALUE_INITIAL_CAPACITY 8

// =============================================================================
// 値の作成
// =============================================================================

static int utf8_count_chars(const char *s, int byte_length) {
    if (s == NULL || byte_length <= 0) return 0;

    int count = 0;
    const char *p = s;
    const char *end = s + byte_length;
    while (p < end) {
        unsigned char c = (unsigned char)*p;
        if (c < 0x80) {
            p++;
        } else if (c < 0xE0) {
            p += 2;
        } else if (c < 0xF0) {
            p += 3;
        } else {
            p += 4;
        }
        count++;
    }

    return count;
}

static void generator_state_release(GeneratorState **state_ref) {
    if (state_ref == NULL || *state_ref == NULL) return;

    GeneratorState *state = *state_ref;
    state->ref_count--;
    if (state->ref_count <= 0) {
        for (int i = 0; i < state->length; i++) {
            value_free(&state->values[i]);
        }
        free(state->values);
        free(state);
    }
    *state_ref = NULL;
}

const char *numeric_dtype_name(NumericDType dtype) {
    switch (dtype) {
        case NUMERIC_DTYPE_F64: return "f64";
        case NUMERIC_DTYPE_F32: return "f32";
        case NUMERIC_DTYPE_I64: return "i64";
        case NUMERIC_DTYPE_I32: return "i32";
        case NUMERIC_DTYPE_BOOL: return "bool";
        default: return "f64";
    }
}

bool numeric_dtype_from_name(const char *name, NumericDType *out_dtype) {
    if (name == NULL || out_dtype == NULL) return false;
    if (strcmp(name, "f64") == 0 || strcmp(name, "double") == 0 || strcmp(name, "float64") == 0) {
        *out_dtype = NUMERIC_DTYPE_F64;
        return true;
    }
    if (strcmp(name, "f32") == 0 || strcmp(name, "float") == 0 || strcmp(name, "float32") == 0) {
        *out_dtype = NUMERIC_DTYPE_F32;
        return true;
    }
    if (strcmp(name, "i64") == 0 || strcmp(name, "int64") == 0 || strcmp(name, "整数64") == 0) {
        *out_dtype = NUMERIC_DTYPE_I64;
        return true;
    }
    if (strcmp(name, "i32") == 0 || strcmp(name, "int32") == 0 || strcmp(name, "整数32") == 0) {
        *out_dtype = NUMERIC_DTYPE_I32;
        return true;
    }
    if (strcmp(name, "bool") == 0 || strcmp(name, "boolean") == 0 || strcmp(name, "真偽値") == 0) {
        *out_dtype = NUMERIC_DTYPE_BOOL;
        return true;
    }
    return false;
}

int numeric_dtype_size(NumericDType dtype) {
    switch (dtype) {
        case NUMERIC_DTYPE_F64: return 8;
        case NUMERIC_DTYPE_F32: return 4;
        case NUMERIC_DTYPE_I64: return 8;
        case NUMERIC_DTYPE_I32: return 4;
        case NUMERIC_DTYPE_BOOL: return 1;
        default: return 8;
    }
}

static double numeric_cast_for_dtype(double value, NumericDType dtype) {
    switch (dtype) {
        case NUMERIC_DTYPE_F32:
            return (double)(float)value;
        case NUMERIC_DTYPE_I64:
            if (isnan(value)) return 0.0;
            return (double)(int64_t)value;
        case NUMERIC_DTYPE_I32:
            if (isnan(value)) return 0.0;
            if (value > (double)INT32_MAX) return (double)INT32_MAX;
            if (value < (double)INT32_MIN) return (double)INT32_MIN;
            return (double)(int32_t)value;
        case NUMERIC_DTYPE_BOOL:
            return value != 0.0 && !isnan(value) ? 1.0 : 0.0;
        case NUMERIC_DTYPE_F64:
        default:
            return value;
    }
}

static size_t numeric_buffer_bytes(int capacity, NumericDType dtype) {
    if (capacity <= 0) return 0;
    return (size_t)capacity * (size_t)numeric_dtype_size(dtype);
}

static double numeric_read_at(const void *data, NumericDType dtype, int index) {
    if (data == NULL || index < 0) return 0.0;
    switch (dtype) {
        case NUMERIC_DTYPE_F64:
            return ((const double *)data)[index];
        case NUMERIC_DTYPE_F32:
            return (double)((const float *)data)[index];
        case NUMERIC_DTYPE_I64:
            return (double)((const int64_t *)data)[index];
        case NUMERIC_DTYPE_I32:
            return (double)((const int32_t *)data)[index];
        case NUMERIC_DTYPE_BOOL:
            return ((const uint8_t *)data)[index] ? 1.0 : 0.0;
        default:
            return ((const double *)data)[index];
    }
}

static void numeric_write_at(void *data, NumericDType dtype, int index, double value) {
    if (data == NULL || index < 0) return;
    double casted = numeric_cast_for_dtype(value, dtype);
    switch (dtype) {
        case NUMERIC_DTYPE_F64:
            ((double *)data)[index] = casted;
            break;
        case NUMERIC_DTYPE_F32:
            ((float *)data)[index] = (float)casted;
            break;
        case NUMERIC_DTYPE_I64:
            ((int64_t *)data)[index] = (int64_t)casted;
            break;
        case NUMERIC_DTYPE_I32:
            ((int32_t *)data)[index] = (int32_t)casted;
            break;
        case NUMERIC_DTYPE_BOOL:
            ((uint8_t *)data)[index] = casted != 0.0 ? 1u : 0u;
            break;
        default:
            ((double *)data)[index] = casted;
            break;
    }
}

Value value_null(void) {
    Value v;
    v.type = VALUE_NULL;
    v.is_const = false;
    v.is_integer = false;
    v.ref_count = 0;
    return v;
}

Value value_number(double n) {
    Value v;
    v.type = VALUE_NUMBER;
    v.is_const = false;
    v.is_integer = isfinite(n) && floor(n) == n;
    v.ref_count = 0;
    v.number = n;
    return v;
}

Value value_bool(bool b) {
    Value v;
    v.type = VALUE_BOOL;
    v.is_const = false;
    v.is_integer = false;
    v.ref_count = 0;
    v.boolean = b;
    return v;
}

Value value_string(const char *s) {
    if (s == NULL) {
        return value_null();
    }
    return value_string_n(s, (int)strlen(s));
}

Value value_string_n(const char *s, int length) {
    Value v;
    v.type = VALUE_STRING;
    v.is_const = false;
    v.is_integer = false;
    v.ref_count = 1;
    
    v.string.byte_length = length;
    v.string.char_length = utf8_count_chars(s, length);
    v.string.capacity = length + 1;
    v.string.data = malloc(v.string.capacity);
    if (v.string.data == NULL) {
        return value_null();
    }

    if (s != NULL) {
        memcpy(v.string.data, s, length);
    }
    v.string.data[length] = '\0';

    return v;
}

Value value_array(void) {
    return value_array_with_capacity(VALUE_INITIAL_CAPACITY);
}

Value value_array_with_capacity(int capacity) {
    Value v;
    v.type = VALUE_ARRAY;
    v.is_const = false;
    v.is_integer = false;
    v.ref_count = 1;
    
    v.array.length = 0;
    v.array.capacity = capacity > 0 ? capacity : VALUE_INITIAL_CAPACITY;
    v.array.elements = malloc(sizeof(Value) * v.array.capacity);
    
    return v;
}

Value value_numeric_array(void) {
    return value_numeric_array_with_capacity(VALUE_INITIAL_CAPACITY);
}

Value value_numeric_array_with_capacity(int capacity) {
    return value_numeric_array_with_dtype(capacity, NUMERIC_DTYPE_F64);
}

Value value_numeric_array_with_dtype(int capacity, NumericDType dtype) {
    Value v;
    v.type = VALUE_NUMERIC_ARRAY;
    v.is_const = false;
    v.is_integer = false;
    v.ref_count = 1;

    v.numeric_array.dtype = dtype;
    v.numeric_array.length = 0;
    v.numeric_array.capacity = capacity > 0 ? capacity : VALUE_INITIAL_CAPACITY;
    v.numeric_array.data = malloc(numeric_buffer_bytes(v.numeric_array.capacity, dtype));

    return v;
}

Value value_numeric_array_from_data(const double *data, int length) {
    return value_numeric_array_from_data_with_dtype(data, length, NUMERIC_DTYPE_F64);
}

Value value_numeric_array_from_data_with_dtype(const double *data, int length, NumericDType dtype) {
    if (length < 0) return value_null();

    Value v = value_numeric_array_with_dtype(length > 0 ? length : VALUE_INITIAL_CAPACITY, dtype);
    if (v.numeric_array.data == NULL) return value_null();

    v.numeric_array.length = length;
    if (data != NULL && length > 0) {
        for (int i = 0; i < length; i++) {
            numeric_write_at(v.numeric_array.data, dtype, i, data[i]);
        }
    }

    return v;
}

Value value_matrix(int rows, int cols) {
    return value_matrix_with_dtype(rows, cols, NUMERIC_DTYPE_F64);
}

Value value_matrix_with_dtype(int rows, int cols, NumericDType dtype) {
    if (rows < 0 || cols < 0) return value_null();

    Value v;
    v.type = VALUE_MATRIX;
    v.is_const = false;
    v.is_integer = false;
    v.ref_count = 1;
    v.matrix.dtype = dtype;
    v.matrix.rows = rows;
    v.matrix.cols = cols;
    v.matrix.row_stride = cols;
    v.matrix.col_stride = 1;
    v.matrix.offset = 0;
    v.matrix.ref_count = malloc(sizeof(int));
    if (v.matrix.ref_count == NULL) return value_null();
    *v.matrix.ref_count = 1;

    size_t count = (size_t)rows * (size_t)cols;
    v.matrix.data = count > 0 ? calloc(count, (size_t)numeric_dtype_size(dtype)) : NULL;
    if (count > 0 && v.matrix.data == NULL) {
        free(v.matrix.ref_count);
        return value_null();
    }

    return v;
}

Value value_matrix_from_data(const double *data, int rows, int cols) {
    return value_matrix_from_data_with_dtype(data, rows, cols, NUMERIC_DTYPE_F64);
}

Value value_matrix_from_data_with_dtype(const double *data, int rows, int cols, NumericDType dtype) {
    Value v = value_matrix_with_dtype(rows, cols, dtype);
    if (v.type != VALUE_MATRIX) return v;

    size_t count = (size_t)rows * (size_t)cols;
    if (data != NULL && count > 0) {
        for (size_t i = 0; i < count; i++) {
            numeric_write_at(v.matrix.data, dtype, (int)i, data[i]);
        }
    }

    return v;
}

Value value_function(struct ASTNode *definition, struct Environment *closure) {
    Value v;
    v.type = VALUE_FUNCTION;
    v.is_const = false;
    v.is_integer = false;
    v.ref_count = 1;
    v.function.definition = definition;
    v.function.closure = closure;
    // クロージャ環境の参照カウントを増加
    if (closure != NULL) {
        env_retain(closure);
    }
    return v;
}

Value value_builtin(BuiltinFn fn, const char *name, int min_args, int max_args) {
    Value v;
    v.type = VALUE_BUILTIN;
    v.is_const = true;
    v.is_integer = false;
    v.ref_count = 0;  // 組み込みは解放しない
    v.builtin.fn = fn;
    v.builtin.name = name;
    v.builtin.min_args = min_args;
    v.builtin.max_args = max_args;
    return v;
}

Value value_class(const char *name, struct ASTNode *definition, Value *parent) {
    Value v;
    v.type = VALUE_CLASS;
    v.is_const = true;
    v.is_integer = false;
    v.ref_count = 1;
    v.class_value.name = strdup(name);
    if (v.class_value.name == NULL) {
        return value_null();
    }
    v.class_value.definition = definition;
    v.class_value.parent = parent;
    return v;
}

Value value_instance(Value *class_ref) {
    Value v;
    v.type = VALUE_INSTANCE;
    v.is_const = false;
    v.is_integer = false;
    v.ref_count = 1;
    v.instance.class_ref = class_ref;
    v.instance.field_names = NULL;
    v.instance.fields = NULL;
    v.instance.field_count = 0;
    v.instance.field_capacity = 0;
    return v;
}

Value value_generator(void) {
    Value v;
    v.type = VALUE_GENERATOR;
    v.is_const = false;
    v.is_integer = false;
    v.ref_count = 1;
    
    GeneratorState *state = calloc(1, sizeof(GeneratorState));
    state->values = NULL;
    state->length = 0;
    state->capacity = 0;
    state->index = 0;
    state->done = false;
    state->ref_count = 1;
    v.generator.state = state;
    
    return v;
}

void generator_add_value(Value *gen, Value val) {
    if (gen->type != VALUE_GENERATOR || gen->generator.state == NULL) return;
    GeneratorState *s = gen->generator.state;
    if (s->length >= s->capacity) {
        int new_cap = s->capacity == 0 ? VALUE_INITIAL_CAPACITY : s->capacity * 2;
        s->values = realloc(s->values, sizeof(Value) * new_cap);
        s->capacity = new_cap;
    }
    s->values[s->length++] = value_copy(val);
}

void instance_set_field(Value *instance, const char *name, Value value) {
    // 既存のフィールドを検索
    for (int i = 0; i < instance->instance.field_count; i++) {
        if (strcmp(instance->instance.field_names[i], name) == 0) {
            value_free(&instance->instance.fields[i]);
            instance->instance.fields[i] = value_copy(value);
            return;
        }
    }
    
    // 新しいフィールドを追加
    if (instance->instance.field_count >= instance->instance.field_capacity) {
        int new_cap = instance->instance.field_capacity == 0 ? 4 : instance->instance.field_capacity * 2;
        instance->instance.field_names = realloc(instance->instance.field_names, new_cap * sizeof(char *));
        instance->instance.fields = realloc(instance->instance.fields, new_cap * sizeof(Value));
        instance->instance.field_capacity = new_cap;
    }
    
    int idx = instance->instance.field_count;
    instance->instance.field_names[idx] = strdup(name);
    if (instance->instance.field_names[idx] == NULL) {
        return;
    }
    instance->instance.fields[idx] = value_copy(value);
    instance->instance.field_count++;
}

Value *instance_get_field(Value *instance, const char *name) {
    for (int i = 0; i < instance->instance.field_count; i++) {
        if (strcmp(instance->instance.field_names[i], name) == 0) {
            return &instance->instance.fields[i];
        }
    }
    return NULL;
}

// 辞書は keys/values の挿入順配列を正とし、検索だけをハッシュ索引で高速化します。
// hash_indices には values/keys の添字 + 1 を格納し、0 を空スロットとして扱います。
#define DICT_HASH_MIN_LENGTH 8

static uint64_t dict_hash_key(const char *key) {
    uint64_t hash = 1469598103934665603ULL; // FNV-1a 64-bit offset basis
    const unsigned char *p = (const unsigned char *)key;
    while (*p != '\0') {
        hash ^= (uint64_t)*p++;
        hash *= 1099511628211ULL;
    }
    return hash;
}

static int dict_hash_capacity_for(int length) {
    if (length < DICT_HASH_MIN_LENGTH) return 0;
    if (length > INT_MAX / 2) return 0;

    int capacity = 16;
    int target = length * 2;
    while (capacity < target) {
        if (capacity > INT_MAX / 2) return 0;
        capacity *= 2;
    }
    return capacity;
}

static void dict_discard_hash_index(Value *dict) {
    if (dict == NULL || dict->type != VALUE_DICT) return;
    free(dict->dict.hash_indices);
    dict->dict.hash_indices = NULL;
    dict->dict.hash_capacity = 0;
    dict->dict.hash_valid = false;
}

static void dict_hash_insert_slot(Value *dict, int value_index) {
    if (dict == NULL || dict->type != VALUE_DICT || !dict->dict.hash_valid ||
        dict->dict.hash_indices == NULL || dict->dict.hash_capacity <= 0 ||
        value_index < 0 || value_index >= dict->dict.length) {
        return;
    }

    uint64_t hash = dict_hash_key(dict->dict.keys[value_index]);
    int mask = dict->dict.hash_capacity - 1;
    int slot = (int)(hash & (uint64_t)mask);
    for (int probes = 0; probes < dict->dict.hash_capacity; probes++) {
        if (dict->dict.hash_indices[slot] == 0) {
            dict->dict.hash_indices[slot] = value_index + 1;
            return;
        }
        slot = (slot + 1) & mask;
    }

    dict->dict.hash_valid = false;
}

static bool dict_rebuild_hash_index(Value *dict) {
    if (dict == NULL || dict->type != VALUE_DICT) return false;

    int hash_capacity = dict_hash_capacity_for(dict->dict.length);
    if (hash_capacity <= 0) {
        dict_discard_hash_index(dict);
        return false;
    }

    int *indices = calloc((size_t)hash_capacity, sizeof(int));
    if (indices == NULL) {
        dict_discard_hash_index(dict);
        return false;
    }

    free(dict->dict.hash_indices);
    dict->dict.hash_indices = indices;
    dict->dict.hash_capacity = hash_capacity;
    dict->dict.hash_valid = true;

    for (int i = 0; i < dict->dict.length; i++) {
        dict_hash_insert_slot(dict, i);
        if (!dict->dict.hash_valid) {
            dict_discard_hash_index(dict);
            return false;
        }
    }
    return true;
}

Value value_dict(void) {
    return value_dict_with_capacity(VALUE_INITIAL_CAPACITY);
}

Value value_dict_with_capacity(int capacity) {
    Value v;
    v.type = VALUE_DICT;
    v.is_const = false;
    v.is_integer = false;
    v.ref_count = 1;
    
    v.dict.length = 0;
    v.dict.capacity = capacity > 0 ? capacity : VALUE_INITIAL_CAPACITY;
    v.dict.keys = malloc(sizeof(char *) * v.dict.capacity);
    v.dict.values = malloc(sizeof(Value) * v.dict.capacity);
    v.dict.hash_indices = NULL;
    v.dict.hash_capacity = 0;
    v.dict.hash_valid = false;
    if (v.dict.keys == NULL || v.dict.values == NULL) {
        free(v.dict.keys);
        free(v.dict.values);
        v.dict.keys = NULL;
        v.dict.values = NULL;
        v.dict.capacity = 0;
    }
    
    return v;
}

// =============================================================================
// 値の操作
// =============================================================================

Value value_copy(Value v) {
    Value copy = v;
    
    switch (v.type) {
        case VALUE_STRING:
            copy.string.data = malloc(v.string.capacity);
            if (copy.string.data == NULL) return value_null();
            memcpy(copy.string.data, v.string.data, v.string.byte_length + 1);
            copy.ref_count = 1;
            break;

        case VALUE_ARRAY:
            copy.array.elements = malloc(sizeof(Value) * v.array.capacity);
            if (copy.array.elements == NULL) return value_null();
            for (int i = 0; i < v.array.length; i++) {
                copy.array.elements[i] = value_copy(v.array.elements[i]);
            }
            copy.ref_count = 1;
            break;

        case VALUE_NUMERIC_ARRAY:
            copy.numeric_array.data = malloc(numeric_buffer_bytes(v.numeric_array.capacity, v.numeric_array.dtype));
            if (copy.numeric_array.data == NULL) return value_null();
            if (v.numeric_array.length > 0) {
                memcpy(copy.numeric_array.data, v.numeric_array.data,
                       numeric_buffer_bytes(v.numeric_array.length, v.numeric_array.dtype));
            }
            copy.ref_count = 1;
            break;

        case VALUE_MATRIX: {
            size_t count = (size_t)v.matrix.rows * (size_t)v.matrix.cols;
            copy.matrix.data = count > 0 ? malloc(count * (size_t)numeric_dtype_size(v.matrix.dtype)) : NULL;
            if (count > 0 && copy.matrix.data == NULL) return value_null();
            copy.matrix.row_stride = v.matrix.cols;
            copy.matrix.col_stride = 1;
            copy.matrix.offset = 0;
            copy.matrix.ref_count = malloc(sizeof(int));
            if (copy.matrix.ref_count == NULL) {
                free(copy.matrix.data);
                return value_null();
            }
            *copy.matrix.ref_count = 1;
            for (int r = 0; r < v.matrix.rows; r++) {
                for (int c = 0; c < v.matrix.cols; c++) {
                    numeric_write_at(copy.matrix.data, copy.matrix.dtype,
                                     r * copy.matrix.row_stride + c * copy.matrix.col_stride,
                                     matrix_get(&v, r, c));
                }
            }
            copy.ref_count = 1;
            break;
        }
            
        case VALUE_DICT:
            copy.dict.keys = malloc(sizeof(char *) * v.dict.capacity);
            copy.dict.values = malloc(sizeof(Value) * v.dict.capacity);
            copy.dict.hash_indices = NULL;
            copy.dict.hash_capacity = 0;
            copy.dict.hash_valid = false;
            if (copy.dict.keys == NULL || copy.dict.values == NULL) {
                free(copy.dict.keys);
                free(copy.dict.values);
                return value_null();
            }
            for (int i = 0; i < v.dict.length; i++) {
                copy.dict.keys[i] = strdup(v.dict.keys[i]);
                if (copy.dict.keys[i] == NULL) {
                    for (int j = 0; j < i; j++) {
                        free(copy.dict.keys[j]);
                        value_free(&copy.dict.values[j]);
                    }
                    free(copy.dict.keys);
                    free(copy.dict.values);
                    return value_null();
                }
                copy.dict.values[i] = value_copy(v.dict.values[i]);
            }
            (void)dict_rebuild_hash_index(&copy);
            copy.ref_count = 1;
            break;
            
        case VALUE_FUNCTION:
            // 関数はシャロウコピー（クロージャを共有、参照カウント増加）
            if (v.function.closure != NULL) {
                env_retain(v.function.closure);
            }
            copy.ref_count = 1;
            break;
        
        case VALUE_CLASS:
            // クラス名と親クラスをディープコピー
            copy.class_value.name = strdup(v.class_value.name);
            if (copy.class_value.name == NULL) {
                return value_null();
            }
            if (v.class_value.parent != NULL) {
                copy.class_value.parent = malloc(sizeof(Value));
                if (copy.class_value.parent == NULL) {
                    free(copy.class_value.name);
                    return value_null();
                }
                *copy.class_value.parent = value_copy(*v.class_value.parent);
            }
            copy.ref_count = 1;
            break;

        case VALUE_INSTANCE:
            // インスタンスはディープコピー
            copy.instance.field_names = malloc(sizeof(char *) * v.instance.field_capacity);
            copy.instance.fields = malloc(sizeof(Value) * v.instance.field_capacity);
            if (copy.instance.field_names == NULL || copy.instance.fields == NULL) {
                free(copy.instance.field_names);
                free(copy.instance.fields);
                return value_null();
            }
            for (int i = 0; i < v.instance.field_count; i++) {
                copy.instance.field_names[i] = strdup(v.instance.field_names[i]);
                if (copy.instance.field_names[i] == NULL) {
                    for (int j = 0; j < i; j++) {
                        free(copy.instance.field_names[j]);
                        value_free(&copy.instance.fields[j]);
                    }
                    free(copy.instance.field_names);
                    free(copy.instance.fields);
                    return value_null();
                }
                copy.instance.fields[i] = value_copy(v.instance.fields[i]);
            }
            if (v.instance.class_ref != NULL) {
                copy.instance.class_ref = malloc(sizeof(Value));
                if (copy.instance.class_ref == NULL) {
                    for (int i = 0; i < v.instance.field_count; i++) {
                        free(copy.instance.field_names[i]);
                        value_free(&copy.instance.fields[i]);
                    }
                    free(copy.instance.field_names);
                    free(copy.instance.fields);
                    return value_null();
                }
                *copy.instance.class_ref = value_copy(*v.instance.class_ref);
            }
            copy.ref_count = 1;
            break;
        
        case VALUE_GENERATOR:
            // ジェネレータはシャロウコピー（状態を共有）
            if (v.generator.state != NULL) {
                v.generator.state->ref_count++;
            }
            copy.ref_count = 1;
            break;
            
        default:
            break;
    }
    
    return copy;
}

void value_free(Value *v) {
    if (v == NULL) return;
    
    switch (v->type) {
        case VALUE_STRING:
            if (v->string.data != NULL) {
                free(v->string.data);
                v->string.data = NULL;
            }
            break;
            
        case VALUE_ARRAY:
            if (v->array.elements != NULL) {
                for (int i = 0; i < v->array.length; i++) {
                    value_free(&v->array.elements[i]);
                }
                free(v->array.elements);
                v->array.elements = NULL;
            }
            break;

        case VALUE_NUMERIC_ARRAY:
            free(v->numeric_array.data);
            v->numeric_array.data = NULL;
            v->numeric_array.length = 0;
            v->numeric_array.capacity = 0;
            break;

        case VALUE_MATRIX:
            if (v->matrix.ref_count != NULL) {
                (*v->matrix.ref_count)--;
                if (*v->matrix.ref_count <= 0) {
                    free(v->matrix.data);
                    free(v->matrix.ref_count);
                }
            } else {
                free(v->matrix.data);
            }
            v->matrix.data = NULL;
            v->matrix.rows = 0;
            v->matrix.cols = 0;
            v->matrix.row_stride = 0;
            v->matrix.col_stride = 0;
            v->matrix.offset = 0;
            v->matrix.ref_count = NULL;
            break;
            
        case VALUE_DICT:
            if (v->dict.keys != NULL) {
                for (int i = 0; i < v->dict.length; i++) {
                    free(v->dict.keys[i]);
                    value_free(&v->dict.values[i]);
                }
                free(v->dict.keys);
                free(v->dict.values);
                v->dict.keys = NULL;
                v->dict.values = NULL;
            }
            free(v->dict.hash_indices);
            v->dict.hash_indices = NULL;
            v->dict.hash_capacity = 0;
            v->dict.hash_valid = false;
            break;
        
        case VALUE_CLASS:
            if (v->class_value.name != NULL) {
                free(v->class_value.name);
                v->class_value.name = NULL;
            }
            if (v->class_value.parent != NULL) {
                value_free(v->class_value.parent);
                free(v->class_value.parent);
                v->class_value.parent = NULL;
            }
            break;
        
        case VALUE_INSTANCE:
            if (v->instance.field_names != NULL) {
                for (int i = 0; i < v->instance.field_count; i++) {
                    free(v->instance.field_names[i]);
                    value_free(&v->instance.fields[i]);
                }
                free(v->instance.field_names);
                free(v->instance.fields);
                v->instance.field_names = NULL;
                v->instance.fields = NULL;
            }
            if (v->instance.class_ref != NULL) {
                value_free(v->instance.class_ref);
                free(v->instance.class_ref);
                v->instance.class_ref = NULL;
            }
            break;
        
        case VALUE_GENERATOR:
            generator_state_release(&v->generator.state);
            break;
        
        case VALUE_FUNCTION:
            // クロージャ環境の参照カウントを減少
            if (v->function.closure != NULL) {
                env_release(v->function.closure);
                v->function.closure = NULL;
            }
            break;
            
        default:
            break;
    }
    
    v->type = VALUE_NULL;
}

void value_retain(Value *v) {
    if (v == NULL) return;
    
    switch (v->type) {
        case VALUE_STRING:
        case VALUE_ARRAY:
        case VALUE_NUMERIC_ARRAY:
        case VALUE_MATRIX:
        case VALUE_DICT:
        case VALUE_FUNCTION:
        case VALUE_INSTANCE:
        case VALUE_CLASS:
        case VALUE_GENERATOR:
            v->ref_count++;
            break;
        default:
            break;
    }
}

void value_release(Value *v) {
    if (v == NULL) return;
    
    switch (v->type) {
        case VALUE_STRING:
        case VALUE_ARRAY:
        case VALUE_NUMERIC_ARRAY:
        case VALUE_MATRIX:
        case VALUE_DICT:
        case VALUE_FUNCTION:
        case VALUE_INSTANCE:
        case VALUE_CLASS:
        case VALUE_GENERATOR:
            v->ref_count--;
            if (v->ref_count <= 0) {
                value_free(v);
            }
            break;
        default:
            break;
    }
}

// =============================================================================
// 配列操作
// =============================================================================

void array_push(Value *array, Value element) {
    if (array == NULL || array->type != VALUE_ARRAY) return;
    
    // 容量が足りなければ拡張
    if (array->array.length >= array->array.capacity) {
        ARRAY_GROW(
            array->array.elements,
            array->array.length,
            array->array.capacity,
            Value,
            abort()
        );
    }
    
    // 要素をコピーして追加
    array->array.elements[array->array.length++] = value_copy(element);
}

Value array_get(Value *array, int index) {
    if (array == NULL || array->type != VALUE_ARRAY) {
        return value_null();
    }
    
    if (index < 0 || index >= array->array.length) {
        return value_null();
    }
    
    return array->array.elements[index];
}

bool array_set(Value *array, int index, Value element) {
    if (array == NULL || array->type != VALUE_ARRAY) {
        return false;
    }
    
    if (index < 0 || index >= array->array.length) {
        return false;
    }
    
    value_free(&array->array.elements[index]);
    array->array.elements[index] = value_copy(element);
    return true;
}

Value array_pop(Value *array) {
    if (array == NULL || array->type != VALUE_ARRAY || array->array.length == 0) {
        return value_null();
    }
    
    return array->array.elements[--array->array.length];
}

int array_length(Value *array) {
    if (array == NULL || array->type != VALUE_ARRAY) {
        return 0;
    }
    return array->array.length;
}

// =============================================================================
// 数値ベクトル操作
// =============================================================================

void numeric_array_push(Value *array, double element) {
    if (array == NULL || array->type != VALUE_NUMERIC_ARRAY) return;

    if (array->numeric_array.length >= array->numeric_array.capacity) {
        int old_capacity = array->numeric_array.capacity;
        int new_capacity = old_capacity > 0 ? old_capacity * 2 : VALUE_INITIAL_CAPACITY;
        void *new_data = realloc(array->numeric_array.data,
                                 numeric_buffer_bytes(new_capacity, array->numeric_array.dtype));
        if (new_data == NULL) abort();
        array->numeric_array.data = new_data;
        array->numeric_array.capacity = new_capacity;
    }

    numeric_write_at(array->numeric_array.data, array->numeric_array.dtype,
                     array->numeric_array.length++, element);
}

double numeric_array_get(Value *array, int index) {
    if (array == NULL || array->type != VALUE_NUMERIC_ARRAY) return 0.0;
    if (index < 0 || index >= array->numeric_array.length) return 0.0;
    return numeric_read_at(array->numeric_array.data, array->numeric_array.dtype, index);
}

bool numeric_array_set(Value *array, int index, double element) {
    if (array == NULL || array->type != VALUE_NUMERIC_ARRAY) return false;
    if (index < 0 || index >= array->numeric_array.length) return false;
    numeric_write_at(array->numeric_array.data, array->numeric_array.dtype, index, element);
    return true;
}

void *numeric_array_raw_data(Value *array) {
    if (array == NULL || array->type != VALUE_NUMERIC_ARRAY) return NULL;
    return array->numeric_array.data;
}

int numeric_array_length(Value *array) {
    if (array == NULL || array->type != VALUE_NUMERIC_ARRAY) return 0;
    return array->numeric_array.length;
}

// =============================================================================
// 数値行列操作
// =============================================================================

double matrix_get(Value *matrix, int row, int col) {
    if (matrix == NULL || matrix->type != VALUE_MATRIX) return 0.0;
    if (row < 0 || row >= matrix->matrix.rows || col < 0 || col >= matrix->matrix.cols) {
        return 0.0;
    }
    int index = matrix->matrix.offset +
                row * matrix->matrix.row_stride +
                col * matrix->matrix.col_stride;
    return numeric_read_at(matrix->matrix.data, matrix->matrix.dtype, index);
}

bool matrix_set(Value *matrix, int row, int col, double element) {
    if (matrix == NULL || matrix->type != VALUE_MATRIX) return false;
    if (row < 0 || row >= matrix->matrix.rows || col < 0 || col >= matrix->matrix.cols) {
        return false;
    }
    int index = matrix->matrix.offset +
                row * matrix->matrix.row_stride +
                col * matrix->matrix.col_stride;
    numeric_write_at(matrix->matrix.data, matrix->matrix.dtype, index, element);
    return true;
}

void *matrix_raw_data(Value *matrix) {
    if (matrix == NULL || matrix->type != VALUE_MATRIX) return NULL;
    return matrix->matrix.data;
}

bool matrix_is_contiguous(Value *matrix) {
    if (matrix == NULL || matrix->type != VALUE_MATRIX) return false;
    return matrix->matrix.offset == 0 &&
           matrix->matrix.row_stride == matrix->matrix.cols &&
           matrix->matrix.col_stride == 1;
}

// =============================================================================
// 辞書操作
// =============================================================================

// 辞書内でキーのインデックスを検索
static int dict_find_key_linear(Value *dict, const char *key) {
    for (int i = 0; i < dict->dict.length; i++) {
        if (strcmp(dict->dict.keys[i], key) == 0) {
            return i;
        }
    }
    return -1;
}

static int dict_find_key(Value *dict, const char *key) {
    if (dict == NULL || dict->type != VALUE_DICT || key == NULL) return -1;

    if (dict->dict.length < DICT_HASH_MIN_LENGTH) {
        return dict_find_key_linear(dict, key);
    }

    if (!dict->dict.hash_valid || dict->dict.hash_indices == NULL) {
        (void)dict_rebuild_hash_index(dict);
    }

    if (!dict->dict.hash_valid || dict->dict.hash_indices == NULL || dict->dict.hash_capacity <= 0) {
        return dict_find_key_linear(dict, key);
    }

    uint64_t hash = dict_hash_key(key);
    int mask = dict->dict.hash_capacity - 1;
    int slot = (int)(hash & (uint64_t)mask);
    for (int probes = 0; probes < dict->dict.hash_capacity; probes++) {
        int stored = dict->dict.hash_indices[slot];
        if (stored == 0) {
            return -1;
        }

        int index = stored - 1;
        if (index >= 0 && index < dict->dict.length && strcmp(dict->dict.keys[index], key) == 0) {
            return index;
        }
        slot = (slot + 1) & mask;
    }

    return dict_find_key_linear(dict, key);
}

bool dict_set(Value *dict, const char *key, Value value) {
    if (dict == NULL || dict->type != VALUE_DICT || key == NULL) return false;
    
    int idx = dict_find_key(dict, key);
    
    if (idx >= 0) {
        // 既存のキーを更新
        value_free(&dict->dict.values[idx]);
        dict->dict.values[idx] = value_copy(value);
        return true;
    }
    
    // 容量が足りなければ拡張
    if (dict->dict.length >= dict->dict.capacity) {
        int new_capacity = dict->dict.capacity > 0 ? dict->dict.capacity * 2 : VALUE_INITIAL_CAPACITY;
        char **new_keys = realloc(dict->dict.keys, sizeof(char *) * (size_t)new_capacity);
        if (new_keys == NULL) {
            abort();
        }
        Value *new_values = realloc(dict->dict.values, sizeof(Value) * (size_t)new_capacity);
        if (new_values == NULL) {
            dict->dict.keys = new_keys;
            abort();
        }
        dict->dict.keys = new_keys;
        dict->dict.values = new_values;
        dict->dict.capacity = new_capacity;
    }
    
    // 新しいキーを追加
    dict->dict.keys[dict->dict.length] = strdup(key);
    if (dict->dict.keys[dict->dict.length] == NULL) {
        return false;
    }
    dict->dict.values[dict->dict.length] = value_copy(value);
    dict->dict.length++;

    if (dict->dict.hash_valid) {
        if ((int64_t)dict->dict.length * 4 >= (int64_t)dict->dict.hash_capacity * 3) {
            (void)dict_rebuild_hash_index(dict);
        } else {
            dict_hash_insert_slot(dict, dict->dict.length - 1);
        }
    } else if (dict->dict.length >= DICT_HASH_MIN_LENGTH) {
        (void)dict_rebuild_hash_index(dict);
    }
    
    return true;
}

Value dict_get(Value *dict, const char *key) {
    if (dict == NULL || dict->type != VALUE_DICT || key == NULL) {
        return value_null();
    }
    
    int idx = dict_find_key(dict, key);
    if (idx >= 0) {
        return dict->dict.values[idx];
    }
    
    return value_null();
}

bool dict_delete(Value *dict, const char *key) {
    if (dict == NULL || dict->type != VALUE_DICT || key == NULL) return false;
    
    int idx = dict_find_key(dict, key);
    if (idx < 0) return false;
    
    // キーと値を解放
    free(dict->dict.keys[idx]);
    value_free(&dict->dict.values[idx]);
    
    // 最後の要素で穴を埋める
    if (idx < dict->dict.length - 1) {
        dict->dict.keys[idx] = dict->dict.keys[dict->dict.length - 1];
        dict->dict.values[idx] = dict->dict.values[dict->dict.length - 1];
    }
    dict->dict.length--;

    if (dict->dict.length < DICT_HASH_MIN_LENGTH) {
        dict_discard_hash_index(dict);
    } else if (dict->dict.hash_valid) {
        (void)dict_rebuild_hash_index(dict);
    }
    
    return true;
}

bool dict_has(Value *dict, const char *key) {
    if (dict == NULL || dict->type != VALUE_DICT || key == NULL) return false;
    return dict_find_key(dict, key) >= 0;
}

Value dict_keys(Value *dict) {
    if (dict == NULL || dict->type != VALUE_DICT) {
        return value_array();
    }
    
    Value keys = value_array_with_capacity(dict->dict.length);
    for (int i = 0; i < dict->dict.length; i++) {
        Value key = value_string(dict->dict.keys[i]);
        array_push(&keys, key);
        value_free(&key);
    }
    
    return keys;
}

Value dict_values(Value *dict) {
    if (dict == NULL || dict->type != VALUE_DICT) {
        return value_array();
    }
    
    Value vals = value_array_with_capacity(dict->dict.length);
    for (int i = 0; i < dict->dict.length; i++) {
        array_push(&vals, dict->dict.values[i]);
    }
    
    return vals;
}

int dict_length(Value *dict) {
    if (dict == NULL || dict->type != VALUE_DICT) {
        return 0;
    }
    return dict->dict.length;
}

// =============================================================================
// 文字列操作
// =============================================================================

Value string_concat(Value a, Value b) {
    if (a.type != VALUE_STRING || b.type != VALUE_STRING) {
        return value_null();
    }
    
    int new_length = a.string.byte_length + b.string.byte_length;
    Value result;
    result.type = VALUE_STRING;
    result.is_const = false;
    result.ref_count = 1;
    result.string.byte_length = new_length;
    result.string.char_length = a.string.char_length + b.string.char_length;
    result.string.capacity = new_length + 1;
    result.string.data = malloc(result.string.capacity);
    
    memcpy(result.string.data, a.string.data, a.string.byte_length);
    memcpy(result.string.data + a.string.byte_length, b.string.data, b.string.byte_length);
    result.string.data[new_length] = '\0';
    
    return result;
}

int string_length(Value *s) {
    if (s == NULL || s->type != VALUE_STRING) {
        return 0;
    }
    return s->string.char_length;
}

Value string_substring(Value *s, int start, int end) {
    if (s == NULL || s->type != VALUE_STRING) {
        return value_null();
    }
    
    // UTF-8での文字位置をバイト位置に変換
    const char *p = s->string.data;
    const char *str_end = p + s->string.byte_length;
    int char_index = 0;
    const char *start_ptr = NULL;
    const char *end_ptr = NULL;
    
    while (p < str_end && (start_ptr == NULL || end_ptr == NULL)) {
        if (char_index == start) start_ptr = p;
        if (char_index == end) end_ptr = p;
        
        unsigned char c = (unsigned char)*p;
        if (c < 0x80) {
            p++;
        } else if (c < 0xE0) {
            p += 2;
        } else if (c < 0xF0) {
            p += 3;
        } else {
            p += 4;
        }
        char_index++;
    }
    
    if (start_ptr == NULL) start_ptr = str_end;
    if (end_ptr == NULL) end_ptr = str_end;
    
    return value_string_n(start_ptr, (int)(end_ptr - start_ptr));
}

// =============================================================================
// 型変換・判定
// =============================================================================

bool value_is_truthy(Value v) {
    switch (v.type) {
        case VALUE_NULL:
            return false;
        case VALUE_BOOL:
            return v.boolean;
        case VALUE_NUMBER:
            return v.number != 0.0 && !isnan(v.number);
        case VALUE_STRING:
            return v.string.byte_length > 0;
        case VALUE_ARRAY:
            return v.array.length > 0;
        case VALUE_NUMERIC_ARRAY:
            return v.numeric_array.length > 0;
        case VALUE_MATRIX:
            return v.matrix.rows > 0 && v.matrix.cols > 0;
        case VALUE_DICT:
            return v.dict.length > 0;
        case VALUE_FUNCTION:
        case VALUE_BUILTIN:
        case VALUE_CLASS:
        case VALUE_INSTANCE:
        case VALUE_GENERATOR:
            return true;
    }
    return false;
}

const char *value_type_name(ValueType type) {
    switch (type) {
        case VALUE_NULL:     return "null";
        case VALUE_NUMBER:   return "数値";
        case VALUE_BOOL:     return "真偽";
        case VALUE_STRING:   return "文字列";
        case VALUE_ARRAY:    return "配列";
        case VALUE_NUMERIC_ARRAY: return "数値ベクトル";
        case VALUE_MATRIX:   return "数値行列";
        case VALUE_DICT:     return "辞書";
        case VALUE_FUNCTION: return "関数";
        case VALUE_BUILTIN:  return "組み込み関数";
        case VALUE_CLASS:    return "クラス";
        case VALUE_INSTANCE: return "インスタンス";
        case VALUE_GENERATOR: return "ジェネレータ";
    }
    return "不明";
}

const char *value_runtime_type_name(Value v) {
    if (v.type == VALUE_NUMBER && v.is_integer) {
        return "整数";
    }
    return value_type_name(v.type);
}

char *value_to_string(Value v) {
    char *buffer;
    
    switch (v.type) {
        case VALUE_NULL:
            buffer = strdup("null");
            if (buffer == NULL) {
                return NULL;
            }
            break;
            
        case VALUE_NUMBER: {
            buffer = malloc(32);
            double intpart;
            if (modf(v.number, &intpart) == 0.0 && 
                v.number >= -999999999 && v.number <= 999999999) {
                snprintf(buffer, 32, "%.0f", v.number);
            } else {
                snprintf(buffer, 32, "%g", v.number);
            }
            break;
        }
        
        case VALUE_BOOL:
            buffer = strdup(v.boolean ? "真" : "偽");
            if (buffer == NULL) {
                return NULL;
            }
            break;
            
        case VALUE_STRING:
            buffer = malloc(v.string.byte_length + 1);
            memcpy(buffer, v.string.data, v.string.byte_length);
            buffer[v.string.byte_length] = '\0';
            break;
            
        case VALUE_ARRAY: {
            // 配列を文字列化
            size_t capacity = 64;
            size_t length = 0;
            buffer = malloc(capacity);
            buffer[length++] = '[';
            
            for (int i = 0; i < v.array.length; i++) {
                if (i > 0) {
                    if (length + 2 >= capacity) {
                        ARRAY_GROW(buffer, length + 2, capacity, char, abort());
                    }
                    buffer[length++] = ',';
                    buffer[length++] = ' ';
                }
                
                char *elem_str = value_to_string(v.array.elements[i]);
                size_t elem_len = strlen(elem_str);
                
                while (length + elem_len + 2 >= capacity) {
                    ARRAY_GROW(buffer, length + elem_len + 2, capacity, char, abort());
                }
                
                memcpy(buffer + length, elem_str, elem_len);
                length += elem_len;
                free(elem_str);
            }
            
            buffer[length++] = ']';
            buffer[length] = '\0';
            break;
        }

        case VALUE_NUMERIC_ARRAY: {
            size_t capacity = 64;
            size_t length = 0;
            buffer = malloc(capacity);
            buffer[length++] = '[';

            int preview = v.numeric_array.length < 12 ? v.numeric_array.length : 12;
            for (int i = 0; i < preview; i++) {
                if (i > 0) {
                    if (length + 2 >= capacity) {
                        ARRAY_GROW(buffer, length + 2, capacity, char, abort());
                    }
                    buffer[length++] = ',';
                    buffer[length++] = ' ';
                }

                char elem[64];
                snprintf(elem, sizeof(elem), "%g", numeric_read_at(v.numeric_array.data, v.numeric_array.dtype, i));
                size_t elem_len = strlen(elem);
                while (length + elem_len + 16 >= capacity) {
                    ARRAY_GROW(buffer, length + elem_len + 16, capacity, char, abort());
                }
                memcpy(buffer + length, elem, elem_len);
                length += elem_len;
            }

            if (v.numeric_array.length > preview) {
                const char *suffix = ", ...";
                size_t suffix_len = strlen(suffix);
                while (length + suffix_len + 2 >= capacity) {
                    ARRAY_GROW(buffer, length + suffix_len + 2, capacity, char, abort());
                }
                memcpy(buffer + length, suffix, suffix_len);
                length += suffix_len;
            }

            buffer[length++] = ']';
            buffer[length] = '\0';
            break;
        }

        case VALUE_MATRIX: {
            size_t capacity = 96;
            size_t length = 0;
            buffer = malloc(capacity);
            buffer[length++] = '[';

            int preview_rows = v.matrix.rows < 6 ? v.matrix.rows : 6;
            for (int r = 0; r < preview_rows; r++) {
                if (r > 0) {
                    while (length + 2 >= capacity) ARRAY_GROW(buffer, length + 2, capacity, char, abort());
                    buffer[length++] = ',';
                    buffer[length++] = ' ';
                }
                while (length + 1 >= capacity) ARRAY_GROW(buffer, length + 1, capacity, char, abort());
                buffer[length++] = '[';

                int preview_cols = v.matrix.cols < 8 ? v.matrix.cols : 8;
                for (int c = 0; c < preview_cols; c++) {
                    if (c > 0) {
                        while (length + 2 >= capacity) ARRAY_GROW(buffer, length + 2, capacity, char, abort());
                        buffer[length++] = ',';
                        buffer[length++] = ' ';
                    }
                    char elem[64];
                    snprintf(elem, sizeof(elem), "%g", matrix_get(&v, r, c));
                    size_t elem_len = strlen(elem);
                    while (length + elem_len + 16 >= capacity) {
                        ARRAY_GROW(buffer, length + elem_len + 16, capacity, char, abort());
                    }
                    memcpy(buffer + length, elem, elem_len);
                    length += elem_len;
                }

                if (v.matrix.cols > preview_cols) {
                    const char *suffix = ", ...";
                    size_t suffix_len = strlen(suffix);
                    while (length + suffix_len + 1 >= capacity) ARRAY_GROW(buffer, length + suffix_len + 1, capacity, char, abort());
                    memcpy(buffer + length, suffix, suffix_len);
                    length += suffix_len;
                }

                while (length + 1 >= capacity) ARRAY_GROW(buffer, length + 1, capacity, char, abort());
                buffer[length++] = ']';
            }

            if (v.matrix.rows > preview_rows) {
                const char *suffix = ", ...";
                size_t suffix_len = strlen(suffix);
                while (length + suffix_len + 1 >= capacity) ARRAY_GROW(buffer, length + suffix_len + 1, capacity, char, abort());
                memcpy(buffer + length, suffix, suffix_len);
                length += suffix_len;
            }

            while (length + 1 >= capacity) ARRAY_GROW(buffer, length + 1, capacity, char, abort());
            buffer[length++] = ']';
            buffer[length] = '\0';
            break;
        }
        
        case VALUE_DICT: {
            // 辞書を文字列化
            size_t capacity = 64;
            size_t length = 0;
            buffer = malloc(capacity);
            buffer[length++] = '{';
            
            for (int i = 0; i < v.dict.length; i++) {
                if (i > 0) {
                    if (length + 2 >= capacity) {
                        ARRAY_GROW(buffer, length + 2, capacity, char, abort());
                    }
                    buffer[length++] = ',';
                    buffer[length++] = ' ';
                }
                
                // キー
                size_t key_len = strlen(v.dict.keys[i]);
                while (length + key_len + 5 >= capacity) {
                    ARRAY_GROW(buffer, length + key_len + 5, capacity, char, abort());
                }
                buffer[length++] = '"';
                memcpy(buffer + length, v.dict.keys[i], key_len);
                length += key_len;
                buffer[length++] = '"';
                buffer[length++] = ':';
                buffer[length++] = ' ';
                
                // 値
                char *val_str = value_to_string(v.dict.values[i]);
                size_t val_len = strlen(val_str);
                
                while (length + val_len + 2 >= capacity) {
                    ARRAY_GROW(buffer, length + val_len + 2, capacity, char, abort());
                }
                
                memcpy(buffer + length, val_str, val_len);
                length += val_len;
                free(val_str);
            }
            
            buffer[length++] = '}';
            buffer[length] = '\0';
            break;
        }
        
        case VALUE_FUNCTION:
            buffer = malloc(32);
            snprintf(buffer, 32, "<関数>");
            break;
            
        case VALUE_BUILTIN:
            buffer = malloc(64);
            snprintf(buffer, 64, "<組み込み関数: %s>", v.builtin.name);
            break;
        
        case VALUE_CLASS:
            buffer = malloc(64);
            snprintf(buffer, 64, "<クラス: %s>", v.class_value.name);
            break;
        
        case VALUE_INSTANCE: {
            const char *class_name = v.instance.class_ref ? 
                v.instance.class_ref->class_value.name : "不明";
            buffer = malloc(64);
            snprintf(buffer, 64, "<%sのインスタンス>", class_name);
            break;
        }
        
        case VALUE_GENERATOR:
            buffer = malloc(64);
            if (v.generator.state != NULL) {
                snprintf(buffer, 64, "<ジェネレータ: %d/%d>", v.generator.state->index, v.generator.state->length);
            } else {
                snprintf(buffer, 64, "<ジェネレータ: 無効>");
            }
            break;
            
        default:
            buffer = malloc(16);
            strcpy(buffer, "<不明>");
    }
    
    return buffer;
}

Value value_to_number(Value v) {
    switch (v.type) {
        case VALUE_NUMBER:
            return v;
        case VALUE_BOOL:
            return value_number(v.boolean ? 1.0 : 0.0);
        case VALUE_STRING: {
            char *endptr;
            double n = strtod(v.string.data, &endptr);
            if (*endptr != '\0') {
                return value_null();  // 変換失敗
            }
            return value_number(n);
        }
        default:
            return value_null();
    }
}

bool value_equals(Value a, Value b) {
    if (a.type != b.type) return false;
    
    switch (a.type) {
        case VALUE_NULL:
            return true;
        case VALUE_NUMBER:
            return a.number == b.number;
        case VALUE_BOOL:
            return a.boolean == b.boolean;
        case VALUE_STRING:
            return a.string.byte_length == b.string.byte_length &&
                   memcmp(a.string.data, b.string.data, a.string.byte_length) == 0;
        case VALUE_ARRAY:
            if (a.array.length != b.array.length) return false;
            for (int i = 0; i < a.array.length; i++) {
                if (!value_equals(a.array.elements[i], b.array.elements[i])) {
                    return false;
                }
            }
            return true;
        case VALUE_NUMERIC_ARRAY:
            if (a.numeric_array.length != b.numeric_array.length) return false;
            for (int i = 0; i < a.numeric_array.length; i++) {
                if (numeric_array_get(&a, i) != numeric_array_get(&b, i)) return false;
            }
            return true;
        case VALUE_MATRIX:
            if (a.matrix.rows != b.matrix.rows || a.matrix.cols != b.matrix.cols) return false;
            for (int r = 0; r < a.matrix.rows; r++) {
                for (int c = 0; c < a.matrix.cols; c++) {
                    if (matrix_get(&a, r, c) != matrix_get(&b, r, c)) return false;
                }
            }
            return true;
        case VALUE_DICT:
            if (a.dict.length != b.dict.length) return false;
            for (int i = 0; i < a.dict.length; i++) {
                // キーが同じ順序で同じ値を持っているか確認
                if (strcmp(a.dict.keys[i], b.dict.keys[i]) != 0) return false;
                if (!value_equals(a.dict.values[i], b.dict.values[i])) return false;
            }
            return true;
        case VALUE_FUNCTION:
            return a.function.definition == b.function.definition;
        case VALUE_BUILTIN:
            return a.builtin.fn == b.builtin.fn;
        case VALUE_CLASS:
            return a.class_value.definition == b.class_value.definition;
        case VALUE_GENERATOR:
            // ジェネレータの比較
            return a.generator.state == b.generator.state;
        case VALUE_INSTANCE:
            // インスタンスは同一性で比較
            return &a == &b;
    }
    return false;
}

int value_compare(Value a, Value b) {
    if (a.type == VALUE_NUMBER && b.type == VALUE_NUMBER) {
        if (a.number < b.number) return -1;
        if (a.number > b.number) return 1;
        return 0;
    }
    
    if (a.type == VALUE_STRING && b.type == VALUE_STRING) {
        return strcmp(a.string.data, b.string.data);
    }

    // 型が異なる場合は型番号順で安定した順序を与える
    if (a.type != b.type) {
        return (a.type < b.type) ? -1 : 1;
    }

    // 同一型の追加対応
    switch (a.type) {
        case VALUE_BOOL:
            // 偽(false) < 真(true)
            return (int)a.boolean - (int)b.boolean;
        case VALUE_NULL:
            return 0;
        default:
            // 配列・辞書・関数・インスタンス等は順序未定義（等価扱い）
            return 0;
    }
}

// =============================================================================
// デバッグ
// =============================================================================

void value_print(Value v) {
    char *str = value_to_string(v);
    printf("%s", str);
    free(str);
}

void value_debug(Value v) {
    printf("Value { type=%s, ref_count=%d, value=", 
           value_type_name(v.type), v.ref_count);
    value_print(v);
    printf(" }\n");
}
