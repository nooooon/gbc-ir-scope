// 波形描画
#include <gb/gb.h>
#include <gb/hardware.h>
#include <string.h>
#include "scope.h"

#define WAVE_TILES   (SCOPE_TILES_W * SCOPE_TILES_H)  // 80 タイル
#define WAVE_BYTES   (WAVE_TILES * 16)                // 1280 バイト

// 波形エリア内の行（ピクセル）
#define ROW_HIGH     4   // 光ありの線
#define ROW_LOW      27  // 光なしの線
#define MID_BYTES    ((ROW_LOW - ROW_HIGH - 1) * 2)  // 2 本の線のあいだ（縦線だけの行）

// HDMA5 の bit7：書き込み時 1 = HBlank DMA、読み出し時 1 = 転送なし（完了）
#define HDMA_HBLANK  0x80

// 列は 8 列ずつ「タイル列」単位のリングに溜める。各要素は 8 列ぶんのビットマスク（左の列が bit7）。
// 画面の 20 タイル列＋書き込み途中の 1 つで 21 要素
#define RING_LEN     (SCOPE_TILES_W + 1)

static uint8_t ring_high[RING_LEN];  // 光ありの列
static uint8_t ring_low[RING_LEN];   // 光なしの列
static uint8_t ring_edge[RING_LEN];  // 縦線を引く列（混在、または前の列から状態が変わった）
static uint8_t ring_pos;             // 書き込み途中の要素
static uint8_t ring_fill;            // その要素に入っている列数（0〜7）
static uint8_t last_state;

static const uint8_t col_bit[8] = { 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01 };

// タイルバッファ（2 面）。HBlank DMA が片方を転送しているあいだに、もう片方へ描く。
// DMA の転送元は 16 バイト境界が必要なので、余分に取って境界にそろえる。
// タイル番号 = SCOPE_TILE_BASE + tx * 4 + ty とし、1 タイル列（4 タイル = 64 バイト）が
// 連続するように並べる。こうすると波形エリアの行 r は、タイル列の先頭から r * 2 バイト目になる
static uint8_t wave_raw[WAVE_BYTES * 2 + 16];
static uint8_t *wave_buf[2];
static uint8_t draw_side;  // 次に描く面

// 面ごと・タイル列ごとに、縦線の行へ最後に書いたマスク（同じなら書き直さない）
static uint8_t drawn_edge[2][SCOPE_TILES_W];

void scope_init(uint8_t map_x, uint8_t map_y) {
    static uint8_t row[SCOPE_TILES_W];
    uint8_t tx, ty;

    wave_buf[0] = (uint8_t *)(((uint16_t)wave_raw + 15) & 0xFFF0);
    wave_buf[1] = wave_buf[0] + WAVE_BYTES;
    memset(wave_buf[0], 0, WAVE_BYTES * 2);
    memset(drawn_edge, 0, sizeof(drawn_edge));
    draw_side = 0;

    // 最初は全部「光なし」
    memset(ring_high, 0, sizeof(ring_high));
    memset(ring_low, 0xFF, sizeof(ring_low));
    memset(ring_edge, 0, sizeof(ring_edge));
    ring_pos = 0;
    ring_fill = 0;
    ring_low[0] = 0;
    last_state = SCOPE_LOW;

    set_bkg_data(SCOPE_TILE_BASE, WAVE_TILES, wave_buf[0]);
    for (ty = 0; ty < SCOPE_TILES_H; ty++) {
        for (tx = 0; tx < SCOPE_TILES_W; tx++) {
            row[tx] = SCOPE_TILE_BASE + tx * SCOPE_TILES_H + ty;
        }
        set_bkg_tiles(map_x, map_y + ty, SCOPE_TILES_W, 1, row);
    }
    VBK_REG = VBK_ATTRIBUTES;
    fill_bkg_rect(map_x, map_y, SCOPE_TILES_W, SCOPE_TILES_H, SCOPE_PAL);
    VBK_REG = VBK_TILES;
}

void scope_push(uint8_t state) {
    uint8_t bit = col_bit[ring_fill];
    uint8_t pos = ring_pos;

    if (state == SCOPE_HIGH) ring_high[pos] |= bit;
    else if (state == SCOPE_LOW) ring_low[pos] |= bit;
    if (state == SCOPE_MIXED || state != last_state) ring_edge[pos] |= bit;
    last_state = state;

    if (++ring_fill == 8) {
        ring_fill = 0;
        if (++pos == RING_LEN) pos = 0;
        ring_pos = pos;
        ring_high[pos] = 0;
        ring_low[pos] = 0;
        ring_edge[pos] = 0;
    }
}

// HBlank DMA は 1 ラインに 16 バイトずつ送るので、VBlank の時間を使わない。
// 80 ブロックなので次のフレームのライン 0〜79 で終わる。
// 転送先のバンクは VBK で決まるため、転送中は VBK をバンク 0 から動かさないこと
void scope_upload(void) {
    uint16_t src;
    uint16_t dst = (uint16_t)_VRAM8800;  // タイル 128 の位置

    // 前回の転送がまだ終わっていなければ、このフレームは送らない（描いた面もそのまま使う）
    if (!(HDMA5_REG & HDMA_HBLANK)) return;

    // 直前に描いた面を送り、次はもう片方の面に描く
    src = (uint16_t)wave_buf[draw_side];
    draw_side ^= 1;

    HDMA1_REG = (uint8_t)(src >> 8);
    HDMA2_REG = (uint8_t)src;
    HDMA3_REG = (uint8_t)(dst >> 8);
    HDMA4_REG = (uint8_t)dst;
    HDMA5_REG = HDMA_HBLANK | (WAVE_BYTES / 16 - 1);
}

// 色：光ありの線と縦線 = 色 3、光なしの線 = 色 2
// 画面の右端が最新の列。書き込み途中の要素に k 列入っていれば、各タイル列は
// リングの隣り合う 2 要素を k ビットずらして組み合わせたものになる
void scope_render(void) {
    uint8_t k = ring_fill;
    uint8_t rk = 8 - k;
    uint8_t i = ring_pos + 1;  // 画面の左端のタイル列 = リングで最も古い要素
    uint8_t *p = wave_buf[draw_side];
    uint8_t *drawn = drawn_edge[draw_side];
    uint8_t tx;

    if (i == RING_LEN) i = 0;

    for (tx = 0; tx < SCOPE_TILES_W; tx++) {
        uint8_t j = i + 1;
        uint8_t high, low, edge;
        uint8_t *q;

        if (j == RING_LEN) j = 0;
        if (k) {
            high = (uint8_t)(ring_high[i] << k) | (uint8_t)(ring_high[j] >> rk);
            low  = (uint8_t)(ring_low[i]  << k) | (uint8_t)(ring_low[j]  >> rk);
            edge = (uint8_t)(ring_edge[i] << k) | (uint8_t)(ring_edge[j] >> rk);
        } else {
            high = ring_high[i];
            low = ring_low[i];
            edge = ring_edge[i];
        }
        i = j;

        // 行 0〜3 と 28〜31 は常に空なので書かない
        q = p + ROW_HIGH * 2;
        q[0] = high | edge;
        q[1] = high | edge;
        if (edge != drawn[tx]) {
            drawn[tx] = edge;
            memset(q + 2, edge, MID_BYTES);
        }
        q += 2 + MID_BYTES;
        q[0] = edge;
        q[1] = low | edge;

        p += SCOPE_TILES_H * 16;
    }
}
