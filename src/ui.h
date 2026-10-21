// 文字表示の補助
#ifndef UI_H
#define UI_H

#include <stdint.h>

// 毎フレーム更新する数値欄。
// GBDK の set_bkg_tiles は 1 タイルごとに STAT を待つので、VBlank の外だと 1 ラインに 1 タイルしか書けない。
// そこで桁の計算（ui_field_set）は VBlank の外で済ませ、VBlank 中は VRAM へ直接書く（ui_field_flush）
typedef struct {
    uint8_t *vram;      // マップ上の先頭の番地
    uint8_t len;        // 桁数（1〜5）
    uint8_t tiles[5];   // 表示するタイル番号
} ui_field_t;

// 数字のタイル番号を調べる。printf を使えるようになった後、最初に 1 回呼ぶ
void ui_init(void);

// 画面の文字と CGB 属性（パレット番号）を消す。モードに入るときに呼ぶ
void ui_clear_screen(void);

// ボタンを読み、新しく押されたボタン（J_A など）を返す。1 フレームに 1 回呼ぶ。
// 十字キーは押しっぱなしにすると一定間隔で繰り返し押されたことにする
uint8_t ui_pad_update(void);

// 10 進 digits 桁（ゼロ埋め、1〜5 桁）を (x, y) に表示する。初期化時の固定表示用
void ui_draw_dec(uint8_t x, uint8_t y, uint16_t v, uint8_t digits);

void ui_field_init(ui_field_t *f, uint8_t x, uint8_t y, uint8_t digits);

// 表示する値を決める（VRAM には触らない）
void ui_field_set(ui_field_t *f, uint16_t v);

// VRAM へ書く。VBlank 中に呼ぶこと（STAT を待たない）
void ui_field_flush(const ui_field_t *f);

#endif
