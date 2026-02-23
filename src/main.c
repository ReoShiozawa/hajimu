/**
 * 日本語プログラミング言語 - メインプログラム
 * 
 * エントリポイントとREPL
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>

/* Windows: コンソール UTF-8 設定を main() 决と履行するための最小ヘッダー。
 * windows.h 内の winnt.h が TokenType を enum 値として定義しており lexer.h の
 * typedef enum と衝突するため、マクロガードで保護してからインクルードする。 */
#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  define TokenType _winnt_TokenType_collision_guard_
#  include <windows.h>   /* SetConsoleOutputCP, SetConsoleMode, GetStdHandle */
#  undef TokenType
#endif
#include "lexer.h"
#include "parser.h"
#include "ast.h"
#include "evaluator.h"
#include "package.h"

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
    
    // 現在のファイルパスを設定（インポートの相対パス解決用）
    eval->current_file = path;
    
    // ソースコードを設定（エラー表示の行テキスト参照用）
    eval->source_code = source;
    
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

// REPL ヒストリ
#define REPL_HISTORY_MAX 100
static char *repl_history[REPL_HISTORY_MAX];
static int repl_history_count = 0;

static void repl_history_add(const char *line) {
    if (repl_history_count > 0 && 
        strcmp(repl_history[repl_history_count - 1], line) == 0) {
        return;  // 直前と同じならスキップ
    }
    if (repl_history_count >= REPL_HISTORY_MAX) {
        free(repl_history[0]);
        memmove(repl_history, repl_history + 1, (REPL_HISTORY_MAX - 1) * sizeof(char *));
        repl_history_count--;
    }
    repl_history[repl_history_count++] = strdup(line);
}

static void repl_history_free(void) {
    for (int i = 0; i < repl_history_count; i++) {
        free(repl_history[i]);
    }
    repl_history_count = 0;
}

// 行が複数行ブロックの開始かどうかを判定
static bool needs_continuation(const char *line) {
    // ブロック開始キーワードの出現をカウント
    const char *keywords[] = {
        "関数 ", "もし ", "それ以外", "繰り返す",
        "条件 ", "各 ", "試行:", "型 ", "列挙 ",
        "照合 ", "生成関数 ", NULL
    };
    const char *end_keyword = "終わり";
    
    int open_count = 0;
    int close_count = 0;
    
    // 各行をチェック
    const char *p = line;
    while (*p) {
        // 終わりをカウント
        if (strncmp(p, end_keyword, strlen(end_keyword)) == 0) {
            close_count++;
        }
        
        // 開始キーワードをカウント
        for (int i = 0; keywords[i] != NULL; i++) {
            if (strncmp(p, keywords[i], strlen(keywords[i])) == 0) {
                open_count++;
                break;
            }
        }
        
        // 次の行に進む
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
    
    return open_count > close_count;
}

static void run_repl(void) {
    printf("日本語プログラミング言語 v%s\n", VERSION);
    printf("作者: %s\n", AUTHOR);
    printf("終了するには「終了」と入力してください。\n");
    printf("複数行入力: 「関数」「もし」等の後、「終わり」まで継続入力\n\n");
    
    Evaluator *eval = evaluator_new();
    char line[4096];
    char multiline_buffer[16384];
    bool in_multiline = false;
    
    while (true) {
        if (in_multiline) {
            printf("... ");
        } else {
            printf(">>> ");
        }
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
        
        // 空行はスキップ（複数行中でなければ）
        if (len == 0 && !in_multiline) continue;
        
        // 終了コマンド（複数行中でなければ）
        if (!in_multiline && 
            (strcmp(line, "終了") == 0 || 
             strcmp(line, "exit") == 0 ||
             strcmp(line, "quit") == 0)) {
            break;
        }
        
        // ヘルプコマンド
        if (!in_multiline && 
            (strcmp(line, "ヘルプ") == 0 || strcmp(line, "help") == 0)) {
            printf("\n使用可能なコマンド:\n");
            printf("  終了, exit, quit  - REPLを終了\n");
            printf("  ヘルプ, help      - このヘルプを表示\n");
            printf("  クリア, clear     - 画面をクリア\n");
            printf("  履歴, history     - 入力履歴を表示\n");
            printf("\n複数行入力:\n");
            printf("  「関数」「もし」等のブロック開始で自動的に複数行モードに入ります。\n");
            printf("  「終わり」で対応するブロックを閉じると実行されます。\n");
            printf("\n");
            continue;
        }
        
        // クリアコマンド
        if (!in_multiline && 
            (strcmp(line, "クリア") == 0 || strcmp(line, "clear") == 0)) {
            printf("\033[2J\033[H");
            continue;
        }
        
        // 履歴表示
        if (!in_multiline && 
            (strcmp(line, "履歴") == 0 || strcmp(line, "history") == 0)) {
            printf("\n入力履歴:\n");
            for (int i = 0; i < repl_history_count; i++) {
                printf("  %d: %s\n", i + 1, repl_history[i]);
            }
            printf("\n");
            continue;
        }
        
        // 複数行バッファに追加
        if (in_multiline) {
            size_t buf_len = strlen(multiline_buffer);
            snprintf(multiline_buffer + buf_len, sizeof(multiline_buffer) - buf_len, "\n%s", line);
        } else {
            strncpy(multiline_buffer, line, sizeof(multiline_buffer) - 1);
            multiline_buffer[sizeof(multiline_buffer) - 1] = '\0';
        }
        
        // 複数行の継続が必要かチェック
        if (needs_continuation(multiline_buffer)) {
            in_multiline = true;
            continue;
        }
        
        in_multiline = false;
        char *input = multiline_buffer;
        
        // ヒストリに追加
        repl_history_add(input);
        
        // パースと実行
        Parser parser;
        parser_init(&parser, input, "<repl>");
        
        // 文キーワードで始まるかどうかを判定
        bool is_statement = false;
        {
            const char *stmt_prefixes[] = {
                "変数 ", "定数 ", "関数 ", "もし ", "繰り返す", "条件 ",
                "各 ", "試行:", "型 ", "列挙 ", "照合 ", "表示(",
                "取り込む", "投げる", "戻す ", "生成関数 ", "@",
                NULL
            };
            for (int i = 0; stmt_prefixes[i] != NULL; i++) {
                if (strncmp(input, stmt_prefixes[i], strlen(stmt_prefixes[i])) == 0) {
                    is_statement = true;
                    break;
                }
            }
            // 代入文の検出: 識別子の後に = がある
            if (!is_statement) {
                const char *eq = strstr(input, " = ");
                if (eq != NULL && strstr(input, "==") == NULL) {
                    is_statement = true;
                }
            }
        }
        
        if (!is_statement) {
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
                    printf("\033[36m=> ");  // シアン色
                    value_print(result);
                    printf("\033[0m\n");    // リセット
                }
                
                node_free(program);
                parser_free(&parser);
                continue;
            }
            // 式パース失敗 → 文として再パース
            parser_free(&parser);
            parser_init(&parser, input, "<repl>");
        }
        
        // 文としてパース
        ASTNode *program = parse_program(&parser);
        
        if (!parser_had_error(&parser)) {
            evaluator_clear_error(eval);
            evaluator_run(eval, program);
        }
        
        node_free(program);
        parser_free(&parser);
    }
    
    repl_history_free();
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
    printf("パッケージ管理:\n");
    printf("  %s パッケージ 初期化                  プロジェクトを初期化 (hajimu.json作成)\n", program_name);
    printf("  %s パッケージ 追加 <ユーザー/リポ>    パッケージをインストール\n", program_name);
    printf("  %s パッケージ 削除 <パッケージ名>     パッケージを削除\n", program_name);
    printf("  %s パッケージ 一覧                    インストール済みパッケージ一覧\n", program_name);
    printf("  %s パッケージ インストール             全依存パッケージをインストール\n", program_name);
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
#ifdef _WIN32
    /* コンソールを UTF-8 モードに切り替えて文字化けを防ぐ。
     * SetConsoleOutputCP/SetConsoleCP を main() 决頭で呼ぶのが最も確実な方法。 */
    SetConsoleOutputCP(65001);   /* CP_UTF8 */
    SetConsoleCP(65001);
    /* ENABLE_VIRTUAL_TERMINAL_PROCESSING (0x0004): ANSIエスケープシーケンスを有効化 */
    HANDLE _hout = GetStdHandle(STD_OUTPUT_HANDLE);
    if (_hout != INVALID_HANDLE_VALUE) {
        DWORD _mode = 0;
        if (GetConsoleMode(_hout, &_mode))
            SetConsoleMode(_hout, _mode | 0x0004);
    }
    HANDLE _herr = GetStdHandle(STD_ERROR_HANDLE);
    if (_herr != INVALID_HANDLE_VALUE) {
        DWORD _mode = 0;
        if (GetConsoleMode(_herr, &_mode))
            SetConsoleMode(_herr, _mode | 0x0004);
    }
#endif
    /* Windows: WSAStartup は http.c / async.c 内の constructor 関数で自動実行済み */

    // ロケール設定（日本語出力のため）
    setlocale(LC_ALL, "");
    
    // パッケージ管理サブコマンド
    if (argc >= 2 && (strcmp(argv[1], "パッケージ") == 0 || strcmp(argv[1], "pkg") == 0)) {
        if (argc < 3) {
            printf("使用方法: %s パッケージ <コマンド> [引数]\n", argv[0]);
            printf("\nコマンド:\n");
            printf("  初期化 (init)              プロジェクトを初期化\n");
            printf("  追加 (add) <ユーザー/リポ> パッケージを追加\n");
            printf("  削除 (remove) <名前>       パッケージを削除\n");
            printf("  一覧 (list)                インストール済み一覧\n");
            printf("  インストール (install)     全依存をインストール\n");
            return 1;
        }
        
        const char *subcmd = argv[2];
        
        if (strcmp(subcmd, "初期化") == 0 || strcmp(subcmd, "init") == 0) {
            return package_init();
        } else if (strcmp(subcmd, "追加") == 0 || strcmp(subcmd, "add") == 0) {
            if (argc < 4) {
                fprintf(stderr, "エラー: パッケージ名またはリポジトリURLを指定してください\n");
                fprintf(stderr, "  例: %s パッケージ 追加 ユーザー名/リポジトリ名\n", argv[0]);
                return 1;
            }
            return package_install(argv[3]);
        } else if (strcmp(subcmd, "削除") == 0 || strcmp(subcmd, "remove") == 0) {
            if (argc < 4) {
                fprintf(stderr, "エラー: パッケージ名を指定してください\n");
                return 1;
            }
            return package_remove(argv[3]);
        } else if (strcmp(subcmd, "一覧") == 0 || strcmp(subcmd, "list") == 0) {
            return package_list();
        } else if (strcmp(subcmd, "インストール") == 0 || strcmp(subcmd, "install") == 0) {
            if (argc >= 4) {
                return package_install(argv[3]);
            }
            return package_install_all();
        } else {
            fprintf(stderr, "未知のパッケージコマンド: %s\n", subcmd);
            return 1;
        }
    }
    
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
