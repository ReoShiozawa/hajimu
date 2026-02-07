# はじむ - 実装ガイド

このガイドでは、はじむインタープリタの実装手順を段階的に説明します。

## 1. 開発環境のセットアップ

### 1.1 必要なツール

```bash
# macOS
xcode-select --install

# または Homebrew で gcc をインストール
brew install gcc

# Linux (Ubuntu/Debian)
sudo apt-get install build-essential

# デバッグツール
brew install lldb  # macOS
sudo apt-get install gdb  # Linux
```

### 1.2 プロジェクト構造

```
jp/
├── src/              # ソースファイル
├── examples/         # サンプルプログラム
├── docs/             # ドキュメント
├── tests/            # テストファイル
├── build/            # ビルド出力（自動生成）
├── Makefile          # ビルド設定
└── README.md
```

### 1.3 最初のビルドテスト

```bash
# ビルドディレクトリ作成
mkdir -p build

# 単純なテストコンパイル
gcc -Wall -Wextra -std=c11 -o test test.c
```

## 2. 実装順序

以下の順序で実装することを推奨します：

```
1. UTF-8ユーティリティ
   ↓
2. 字句解析器 (Lexer)
   ↓
3. 値システム (Value)
   ↓
4. 抽象構文木 (AST)
   ↓
5. 構文解析器 (Parser)
   ↓
6. 環境 (Environment)
   ↓
7. 評価器 (Evaluator)
   ↓
8. 組み込み関数
   ↓
9. メインプログラム & REPL
```

## 3. Phase 1: 字句解析器の実装

### 3.1 UTF-8処理の基本

日本語を扱うため、UTF-8の処理が必須です。

```c
// UTF-8のバイト数を判定
int utf8_char_length(unsigned char c) {
    if (c < 0x80) return 1;       // ASCII (0xxxxxxx)
    if (c < 0xC0) return 0;       // 継続バイト（単独では無効）
    if (c < 0xE0) return 2;       // 2バイト (110xxxxx)
    if (c < 0xF0) return 3;       // 3バイト (1110xxxx) ← 日本語
    if (c < 0xF8) return 4;       // 4バイト (11110xxx)
    return 0;                      // 無効
}
```

### 3.2 日本語文字の判定

```c
// ひらがな: U+3040 - U+309F
// カタカナ: U+30A0 - U+30FF
// 漢字:     U+4E00 - U+9FFF
// 全角英数: U+FF00 - U+FFEF

bool is_japanese_char(uint32_t codepoint) {
    return (codepoint >= 0x3040 && codepoint <= 0x309F) ||  // ひらがな
           (codepoint >= 0x30A0 && codepoint <= 0x30FF) ||  // カタカナ
           (codepoint >= 0x4E00 && codepoint <= 0x9FFF) ||  // CJK統合漢字
           (codepoint >= 0xFF00 && codepoint <= 0xFFEF);    // 全角
}

bool is_identifier_start(uint32_t codepoint) {
    return is_japanese_char(codepoint) ||
           (codepoint >= 'a' && codepoint <= 'z') ||
           (codepoint >= 'A' && codepoint <= 'Z') ||
           codepoint == '_';
}

bool is_identifier_char(uint32_t codepoint) {
    return is_identifier_start(codepoint) ||
           (codepoint >= '0' && codepoint <= '9');
}
```

### 3.3 Lexerの実装手順

1. **基本構造の定義**
   - Token構造体
   - Lexer構造体
   - TokenType列挙型

2. **単純なトークンの処理**
   - 演算子 (+, -, *, /, etc.)
   - 区切り記号 (, ), [, ], etc.)
   - 空白・改行

3. **数値リテラル**
   - 整数
   - 小数点数

4. **文字列リテラル**
   - "..."形式
   - エスケープシーケンス (\n, \t, \\, \")

5. **識別子とキーワード**
   - UTF-8識別子のスキャン
   - キーワードテーブルとの照合

6. **インデント処理**
   - スタックベースのインデント管理
   - INDENT/DEDENTトークンの生成

### 3.4 テスト方法

```c
void test_lexer(void) {
    const char *source = "関数 テスト():\n    表示(123)\n終わり\n";
    Lexer lexer;
    lexer_init(&lexer, source, "test");
    
    Token token;
    while ((token = lexer_next(&lexer)).type != TOKEN_EOF) {
        printf("Token: type=%d, value='%s', line=%d\n",
               token.type, token.value, token.line);
    }
}
```

## 4. Phase 2: 値システムの実装

### 4.1 Value構造体

```c
typedef struct Value {
    ValueType type;
    union {
        double number;
        bool boolean;
        struct { char *data; int length; } string;
        struct { struct Value *items; int count; int capacity; } array;
        // ... 他の型
    };
} Value;
```

### 4.2 値の作成・解放

```c
Value make_number(double n);
Value make_bool(bool b);
Value make_string(const char *s);
Value make_array(void);

void value_free(Value *v);
char *value_to_string(Value v);  // デバッグ用
```

### 4.3 型変換と真偽判定

```c
bool is_truthy(Value v) {
    switch (v.type) {
        case VALUE_NULL: return false;
        case VALUE_BOOL: return v.boolean;
        case VALUE_NUMBER: return v.number != 0;
        case VALUE_STRING: return v.string.length > 0;
        case VALUE_ARRAY: return v.array.count > 0;
        default: return true;
    }
}
```

## 5. Phase 3: ASTの実装

### 5.1 ノード種別の設計

まず必要最小限のノードから始める：

```c
typedef enum {
    NODE_PROGRAM,
    NODE_FUNCTION_DEF,
    NODE_BLOCK,
    NODE_VAR_DECL,
    NODE_ASSIGN,
    NODE_IF,
    NODE_WHILE,
    NODE_FOR,
    NODE_RETURN,
    NODE_EXPR_STMT,
    NODE_BINARY,
    NODE_UNARY,
    NODE_CALL,
    NODE_IDENTIFIER,
    NODE_NUMBER,
    NODE_STRING,
    NODE_BOOL,
} NodeType;
```

### 5.2 ノードの作成関数

```c
ASTNode *node_new(NodeType type, int line, int column);
ASTNode *node_number(double value, int line, int column);
ASTNode *node_string(const char *value, int line, int column);
ASTNode *node_identifier(const char *name, int line, int column);
ASTNode *node_binary(TokenType op, ASTNode *left, ASTNode *right);
// ... etc.
```

### 5.3 ASTのデバッグ出力

```c
void ast_print(ASTNode *node, int indent) {
    for (int i = 0; i < indent; i++) printf("  ");
    
    switch (node->type) {
        case NODE_NUMBER:
            printf("Number: %g\n", node->number_value);
            break;
        case NODE_BINARY:
            printf("Binary: %s\n", token_name(node->binary.operator));
            ast_print(node->binary.left, indent + 1);
            ast_print(node->binary.right, indent + 1);
            break;
        // ... etc.
    }
}
```

## 6. Phase 4: 構文解析器の実装

### 6.1 再帰下降パーサの基本

```c
typedef struct {
    Lexer *lexer;
    Token current;
    Token previous;
    bool had_error;
} Parser;

// ヘルパー関数
static Token advance(Parser *p) {
    p->previous = p->current;
    p->current = lexer_next(p->lexer);
    return p->previous;
}

static bool check(Parser *p, TokenType type) {
    return p->current.type == type;
}

static bool match(Parser *p, TokenType type) {
    if (!check(p, type)) return false;
    advance(p);
    return true;
}

static void expect(Parser *p, TokenType type, const char *message) {
    if (!match(p, type)) {
        parse_error(p, message);
    }
}
```

### 6.2 式のパース（優先順位クライミング）

```c
// 式 → or_expr
static ASTNode *parse_expression(Parser *p) {
    return parse_or(p);
}

// or_expr → and_expr { "または" and_expr }
static ASTNode *parse_or(Parser *p) {
    ASTNode *left = parse_and(p);
    
    while (match(p, TOKEN_OR)) {
        ASTNode *right = parse_and(p);
        left = node_binary(TOKEN_OR, left, right);
    }
    
    return left;
}

// 以降、優先順位順に...
```

### 6.3 文のパース

```c
static ASTNode *parse_statement(Parser *p) {
    if (match(p, TOKEN_VARIABLE)) {
        return parse_var_decl(p, false);
    }
    if (match(p, TOKEN_CONSTANT)) {
        return parse_var_decl(p, true);
    }
    if (match(p, TOKEN_IF)) {
        return parse_if(p);
    }
    // ... etc.
    
    return parse_expression_statement(p);
}
```

## 7. Phase 5: 環境の実装

### 7.1 ハッシュテーブル

```c
static uint32_t hash_string(const char *s) {
    uint32_t hash = 2166136261u;
    while (*s) {
        hash ^= (unsigned char)*s++;
        hash *= 16777619;
    }
    return hash;
}
```

### 7.2 スコープチェーン

```c
Value *env_get(Environment *env, const char *name) {
    Environment *e = env;
    while (e != NULL) {
        Value *v = env_lookup_local(e, name);
        if (v != NULL) return v;
        e = e->parent;
    }
    return NULL;  // 見つからない
}
```

## 8. Phase 6: 評価器の実装

### 8.1 ディスパッチャ

```c
Value evaluate(Evaluator *eval, ASTNode *node) {
    switch (node->type) {
        case NODE_NUMBER:
            return make_number(node->number_value);
        case NODE_STRING:
            return make_string(node->string_value);
        case NODE_BOOL:
            return make_bool(node->bool_value);
        case NODE_IDENTIFIER:
            return eval_identifier(eval, node);
        case NODE_BINARY:
            return eval_binary(eval, node);
        case NODE_CALL:
            return eval_call(eval, node);
        // ... etc.
    }
}
```

### 8.2 関数呼び出し

```c
Value eval_call(Evaluator *eval, ASTNode *node) {
    // 呼び出し対象を評価
    Value callee = evaluate(eval, node->call.callee);
    
    // 引数を評価
    Value *args = malloc(sizeof(Value) * node->call.arg_count);
    for (int i = 0; i < node->call.arg_count; i++) {
        args[i] = evaluate(eval, node->call.arguments[i]);
    }
    
    // 組み込み関数
    if (callee.type == VALUE_BUILTIN) {
        Value result = callee.builtin.fn(node->call.arg_count, args);
        free(args);
        return result;
    }
    
    // ユーザー定義関数
    if (callee.type == VALUE_FUNCTION) {
        // 新しいスコープを作成
        Environment *local = env_new(callee.function.closure);
        
        // 引数をバインド
        ASTNode *def = callee.function.definition;
        for (int i = 0; i < def->function.param_count; i++) {
            env_define(local, def->function.params[i], args[i]);
        }
        
        // 関数本体を実行
        Environment *prev = eval->current;
        eval->current = local;
        
        evaluate(eval, def->function.body);
        
        Value result = eval->return_value;
        eval->returning = false;
        
        eval->current = prev;
        env_free(local);
        free(args);
        
        return result;
    }
    
    runtime_error(eval, "呼び出し可能ではありません");
    return make_null();
}
```

## 9. デバッグのコツ

### 9.1 デバッグ出力マクロ

```c
#ifdef DEBUG
#define DEBUG_LOG(fmt, ...) \
    fprintf(stderr, "[DEBUG] %s:%d: " fmt "\n", \
            __FILE__, __LINE__, ##__VA_ARGS__)
#else
#define DEBUG_LOG(fmt, ...)
#endif
```

### 9.2 トークンのダンプ

```c
void dump_tokens(const char *source) {
    Lexer lexer;
    lexer_init(&lexer, source, "<debug>");
    
    Token tok;
    while ((tok = lexer_next(&lexer)).type != TOKEN_EOF) {
        printf("[%3d:%2d] %-20s '%s'\n",
               tok.line, tok.column,
               token_type_name(tok.type),
               tok.value ? tok.value : "");
    }
}
```

### 9.3 ASTのビジュアル化

```c
void ast_dump(ASTNode *node, int depth) {
    const char *indent = "  ";
    for (int i = 0; i < depth; i++) printf("%s", indent);
    
    printf("<%s", node_type_name(node->type));
    
    // ノード固有の属性を出力
    switch (node->type) {
        case NODE_NUMBER:
            printf(" value=%g", node->number_value);
            break;
        case NODE_IDENTIFIER:
            printf(" name=\"%s\"", node->string_value);
            break;
        // ...
    }
    
    printf(">\n");
    
    // 子ノードを再帰的にダンプ
    // ...
}
```

## 10. よくある問題と解決策

### 10.1 UTF-8関連

**問題**: 日本語が文字化けする

**解決策**:
```c
// ソースファイルがUTF-8であることを確認
// printfの前に:
setlocale(LC_ALL, "");
```

### 10.2 メモリリーク

**問題**: valgrindでリークが検出される

**解決策**:
- すべてのmallocに対応するfreeがあるか確認
- ASTノードの解放を再帰的に行う
- 文字列のコピーを忘れない

```c
// 文字列は必ずコピー
char *copy = strdup(original);

// 使い終わったら解放
free(copy);
```

### 10.3 無限ループ

**問題**: パーサが無限ループする

**解決策**:
- 必ずadvance()を呼んでいるか確認
- エラー回復時に無限ループしていないか
- 再帰呼び出しの終了条件を確認

### 10.4 スタックオーバーフロー

**問題**: 深い再帰で落ちる

**解決策**:
```c
// 再帰深度を制限
#define MAX_RECURSION_DEPTH 1000

Value evaluate(Evaluator *eval, ASTNode *node) {
    if (eval->recursion_depth > MAX_RECURSION_DEPTH) {
        runtime_error(eval, "スタックオーバーフロー");
        return make_null();
    }
    eval->recursion_depth++;
    
    // ... 評価 ...
    
    eval->recursion_depth--;
    return result;
}
```

## 11. テスト戦略

### 11.1 単体テスト

```c
// test_lexer.c
void test_number_literals(void) {
    struct { const char *input; double expected; } tests[] = {
        {"123", 123.0},
        {"3.14", 3.14},
        {"0.5", 0.5},
        {".5", 0.5},
        {"1e10", 1e10},
    };
    
    for (int i = 0; i < sizeof(tests)/sizeof(tests[0]); i++) {
        Lexer l;
        lexer_init(&l, tests[i].input, "test");
        Token t = lexer_next(&l);
        
        assert(t.type == TOKEN_NUMBER);
        assert(fabs(atof(t.value) - tests[i].expected) < 0.0001);
    }
    
    printf("✓ test_number_literals passed\n");
}
```

### 11.2 統合テスト

```bash
# tests/run_tests.sh
#!/bin/bash

for file in tests/*.jp; do
    echo "Testing: $file"
    ./nihongo "$file" > /tmp/output.txt 2>&1
    
    expected="${file%.jp}.expected"
    if diff -q /tmp/output.txt "$expected" > /dev/null; then
        echo "  ✓ PASS"
    else
        echo "  ✗ FAIL"
        diff /tmp/output.txt "$expected"
    fi
done
```

### 11.3 ファズテスト

```c
// 無効な入力でクラッシュしないことを確認
void fuzz_test(void) {
    const char *invalid_inputs[] = {
        "",
        "あ",
        "((((",
        "\"未終了の文字列",
        "関数 (",
        // ...
    };
    
    for (int i = 0; i < sizeof(invalid_inputs)/sizeof(invalid_inputs[0]); i++) {
        // クラッシュしないことを確認
        run_source(invalid_inputs[i]);
    }
}
```

## 12. パフォーマンス最適化

### 12.1 文字列の最適化

- 短い文字列はスタック上に
- 文字列インターニング
- 文字列連結の効率化（StringBuilder パターン）

### 12.2 ハッシュテーブルの最適化

- 適切なテーブルサイズ
- 良いハッシュ関数（FNV-1a推奨）
- ロードファクターの管理

### 12.3 メモリ管理の最適化

- オブジェクトプール
- アリーナアロケータ
- コピーオンライト

## 13. 次のステップ

実装が完了したら：

1. **ドキュメント整備**
   - 言語リファレンス
   - チュートリアル
   - APIドキュメント

2. **開発ツール**
   - シンタックスハイライト（VSCode拡張）
   - REPL改善
   - デバッガ

3. **標準ライブラリ**
   - ファイルI/O
   - ネットワーク
   - データ構造

4. **最適化**
   - バイトコードコンパイル
   - JIT
   - 並列処理

頑張ってください！
