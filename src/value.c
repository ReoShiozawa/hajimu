/**
 * 日本語プログラミング言語 - 値システム実装
 */

#include "value.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// =============================================================================
// 値の作成
// =============================================================================

Value value_null(void) {
    Value v;
    v.type = VALUE_NULL;
    v.is_const = false;
    v.ref_count = 0;
    return v;
}

Value value_number(double n) {
    Value v;
    v.type = VALUE_NUMBER;
    v.is_const = false;
    v.ref_count = 0;
    v.number = n;
    return v;
}

Value value_bool(bool b) {
    Value v;
    v.type = VALUE_BOOL;
    v.is_const = false;
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
    v.ref_count = 1;
    
    v.string.length = length;
    v.string.capacity = length + 1;
    v.string.data = malloc(v.string.capacity);
    
    if (s != NULL) {
        memcpy(v.string.data, s, length);
    }
    v.string.data[length] = '\0';
    
    return v;
}

Value value_array(void) {
    return value_array_with_capacity(8);
}

Value value_array_with_capacity(int capacity) {
    Value v;
    v.type = VALUE_ARRAY;
    v.is_const = false;
    v.ref_count = 1;
    
    v.array.length = 0;
    v.array.capacity = capacity > 0 ? capacity : 8;
    v.array.elements = malloc(sizeof(Value) * v.array.capacity);
    
    return v;
}

Value value_function(struct ASTNode *definition, struct Environment *closure) {
    Value v;
    v.type = VALUE_FUNCTION;
    v.is_const = false;
    v.ref_count = 1;
    v.function.definition = definition;
    v.function.closure = closure;
    return v;
}

Value value_builtin(BuiltinFn fn, const char *name, int min_args, int max_args) {
    Value v;
    v.type = VALUE_BUILTIN;
    v.is_const = true;
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
    v.ref_count = 1;
    v.class_value.name = strdup(name);
    v.class_value.definition = definition;
    v.class_value.parent = parent;
    return v;
}

Value value_instance(Value *class_ref) {
    Value v;
    v.type = VALUE_INSTANCE;
    v.is_const = false;
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
        int new_cap = s->capacity == 0 ? 8 : s->capacity * 2;
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
    
    int idx = instance->instance.field_count++;
    instance->instance.field_names[idx] = strdup(name);
    instance->instance.fields[idx] = value_copy(value);
}

Value *instance_get_field(Value *instance, const char *name) {
    for (int i = 0; i < instance->instance.field_count; i++) {
        if (strcmp(instance->instance.field_names[i], name) == 0) {
            return &instance->instance.fields[i];
        }
    }
    return NULL;
}

Value value_dict(void) {
    return value_dict_with_capacity(8);
}

Value value_dict_with_capacity(int capacity) {
    Value v;
    v.type = VALUE_DICT;
    v.is_const = false;
    v.ref_count = 1;
    
    v.dict.length = 0;
    v.dict.capacity = capacity > 0 ? capacity : 8;
    v.dict.keys = malloc(sizeof(char *) * v.dict.capacity);
    v.dict.values = malloc(sizeof(Value) * v.dict.capacity);
    
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
            memcpy(copy.string.data, v.string.data, v.string.length + 1);
            copy.ref_count = 1;
            break;
            
        case VALUE_ARRAY:
            copy.array.elements = malloc(sizeof(Value) * v.array.capacity);
            for (int i = 0; i < v.array.length; i++) {
                copy.array.elements[i] = value_copy(v.array.elements[i]);
            }
            copy.ref_count = 1;
            break;
            
        case VALUE_DICT:
            copy.dict.keys = malloc(sizeof(char *) * v.dict.capacity);
            copy.dict.values = malloc(sizeof(Value) * v.dict.capacity);
            for (int i = 0; i < v.dict.length; i++) {
                copy.dict.keys[i] = strdup(v.dict.keys[i]);
                copy.dict.values[i] = value_copy(v.dict.values[i]);
            }
            copy.ref_count = 1;
            break;
            
        case VALUE_FUNCTION:
            // 関数はシャロウコピー（クロージャを共有）
            copy.ref_count = 1;
            break;
        
        case VALUE_CLASS:
            // クラスもシャロウコピー
            copy.class_value.name = strdup(v.class_value.name);
            copy.ref_count = 1;
            break;
        
        case VALUE_INSTANCE:
            // インスタンスはディープコピー
            copy.instance.field_names = malloc(sizeof(char *) * v.instance.field_capacity);
            copy.instance.fields = malloc(sizeof(Value) * v.instance.field_capacity);
            for (int i = 0; i < v.instance.field_count; i++) {
                copy.instance.field_names[i] = strdup(v.instance.field_names[i]);
                copy.instance.fields[i] = value_copy(v.instance.fields[i]);
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
            break;
        
        case VALUE_CLASS:
            if (v->class_value.name != NULL) {
                free(v->class_value.name);
                v->class_value.name = NULL;
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
            break;
        
        case VALUE_GENERATOR:
            if (v->generator.state != NULL) {
                v->generator.state->ref_count--;
                if (v->generator.state->ref_count <= 0) {
                    for (int i = 0; i < v->generator.state->length; i++) {
                        value_free(&v->generator.state->values[i]);
                    }
                    free(v->generator.state->values);
                    free(v->generator.state);
                }
                v->generator.state = NULL;
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
        case VALUE_DICT:
        case VALUE_FUNCTION:
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
        case VALUE_DICT:
        case VALUE_FUNCTION:
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
        array->array.capacity *= 2;
        array->array.elements = realloc(
            array->array.elements, 
            sizeof(Value) * array->array.capacity
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
// 辞書操作
// =============================================================================

// 辞書内でキーのインデックスを検索
static int dict_find_key(Value *dict, const char *key) {
    for (int i = 0; i < dict->dict.length; i++) {
        if (strcmp(dict->dict.keys[i], key) == 0) {
            return i;
        }
    }
    return -1;
}

bool dict_set(Value *dict, const char *key, Value value) {
    if (dict == NULL || dict->type != VALUE_DICT || key == NULL) return false;
    
    // 既存のキーを検索
    int idx = dict_find_key(dict, key);
    
    if (idx >= 0) {
        // 既存のキーを更新
        value_free(&dict->dict.values[idx]);
        dict->dict.values[idx] = value_copy(value);
        return true;
    }
    
    // 容量が足りなければ拡張
    if (dict->dict.length >= dict->dict.capacity) {
        dict->dict.capacity *= 2;
        dict->dict.keys = realloc(dict->dict.keys, sizeof(char *) * dict->dict.capacity);
        dict->dict.values = realloc(dict->dict.values, sizeof(Value) * dict->dict.capacity);
    }
    
    // 新しいキーを追加
    dict->dict.keys[dict->dict.length] = strdup(key);
    dict->dict.values[dict->dict.length] = value_copy(value);
    dict->dict.length++;
    
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
    
    int new_length = a.string.length + b.string.length;
    Value result;
    result.type = VALUE_STRING;
    result.is_const = false;
    result.ref_count = 1;
    result.string.length = new_length;
    result.string.capacity = new_length + 1;
    result.string.data = malloc(result.string.capacity);
    
    memcpy(result.string.data, a.string.data, a.string.length);
    memcpy(result.string.data + a.string.length, b.string.data, b.string.length);
    result.string.data[new_length] = '\0';
    
    return result;
}

// UTF-8文字列の文字数をカウント
int string_length(Value *s) {
    if (s == NULL || s->type != VALUE_STRING) {
        return 0;
    }
    
    int count = 0;
    const char *p = s->string.data;
    const char *end = p + s->string.length;
    
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

Value string_substring(Value *s, int start, int end) {
    if (s == NULL || s->type != VALUE_STRING) {
        return value_null();
    }
    
    // UTF-8での文字位置をバイト位置に変換
    const char *p = s->string.data;
    const char *str_end = p + s->string.length;
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
            return v.string.length > 0;
        case VALUE_ARRAY:
            return v.array.length > 0;
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
        case VALUE_DICT:     return "辞書";
        case VALUE_FUNCTION: return "関数";
        case VALUE_BUILTIN:  return "組み込み関数";
        case VALUE_CLASS:    return "クラス";
        case VALUE_INSTANCE: return "インスタンス";
        case VALUE_GENERATOR: return "ジェネレータ";
    }
    return "不明";
}

char *value_to_string(Value v) {
    char *buffer;
    
    switch (v.type) {
        case VALUE_NULL:
            buffer = malloc(5);
            strcpy(buffer, "null");
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
            buffer = malloc(strlen(v.boolean ? "真" : "偽") + 1);
            strcpy(buffer, v.boolean ? "真" : "偽");
            break;
            
        case VALUE_STRING:
            buffer = malloc(v.string.length + 1);
            memcpy(buffer, v.string.data, v.string.length);
            buffer[v.string.length] = '\0';
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
                        capacity *= 2;
                        buffer = realloc(buffer, capacity);
                    }
                    buffer[length++] = ',';
                    buffer[length++] = ' ';
                }
                
                char *elem_str = value_to_string(v.array.elements[i]);
                size_t elem_len = strlen(elem_str);
                
                while (length + elem_len + 2 >= capacity) {
                    capacity *= 2;
                    buffer = realloc(buffer, capacity);
                }
                
                memcpy(buffer + length, elem_str, elem_len);
                length += elem_len;
                free(elem_str);
            }
            
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
                        capacity *= 2;
                        buffer = realloc(buffer, capacity);
                    }
                    buffer[length++] = ',';
                    buffer[length++] = ' ';
                }
                
                // キー
                size_t key_len = strlen(v.dict.keys[i]);
                while (length + key_len + 5 >= capacity) {
                    capacity *= 2;
                    buffer = realloc(buffer, capacity);
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
                    capacity *= 2;
                    buffer = realloc(buffer, capacity);
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
            return a.string.length == b.string.length &&
                   memcmp(a.string.data, b.string.data, a.string.length) == 0;
        case VALUE_ARRAY:
            if (a.array.length != b.array.length) return false;
            for (int i = 0; i < a.array.length; i++) {
                if (!value_equals(a.array.elements[i], b.array.elements[i])) {
                    return false;
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
    
    // 比較不可能
    return 0;
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
