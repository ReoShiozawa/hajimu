#include <emscripten/emscripten.h>
#include <stdbool.h>
#include <stdlib.h>

#include "ast.h"
#include "evaluator.h"
#include "parser.h"

#define HAJIMU_WASM_VERSION "1.4.0"

EMSCRIPTEN_KEEPALIVE
int hajimu_run_source(const char *source) {
    if (source == NULL) return 1;

    Parser parser;
    parser_init(&parser, source, "<browser>");
    ASTNode *program = parse_program(&parser);

    if (parser_had_error(&parser)) {
        parser_free(&parser);
        node_free(program);
        return 1;
    }

    Evaluator *eval = evaluator_new();
    eval->current_file = "<browser>";
    eval->source_code = source;

    Value result = evaluator_run(eval, program);
    value_free(&result);

    int exit_code = evaluator_had_error(eval) ? 1 : 0;
    evaluator_free(eval);
    parser_free(&parser);
    node_free(program);

    return exit_code;
}

EMSCRIPTEN_KEEPALIVE
const char *hajimu_version(void) {
    return HAJIMU_WASM_VERSION;
}
