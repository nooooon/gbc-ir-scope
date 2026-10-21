# gbc-ir-scope Makefile
# GBDK-2020 の場所（環境変数 GBDK_HOME で上書き可）
GBDK_HOME ?= $(HOME)/tool/gbdk
LCC       := $(GBDK_HOME)/bin/lcc

# Emulicious の jar の場所（環境変数で上書き可）
EMULICIOUS ?= $(HOME)/tool/Emulicious/Emulicious.jar

PROJECT := gbc-ir-scope
TITLE   := IRSCOPE
SRCDIR  := src
# リリースは build/、デバッグは build/debug/ に分けて出力する
# （同じ obj を共有すると、debug 後の make で疑似入力入りの ROM が残るため）
BUILDDIR ?= build
OBJDIR  := $(BUILDDIR)/obj
ROM     := $(BUILDDIR)/$(PROJECT).gbc
DEBUG_ROM := build/debug/$(PROJECT).gbc

CSRCS := $(wildcard $(SRCDIR)/*.c)
ASRCS := $(wildcard $(SRCDIR)/*.s)
OBJS  := $(CSRCS:$(SRCDIR)/%.c=$(OBJDIR)/%.o) $(ASRCS:$(SRCDIR)/%.s=$(OBJDIR)/%.o)

CFLAGS  := -Wa-l -Wl-m -Wl-j
# -Wm-yC: CGB 専用 / -Wm-yn: タイトル / -Wm-yS: Emulicious 用シンボル
LDFLAGS := -Wm-yC -Wm-yn"$(TITLE)" -Wl-j -Wm-yS
DEFS    ?=

.PHONY: all debug run run-debug run-sameboy clean check-gbdk

all: check-gbdk $(ROM)

debug:
	$(MAKE) BUILDDIR=build/debug DEFS=-DIR_FAKE_INPUT all

check-gbdk:
	@test -x "$(LCC)" || (echo "lcc が見つかりません: $(LCC)  (GBDK_HOME を確認)"; exit 1)

# -Wf-MMD: ヘッダの依存関係を .d に出力（ヘッダだけ変えたときも再ビルドする）
$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(LCC) $(CFLAGS) -Wf-MMD $(DEFS) -c -o $@ $<

$(OBJDIR)/%.o: $(SRCDIR)/%.s | $(OBJDIR)
	$(LCC) $(CFLAGS) -c -o $@ $<

$(ROM): $(OBJS)
	$(LCC) $(LDFLAGS) -o $@ $(OBJS)

$(OBJDIR):
	mkdir -p $@

run: all
	java -jar "$(EMULICIOUS)" "$(ROM)"

run-debug: debug
	java -jar "$(EMULICIOUS)" "$(DEBUG_ROM)"

run-sameboy: all
	open -a SameBoy "$(ROM)"

clean:
	rm -rf build

-include $(OBJS:.o=.d)
