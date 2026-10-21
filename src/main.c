// gbc-ir-scope — 初期化・CGB 判定・モード遷移
#include <gb/gb.h>
#include <gb/cgb.h>
#include <stdio.h>
#include "ui.h"
#include "live.h"
#include "capmode.h"

// 背景パレット（フォントとタイルは色 0 = 背景、色 3 = 前景）
static const palette_color_t pal_bkg[] = {
    // 0: 文字
    RGB8( 16,  24,  48),  // 背景（濃紺）
    RGB8( 40,  60, 110),
    RGB8(120, 180, 220),
    RGB8(120, 255, 200),  // 文字（ミント）
    // 1: 波形（SCOPE_PAL）
    RGB8( 16,  24,  48),
    RGB8( 40,  60, 110),
    RGB8( 70, 110, 170),  // 光なしの線
    RGB8(255, 220,  90),  // 光ありの線・縦線
};

void main(void) {
    // CGB 以外（DMG / GBA）では IR が使えないので停止する
    if (_cpu != CGB_TYPE || _is_GBA) {
        printf("\n\n  GBC REQUIRED\n\n");
        printf("  THIS TOOL NEEDS\n  THE GBC IR PORT.\n");
        if (_is_GBA) printf("\n  (GBA HAS NO IR)\n");
        while (1) vsync();
    }

    // タイマーの設定は倍速モードが前提（ir.c の IR_TMA）
    cpu_fast();
    set_bkg_palette(0, sizeof(pal_bkg) / sizeof(pal_bkg[0]) / 4, pal_bkg);

    ui_init();
    while (1) {
        live_run();
        capmode_run();
    }
}
