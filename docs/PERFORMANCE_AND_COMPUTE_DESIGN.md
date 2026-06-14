# はじむ 高速標準処理・研究計算基盤 設計書

作成日: 2026-06-08

この文書は、はじむを実用的な研究・数値計算・機械学習の入口として使えるようにするための設計方針です。

目的は、はじむをいきなり巨大な ML フレームワークにすることではありません。まず、言語標準の処理速度と計算力を底上げし、将来的に数値ライブラリ・統計・機械学習・外部ネイティブ連携へ自然に進める土台を作ります。

## 1. 目標

### 1.1 短期目標

- 大量の数値配列を通常の `配列` より低コストに扱えるようにする
- 標準の配列・文字列・辞書・JSON・ファイル処理を高速化する
- ベンチマークを継続的に実行できる仕組みを入れる
- 研究用途で必要な基本統計・線形代数の最小セットを標準または公式パッケージとして提供する
- C プラグイン / HJPB / WASM と衝突しない拡張設計にする

### 1.2 中期目標

- typed numeric buffer を使った `vector` / `matrix` を導入する
- C 側で一括処理する `map` / `reduce` / `dot` / `matmul` / `mean` / `std` を提供する
- 必要に応じて BLAS / LAPACK など外部高速ライブラリへ接続できるようにする
- CSV / TSV / JSON Lines など研究データの読み込みを標準化する
- 簡易 ML API として線形回帰、ロジスティック回帰、k-means などを公式パッケージ化する

### 1.3 長期目標

- `tensor` 型を導入し、多次元配列を扱えるようにする
- 自動微分または計算グラフは公式拡張として段階的に検討する
- GPU 連携は直接標準化せず、まず C ABI / plugin / package から接続する
- WASM 版でも教育用途の軽量数値計算を動かせるようにする

## 2. 現状の強みと制約

### 2.1 強み

- C 実装なので、標準関数を C 側に寄せれば大きく高速化できる
- `Value` / `Evaluator` / `BuiltinFn` がすでにあり、組み込み関数追加がしやすい
- C ABI プラグインがあるため、重い処理をネイティブ拡張へ逃がせる
- HJPB `.hjp` と WASM の土台があり、配布・教育・ブラウザ実行へ展開しやすい
- async / channel / mutex / semaphore / atomic があり、並列処理 API を育てやすい

### 2.2 制約

- 現在の `VALUE_ARRAY` は `Value *elements` で、数値だけの配列でも各要素が `Value` になる
- 数値計算で必要な contiguous memory / cache locality / SIMD 適性が不足している
- 汎用 `Value` 配列の `map` / `filter` / `reduce` は柔軟だが、ML 用の大量計算には重い
- 現在の bytecode はソース埋め込み型に近く、AST / IR 最適化の実行基盤としてはまだ弱い
- GC は主に環境循環の回収が中心で、大規模データバッファの所有権管理は専用設計が必要

## 3. 基本方針

### 3.1 互換性を壊さない

既存の `配列` / `array` は残します。研究計算向けには新しい typed data 型を追加し、通常配列とは役割を分けます。

```hajimu
var xs = [1, 2, 3]        // 汎用配列: 何でも入る
var vx = vector([1, 2, 3]) // 数値ベクトル: 高速計算向け
```

### 3.2 速い処理は C 側で一括処理する

はじむコードから要素ごとに関数呼び出しする処理は、研究計算では遅くなります。標準の高速 API は「配列全体を C 側に渡す」形にします。

```hajimu
var x = vector_range(0, 1000000)
var y = vector_mul(vector_sin(x), 0.5)
var m = mean(y)
```

### 3.3 標準ライブラリと公式パッケージを分ける

標準に入れるもの:

- 軽量で依存が少ない
- 研究用途の基礎として普遍的
- WASM / Windows / macOS / Linux で保ちやすい

公式パッケージにするもの:

- BLAS / LAPACK / OpenMP など外部依存がある
- ML アルゴリズム群
- 大規模データ処理
- GPU 連携

### 3.4 日本語 API と英語 alias を同時に設計する

研究・ML は英語圏の用語が強いため、最初から日本語名と英語 alias を対で定義します。

例:

| 日本語 | 英語 alias | 意味 |
|---|---|---|
| `ベクトル` | `vector` | 数値ベクトル作成 |
| `行列` | `matrix` | 数値行列作成 |
| `平均` | `mean` | 平均 |
| `分散` | `variance` | 分散 |
| `標準偏差` | `std` | 標準偏差 |
| `内積` | `dot` | dot product |
| `行列積` | `matmul` | matrix multiplication |

## 4. 新しい内部型

### 4.1 `VALUE_NUMERIC_ARRAY`

まず 1 次元の連続数値配列を追加します。

```c
typedef enum {
    NUMERIC_F64,
    NUMERIC_F32,
    NUMERIC_I64,
    NUMERIC_I32,
    NUMERIC_BOOL
} NumericDType;

typedef struct {
    NumericDType dtype;
    void *data;
    int64_t length;
    int64_t capacity;
    int ref_count;
    bool readonly;
} NumericArray;
```

`ValueType` には次を追加します。

```c
VALUE_NUMERIC_ARRAY
```

最初の実装では `NUMERIC_F64` を主対象にします。`f32` / `i64` / `i32` は API を先に用意し、内部対応は段階的に広げます。

### 4.2 `VALUE_MATRIX`

行列は 2 次元ビューとして設計します。実体は `NumericArray` を共有し、shape / stride / offset を持ちます。

```c
typedef struct {
    NumericArray *buffer;
    int64_t rows;
    int64_t cols;
    int64_t row_stride;
    int64_t col_stride;
    int64_t offset;
    bool readonly;
} MatrixValue;
```

この設計にすると、転置やスライスでデータコピーを避けられます。

### 4.3 `VALUE_TENSOR`

`tensor` は長期目標です。最初から実装しませんが、`MatrixValue` を tensor へ拡張できるよう shape / stride の考え方を揃えます。

```c
typedef struct {
    NumericArray *buffer;
    int ndim;
    int64_t *shape;
    int64_t *strides;
    int64_t offset;
} TensorValue;
```

## 5. 標準 API 設計

### 5.1 作成・変換

```hajimu
var v = vector([1, 2, 3])
var z = zeros(1000)
var o = ones(1000)
var r = range_vector(0, 10, 0.5)
var a = to_array(v)
```

日本語名:

```hajimu
変数 v = ベクトル([1, 2, 3])
変数 z = ゼロ配列(1000)
変数 o = 一配列(1000)
変数 r = 範囲ベクトル(0, 10, 0.5)
変数 a = 配列化(v)
```

### 5.2 要約統計

```hajimu
mean(v)
sum(v)
min(v)
max(v)
variance(v)
std(v)
norm(v)
minmax_scale(v)
clip(v, 0, 1)
median(v)
quantile(v, 0.95)
```

日本語 alias:

```hajimu
平均(v)
合計(v)
最小(v)
最大(v)
分散(v)
標準偏差(v)
ノルム(v)
最小最大スケール(v)
クリップ(v, 0, 1)
中央値(v)
分位点(v, 0.95)
```

### 5.3 ベクトル演算

```hajimu
vector_add(a, b)
vector_sub(a, b)
vector_mul(a, b)
vector_div(a, b)
dot(a, b)
norm(a)
normalize(a)
```

スカラーとの演算も許可します。

```hajimu
vector_mul(a, 0.01)
vector_add(a, 10)
```

### 5.4 行列演算

```hajimu
var m = matrix([[1, 2], [3, 4]])
shape(m)         // [2, 2]
transpose(m)
matmul(m, m)
matrix_add(m, m)
matrix_scale(m, 0.5)
matrix_hadamard(m, m)
matrix_get(m, 0, 1)
matrix_set(m, 0, 1, 9)
```

行列は最初 `f64` のみ対応し、BLAS 連携前でも C の三重ループで正しく動かします。

### 5.5 データ読み込み

```hajimu
var rows = read_csv("data.csv")
var x = csv_column(rows, "height")
var y = csv_column(rows, "weight")
var vx = vector(x)
```

大規模データ用には、全件を `Value` 化しない読み込みも検討します。

```hajimu
var data = read_csv_numeric("data.csv", columns=["height", "weight"])
var x = column(data, "height")
```

## 6. 高速化対象

### 6.1 配列

対象:

- `array_push`
- `array_get`
- `array_set`
- `map`
- `filter`
- `reduce`
- `sort`
- `slice`

方針:

- 容量拡張を `array_grow.h` に寄せている現状を維持しつつ、overflow check を強化する
- `Value` コピー回数を減らす
- 数値だけの場合は `vector` への変換を案内する
- `map` / `reduce` は汎用版と numeric 版を分ける

### 6.2 辞書

対象:

- 現在の線形探索がボトルネックになる箇所
- JSON object
- CSV row
- インスタンス field lookup

方針:

- 内部ハッシュテーブルを導入する
- 小さい辞書では線形探索、大きい辞書では hash index へ切り替える
- 文字列 key の hash をキャッシュする

### 6.3 文字列

対象:

- 連結
- split / join
- substring
- UTF-8 index

方針:

- 連結の一時 allocation を減らす
- StringBuffer を標準化し、JSON / HTTP / value_to_string で共有する
- UTF-8 文字位置キャッシュは大きな文字列のみに限定する

### 6.4 evaluator

対象:

- 関数呼び出し
- 環境 lookup
- ループ
- binary operation

方針:

- よく使う built-in は名前 lookup 後にキャッシュできるようにする
- ループ内の `Value` 一時オブジェクトを減らす
- `VALUE_NUMBER` 同士の演算は fast path を明示する
- AST node に評価補助キャッシュを持たせることを検討する

## 7. 研究・ML 向け公式パッケージ

### 7.1 `hajimu_stats`

標準関数だけで完結できる軽量統計パッケージです。

- mean / variance / std（標準関数として初期実装済み）
- covariance / correlation（標準関数として初期実装済み）
- histogram（標準関数として初期実装済み）
- quantile / median（標準関数として初期実装済み）
- normalize / z_score（標準関数として初期実装済み）
- norm / minmax_scale / clip（標準関数として初期実装済み）
- train_test_split（標準関数として初期実装済み、seed 指定シャッフル対応）
- mse / mae / r2_score / accuracy / precision / recall（標準関数として初期実装済み）

### 7.2 `hajimu_linalg`

線形代数パッケージです。最初は純 C 実装、後で BLAS 連携を追加します。

- dot
- matmul（標準関数として初期実装済み）
- transpose（標準関数として初期実装済み）
- identity（標準関数として初期実装済み）
- determinant（標準関数として初期実装済み）
- inverse（標準関数として初期実装済み）
- solve / solve_linear（`solve_linear` は標準関数として初期実装済み）
- matrix_add / matrix_sub / matrix_scale / matrix_hadamard（標準関数として初期実装済み）
- eigen は後回し

### 7.3 `hajimu_ml`

学習用の小さな ML パッケージです。

- linear_regression / predict_linear（標準関数として初期実装済み）
- logistic_regression / predict_logistic（標準関数として初期実装済み）
- kmeans（標準関数として初期実装済み）
- knn
- decision_stump
- metrics: mse / mae / r2_score / accuracy / precision / recall（標準関数として初期実装済み）

深層学習は初期対象にしません。まず古典的 ML とデータ処理を安定させます。

## 8. ネイティブ高速化

### 8.1 C ABI 拡張

既存の `include/hajimu_plugin.h` を拡張し、numeric buffer をプラグインに安全に渡せる API を追加します。

```c
bool hajimu_is_numeric_array(Value *v);
double *hajimu_numeric_f64_data(Value *v);
void *hajimu_numeric_raw_data(Value *v);
NumericDType hajimu_numeric_dtype(Value *v);
int64_t hajimu_numeric_length(Value *v);
Value hajimu_numeric_from_f64_copy(const double *data, int64_t length);
```

重要:

- 直接ポインタを渡す API は lifetime を明記する
- plugin 側が保持したい場合は copy または retain を使う
- readonly view と mutable view を分ける

### 8.2 BLAS 連携

BLAS は標準必須依存にしません。公式パッケージまたは optional build にします。

```bash
make linalg-blas
```

初期実装では `matmul` が通常ビルドでは純 C の三重ループ、`make linalg-blas` では macOS の Accelerate または Linux の OpenBLAS / CBLAS に切り替わります。Windows は OpenBLAS の同梱方法を配布設計と合わせて詰めます。

候補:

- macOS: Accelerate
- Linux: OpenBLAS
- Windows: OpenBLAS static / bundled DLL

## 9. Bytecode / IR 最適化

HJPB は現状の配布基盤として維持します。高速化は次の順で進めます。

1. AST 実行の fast path
2. built-in numeric API
3. AST node の軽量キャッシュ
4. bytecode への命令列追加
5. JIT は長期検討

短期では JIT より、C built-in、typed numeric buffer、辞書 lookup の高速化、行列 view の効果が大きいです。

実装済み:

- HJPB 読み込み時の長さ整合性チェックを追加
- 9件以上の辞書リテラルで AST values 配列が拡張されないメモリ破壊を修正
- `--profile` で読み込み・パース・実行・合計時間を表示
- `--profile-ast` で AST ノード単位の実行回数、合計時間、最大時間、代表行/列を表示

長期研究・次期設計:

- HJPB を命令列 IR として実行する VM
- ホットパスの自動書き換え

## 10. ベンチマーク設計

`benchmarks/` を追加し、以下を測ります。

```text
benchmarks/
├── array_push.jp
├── array_map.jp
├── dict_lookup.jp
├── string_concat.jp
├── json_parse.jp
├── vector_sum.jp
├── vector_dot.jp
├── matrix_mul.jp
└── startup.jp
```

CLI:

```bash
hajimu bench benchmarks/vector_sum.jp
```

最初は外部ツールで十分です。

```bash
/usr/bin/time -p ./nihongo benchmarks/vector_sum.jp
```

`--profile` でスクリプト全体の読み込み・パース・実行時間を確認できます。

```bash
hajimu --profile script.jp
```

出力例:

```text
プロファイル: 読込 0.023 ms, パース 0.087 ms, 実行 0.126 ms, 合計 1.126 ms
関数別時間:
  vector_sum       12.4 ms
  parse_csv         8.9 ms
  main              4.1 ms

allocation:
  Value copies      12003
  strings           832
  arrays            12
```

## 11. 実装フェーズ

### Phase 1: 計測と低リスク高速化

- `benchmarks/` を追加（初期実装済み）
- `make bench` を追加（初期実装済み）
- `VALUE_NUMBER` 演算の fast path を確認・整理
- `array_push` / `value_copy` / `value_free` のホットスポットを測定
- JSON encode/decode の StringBuffer 再利用を進める
- 辞書・環境 lookup のベンチを作る（辞書 lookup は初期実装済み）

完了条件:

- 主要操作の基準値が記録されている
- 高速化前後を比較できる
- 既存テストがすべて通る

### Phase 2: `vector` 基盤

- `VALUE_NUMERIC_ARRAY` を追加（初期実装済み）
- `value_numeric_array_*` を実装（初期実装済み）
- `vector` / `zeros` / `ones` / `range_vector` を追加（初期実装済み）
- `sum` / `mean` / `min` / `max` / `dot` を追加（`vector_sum` / `mean` / `dot` は初期実装済み）
- 日本語 alias を追加
- `docs/REFERENCE.md` / `docs/REFERENCE_en.md` を更新

完了条件:

- 100 万要素の合計が汎用配列より明確に速い
- `.jp` / `.haj` の両方で動く
- WASM 版でも基本 API が動く

### Phase 3: `matrix` 基盤

- `VALUE_MATRIX` を追加（初期実装済み）
- `matrix` / `shape` / `transpose` / `matmul` を追加（初期実装済み）
- `identity` / `determinant` / `inverse` / `solve_linear` を追加（初期実装済み）
- `dtype` / `astype` で `f64` / `f32` / `i64` / `i32` / `bool` を扱う API を追加（dtype 別の実バッファ保存まで初期実装済み）
- `dtype_size` / `nbytes` / `storage_bytes` で論理 dtype サイズと実保存サイズを確認できるようにする（初期実装済み）
- row-major contiguous matrix を先に実装（初期実装済み）
- `row_stride` / `col_stride` / `offset` を `VALUE_MATRIX` に追加し、`matrix_get` / `matrix_set` は stride 対応済み
- `転置` は共有バッファ view と lifetime 管理に対応済み
- `matmul` の shape mismatch 診断、行列作成時の ragged row 診断は初期実装済み

完了条件:

- 小中規模の行列積が動く
- shape mismatch エラーが親切（初期実装済み）
- 研究用途のサンプルを追加

### Phase 4: データ処理

- `read_csv_numeric`（ヘッダーなし/ありの数値 CSV は初期実装済み）
- `read_tsv_numeric`（ヘッダーなし/ありの数値 TSV は初期実装済み）
- `read_csv` / `CSV読込`（ヘッダーありの辞書行、ヘッダーなしの配列行、引用符付きセルは初期実装済み）
- `csv_column` / `CSV列`（辞書行の列名指定、配列行の列番号指定は初期実装済み）
- `read_json_lines` / `JSON行読込` / `JSONL読込`（非空行を1行ずつ JSON として読む API は初期実装済み）
- `column`（`matrix_column` / `列取得` は初期実装済み）
- `describe`（`要約` / `describe` は初期実装済み）
- CSV の非数値セル、列数不一致、長すぎる行、閉じていない引用符の診断は初期実装済み
- missing value handling（`read_csv_numeric(..., "nan"|"zero")`, `drop_missing`, `fill_missing`, `is_nan` は初期実装済み）
- 前処理（`normalize` / `z_score` / `minmax_scale` / `clip` / `norm` は初期実装済み）

完了条件:

- CSV から vector / matrix へ変換できる
- 統計サンプルが書ける

### Phase 5: 公式研究パッケージ

- `hajimu_stats`
- `hajimu_linalg`
- `hajimu_ml`

完了条件:

- 線形回帰サンプル
- k-means サンプル
- k-nearest neighbors / ロジスティック分類サンプル
- 評価指標サンプル（MSE / MAE / R2 / accuracy / precision / recall / F1 / confusion matrix は初期実装済み）

### Phase 6: optional native acceleration

- BLAS 連携（`make linalg-blas` と `matmul` の CBLAS/Accelerate 経路は初期実装済み）
- plugin API の numeric buffer 対応（`hajimu_plugin.h` の inline helper は初期実装済み）
- package manager で platform-specific binary を扱いやすくする

完了条件:

- BLAS あり/なしの両方でビルドできる（macOS Accelerate 経路は初期確認対象）
- Windows / macOS / Linux の扱いがドキュメント化されている

## 12. API 例

### 12.1 統計

```hajimu
var x = vector([1, 2, 3, 4, 5])

print(mean(x))
print(std(x))
print(quantile(x, 0.5))
```

```hajimu
変数 x = ベクトル([1, 2, 3, 4, 5])

表示(平均(x))
表示(標準偏差(x))
表示(分位点(x, 0.5))
```

### 12.2 線形回帰

```hajimu
var x = matrix([[1], [2], [3], [4]])
var y = vector([2, 4, 6, 8])

var model = linear_regression(x, y)
print(model.weights)
print(predict_linear(model, matrix([[5]])))
```

### 12.3 データ読み込み

```hajimu
var data = read_csv_numeric("iris.csv", columns=["sepal_length", "sepal_width"])
var x = column(data, "sepal_length")

print(mean(x))
print(std(x))
```

## 13. エラー設計

研究計算では shape / dtype / missing value のエラーが重要です。

初期実装済み:

- `vector_add` / `dot` などのベクトル長不一致
- `vector_div` の 0 除算、`vector_sqrt` / `vector_log` の定義域外入力
- `matrix` の行長不一致、非数値要素
- `matmul` の行列サイズ不一致
- `matrix_get` / `matrix_set` / `matrix_row` / `matrix_column` の範囲外インデックス
- `read_csv_numeric` の非数値セル、欠損値モード、列数不一致、行長制限
- `read_csv_numeric` の空セルは missing value として扱い、`"nan"` / `"zero"` / エラーの各モードに対応
- `試行` / `try` の中で発生した実行時エラーは、捕獲変数に `message` / `line` / `column` / `file` / `kind` を持つ診断辞書として渡せる初期実装済み

今後:

- 捕獲済み structured diagnostic の表示抑制や、カテゴリのさらに細かい構造化

例:

```text
エラー: matmul の形が合いません
  左:  3 x 4
  右:  5 x 2

原因:
  行列積では 左の列数 と 右の行数 が同じである必要があります。

修正例:
  matmul(A, transpose(B))
```

英語 alias 利用時:

```text
Error: matmul shape mismatch
  left:  3 x 4
  right: 5 x 2

Why:
  Matrix multiplication requires left columns == right rows.

Try:
  matmul(A, transpose(B))
```

## 14. セキュリティ・安定性

- 大きな allocation は必ず overflow check を行う
- `int` ではなく `int64_t` / `size_t` を使う
- `length * sizeof(double)` の計算は専用 helper で検査する
- 外部 BLAS / plugin は sandbox ではないことを明記する
- CSV / JSON は入力サイズ制限を設定できるようにする（JSON Lines は最大行数、CSV / JSON Lines は1行長制限を初期実装済み）
- WASM では HTTP / file / native plugin を制限し、numeric API を中心にする

## 15. 優先順位

最優先:

1. benchmark 基盤
2. `VALUE_NUMERIC_ARRAY`
3. `vector` / `sum` / `mean` / `dot`
4. shape / dtype エラー
5. CSV numeric 読み込み

次点:

1. `matrix`
2. `matmul`
3. `hajimu_stats`
4. `hajimu_linalg`
5. `hajimu_ml`

後回し:

1. tensor
2. 自動微分
3. JIT
4. GPU

## 16. まとめ

はじむを研究・機械学習にも使える言語へ進めるには、まず「汎用配列を速くする」だけでは足りません。

必要なのは、次の二層構造です。

- 学習者や日常スクリプト向けの柔軟な `配列`
- 研究計算向けの連続メモリ `vector` / `matrix`

この二層を C 実装の標準関数でつなぐことで、はじむらしい読みやすさを保ちながら、実用的な計算性能へ近づけられます。
