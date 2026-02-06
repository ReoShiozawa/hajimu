/**
 * 日本語プログラミング言語 - メインプログラム
 * 
 * エントリポイントとREPL
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>

#include "lexer.h"
#include "parser.h"
#include "ast.h"
#include "evaluator.h"

// =============================================================================
// バージョン情報
// =============================================================================

#define VERSION "0.1.0"
#define AUTHOR "Reo Shiozawa"

// =============================================================================
// ファイル読み込み
// =============================================================================

static char *read_file(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "エラー: ファイルを開けません: %s\n", path);
        return NULL;
    }
    
    // ファイルサイズを取得
    fseek(file, 0L, SEEK_END);
    size_t file_size = ftell(file);
    rewind(file);
    
    // バッファを確保
    char *buffer = malloc(file_size + 1);
    if (buffer == NULL) {
        fprintf(stderr, "エラー: メモリを確保できません\n");
        fclose(file);
        return NULL;
    }
    
    // ファイルを読み込む
    size_t bytes_read = fread(buffer, 1, file_size, file);
    if (bytes_read < file_size) {
        fprintf(stderr, "エラー: ファイルを読み込めません: %s\n", path);
        free(buffer);
        fclose(file);
        return NULL;
    }
    
    buffer[bytes_read] = '\0';
    fclose(file);
    
    return buffer;
}

// =============================================================================
// ファイル実行
// =============================================================================

static int run_file(const char *path, bool debug_mode, int script_argc, char **script_argv) {
    char *source = read_file(path);
    if (source == NULL) {
        return 1;
    }
    
    // パース
    Parser parser;
    parser_init(&parser, source, path);
    
    ASTNode *program = parse_program(&parser);
    
    if (parser_had_error(&parser)) {
        parser_free(&parser);
        node_free(program);
        free(source);
        return 1;
    }
    
    // デバッグ: ASTを表示
    #ifdef DEBUG
    printf("=== AST ===\n");
    ast_print(program, 0);
    printf("===========\n\n");
    #endif
    
    // 実行
    Evaluator *eval = evaluator_new();
    
    // コマンドライン引数を「引数」変数として設定
    Value args_array = value_array_with_capacity(script_argc > 0 ? script_argc : 1);
    for (int i = 0; i < script_argc; i++) {
        array_push(&args_array, value_string(script_argv[i]));
    }
    env_define(eval->global, "引数", args_array, true);
    
    // デバッグモードを設定
    if (debug_mode) {
        evaluator_set_debug_mode(eval, true);
        printf("=== デバッグモード ===\n");
        printf("Enter: 次のステップ / 'v': 変数表示 / 'c': 継続実行\n\n");
    }
    
    Value result = evaluator_run(eval, program);
    (void)result;  // 結果は使用しない
    
    int exit_code = evaluator_had_error(eval) ? 1 : 0;
    
    // クリーンアップ
    evaluator_free(eval);
    parser_free(&parser);
    node_free(program);
    free(source);
    
    return exit_code;
}

// =============================================================================
// REPL
// =============================================================================

static void run_repl(void) {
    printf("日本語プログラミング言語 v%s\n", VERSION);
    printf("作者: %s\n", AUTHOR);
    printf("終了するには「終了」と入力してください。\n\n");
    
    Evaluator *eval = evaluator_new();
    char line[4096];
    
    while (true) {
        printf(">>> ");
        fflush(stdout);
        
        if (fgets(line, sizeof(line), stdin) == NULL) {
            printf("\n");
            break;
        }
        
        // 改行を削除
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
            len--;
        }
        
        // 空行はスキップ
        if (len == 0) continue;
        
        // 終了コマンド
        if (strcmp(line, "終了") == 0 || 
            strcmp(line, "exit") == 0 ||
            strcmp(line, "quit") == 0) {
            break;
        }
        
        // ヘルプコマンド
        if (strcmp(line, "ヘルプ") == 0 || 
            strcmp(line, "help") == 0) {
            printf("\n使用可能なコマンド:\n");
            printf("  終了, exit, quit - REPLを終了\n");
            printf("  ヘルプ, help     - このヘルプを表示\n");
            printf("  クリア, clear    - 画面をクリア\n");
            printf("\n");
            continue;
        }
        
        // クリアコマンド
        if (strcmp(line, "クリア") == 0 || 
            strcmp(line, "clear") == 0) {
            printf("\033[2J\033[H");  // ANSIエスケープ
            continue;
        }
        
        // パースと実行
        Parser parser;
        parser_init(&parser, line, "<repl>");
        
        // 式として評価を試みる
        ASTNode *expr = parse_expression(&parser);
        
        if (!parser_had_error(&parser)) {
            // 式文としてラップ
            ASTNode *program = node_program(1, 1);
            ASTNode *stmt = node_expr_stmt(expr, 1, 1);
            block_add_statement(program, stmt);
            
            evaluator_clear_error(eval);
            Value result = evaluator_run(eval, program);
            
            if (!evaluator_had_error(eval) && result.type != VALUE_NULL) {
                printf("=> ");
                value_print(result);
                printf("\n");
            }
            
            node_free(program);
        } else {
            // 文として再パース
            parser_free(&parser);
            parser_init(&parser, line, "<repl>");
            
            ASTNode *program = parse_program(&parser);
            
            if (!parser_had_error(&parser)) {
                evaluator_clear_error(eval);
                evaluator_run(eval, program);
            }
            
            node_free(program);
        }
        
        parser_free(&parser);
    }
    
    evaluator_free(eval);
    printf("さようなら！\n");
}

// =============================================================================
// 使用方法の表示
// =============================================================================

static void print_usage(const char *program_name) {
    printf("使用方法: %s [オプション] [ファイル]\n", program_name);
    printf("\n");
    printf("オプション:\n");
    printf("  -h, --help     このヘルプを表示\n");
    printf("  -v, --version  バージョン情報を表示\n");
    printf("  -d, --debug    デバッグモードで実行\n");
    printf("  -t, --tokens   トークンを表示\n");
    printf("  -a, --ast      ASTを表示\n");
    printf("\n");
    printf("ファイルを指定しない場合、REPLモードで起動します。\n");
}

static void print_version(void) {
    printf("日本語プログラミング言語 v%s\n", VERSION);
    printf("作者: %s\n", AUTHOR);
}

// =============================================================================
// トークン表示
// =============================================================================

static void show_tokens(const char *source, const char *filename) {
    Lexer lexer;
    lexer_init(&lexer, source, filename);
    
    printf("=== トークン ===\n");
    
    Token token;
    while ((token = lexer_next(&lexer)).type != TOKEN_EOF) {
        char buffer[256];
        token_to_string(token, buffer, sizeof(buffer));
        printf("[%3d:%2d] %s\n", token.line, token.column, buffer);
        
        if (token.type == TOKEN_ERROR) break;
    }
    
    printf("================\n");
}

// =============================================================================
// AST表示
// =============================================================================

static void show_ast(const char *source, const char *filename) {
    Parser parser;
    parser_init(&parser, source, filename);
    
    ASTNode *program = parse_program(&parser);
    
    if (!parser_had_error(&parser)) {
        printf("=== AST ===\n");
        ast_print(program, 0);
        printf("===========\n");
    }
    
    node_free(program);
    parser_free(&parser);
}

// =============================================================================
// メイン関数
// =============================================================================

int main(int argc, char *argv[]) {
    // ロケール設定（日本語出力のため）
    setlocale(LC_ALL, "");
    
    // オプション
    bool show_help = false;
    bool show_ver = false;
    bool debug_mode = false;
    bool show_tok = false;
    bool show_tree = false;
    const char *filename = NULL;
    
    // 引数解析
    int filename_index = -1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            show_help = true;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            show_ver = true;
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--debug") == 0) {
            debug_mode = true;
        } else if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--tokens") == 0) {
            show_tok = true;
        } else if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--ast") == 0) {
            show_tree = true;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "未知のオプション: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        } else {
            filename = argv[i];
            filename_index = i;
            break;  // ファイル名以降はスクリプト引数として扱う
        }
    }
    
    // ヘルプ表示
    if (show_help) {
        print_usage(argv[0]);
        return 0;
    }
    
    // バージョン表示
    if (show_ver) {
        print_version();
        return 0;
    }
    
    // ファイル指定がある場合
    if (filename != NULL) {
        char *source = read_file(filename);
        if (source == NULL) {
            return 1;
        }
        
        // トークン表示
        if (show_tok) {
            show_tokens(source, filename);
            if (!show_tree && !debug_mode) {
                free(source);
                return 0;
            }
        }
        
        // AST表示
        if (show_tree) {
            show_ast(source, filename);
            if (!debug_mode) {
                free(source);
                return 0;
            }
        }
        
        free(source);
        
        // 実行
        int script_argc = 0;
        char **script_argv = NULL;
        if (filename_index >= 0 && filename_index + 1 < argc) {
            script_argc = argc - filename_index - 1;
            script_argv = &argv[filename_index + 1];
        }
        return run_file(filename, debug_mode, script_argc, script_argv);
    }
    
    // REPLモード
    run_repl();
    return 0;
}
