// ライブモード
#include <gb/gb.h>
#include <gb/cgb.h>
#include <gb/hardware.h>
#include <gbdk/console.h>
#include <stdio.h>
#include "ir.h"
#include "scope.h"
#include "ui.h"
#include "menu.h"
#include "live.h"

// 波形 1 列 = 8 * BYTES_PER_COL サンプル。どちらの周波数でも 1 列 ≒ 1.95ms にそろえる
#define BYTES_PER_COL (IR_LIVE_HZ / 4096)
// 波形エリア全体の時間幅（ms）
#define SPAN_MS       ((uint16_t)(SCOPE_WIDTH * 8UL * 1000UL / 4096UL))

// 受光率は IR_LIVE_HZ サンプル（ちょうど 1 秒）ごとに更新する。
// 1 秒 = IR_LIVE_HZ / 8 バイト。分母が 2 のべき乗なので割り算の代わりにシフトで済む
#define PCT_BYTES     (IR_LIVE_HZ / 8)
#define PCT_SHIFT     ((IR_LIVE_HZ == 4096) ? 12 : 14)
// 受光を検出してからランプを点けておくフレーム数（1 フレームだけの光も見えるように）
#define LAMP_HOLD     6

// 波形は画面の下側（ライン 88〜119）に置く。HBlank DMA はライン 0〜79 で終わるので、
// 波形が表示される前に転送が済み、新旧の混ざった絵にならない
#define WAVE_Y        11
#define LAMP_X        2
#define LAMP_Y        2
#define LAMP_TILE     ((uint8_t)SCOPE_TILE_END)  // 波形タイルの次

// 数値の位置
#define VAL_X         8
#define VAL_Y_RATE    5
#define VAL_Y_LIGHT   6
#define VAL_Y_EDGES   7
#define VAL_Y_DROPS   8

static const palette_color_t pal_lamp_on[4] = {
    RGB8( 16,  24,  48), RGB8( 16,  24,  48), RGB8( 16,  24,  48), RGB8(255,  60,  40),
};
static const palette_color_t pal_lamp_off[4] = {
    RGB8( 16,  24,  48), RGB8( 16,  24,  48), RGB8( 16,  24,  48), RGB8( 70,  30,  40),
};

static const uint8_t solid_tile[16] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};

static uint8_t popcount[256];

// 受光率用（今の 1 秒ぶんの光ありサンプル数と、読んだバイト数）
static uint16_t pct_light;
static uint16_t pct_bytes;
static uint8_t light_pct;

static uint8_t ring_tail;   // リングバッファの読み位置
static uint8_t prev_bit;    // 直前のサンプル（エッジ検出用）
static uint16_t edges;      // OFF→ON の累計（65535 で一周）
static uint16_t drops;      // メインループが VBlank に間に合わなかったフレーム数

// 波形 1 列ぶんの集計
static uint8_t col_n;
static uint8_t col_or;
static uint8_t col_and;

static uint8_t frame_any;   // このフレームで 1 サンプルでも光があったか
static uint8_t lamp_hold;
static uint8_t lamp_lit;    // 画面に出ているランプの状態

static ui_field_t field_light;
static ui_field_t field_edges;
static ui_field_t field_drops;
static uint8_t field_turn;  // 桁の計算は重いので、1 フレームに 1 欄ずつ回す
static uint8_t shown_pct;
static uint16_t shown_edges;
static uint16_t shown_drops;

static void init_popcount(void) {
    uint8_t i = 0;
    do {
        popcount[i] = (i & 1) + popcount[i >> 1];
        i++;
    } while (i);
}

static void draw_static(void) {
    ui_title("LIVE");
#ifdef IR_FAKE_INPUT
    gotoxy(5, 1);
    printf("FAKE INPUT");
#endif

    set_bkg_data(LAMP_TILE, 1, solid_tile);
    fill_bkg_rect(LAMP_X, LAMP_Y, 2, 2, LAMP_TILE);
    VBK_REG = VBK_ATTRIBUTES;
    fill_bkg_rect(LAMP_X, LAMP_Y, 2, 2, LIVE_PAL_LAMP);
    VBK_REG = VBK_TILES;
    set_bkg_palette(LIVE_PAL_LAMP, 1, pal_lamp_off);
    gotoxy(LAMP_X + 3, LAMP_Y);
    printf("IR LAMP");

    gotoxy(1, VAL_Y_RATE);  printf("RATE :       HZ");
    ui_draw_dec(VAL_X, VAL_Y_RATE, IR_LIVE_HZ, 5);
    gotoxy(1, VAL_Y_LIGHT); printf("LIGHT:    %% (1S)");
    gotoxy(1, VAL_Y_EDGES); printf("EDGES:");
    gotoxy(1, VAL_Y_DROPS); printf("DROP :");

    gotoxy(0, WAVE_Y + 4);
    printf("-");
    ui_draw_dec(1, WAVE_Y + 4, SPAN_MS, 3);
    gotoxy(4, WAVE_Y + 4);
    printf("MS");
    gotoxy(17, WAVE_Y + 4);
    printf("NOW");

    ui_print(1, 16, "BLOCK=BLINK(38KHZ)");
    ui_print(1, 17, "A:CAPTURE  B:MENU");
    ui_dim_rows(16, 2);

    ui_field_init(&field_light, VAL_X, VAL_Y_LIGHT, 3);
    ui_field_init(&field_edges, VAL_X, VAL_Y_EDGES, 5);
    ui_field_init(&field_drops, VAL_X, VAL_Y_DROPS, 5);
}

// 割り込みが溜めたサンプルを読み、統計と波形の列に反映する
static void consume_samples(void) {
    uint8_t head = ir_ring_head;

    frame_any = 0;
    while (ring_tail != head) {
        uint8_t b = ir_ring[ring_tail];
        uint8_t rising;
        ring_tail++;

        frame_any |= b;
        pct_light += popcount[b];
        if (++pct_bytes == PCT_BYTES) {
            // 100 / 2^PCT_SHIFT を掛ける（100 = 64 + 32 + 4）
            uint32_t x = pct_light;
            light_pct = (uint8_t)(((x << 6) + (x << 5) + (x << 2)) >> PCT_SHIFT);
            pct_light = 0;
            pct_bytes = 0;
        }

        // 各ビットの 1 つ前のサンプル（bit7 の前は前のバイトの bit0）と比べて OFF→ON を数える
        rising = b & (uint8_t)~((uint8_t)(b >> 1) | (uint8_t)(prev_bit << 7));
        edges += popcount[rising];
        prev_bit = b & 1;

        col_or |= b;
        col_and &= b;
        if (++col_n == BYTES_PER_COL) {
            if (col_and == 0xFF) scope_push(SCOPE_HIGH);
            else if (col_or) scope_push(SCOPE_MIXED);
            else scope_push(SCOPE_LOW);
            col_n = 0;
            col_or = 0;
            col_and = 0xFF;
        }
    }

    if (frame_any) {
        lamp_hold = LAMP_HOLD;
    } else if (lamp_hold) {
        lamp_hold--;
    }

    switch (field_turn) {
    case 0:
        if (shown_pct != light_pct) ui_field_set(&field_light, shown_pct = light_pct);
        break;
    case 1:
        if (shown_edges != edges) ui_field_set(&field_edges, shown_edges = edges);
        break;
    default:
        if (shown_drops != drops) ui_field_set(&field_drops, shown_drops = drops);
        break;
    }
    if (++field_turn == 3) field_turn = 0;
}

// VBlank 中に行う画面更新（前のフレームで用意したパレットと数値を書くだけ）
static void draw_vblank(void) {
    uint8_t lit = lamp_hold ? 1 : 0;
    if (lit != lamp_lit) {
        lamp_lit = lit;
        set_bkg_palette(LIVE_PAL_LAMP, 1, lit ? pal_lamp_on : pal_lamp_off);
    }
    ui_field_flush(&field_light);
    ui_field_flush(&field_edges);
    ui_field_flush(&field_drops);
}

// 入るたびに表示と集計を初期化する
static void reset_state(void) {
    pct_light = 0;
    pct_bytes = 0;
    light_pct = 0;
    prev_bit = 0;
    edges = 0;
    drops = 0;
    col_n = 0;
    col_or = 0;
    col_and = 0xFF;
    frame_any = 0;
    lamp_hold = 0;
    lamp_lit = 0;
    field_turn = 0;
    shown_pct = 0;
    shown_edges = 0;
    shown_drops = 0;
}

uint8_t live_run(void) {
    uint8_t last_time;
    uint8_t next;

    init_popcount();
    reset_state();

    ui_clear_screen();
    draw_static();
    scope_init(0, WAVE_Y);

    ir_live_start();
    ring_tail = ir_ring_head;
    last_time = (uint8_t)sys_time;

    while (1) {
        uint8_t now;

        vsync();
        // 処理が 1 フレームに収まらなかったとき、vsync() は済んだ VBlank の印を見てすぐ戻ってくる。
        // VBlank の外で VRAM を書かないよう、次の VBlank まで待ち直す
        while (LY_REG < 144) vsync();
        // 前のフレームで描いた波形の転送を始め、VBlank のうちに数値を書く
        scope_upload();
        draw_vblank();

        // sys_time は VBlank ごとに 1 増える。2 以上進んでいたら取りこぼし
        now = (uint8_t)sys_time;
        if ((uint8_t)(now - last_time) > 1) drops += (uint8_t)(now - last_time) - 1;
        last_time = now;

        consume_samples();
        scope_render();

        next = ui_pad_update();
        if (next & J_A) { next = MODE_CAPTURE; break; }
        if (next & J_B) { next = MODE_MENU; break; }
    }

    ir_live_stop();
    return next;
}
