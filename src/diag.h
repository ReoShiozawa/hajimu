/**
 * 日本語プログラミング言語 - 診断メッセージユーティリティ
 *
 * Python / Rust 風のソース位置付きエラー表示を提供する。
 *
 * 出力例:
 *
 *   構文エラー --> test.jp:15:8
 *      |
 *   15 |     変数 x = もし 条件 なら
 *      |              ^^^^^^^^^^^
 *      = ヒント: 'なら' の後にブロックが必要です
 */

#ifndef DIAG_H
#define DIAG_H

#include <stdio.h>
#include <stdbool.h>

// =============================================================================
// エラー種別
// =============================================================================

typedef enum {
    DIAG_SYNTAX,        // 構文エラー
    DIAG_RUNTIME,       // 実行時エラー
    DIAG_NAME,          // 名前エラー  (未定義変数/関数)
    DIAG_TYPE,          // 型エラー    (型が合わない)
    DIAG_VALUE,         // 値エラー    (不正な値)
    DIAG_INDEX,         // インデックスエラー (範囲外)
    DIAG_ZERO_DIV,      // ゼロ除算
    DIAG_OVERFLOW,      // スタックオーバーフロー
    DIAG_ATTRIBUTE,     // 属性エラー  (存在しないメンバー)
    DIAG_USER,          // ユーザー定義例外
} DiagKind;

// =============================================================================
// 公開関数
// =============================================================================

/**
 * エラーの見出し文字列を返す (例: "構文エラー")
 */
const char *diag_kind_label(DiagKind kind);

/**
 * エラーメッセージをソース位置情報付きで stderr に出力する。
 *
 * @param kind      エラー種別
 * @param filename  ソースファイル名 (NULL 可)
 * @param source    ソースコード全体 (NULL 可: 行表示なし)
 * @param line      行番号 (1 始まり)
 * @param col       列番号 (UTF-8 文字単位, 1 始まり)
 * @param col_end   ハイライト終端列 (col と同じなら 1 文字分)
 * @param message   エラー本文 (NULL 不可)
 */
void diag_report(
    DiagKind kind,
    const char *filename,
    const char *source,
    int line,
    int col,
    int col_end,
    const char *message
);

/**
 * ソースから指定行のテキストを buf に書き出す。
 * @return 書き込んだバイト数
 */
int diag_extract_line(const char *source, int line_num,
                      char *buf, int buf_size);

/**
 * UTF-8 バイト列の文字数を返す (代わりにカラム幅を計算)
 */
int diag_utf8_strlen(const char *s, int byte_len);

#endif /* DIAG_H */
