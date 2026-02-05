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
            
        case VALUE_FUNCTION:
            // 関数はシャロウコピー（クロージャを共有）
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
        case VALUE_FUNCTION:
        case VALUE_BUILTIN:
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
        case VALUE_FUNCTION: return "関数";
        case VALUE_BUILTIN:  return "組み込み関数";
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
            buffer = malloc(3);
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
        
        case VALUE_FUNCTION:
            buffer = malloc(32);
            snprintf(buffer, 32, "<関数>");
            break;
            
        case VALUE_BUILTIN:
            buffer = malloc(64);
            snprintf(buffer, 64, "<組み込み関数: %s>", v.builtin.name);
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
        case VALUE_FUNCTION:
            return a.function.definition == b.function.definition;
        case VALUE_BUILTIN:
            return a.builtin.fn == b.builtin.fn;
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
