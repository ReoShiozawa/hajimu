/**
 * 日本語プログラミング言語 - 抽象構文木（AST）実装
 */

#include "ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// =============================================================================
// ノード種別名
// =============================================================================

static const char *node_type_names[] = {
    [NODE_PROGRAM] = "PROGRAM",
    [NODE_FUNCTION_DEF] = "FUNCTION_DEF",
    [NODE_BLOCK] = "BLOCK",
    [NODE_VAR_DECL] = "VAR_DECL",
    [NODE_ASSIGN] = "ASSIGN",
    [NODE_IF] = "IF",
    [NODE_WHILE] = "WHILE",
    [NODE_FOR] = "FOR",
    [NODE_RETURN] = "RETURN",
    [NODE_BREAK] = "BREAK",
    [NODE_CONTINUE] = "CONTINUE",
    [NODE_EXPR_STMT] = "EXPR_STMT",
    [NODE_BINARY] = "BINARY",
    [NODE_UNARY] = "UNARY",
    [NODE_CALL] = "CALL",
    [NODE_INDEX] = "INDEX",
    [NODE_MEMBER] = "MEMBER",
    [NODE_IDENTIFIER] = "IDENTIFIER",
    [NODE_NUMBER] = "NUMBER",
    [NODE_STRING] = "STRING",
    [NODE_BOOL] = "BOOL",
    [NODE_ARRAY] = "ARRAY",
    [NODE_NULL] = "NULL",
};

// =============================================================================
// ノード作成関数
// =============================================================================

ASTNode *node_new(NodeType type, int line, int column) {
    ASTNode *node = calloc(1, sizeof(ASTNode));
    node->type = type;
    node->location.line = line;
    node->location.column = column;
    node->location.filename = NULL;
    return node;
}

ASTNode *node_number(double value, int line, int column) {
    ASTNode *node = node_new(NODE_NUMBER, line, column);
    node->number_value = value;
    return node;
}

ASTNode *node_string(const char *value, int line, int column) {
    ASTNode *node = node_new(NODE_STRING, line, column);
    node->string_value = strdup(value);
    return node;
}

ASTNode *node_bool(bool value, int line, int column) {
    ASTNode *node = node_new(NODE_BOOL, line, column);
    node->bool_value = value;
    return node;
}

ASTNode *node_identifier(const char *name, int line, int column) {
    ASTNode *node = node_new(NODE_IDENTIFIER, line, column);
    node->string_value = strdup(name);
    return node;
}

ASTNode *node_null(int line, int column) {
    return node_new(NODE_NULL, line, column);
}

ASTNode *node_binary(TokenType op, ASTNode *left, ASTNode *right, int line, int column) {
    ASTNode *node = node_new(NODE_BINARY, line, column);
    node->binary.operator = op;
    node->binary.left = left;
    node->binary.right = right;
    return node;
}

ASTNode *node_unary(TokenType op, ASTNode *operand, int line, int column) {
    ASTNode *node = node_new(NODE_UNARY, line, column);
    node->unary.operator = op;
    node->unary.operand = operand;
    return node;
}

ASTNode *node_call(ASTNode *callee, ASTNode **args, int arg_count, int line, int column) {
    ASTNode *node = node_new(NODE_CALL, line, column);
    node->call.callee = callee;
    node->call.arguments = args;
    node->call.arg_count = arg_count;
    return node;
}

ASTNode *node_index(ASTNode *array, ASTNode *index, int line, int column) {
    ASTNode *node = node_new(NODE_INDEX, line, column);
    node->index.array = array;
    node->index.index = index;
    return node;
}

ASTNode *node_array(ASTNode **elements, int count, int line, int column) {
    ASTNode *node = node_new(NODE_ARRAY, line, column);
    node->block.statements = elements;
    node->block.count = count;
    node->block.capacity = count;
    return node;
}

ASTNode *node_function_def(const char *name, Parameter *params, int param_count,
                           ValueType return_type, bool has_return_type,
                           ASTNode *body, int line, int column) {
    ASTNode *node = node_new(NODE_FUNCTION_DEF, line, column);
    node->function.name = strdup(name);
    node->function.params = params;
    node->function.param_count = param_count;
    node->function.return_type = return_type;
    node->function.has_return_type = has_return_type;
    node->function.body = body;
    return node;
}

ASTNode *node_var_decl(const char *name, ASTNode *initializer, bool is_const,
                       int line, int column) {
    ASTNode *node = node_new(NODE_VAR_DECL, line, column);
    node->var_decl.name = strdup(name);
    node->var_decl.initializer = initializer;
    node->var_decl.is_const = is_const;
    return node;
}

ASTNode *node_assign(ASTNode *target, TokenType op, ASTNode *value, int line, int column) {
    ASTNode *node = node_new(NODE_ASSIGN, line, column);
    node->assign.target = target;
    node->assign.operator = op;
    node->assign.value = value;
    return node;
}

ASTNode *node_if(ASTNode *condition, ASTNode *then_branch, ASTNode *else_branch,
                 int line, int column) {
    ASTNode *node = node_new(NODE_IF, line, column);
    node->if_stmt.condition = condition;
    node->if_stmt.then_branch = then_branch;
    node->if_stmt.else_branch = else_branch;
    return node;
}

ASTNode *node_while(ASTNode *condition, ASTNode *body, int line, int column) {
    ASTNode *node = node_new(NODE_WHILE, line, column);
    node->while_stmt.condition = condition;
    node->while_stmt.body = body;
    return node;
}

ASTNode *node_for(const char *var_name, ASTNode *start, ASTNode *end,
                  ASTNode *step, ASTNode *body, int line, int column) {
    ASTNode *node = node_new(NODE_FOR, line, column);
    node->for_stmt.var_name = strdup(var_name);
    node->for_stmt.start = start;
    node->for_stmt.end = end;
    node->for_stmt.step = step;
    node->for_stmt.body = body;
    return node;
}

ASTNode *node_return(ASTNode *value, int line, int column) {
    ASTNode *node = node_new(NODE_RETURN, line, column);
    node->return_stmt.value = value;
    return node;
}

ASTNode *node_break(int line, int column) {
    return node_new(NODE_BREAK, line, column);
}

ASTNode *node_continue(int line, int column) {
    return node_new(NODE_CONTINUE, line, column);
}

ASTNode *node_expr_stmt(ASTNode *expression, int line, int column) {
    ASTNode *node = node_new(NODE_EXPR_STMT, line, column);
    node->expr_stmt.expression = expression;
    return node;
}

ASTNode *node_block(int line, int column) {
    ASTNode *node = node_new(NODE_BLOCK, line, column);
    node->block.statements = NULL;
    node->block.count = 0;
    node->block.capacity = 0;
    return node;
}

ASTNode *node_program(int line, int column) {
    ASTNode *node = node_new(NODE_PROGRAM, line, column);
    node->block.statements = NULL;
    node->block.count = 0;
    node->block.capacity = 0;
    return node;
}

// =============================================================================
// ブロック操作
// =============================================================================

void block_add_statement(ASTNode *block, ASTNode *stmt) {
    if (block == NULL || stmt == NULL) return;
    if (block->type != NODE_BLOCK && block->type != NODE_PROGRAM && 
        block->type != NODE_ARRAY) return;
    
    // 容量が足りなければ拡張
    if (block->block.count >= block->block.capacity) {
        int new_capacity = block->block.capacity == 0 ? 8 : block->block.capacity * 2;
        block->block.statements = realloc(
            block->block.statements,
            sizeof(ASTNode *) * new_capacity
        );
        block->block.capacity = new_capacity;
    }
    
    block->block.statements[block->block.count++] = stmt;
}

// =============================================================================
// メモリ管理
// =============================================================================

void params_free(Parameter *params, int count) {
    if (params == NULL) return;
    
    for (int i = 0; i < count; i++) {
        free(params[i].name);
    }
    free(params);
}

void node_free(ASTNode *node) {
    if (node == NULL) return;
    
    switch (node->type) {
        case NODE_NUMBER:
        case NODE_BOOL:
        case NODE_NULL:
        case NODE_BREAK:
        case NODE_CONTINUE:
            // 子ノードなし
            break;
            
        case NODE_STRING:
        case NODE_IDENTIFIER:
            free(node->string_value);
            break;
            
        case NODE_BINARY:
            node_free(node->binary.left);
            node_free(node->binary.right);
            break;
            
        case NODE_UNARY:
            node_free(node->unary.operand);
            break;
            
        case NODE_CALL:
            node_free(node->call.callee);
            for (int i = 0; i < node->call.arg_count; i++) {
                node_free(node->call.arguments[i]);
            }
            free(node->call.arguments);
            break;
            
        case NODE_INDEX:
            node_free(node->index.array);
            node_free(node->index.index);
            break;
            
        case NODE_MEMBER:
            node_free(node->member.object);
            free(node->member.member_name);
            break;
            
        case NODE_FUNCTION_DEF:
            free(node->function.name);
            params_free(node->function.params, node->function.param_count);
            node_free(node->function.body);
            break;
            
        case NODE_VAR_DECL:
            free(node->var_decl.name);
            node_free(node->var_decl.initializer);
            break;
            
        case NODE_ASSIGN:
            node_free(node->assign.target);
            node_free(node->assign.value);
            break;
            
        case NODE_IF:
            node_free(node->if_stmt.condition);
            node_free(node->if_stmt.then_branch);
            node_free(node->if_stmt.else_branch);
            break;
            
        case NODE_WHILE:
            node_free(node->while_stmt.condition);
            node_free(node->while_stmt.body);
            break;
            
        case NODE_FOR:
            free(node->for_stmt.var_name);
            node_free(node->for_stmt.start);
            node_free(node->for_stmt.end);
            node_free(node->for_stmt.step);
            node_free(node->for_stmt.body);
            break;
            
        case NODE_RETURN:
            node_free(node->return_stmt.value);
            break;
            
        case NODE_EXPR_STMT:
            node_free(node->expr_stmt.expression);
            break;
            
        case NODE_PROGRAM:
        case NODE_BLOCK:
        case NODE_ARRAY:
            for (int i = 0; i < node->block.count; i++) {
                node_free(node->block.statements[i]);
            }
            free(node->block.statements);
            break;
            
        default:
            break;
    }
    
    free(node);
}

// =============================================================================
// デバッグ
// =============================================================================

const char *node_type_name(NodeType type) {
    if (type >= 0 && type < NODE_TYPE_COUNT) {
        return node_type_names[type];
    }
    return "UNKNOWN";
}

static void print_indent(int indent) {
    for (int i = 0; i < indent; i++) {
        printf("  ");
    }
}

void ast_print(ASTNode *node, int indent) {
    if (node == NULL) {
        print_indent(indent);
        printf("(null)\n");
        return;
    }
    
    print_indent(indent);
    
    switch (node->type) {
        case NODE_PROGRAM:
            printf("Program:\n");
            for (int i = 0; i < node->block.count; i++) {
                ast_print(node->block.statements[i], indent + 1);
            }
            break;
            
        case NODE_FUNCTION_DEF:
            printf("FunctionDef: %s(", node->function.name);
            for (int i = 0; i < node->function.param_count; i++) {
                if (i > 0) printf(", ");
                printf("%s", node->function.params[i].name);
            }
            printf(")\n");
            ast_print(node->function.body, indent + 1);
            break;
            
        case NODE_BLOCK:
            printf("Block:\n");
            for (int i = 0; i < node->block.count; i++) {
                ast_print(node->block.statements[i], indent + 1);
            }
            break;
            
        case NODE_VAR_DECL:
            printf("VarDecl: %s%s =\n", 
                   node->var_decl.is_const ? "定数 " : "変数 ",
                   node->var_decl.name);
            ast_print(node->var_decl.initializer, indent + 1);
            break;
            
        case NODE_ASSIGN:
            printf("Assign: %s\n", token_type_name(node->assign.operator));
            print_indent(indent + 1);
            printf("target:\n");
            ast_print(node->assign.target, indent + 2);
            print_indent(indent + 1);
            printf("value:\n");
            ast_print(node->assign.value, indent + 2);
            break;
            
        case NODE_IF:
            printf("If:\n");
            print_indent(indent + 1);
            printf("condition:\n");
            ast_print(node->if_stmt.condition, indent + 2);
            print_indent(indent + 1);
            printf("then:\n");
            ast_print(node->if_stmt.then_branch, indent + 2);
            if (node->if_stmt.else_branch) {
                print_indent(indent + 1);
                printf("else:\n");
                ast_print(node->if_stmt.else_branch, indent + 2);
            }
            break;
            
        case NODE_WHILE:
            printf("While:\n");
            print_indent(indent + 1);
            printf("condition:\n");
            ast_print(node->while_stmt.condition, indent + 2);
            print_indent(indent + 1);
            printf("body:\n");
            ast_print(node->while_stmt.body, indent + 2);
            break;
            
        case NODE_FOR:
            printf("For: %s\n", node->for_stmt.var_name);
            print_indent(indent + 1);
            printf("from:\n");
            ast_print(node->for_stmt.start, indent + 2);
            print_indent(indent + 1);
            printf("to:\n");
            ast_print(node->for_stmt.end, indent + 2);
            if (node->for_stmt.step) {
                print_indent(indent + 1);
                printf("step:\n");
                ast_print(node->for_stmt.step, indent + 2);
            }
            print_indent(indent + 1);
            printf("body:\n");
            ast_print(node->for_stmt.body, indent + 2);
            break;
            
        case NODE_RETURN:
            printf("Return:\n");
            if (node->return_stmt.value) {
                ast_print(node->return_stmt.value, indent + 1);
            }
            break;
            
        case NODE_BREAK:
            printf("Break\n");
            break;
            
        case NODE_CONTINUE:
            printf("Continue\n");
            break;
            
        case NODE_EXPR_STMT:
            printf("ExprStmt:\n");
            ast_print(node->expr_stmt.expression, indent + 1);
            break;
            
        case NODE_BINARY:
            printf("Binary: %s\n", token_type_name(node->binary.operator));
            ast_print(node->binary.left, indent + 1);
            ast_print(node->binary.right, indent + 1);
            break;
            
        case NODE_UNARY:
            printf("Unary: %s\n", token_type_name(node->unary.operator));
            ast_print(node->unary.operand, indent + 1);
            break;
            
        case NODE_CALL:
            printf("Call:\n");
            print_indent(indent + 1);
            printf("callee:\n");
            ast_print(node->call.callee, indent + 2);
            print_indent(indent + 1);
            printf("args:\n");
            for (int i = 0; i < node->call.arg_count; i++) {
                ast_print(node->call.arguments[i], indent + 2);
            }
            break;
            
        case NODE_INDEX:
            printf("Index:\n");
            print_indent(indent + 1);
            printf("array:\n");
            ast_print(node->index.array, indent + 2);
            print_indent(indent + 1);
            printf("index:\n");
            ast_print(node->index.index, indent + 2);
            break;
            
        case NODE_IDENTIFIER:
            printf("Identifier: %s\n", node->string_value);
            break;
            
        case NODE_NUMBER:
            printf("Number: %g\n", node->number_value);
            break;
            
        case NODE_STRING:
            printf("String: \"%s\"\n", node->string_value);
            break;
            
        case NODE_BOOL:
            printf("Bool: %s\n", node->bool_value ? "真" : "偽");
            break;
            
        case NODE_ARRAY:
            printf("Array:\n");
            for (int i = 0; i < node->block.count; i++) {
                ast_print(node->block.statements[i], indent + 1);
            }
            break;
            
        case NODE_NULL:
            printf("Null\n");
            break;
            
        default:
            printf("Unknown node type: %d\n", node->type);
            break;
    }
}

void ast_to_json(ASTNode *node, int indent) {
    if (node == NULL) {
        printf("null");
        return;
    }
    
    printf("{\n");
    print_indent(indent + 1);
    printf("\"type\": \"%s\"", node_type_name(node->type));
    
    printf(",\n");
    print_indent(indent + 1);
    printf("\"line\": %d", node->location.line);
    
    switch (node->type) {
        case NODE_NUMBER:
            printf(",\n");
            print_indent(indent + 1);
            printf("\"value\": %g", node->number_value);
            break;
            
        case NODE_STRING:
        case NODE_IDENTIFIER:
            printf(",\n");
            print_indent(indent + 1);
            printf("\"value\": \"%s\"", node->string_value);
            break;
            
        case NODE_BOOL:
            printf(",\n");
            print_indent(indent + 1);
            printf("\"value\": %s", node->bool_value ? "true" : "false");
            break;
            
        // 他のノードタイプも必要に応じて追加
        default:
            break;
    }
    
    printf("\n");
    print_indent(indent);
    printf("}");
}
