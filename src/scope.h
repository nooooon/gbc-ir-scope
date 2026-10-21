// 波形描画：WRAM のタイルバッファにピクセルを描き、HBlank DMA で VRAM へ転送する
#ifndef SCOPE_H
#define SCOPE_H

#include <stdint.h>

// 波形エリア：20x4 タイル = 160x32 ピクセル。1 列 = 1 ピクセル幅
#define SCOPE_WIDTH      160
#define SCOPE_TILES_W    20
#define SCOPE_TILES_H    4

// 波形タイルの番号（VRAM バンク 0 の 128〜207）。
// 128〜255 は LCDC の設定によらず 0x8800〜 にあるので、DMA の転送先を固定できる
#define SCOPE_TILE_BASE  128
#define SCOPE_TILE_END   (SCOPE_TILE_BASE + SCOPE_TILES_W * SCOPE_TILES_H)

// 波形タイルが使う CGB 背景パレット番号
#define SCOPE_PAL        1

// 列の状態
#define SCOPE_LOW        0  // ずっと光なし
#define SCOPE_HIGH       1  // ずっと光あり
#define SCOPE_MIXED      2  // 列の中で ON/OFF が混ざっている（速い点滅）

// 波形エリアを (map_x, map_y) に置く
void scope_init(uint8_t map_x, uint8_t map_y);

// 列をすべて「光なし」に戻す（タイルとマップはそのまま）。
// このあと SCOPE_WIDTH 列 push すると、ちょうど画面 1 枚ぶんの列になる
void scope_clear(void);

// 右端に 1 列追加する（全体が 1 ピクセル左に流れる）
void scope_push(uint8_t state);

// 直前に描いた面の転送を始める。VBlank 中に呼ぶ（転送は次のフレームの HBlank ごとに進む）
void scope_upload(void);

// 転送が終わるまで待つ（HALT すると転送が止まるので、そのあいだ vsync() を呼ばないこと）
void scope_wait_upload(void);

// 転送に使っていないほうの面に、現在の列で波形を描く
void scope_render(void);

#endif
