#include "../include/hajimu_embed.h"
#include "tooling.h"
#include "http.h"
#include "evaluator.h"
#include "parser.h"
#include "diag.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

// 解析と実行はGCなどのプロセス共通状態を共有するため、同じ排他で守る。
static atomic_flag runtime_busy=ATOMIC_FLAG_INIT;
static void put(Value *object,const char *key,Value value) {
    dict_set(object,key,value);
    value_free(&value);
}

unsigned int hajimu_embed_abi_version(void) { return HAJIMU_EMBED_ABI_VERSION; }
char *hajimu_embed_analyze(const char *source, const char *filename) {
    if (!source || atomic_flag_test_and_set(&runtime_busy)) return NULL;
    Value analysis=hajimu_analyze_source(source, filename ? filename : "<embed>");
    Value json=json_encode(analysis);
    char *result=json.type==VALUE_STRING ? strdup(json.string.data) : NULL;
    value_free(&json); value_free(&analysis);
    atomic_flag_clear(&runtime_busy);
    return result;
}
void hajimu_embed_free(char *result) { free(result); }

static void capture_runtime(DiagKind kind,int line,int column,int end,const char *message,void *context) {
    (void)end;
    Value item=value_dict();
    put(&item,"message",value_string(message));
    put(&item,"line",value_number(line)); put(&item,"column",value_number(column));
    put(&item,"kind",value_number(kind)); array_push(context,item);value_free(&item);
}
char *hajimu_embed_run(const char *source,const char *filename) {
    if(!source || atomic_flag_test_and_set(&runtime_busy)) return NULL;
    const char *name=filename ? filename : "<embed>";
    Value response=value_dict(),diagnostics=value_array();
    diag_set_sink(capture_runtime,&diagnostics);
    Parser parser;parser_init(&parser,source,name);
    ASTNode *tree=parse_program(&parser);
    bool ok=!parser_had_error(&parser);
    Evaluator *eval=NULL;
    if(ok) {
        eval=evaluator_new(); eval->source_code=source;eval->current_file=name;
        Value value=evaluator_run(eval,tree);
        ok=!evaluator_had_error(eval);
        char *description=value_to_string(value);
        put(&response,"value",value_string(description ? description : ""));free(description);
        value_free(&value);
    }
    diag_set_sink(NULL,NULL);
    put(&response,"ok",value_bool(ok));put(&response,"diagnostics",diagnostics);
    Value json=json_encode(response);
    char *result=json.type==VALUE_STRING ? strdup(json.string.data) : NULL;
    value_free(&json);value_free(&response);
    if(eval) evaluator_free(eval);
    node_free(tree);parser_free(&parser);diag_set_sink(NULL,NULL);
    atomic_flag_clear(&runtime_busy);
    return result;
}
