#include "parser.h"
#include "diag.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
static void discard(DiagKind k,int l,int c,int e,const char *m,void *p) {
    (void)k;(void)l;(void)c;(void)e;(void)m;(void)p;
}
int LLVMFuzzerTestOneInput(const uint8_t *data,size_t size) {
    if(size>65536 || memchr(data,0,size)) return 0;
    char *source=malloc(size+1);
    if(!source) return 0;
    memcpy(source,data,size);source[size]=0;
    diag_set_sink(discard,NULL);
    Parser parser;parser_init(&parser,source,"<fuzz>");
    ASTNode *tree=parse_program(&parser);
    node_free(tree);parser_free(&parser);free(source);
    diag_set_sink(NULL,NULL);
    return 0;
}
