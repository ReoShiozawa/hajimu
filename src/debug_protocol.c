#include "debug_protocol.h"
#include "parser.h"
#include "http.h"
#include "diag.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* アダプターとの専用JSON行通信。通常のプログラム出力はstdoutへ残す。 */
static void emit(Value event) {
    Value json = json_encode(event);
    fprintf(stderr, "@hajimu:%s\n", json.string.data);
    fflush(stderr);
    value_free(&json); value_free(&event);
}
static Value get(Value *v, const char *key) { return dict_get(v, key); }
static const char *str(Value v) { return v.type == VALUE_STRING ? v.string.data : ""; }
static void put(Value *v, const char *key, Value x) { dict_set(v, key, x); }
static Value breakpoints;
static int mode, target_depth;
static bool inspecting;

/* 停止中の評価で関数呼出し・代入・importを許可しない。 */
static bool pure(ASTNode *n) {
    if (!n) return false;
    switch(n->type) {
        case NODE_STRING: return !strchr(n->string_value,'{');
        case NODE_NUMBER: case NODE_BOOL: case NODE_NULL: case NODE_IDENTIFIER: return true;
        case NODE_BINARY: return pure(n->binary.left) && pure(n->binary.right);
        case NODE_UNARY: return pure(n->unary.operand);
        case NODE_INDEX: return pure(n->index.array) && pure(n->index.index);
        default: return false;
    }
}
static void discard_diagnostic(DiagKind k, int l, int c, int e, const char *m, void *p) {
    (void)k; (void)l; (void)c; (void)e; (void)m; (void)p;
}
static Value inspect_expression(Evaluator *eval, const char *text, bool *ok) {
    diag_set_sink(discard_diagnostic, NULL);
    Parser parser; parser_init(&parser, text, "<debug>");
    ASTNode *expr = parse_expression(&parser);
    *ok = expr && !parser_had_error(&parser) && parser.current.type == TOKEN_EOF && pure(expr);
    bool old_error = eval->had_error;
    bool old_throwing = eval->throwing;
    char old_message[sizeof(eval->error_message)]; memcpy(old_message, eval->error_message, sizeof(old_message));
    int old_line=eval->error_line, old_column=eval->error_column;
    inspecting = true;
    Value result = *ok ? evaluate(eval, expr) : value_null();
    *ok = *ok && !eval->had_error;
    inspecting = false;
    eval->had_error=old_error; eval->throwing=old_throwing;
    memcpy(eval->error_message, old_message, sizeof(old_message));
    eval->error_line=old_line; eval->error_column=old_column;
    node_free(expr); parser_free(&parser); diag_set_sink(NULL, NULL);
    return result;
}
static Value variables(Environment *env) {
    Value result=value_array();
    if (!env) return result;
    for (int i=0;i<ENV_HASH_SIZE;i++) for (EnvEntry *p=env->table[i];p;p=p->next) {
        if (p->value.type == VALUE_BUILTIN) continue;
        Value item=value_dict(); char *description=value_to_string(p->value);
        put(&item,"name",value_string(p->name));
        put(&item,"value",value_string(description ? description : ""));
        put(&item,"type",value_number(p->value.type));
        array_push(&result,item); free(description);
    }
    return result;
}
static void snapshot(Evaluator *eval, ASTNode *node, const char *reason) {
    Value event=value_dict(), frames=value_array(), scopes=value_array();
    put(&event,"event",value_string("stopped")); put(&event,"reason",value_string(reason));
    Value frame=value_dict();
    put(&frame,"name",value_string(eval->call_stack_depth ? eval->call_stack[eval->call_stack_depth-1].func_name : "<main>"));
    put(&frame,"line",value_number(node->location.line)); put(&frame,"file",value_string(eval->current_file ? eval->current_file : ""));
    array_push(&frames,frame); array_push(&scopes,variables(eval->current));
    for (int i=eval->call_stack_depth-1;i>=0;i--) {
        frame=value_dict();
        put(&frame,"name",value_string(i ? eval->call_stack[i-1].func_name : "<main>"));
        put(&frame,"line",value_number(eval->call_stack[i].line));
        put(&frame,"file",value_string(eval->call_stack[i].file ? eval->call_stack[i].file : ""));
        array_push(&frames,frame); array_push(&scopes,variables(eval->call_stack[i].environment));
    }
    put(&event,"frames",frames); put(&event,"locals",scopes); put(&event,"globals",variables(eval->global)); emit(event);
}
static bool read_command(Value *command) {
    char *line=malloc(1024*1024);
    if (!line) return false;
    bool ok=fgets(line,1024*1024,stdin) && strchr(line,'\n') && json_decode_checked(line,(int)strlen(line),command);
    free(line); return ok;
}
static bool commands(Evaluator *eval) {
    Value command;
    while (read_command(&command)) {
        const char *name=str(get(&command,"command"));
        if (strcmp(name,"breakpoints")==0) {
            value_free(&breakpoints); breakpoints=value_copy(get(&command,"breakpoints"));
        } else if (strcmp(name,"evaluate")==0) {
            bool ok; Value result=inspect_expression(eval,str(get(&command,"expression")),&ok);
            Value event=value_dict(); char *description=ok ? value_to_string(result) : NULL;
            put(&event,"event",value_string("evaluation")); put(&event,"id",value_copy(get(&command,"id")));
            put(&event,"ok",value_bool(ok)); put(&event,"value",value_string(description ? description : "式を評価できません。関数呼出しや代入は許可されません。"));
            free(description); emit(event);
        } else if (strcmp(name,"continue")==0 || strcmp(name,"step")==0 || strcmp(name,"next")==0 || strcmp(name,"out")==0) {
            mode=strcmp(name,"continue")==0 ? 0 : strcmp(name,"step")==0 ? 1 : strcmp(name,"next")==0 ? 2 : 3;
            target_depth=eval->call_stack_depth; value_free(&command); return true;
        } else if (strcmp(name,"disconnect")==0) {
            eval->had_error=true; value_free(&command); return false;
        }
        value_free(&command);
    }
    eval->had_error=true; return false;
}
static void on_statement(void *context, void *pointer, ASTNode *node) {
    (void)context;
    if (inspecting) return;
    Evaluator *eval=pointer;
    bool stop=mode==1 || (mode==2 && eval->call_stack_depth<=target_depth) || (mode==3 && eval->call_stack_depth<target_depth);
    const char *reason="step";
    if (breakpoints.type==VALUE_ARRAY) for (int i=0;i<breakpoints.array.length;i++) {
        Value *bp=&breakpoints.array.elements[i];
        if (get(bp,"line").number!=node->location.line || strcmp(str(get(bp,"file")),eval->current_file ? eval->current_file : "")!=0) continue;
        const char *condition=str(get(bp,"condition")); bool ok=true;
        Value result=condition[0] ? inspect_expression(eval,condition,&ok) : value_bool(true);
        if (ok && value_is_truthy(result)) { stop=true; reason="breakpoint"; break; }
    }
    if (stop) { snapshot(eval,node,reason); commands(eval); }
}
void hajimu_debug_attach(Evaluator *eval) {
    breakpoints=value_array(); mode=0; inspecting=false;
    eval->debug_hook=on_statement;
    Value ready=value_dict(); put(&ready,"event",value_string("ready")); emit(ready);
    commands(eval);
}
