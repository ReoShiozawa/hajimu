# 日本語プログラミング言語 - Makefile

CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -g -O2
LDFLAGS = -lm -lcurl -lpthread

# Linux では -ldl が必要（macOS では不要）
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
    LDFLAGS += -ldl
endif

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
          $(SRC_DIR)/diag.c \
          $(SRC_DIR)/http.c \
          $(SRC_DIR)/async.c \
          $(SRC_DIR)/package.c \
          $(SRC_DIR)/plugin.c

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
$(BUILD_DIR)/diag.o: $(SRC_DIR)/diag.c $(SRC_DIR)/diag.h $(SRC_DIR)/lexer.h
$(BUILD_DIR)/http.o: $(SRC_DIR)/http.c $(SRC_DIR)/http.h $(SRC_DIR)/value.h
$(BUILD_DIR)/async.o: $(SRC_DIR)/async.c $(SRC_DIR)/async.h $(SRC_DIR)/evaluator.h $(SRC_DIR)/value.h
$(BUILD_DIR)/package.o: $(SRC_DIR)/package.c $(SRC_DIR)/package.h
$(BUILD_DIR)/plugin.o: $(SRC_DIR)/plugin.c $(SRC_DIR)/plugin.h $(SRC_DIR)/value.h
$(BUILD_DIR)/main.o: $(SRC_DIR)/package.h
$(BUILD_DIR)/evaluator.o: $(SRC_DIR)/package.h $(SRC_DIR)/plugin.h

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
	@echo "  all               - ビルド（デフォルト）"
	@echo "  run               - REPL起動"
	@echo "  hello             - Hello Worldサンプル実行"
	@echo "  test              - テスト実行"
	@echo "  clean             - クリーンアップ"
	@echo "  debug             - デバッグビルド"
	@echo "  release           - リリースビルド"
	@echo "  windows           - Windows向けクロスコンパイル (hajimu.exe)"
	@echo "  windows-installer - Windows インストーラー .exe を生成 (NSIS必要)"
	@echo "  help              - このヘルプを表示"

# =============================================================================
# Windows クロスコンパイル (macOS/Linux ホストから MinGW-w64 を使用)
# =============================================================================

WIN_CC      = x86_64-w64-mingw32-gcc
WIN_CFLAGS  = -Wall -Wextra -std=c11 -O2 -D_WIN32_WINNT=0x0601
WIN_BUILD   = win/build
WIN_DIST    = win/dist
WIN_TARGET  = $(WIN_DIST)/hajimu.exe

# Windows 向けソースは同じ SOURCES を使用
WIN_OBJECTS = $(SOURCES:$(SRC_DIR)/%.c=$(WIN_BUILD)/%.o)

# curl for Windows のパス (win/build_win.sh でセットアップ)
WIN_CURL_DIR = win/curl-win64
WIN_CFLAGS  += -I$(WIN_CURL_DIR)/include
WIN_LDFLAGS  = -L$(WIN_CURL_DIR)/lib -lcurl -lws2_32 -lwsock32 -lpthread -lm

windows: win/curl-win64 $(WIN_BUILD) $(WIN_DIST) $(WIN_TARGET)
	@echo "Windows ビルド完了: $(WIN_TARGET)"
	@echo "同梱 DLL を $(WIN_DIST)/ にコピー中..."
	@bash win/copy_dlls.sh "$(WIN_DIST)" "$(WIN_CURL_DIR)"

$(WIN_BUILD):
	mkdir -p $(WIN_BUILD)

$(WIN_DIST):
	mkdir -p $(WIN_DIST)

$(WIN_TARGET): $(WIN_OBJECTS)
	$(WIN_CC) $(WIN_OBJECTS) -o $@ $(WIN_LDFLAGS) -static-libgcc -static-libstdc++
	@echo "リンク完了: $@"

$(WIN_BUILD)/%.o: $(SRC_DIR)/%.c
	$(WIN_CC) $(WIN_CFLAGS) -c $< -o $@

# curl for Windows を自動取得
win/curl-win64:
	@echo "curl for Windows をダウンロード中..."
	bash win/setup_curl.sh

# NSIS インストーラー生成
windows-installer: windows
	@command -v makensis >/dev/null 2>&1 || \
	  { echo "NSISが必要です: brew install nsis"; exit 1; }
	makensis -NOCD win/installer.nsi
	@echo "インストーラー生成完了: win/dist/hajimu_setup.exe"

# Windows ビルド成果物をクリーン
clean-windows:
	rm -rf $(WIN_BUILD) $(WIN_DIST)
	@echo "Windows ビルドをクリーンアップ完了"

.PHONY: all run hello factorial fibonacci test clean debug release rebuild help \
        windows windows-installer clean-windows
