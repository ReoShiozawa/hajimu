# 日本語プログラミング言語 - Makefile

CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -g -O2
LDFLAGS = -lm -lcurl -lpthread

# ディレクトリ
SRC_DIR = src
BUILD_DIR = build
EXAMPLES_DIR = examples

# ソースファイル
SOURCES = $(SRC_DIR)/main.c \
          $(SRC_DIR)/lexer.c \
          $(SRC_DIR)/ast.c \
          $(SRC_DIR)/parser.c \
          $(SRC_DIR)/value.c \
          $(SRC_DIR)/environment.c \
          $(SRC_DIR)/evaluator.c \
          $(SRC_DIR)/http.c \
          $(SRC_DIR)/async.c

# オブジェクトファイル
OBJECTS = $(SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

# 実行ファイル
TARGET = nihongo

# デフォルトターゲット
all: $(BUILD_DIR) $(TARGET)

# ビルドディレクトリ作成
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# リンク
$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET) $(LDFLAGS)
	@echo "ビルド完了: $(TARGET)"

# コンパイル
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# 依存関係
$(BUILD_DIR)/main.o: $(SRC_DIR)/main.c $(SRC_DIR)/lexer.h $(SRC_DIR)/parser.h $(SRC_DIR)/evaluator.h
$(BUILD_DIR)/lexer.o: $(SRC_DIR)/lexer.c $(SRC_DIR)/lexer.h
$(BUILD_DIR)/ast.o: $(SRC_DIR)/ast.c $(SRC_DIR)/ast.h $(SRC_DIR)/value.h
$(BUILD_DIR)/parser.o: $(SRC_DIR)/parser.c $(SRC_DIR)/parser.h $(SRC_DIR)/lexer.h $(SRC_DIR)/ast.h
$(BUILD_DIR)/value.o: $(SRC_DIR)/value.c $(SRC_DIR)/value.h
$(BUILD_DIR)/environment.o: $(SRC_DIR)/environment.c $(SRC_DIR)/environment.h $(SRC_DIR)/value.h
$(BUILD_DIR)/evaluator.o: $(SRC_DIR)/evaluator.c $(SRC_DIR)/evaluator.h $(SRC_DIR)/ast.h $(SRC_DIR)/environment.h $(SRC_DIR)/http.h $(SRC_DIR)/async.h
$(BUILD_DIR)/http.o: $(SRC_DIR)/http.c $(SRC_DIR)/http.h $(SRC_DIR)/value.h
$(BUILD_DIR)/async.o: $(SRC_DIR)/async.c $(SRC_DIR)/async.h $(SRC_DIR)/evaluator.h $(SRC_DIR)/value.h

# 実行
run: $(TARGET)
	./$(TARGET)

# サンプル実行
hello: $(TARGET)
	./$(TARGET) $(EXAMPLES_DIR)/hello.jp

factorial: $(TARGET)
	./$(TARGET) $(EXAMPLES_DIR)/factorial.jp

fibonacci: $(TARGET)
	./$(TARGET) $(EXAMPLES_DIR)/fibonacci.jp

# テスト
test: $(TARGET)
	@echo "テスト実行中..."
	@for file in tests/*.jp; do \
		echo "テスト: $$file"; \
		./$(TARGET) $$file; \
	done

# クリーンアップ
clean:
	rm -rf $(BUILD_DIR) $(TARGET)
	@echo "クリーンアップ完了"

# デバッグビルド
debug: CFLAGS += -DDEBUG -g3
debug: clean all

# リリースビルド
release: CFLAGS += -O3 -DNDEBUG
release: clean all

# 再ビルド
rebuild: clean all

# ヘルプ
help:
	@echo "使用可能なターゲット:"
	@echo "  all      - ビルド（デフォルト）"
	@echo "  run      - REPL起動"
	@echo "  hello    - Hello Worldサンプル実行"
	@echo "  test     - テスト実行"
	@echo "  clean    - クリーンアップ"
	@echo "  debug    - デバッグビルド"
	@echo "  release  - リリースビルド"
	@echo "  help     - このヘルプを表示"

.PHONY: all run hello factorial fibonacci test clean debug release rebuild help
