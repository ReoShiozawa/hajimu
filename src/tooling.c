#include "tooling.h"
#include "parser.h"
#include "diag.h"
#include "http.h"
#include "evaluator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void put(Value *object, const char *key, Value value) { dict_set(object, key, value); }

static void capture(DiagKind kind, int line, int column, int end, const char *message, void *context) {
    Value diagnostic = value_dict();
    put(&diagnostic, "message", value_string(message));
    put(&diagnostic, "kind", value_number(kind));
    put(&diagnostic, "line", value_number(line));
    put(&diagnostic, "column", value_number(column));
    put(&diagnostic, "endColumn", value_number(end + 1));
    array_push(context, diagnostic);
}

static _Thread_local int scope_end;
static int last_line(ASTNode *n) {
    if (!n) return 0;
    int last=n->location.line;
#define MAX_NODE(child) do { int end=last_line(child); if(end>last) last=end; } while(0)
    switch(n->type) {
        case NODE_BLOCK: case NODE_PROGRAM:
            for(int i=0;i<n->block.count;i++) { MAX_NODE(n->block.statements[i]); }
            break;
        case NODE_IF: MAX_NODE(n->if_stmt.then_branch); MAX_NODE(n->if_stmt.else_branch); break;
        case NODE_WHILE: MAX_NODE(n->while_stmt.body); break;
        case NODE_FOR: MAX_NODE(n->for_stmt.body); break;
        case NODE_FOREACH: MAX_NODE(n->foreach_stmt.body); break;
        case NODE_FUNCTION_DEF: MAX_NODE(n->function.body); break;
        case NODE_TRY: MAX_NODE(n->try_stmt.try_block); MAX_NODE(n->try_stmt.catch_block); MAX_NODE(n->try_stmt.finally_block); break;
        default: break;
    }
#undef MAX_NODE
    return last;
}
static void symbol(Value *symbols, const char *name, const char *kind, ASTNode *node, int scope) {
    if (!name || !node) return;
    Value item = value_dict();
    put(&item, "name", value_string(name));
    put(&item, "kind", value_string(kind));
    put(&item, "line", value_number(node->location.line));
    put(&item, "column", value_number(node->location.column));
    put(&item, "scope", value_number(scope));
    put(&item, "scopeEnd", value_number(scope_end));
    array_push(symbols, item);
}

static void walk(ASTNode *n, Value *symbols, int scope) {
    if (!n) return;
    int previous_end=scope_end;
#define W(child) walk((child), symbols, scope)
    switch (n->type) {
        case NODE_PROGRAM: case NODE_BLOCK: case NODE_ARRAY:
            for (int i = 0; i < n->block.count; i++) W(n->block.statements[i]);
            break;
        case NODE_FUNCTION_DEF:
            symbol(symbols, n->function.name, "function", n, scope);
            scope = n->location.line;
            scope_end=last_line(n->function.body);
            for (int i = 0; i < n->function.param_count; i++) {
                symbol(symbols, n->function.params[i].name, "parameter", n, scope);
                W(n->function.params[i].default_value);
            }
            W(n->function.body); break;
        case NODE_METHOD_DEF:
            symbol(symbols, n->method.name, "method", n, scope);
            scope = n->location.line;
            scope_end=last_line(n->method.body);
            for (int i = 0; i < n->method.param_count; i++) symbol(symbols, n->method.params[i].name, "parameter", n, scope);
            W(n->method.body); break;
        case NODE_CLASS_DEF:
            symbol(symbols, n->class_def.name, "class", n, scope);
            W(n->class_def.init_method);
            for (int i = 0; i < n->class_def.method_count; i++) W(n->class_def.methods[i]);
            for (int i = 0; i < n->class_def.static_method_count; i++) W(n->class_def.static_methods[i]);
            break;
        case NODE_VAR_DECL: symbol(symbols, n->var_decl.name, "variable", n, scope); W(n->var_decl.initializer); break;
        case NODE_FOR: symbol(symbols, n->for_stmt.var_name, "variable", n, scope); W(n->for_stmt.start); W(n->for_stmt.end); W(n->for_stmt.body); break;
        case NODE_FOREACH: symbol(symbols, n->foreach_stmt.var_name, "variable", n, scope); symbol(symbols, n->foreach_stmt.value_name, "variable", n, scope); W(n->foreach_stmt.iterable); W(n->foreach_stmt.body); break;
        case NODE_IF: W(n->if_stmt.condition); W(n->if_stmt.then_branch); W(n->if_stmt.else_branch); break;
        case NODE_WHILE: W(n->while_stmt.condition); W(n->while_stmt.body); break;
        case NODE_TRY: symbol(symbols, n->try_stmt.catch_var, "variable", n, scope); W(n->try_stmt.try_block); W(n->try_stmt.catch_block); W(n->try_stmt.finally_block); break;
        case NODE_SWITCH: W(n->switch_stmt.target); for (int i=0;i<n->switch_stmt.case_count;i++) { W(n->switch_stmt.case_values[i]); W(n->switch_stmt.case_bodies[i]); } W(n->switch_stmt.default_body); break;
        case NODE_IMPORT: symbol(symbols, n->import_stmt.alias, "module", n, scope); break;
        default: break;
    }
#undef W
    scope_end=previous_end;
}

Value hajimu_analyze_source(const char *source, const char *filename) {
    Value result = value_dict(), diagnostics = value_array(), symbols = value_array(), tokens = value_array(), lexemes = value_array();
    diag_set_sink(capture, &diagnostics);
    Parser parser;
    parser_init(&parser, source, filename);
    ASTNode *program = parse_program(&parser);
    scope_end=last_line(program);
    walk(program, &symbols, 0);
    bool valid = !parser_had_error(&parser);
    node_free(program);
    parser_free(&parser);
    diag_set_sink(NULL, NULL);

    Lexer lexer;
    lexer_init(&lexer, source, filename);
    Token token;
    TokenType previous_type=TOKEN_EOF;
    do {
        token = lexer_next(&lexer);
        if (token.type != TOKEN_EOF && token.type != TOKEN_NEWLINE && token.type != TOKEN_INDENT && token.type != TOKEN_DEDENT) {
            array_push(&lexemes, value_string_n(token.start, token.length));
        }
        if (token.type == TOKEN_IDENTIFIER) {
            Value item = value_dict();
            put(&item, "name", value_string_n(token.start, token.length));
            put(&item, "line", value_number(token.line));
            put(&item, "column", value_number(token.column));
            put(&item, "member", value_bool(previous_type==TOKEN_DOT));
            array_push(&tokens, item);
        }
        if (token.type == TOKEN_STRING) free(token.value.string);
        previous_type=token.type;
    } while (token.type != TOKEN_EOF);
    put(&result, "valid", value_bool(valid));
    put(&result, "diagnostics", diagnostics);
    put(&result, "symbols", symbols);
    put(&result, "identifiers", tokens);
    put(&result, "lexemes", lexemes);
    put(&result, "builtins", evaluator_builtin_names());
    return result;
}

int hajimu_analyze_stdin(const char *filename) {
    size_t length = 0, capacity = 4096;
    char *source = malloc(capacity);
    if (!source) return 1;
    int ch;
    while ((ch = getchar()) != EOF) {
        if (length + 1 >= capacity) {
            if (capacity >= 16 * 1024 * 1024) { free(source); return 1; }
            capacity *= 2;
            char *next = realloc(source, capacity);
            if (!next) { free(source); return 1; }
            source = next;
        }
        source[length++] = (char)ch;
    }
    source[length] = '\0';
    Value result = hajimu_analyze_source(source, filename);
    Value encoded = json_encode(result);
    puts(encoded.string.data);
    value_free(&encoded); value_free(&result); free(source);
    return 0;
}
