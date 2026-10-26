// キャプチャモード
#include <gb/gb.h>
#include <gb/cgb.h>
#include <gb/hardware.h>
#include <gbdk/console.h>
#include <stdio.h>
#include "ir.h"
#include "capture.h"
#include "scope.h"
#include "stats.h"
#include "ui.h"
#include "menu.h"
#include "sendmode.h"
#include "capmode.h"

#define WAVE_Y        11

// ラン 256 個ごとの開始時刻（カウント）。表示位置からランを探すときの目印
#define INDEX_SHIFT   8
#define INDEX_LEN     (CAPTURE_MAX_RUNS >> INDEX_SHIFT)

// 1 列の幅は 2^zoom カウント。左右キーは画面の 1/4 ずつ動かす
#define SCROLL_COLS   (SCOPE_WIDTH / 4)

// 画面（SELECT で切り替え）
#define PAGE_WAVE     0
#define PAGE_STATS    1
#define PAGE_LIST     2
#define PAGE_SEND     3
#define PAGE_COUNT    4

// ヒストグラムの棒のタイル（波形タイルとランプ用タイルの次に置く）
//   HIST_TILE + k     ：下から k 行（0〜8）埋めたもの
//   HIST_TILE_LOW + k ：いちばん下の 1 行を空けて、その上を k 行（0〜7）埋めたもの（最下段用）
// 最下段の 1 行を空けるのは、すぐ下の目盛の文字と棒がくっつかないようにするため
#define HIST_TILE     ((uint8_t)(SCOPE_TILE_END + 1))
#define HIST_TILE_LOW ((uint8_t)(HIST_TILE + 9))
#define HIST_ROWS     3                 // 棒の領域（タイル）= 24 ピクセル
#define HIST_MAX_H    (HIST_ROWS * 8 - 1)  // 棒の最大の高さ（最下段の 1 行を除く）
#define HIST_ON_Y     8
#define HIST_OFF_Y    12
#define HIST_PAL_OFF  2                 // OFF の棒の色（ON は波形と同じ SCOPE_PAL）

// 一覧の行数
#define LIST_Y        2
#define LIST_ROWS     13

static uint32_t run_index[INDEX_LEN];
static uint32_t total_counts;
static uint8_t end_reason;

static uint8_t zoom;
static uint8_t zoom_max;
static uint32_t view_start;

static uint8_t page;
static uint8_t burst_mode = 1;     // 統計と一覧：1 = BURST（短い OFF をつなぐ）、0 = RAW
static stats_entry_t list_top;     // 一覧のいちばん上の項目

static const palette_color_t pal_off_bar[4] = {
    RGB8( 16,  24,  48), RGB8( 16,  24,  48), RGB8( 16,  24,  48), RGB8( 90, 150, 230),
};

// カウント → ms。count * 5.2452µs / 1000 ≒ count * 1375 / 2^18。
// そのまま掛けると 32bit を超えるので、上位（4096 カウント単位）と下位に分けて計算する（誤差 1ms 未満）
static uint16_t counts_to_ms(uint32_t counts) {
    uint32_t hi = counts >> 12;
    uint32_t lo = counts & 4095;
    return (uint16_t)((hi * 1375UL + ((lo * 1375UL) >> 12)) >> 6);
}

#ifdef IR_FAKE_INPUT
// 疑似入力：NEC 形式のリモコン信号（アドレス 0x00、コマンド 0x45）を 38kHz の搬送波つきで作る。
// 搬送波 1 周期 = ON 3 カウント + OFF 2 カウント = 26.2µs（≒ 38.1kHz）
#define FAKE_CARRIER_ON   3
#define FAKE_CARRIER_OFF  2
#define FAKE_LEADER       342   // 9ms ぶんの搬送波の周期数
#define FAKE_BIT_BURST    21    // 560µs
#define FAKE_LEADER_SPACE 858   // 4.5ms
#define FAKE_ZERO_SPACE   107   // 560µs
#define FAKE_ONE_SPACE    322   // 1690µs

static uint16_t fake_n;
static uint8_t fake_level;
static uint16_t fake_len;

static void fake_emit(uint8_t level, uint16_t counts) {
    if (level == fake_level) {
        fake_len += counts;
        return;
    }
    if (fake_n < CAPTURE_MAX_RUNS) capture_write(fake_n++, fake_len);
    fake_level = level;
    fake_len = counts;
}

static void fake_burst(uint16_t cycles) {
    while (cycles--) {
        fake_emit(1, FAKE_CARRIER_ON);
        fake_emit(0, FAKE_CARRIER_OFF);
    }
}

static void fake_byte(uint8_t v) {
    uint8_t i;
    for (i = 0; i < 8; i++) {
        fake_burst(FAKE_BIT_BURST);
        fake_emit(0, (v & 1) ? FAKE_ONE_SPACE : FAKE_ZERO_SPACE);
        v >>= 1;  // NEC は下位ビットから送る
    }
}

static uint8_t fake_capture(void) {
    fake_n = 0;
    fake_level = 1;
    fake_len = 0;
    disable_interrupts();
    fake_burst(FAKE_LEADER);
    fake_emit(0, FAKE_LEADER_SPACE);
    fake_byte(0x00);
    fake_byte(0xFF);
    fake_byte(0x45);
    fake_byte(0xBA);
    fake_burst(FAKE_BIT_BURST);
    // 最後の OFF は記録しない（実機ではタイムアウトになる部分）
    enable_interrupts();
    capture_runs = fake_n;
    return CAPTURE_END_TIMEOUT;
}
#endif

// 記録する。キャンセルされたら 0 を返す
static uint8_t do_capture(void) {
    ui_clear_screen();
    ui_title("CAPTURE");
    ui_print(1, 6, "ARMED.");
    ui_print(1, 7, "WAITING FOR LIGHT");
    ui_print(1, 17, "B:CANCEL");
    ui_dim_rows(17, 1);

#ifdef IR_FAKE_INPUT
    end_reason = fake_capture();
#else
    // 記録中は割り込み禁止（ループのサイクル数を乱さないため）。画面はそのまま表示され続ける
    ir_rx_begin();
    disable_interrupts();
    end_reason = capture_run();
    enable_interrupts();
    ir_off();
#endif
    if (end_reason == CAPTURE_END_CANCEL) return 0;
    gotoxy(1, 11); printf("CALCULATING...");
    return 1;
}

// ランの開始時刻の目印と全体の長さを作る
static void build_index(void) {
    uint32_t t = 0;
    uint16_t i;
    disable_interrupts();
    for (i = 0; i < capture_runs; i++) {
        if (((uint8_t)i) == 0) run_index[i >> INDEX_SHIFT] = t;
        t += capture_read(i);
    }
    enable_interrupts();
    total_counts = t;

    // 全体が画面に収まる最小の拡大率
    zoom_max = 0;
    while (zoom_max < 24 && ((uint32_t)SCOPE_WIDTH << zoom_max) < total_counts) zoom_max++;
}

static uint32_t view_width(void) {
    return (uint32_t)SCOPE_WIDTH << zoom;
}

static void clamp_view(void) {
    uint32_t w = view_width();
    if (total_counts <= w) {
        view_start = 0;
    } else if (view_start > total_counts - w) {
        view_start = total_counts - w;
    }
}

// 表示範囲の 160 列を作って描く。重いので、表示が変わったときだけ呼ぶ
static void render_view(void) {
    uint32_t col_w = 1UL << zoom;
    uint32_t col_a = view_start;
    uint32_t run_start, run_end;
    uint16_t runs = capture_runs;
    uint16_t i;
    uint8_t k, c;

    // 表示の左端を含むランを探す
    k = 0;
    while (k + 1 < INDEX_LEN && ((uint16_t)(k + 1) << INDEX_SHIFT) < runs && run_index[k + 1] <= col_a) k++;
    i = (uint16_t)k << INDEX_SHIFT;
    run_start = run_index[k];

    disable_interrupts();
    run_end = run_start + (i < runs ? capture_read(i) : 0);
    while (i < runs && run_end <= col_a) {
        i++;
        run_start = run_end;
        if (i < runs) run_end += capture_read(i);
    }

    scope_clear();
    for (c = 0; c < SCOPE_WIDTH; c++) {
        uint32_t col_b = col_a + col_w;
        uint8_t state;

        if (i >= runs) {
            state = SCOPE_LOW;
        } else {
            uint8_t level = (i & 1) ? SCOPE_LOW : SCOPE_HIGH;  // 第 0 ランが ON
            if (run_end >= col_b) {
                state = level;
            } else if (i + 1 < runs || level == SCOPE_HIGH) {
                state = SCOPE_MIXED;  // 列の途中でレベルが変わる
            } else {
                state = SCOPE_LOW;    // 最後のラン（OFF）のあとも OFF のまま
            }
            // この列で終わるランを読み進める
            while (i < runs && run_end <= col_b) {
                i++;
                run_start = run_end;
                if (i < runs) run_end += capture_read(i);
            }
        }
        scope_push(state);
        col_a = col_b;
    }
    enable_interrupts();

    scope_render();
}

// 全画面に共通の操作説明
static void draw_help(void) {
    ui_print(1, 16, "SEL:PAGE  A:NEW");
    ui_print(1, 17, "B:MENU  START:MODE");
    ui_dim_rows(16, 2);
}

static void print_mode(uint8_t x, uint8_t y) {
    gotoxy(x, y);
    printf(burst_mode ? "BURST" : "RAW  ");
}

// ---- 波形画面 ----

static void draw_wave_static(void) {
    ui_title("CAPTURE: WAVE");
#ifdef IR_FAKE_INPUT
    gotoxy(14, 0); printf("(FAKE)");
#endif
    gotoxy(1, 2); printf("RUNS :");
    gotoxy(1, 3); printf("END  :");
    gotoxy(1, 4); printf("TOTAL:       MS");
    gotoxy(1, 6); printf("ZOOM :");
    gotoxy(1, 7); printf("FROM :       MS");
    gotoxy(1, 8); printf("WIDTH:       MS");
    ui_print(1, 15, "UD:ZOOM LR:SCROLL");
    ui_dim_rows(15, 1);
    draw_help();

    ui_draw_dec(8, 2, capture_runs, 5);
    gotoxy(8, 3);
    printf(end_reason == CAPTURE_END_FULL ? "BUFFER FULL" : "TIMEOUT");
    ui_draw_dec(8, 4, counts_to_ms(total_counts), 5);
}

static void draw_wave_view(void) {
    // 1 列あたりの時間。2^zoom カウント × 5.2452µs
    uint32_t us = (zoom >= CAPTURE_US_SHIFT)
        ? (CAPTURE_US_NUM << (zoom - CAPTURE_US_SHIFT))
        : ((CAPTURE_US_NUM << zoom) >> CAPTURE_US_SHIFT);
    if (us < 100000UL) {
        ui_draw_dec(8, 6, (uint16_t)us, 5);
        gotoxy(13, 6); printf("US/PX");
    } else {
        ui_draw_dec(8, 6, (uint16_t)(us / 1000), 5);
        gotoxy(13, 6); printf("MS/PX");
    }
    ui_draw_dec(8, 7, counts_to_ms(view_start), 5);
    ui_draw_dec(8, 8, counts_to_ms(view_width()), 5);
}

// 波形の転送は VBlank の中で始める（scope_upload の約束）。
// HBlank DMA は CPU が HALT していると進まない（SameBoy で確認）ので、終わるまで vsync() せずに待つ
static void show_wave(void) {
    render_view();
    draw_wave_view();
    vsync();
    while (LY_REG < 144) vsync();
    scope_upload();
    scope_wait_upload();
}

// 上下で拡大・縮小（画面の中央を保つ）、左右でスクロール。表示が変わったら 1 を返す
static uint8_t wave_input(uint8_t pad) {
    if (pad & (J_UP | J_DOWN)) {
        uint32_t center = view_start + (view_width() >> 1);
        if ((pad & J_UP) && zoom > 0) zoom--;
        else if ((pad & J_DOWN) && zoom < zoom_max) zoom++;
        else return 0;
        view_start = (center > (view_width() >> 1)) ? center - (view_width() >> 1) : 0;
        clamp_view();
        return 1;
    }
    if (pad & (J_LEFT | J_RIGHT)) {
        uint32_t step = (uint32_t)SCROLL_COLS << zoom;
        uint32_t before = view_start;
        if (pad & J_LEFT) view_start = (view_start > step) ? view_start - step : 0;
        else view_start += step;
        clamp_view();
        return view_start != before;
    }
    return 0;
}

// ---- 統計画面 ----

// 行 top〜bottom を埋めたタイルを 1 枚作る。右端の 1 ピクセルは隣の棒との区切りとして空ける
static void load_bar_tile(uint8_t index, uint8_t top, uint8_t bottom) {
    static uint8_t tile[16];
    uint8_t r;
    for (r = 0; r < 8; r++) {
        uint8_t v = (r >= top && r <= bottom) ? 0xFE : 0x00;
        tile[r * 2] = v;
        tile[r * 2 + 1] = v;
    }
    set_bkg_data(index, 1, tile);
}

static void load_hist_tiles(void) {
    uint8_t k;
    for (k = 0; k <= 8; k++) load_bar_tile(HIST_TILE + k, 8 - k, 7);
    for (k = 0; k <= 7; k++) load_bar_tile(HIST_TILE_LOW + k, 7 - k, 6);
}

static void draw_hist(uint8_t y, const uint16_t *hist) {
    static uint8_t row[STATS_BINS];
    uint16_t maxv = 0;
    uint8_t b, r;
    for (b = 0; b < STATS_BINS; b++) {
        if (hist[b] > maxv) maxv = hist[b];
    }
    for (r = 0; r < HIST_ROWS; r++) {
        // r = 0 がいちばん上のタイル行。最下段は 1 行空けるので 7 ピクセルぶんしかない
        uint8_t bottom_row = (r == HIST_ROWS - 1);
        uint8_t below = bottom_row ? 0 : (uint8_t)((HIST_ROWS - 2 - r) * 8 + 7);
        uint8_t cap = bottom_row ? 7 : 8;
        for (b = 0; b < STATS_BINS; b++) {
            uint8_t h = 0, k;
            if (hist[b]) {
                // 最大の区間を HIST_MAX_H ピクセルとして比例させる（0 件でなければ最低 1 ピクセル）
                h = (uint8_t)(((uint32_t)hist[b] * HIST_MAX_H + maxv - 1) / maxv);
            }
            k = (h <= below) ? 0 : ((h - below >= cap) ? cap : h - below);
            row[b] = (bottom_row ? HIST_TILE_LOW : HIST_TILE) + k;
        }
        set_bkg_tiles(0, y + r, STATS_BINS, 1, row);
    }
}

static void draw_level_stats(uint8_t x, const stats_level_t *s) {
    ui_draw_dec32(x, 2, s->n, 6);
    if (s->n == 0) return;
    ui_draw_dec32(x, 3, stats_counts_to_us(s->min), 6);
    ui_draw_dec32(x, 4, stats_counts_to_us(s->sum / s->n), 6);
    ui_draw_dec32(x, 5, stats_counts_to_us(s->max), 6);
}

static void draw_stats(void) {
    const stats_t *st = burst_mode ? &stats_burst : &stats_raw;

    ui_title("STATS:");
    print_mode(8, 0);
    gotoxy(0, 1); printf("    ON(US)  OFF(US)");
    gotoxy(0, 2); printf("N");
    gotoxy(0, 3); printf("MIN");
    gotoxy(0, 4); printf("AVG");
    gotoxy(0, 5); printf("MAX");
    draw_level_stats(4, &st->on);
    draw_level_stats(12, &st->off);

    gotoxy(0, 6);
    if (burst_mode) {
        printf("OFF<100US = JOINED");
    } else if (stats_carrier_n) {
        // 搬送波の周波数 = 周期数 / 合計時間。kHz の 10 倍 = n * 10000 / µs
        uint32_t us = stats_counts_to_us(stats_carrier_sum);
        uint16_t f10 = us ? (uint16_t)(((uint32_t)stats_carrier_n * 10000UL) / us) : 0;
        printf("CARRIER:    .  KHZ");
        ui_draw_dec(9, 6, f10 / 10, 3);
        ui_draw_dec(13, 6, f10 % 10, 1);
    } else {
        printf("CARRIER: NONE");
    }

    gotoxy(0, HIST_ON_Y - 1);  printf("ON");
    gotoxy(0, HIST_OFF_Y - 1); printf("OFF");
    draw_hist(HIST_ON_Y, st->on.hist);
    draw_hist(HIST_OFF_Y, st->off.hist);
    // 目盛：区間 0 = 10µs、6 ≒ 100µs、12 ≒ 1ms、19 ≒ 10ms
    gotoxy(0, 15);  printf("10U");
    gotoxy(6, 15);  printf("100U");
    gotoxy(12, 15); printf("1M");
    gotoxy(17, 15); printf("10M");
    draw_help();
}

static void open_stats(void) {
    load_hist_tiles();
    set_bkg_palette(HIST_PAL_OFF, 1, pal_off_bar);
    VBK_REG = VBK_ATTRIBUTES;
    fill_bkg_rect(0, HIST_ON_Y, STATS_BINS, HIST_ROWS, SCOPE_PAL);
    fill_bkg_rect(0, HIST_OFF_Y, STATS_BINS, HIST_ROWS, HIST_PAL_OFF);
    VBK_REG = VBK_TILES;
    draw_stats();
}

// ---- 一覧画面 ----

static void draw_list(void) {
    stats_entry_t e = list_top;
    uint8_t r;

    ui_title("LIST:");
    print_mode(7, 0);
    gotoxy(0, 1); printf("    #  LV       US");
    for (r = 0; r < LIST_ROWS; r++) {
        uint8_t y = LIST_Y + r;
        if (r > 0 && !stats_entry_next(&e, burst_mode)) {
            gotoxy(0, y); printf("                    ");
            continue;
        }
        ui_draw_dec(0, y, e.index, 5);
        gotoxy(6, y);
        printf(STATS_ENTRY_ON(&e) ? "ON " : "OFF");
        ui_draw_dec32(11, y, stats_counts_to_us(e.counts), 6);
    }
    gotoxy(1, 15); printf("UD:1  LR:%u", (uint16_t)LIST_ROWS);
    ui_dim_rows(15, 1);
    draw_help();
}

// 上下で 1 項目、左右で 1 ページ動かす。動いたら 1 を返す
static uint8_t list_input(uint8_t pad) {
    uint8_t n = 0, moved = 0;
    if (pad & (J_UP | J_DOWN)) n = 1;
    else if (pad & (J_LEFT | J_RIGHT)) n = LIST_ROWS;
    while (n--) {
        if (pad & (J_UP | J_LEFT)) moved |= stats_entry_prev(&list_top, burst_mode);
        else moved |= stats_entry_next(&list_top, burst_mode);
    }
    return moved;
}

// ---- 画面の切り替え ----

static void open_page(void) {
    ui_clear_screen();
    switch (page) {
    case PAGE_WAVE:
        draw_wave_static();
        scope_init(0, WAVE_Y);
        show_wave();
        break;
    case PAGE_STATS:
        open_stats();
        break;
    case PAGE_LIST:
        draw_list();
        break;
    default:
        send_page_draw();
        break;
    }
}

uint8_t capmode_run(void) {
    while (1) {
        if (!do_capture()) return MODE_MENU;

        build_index();
        stats_compute();
        send_prepare();
        zoom = zoom_max;
        view_start = 0;
        stats_entry_first(&list_top, burst_mode);
        page = PAGE_WAVE;
        open_page();

        while (1) {
            uint8_t pad;

            vsync();
            pad = ui_pad_update();
            if (pad & J_B) return MODE_MENU;
            if (pad & J_A) {
                // SEND 画面の A は送信。それ以外の画面の A は取り直し
                if (page != PAGE_SEND) break;
                send_page_send();
                continue;
            }

            if (pad & J_SELECT) {
                if (++page == PAGE_COUNT) page = PAGE_WAVE;
                open_page();
            } else if (pad & J_START) {
                burst_mode ^= 1;
                stats_entry_first(&list_top, burst_mode);
                if (page != PAGE_WAVE) open_page();
            } else if (page == PAGE_WAVE) {
                if (wave_input(pad)) show_wave();
            } else if (page == PAGE_LIST) {
                if (list_input(pad)) draw_list();
            }
        }
    }
}
