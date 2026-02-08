# はじむプラグイン用 Makefile
# 全OS共通で .hjp 拡張子を出力

CC = gcc
CFLAGS = -fPIC -Wall -Wextra -I../../include

# OS判定
UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Darwin)
    SHARED_FLAG = -shared -dynamiclib
    MATH_LIB =
else
    SHARED_FLAG = -shared
    MATH_LIB = -lm
endif

# ターゲット: 常に .hjp
all: math_plugin.hjp

math_plugin.hjp: math_plugin.c ../../include/hajimu_plugin.h
	$(CC) $(CFLAGS) $(SHARED_FLAG) -o $@ $< $(MATH_LIB)
	@echo "✅ プラグインをビルドしました: $@"

clean:
	rm -f *.hjp *.dylib *.so *.dll
	@echo "クリーンアップ完了"

.PHONY: all clean
