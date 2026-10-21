// 文字表示の補助
#include <gb/gb.h>
#include <gbdk/console.h>
#include <stdio.h>
#include "ui.h"

static const uint16_t pow10[] = { 10000, 1000, 100, 10, 1 };

// 十字キーのリピート（フレーム数）
#define REPEAT_DELAY 20
#define REPEAT_RATE  4
#define PAD_DIRS     (J_UP | J_DOWN | J_LEFT | J_RIGHT)

static uint8_t pad_prev;
static uint8_t pad_hold;

// '0'〜'9' のタイル番号（GBDK のフォントの並びに依存しないよう、実際に書いて読み戻す）
static uint8_t digit_tile[10];

void ui_init(void) {
    uint8_t i;
    gotoxy(0, 0);
    printf("0123456789");
    for (i = 0; i < 10; i++) {
        digit_tile[i] = get_bkg_tile_xy(i, 0);
    }
    gotoxy(0, 0);
    printf("          ");
}

void ui_clear_screen(void) {
    cls();
    VBK_REG = VBK_ATTRIBUTES;
    fill_bkg_rect(0, 0, DEVICE_SCREEN_WIDTH, DEVICE_SCREEN_HEIGHT, 0);
    VBK_REG = VBK_TILES;
}

uint8_t ui_pad_update(void) {
    uint8_t cur = joypad();
    uint8_t pressed = cur & (uint8_t)~pad_prev;

    if ((cur & PAD_DIRS) && (cur & PAD_DIRS) == (pad_prev & PAD_DIRS)) {
        if (++pad_hold >= REPEAT_DELAY) {
            pad_hold = REPEAT_DELAY - REPEAT_RATE;
            pressed |= cur & PAD_DIRS;
        }
    } else {
        pad_hold = 0;
    }
    pad_prev = cur;
    return pressed;
}

// 割り算を使わず引き算で桁を出す。out には下 digits 桁を書く
static void format_dec(uint8_t *out, uint16_t v, uint8_t digits) {
    uint8_t i;
    for (i = 0; i < 5; i++) {
        uint8_t d = 0;
        while (v >= pow10[i]) {
            v -= pow10[i];
            d++;
        }
        if (i >= 5 - digits) *out++ = digit_tile[d];
    }
}

void ui_draw_dec(uint8_t x, uint8_t y, uint16_t v, uint8_t digits) {
    static uint8_t buf[5];
    format_dec(buf, v, digits);
    set_bkg_tiles(x, y, digits, 1, buf);
}

void ui_field_init(ui_field_t *f, uint8_t x, uint8_t y, uint8_t digits) {
    f->vram = get_bkg_xy_addr(x, y);
    f->len = digits;
    ui_field_set(f, 0);
}

void ui_field_set(ui_field_t *f, uint16_t v) {
    format_dec(f->tiles, v, f->len);
}

void ui_field_flush(const ui_field_t *f) {
    uint8_t *dst = f->vram;
    const uint8_t *src = f->tiles;
    uint8_t n = f->len;
    do {
        *dst++ = *src++;
    } while (--n);
}
