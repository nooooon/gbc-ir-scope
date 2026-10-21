// キャプチャモード
#include <gb/gb.h>
#include <gb/hardware.h>
#include <gbdk/console.h>
#include <stdio.h>
#include "ir.h"
#include "capture.h"
#include "scope.h"
#include "ui.h"
#include "capmode.h"

#define WAVE_Y        11

// ラン 256 個ごとの開始時刻（カウント）。表示位置からランを探すときの目印
#define INDEX_SHIFT   8
#define INDEX_LEN     (CAPTURE_MAX_RUNS >> INDEX_SHIFT)

// 1 列の幅は 2^zoom カウント。左右キーは画面の 1/4 ずつ動かす
#define SCROLL_COLS   (SCOPE_WIDTH / 4)

static uint32_t run_index[INDEX_LEN];
static uint32_t total_counts;
static uint8_t end_reason;

static uint8_t zoom;
static uint8_t zoom_max;
static uint32_t view_start;

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
    gotoxy(0, 0);  printf("GBC IR SCOPE CAPTURE");
    gotoxy(1, 6);  printf("ARMED.");
    gotoxy(1, 7);  printf("WAITING FOR LIGHT");
    gotoxy(1, 9);  printf("B: CANCEL");

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
    return end_reason != CAPTURE_END_CANCEL;
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

static void draw_info_static(void) {
    ui_clear_screen();
    gotoxy(0, 0); printf("GBC IR SCOPE CAPTURE");
#ifdef IR_FAKE_INPUT
    gotoxy(5, 1); printf("FAKE INPUT");
#endif
    gotoxy(1, 2); printf("RUNS :");
    gotoxy(1, 3); printf("END  :");
    gotoxy(1, 4); printf("TOTAL:       MS");
    gotoxy(1, 6); printf("ZOOM :");
    gotoxy(1, 7); printf("FROM :       MS");
    gotoxy(1, 8); printf("WIDTH:       MS");
    gotoxy(1, 16); printf("UD:ZOOM LR:SCROLL");
    gotoxy(1, 17); printf("A:AGAIN  B:LIVE");

    ui_draw_dec(8, 2, capture_runs, 5);
    gotoxy(8, 3);
    printf(end_reason == CAPTURE_END_FULL ? "BUFFER FULL" : "TIMEOUT");
    ui_draw_dec(8, 4, counts_to_ms(total_counts), 5);
}

static void draw_info_view(void) {
    // 1 列あたりの時間。2^zoom カウント × 5.2452µs
    uint32_t us = (zoom >= CAPTURE_US_SHIFT)
        ? (CAPTURE_US_NUM << (zoom - CAPTURE_US_SHIFT))
        : ((CAPTURE_US_NUM << zoom) >> CAPTURE_US_SHIFT);
    gotoxy(8, 6);
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
static void show_view(void) {
    render_view();
    draw_info_view();
    vsync();
    while (LY_REG < 144) vsync();
    scope_upload();
    scope_wait_upload();
}

void capmode_run(void) {
    while (1) {
        uint8_t dirty;

        if (!do_capture()) return;

        build_index();
        zoom = zoom_max;
        view_start = 0;

        draw_info_static();
        scope_init(0, WAVE_Y);
        dirty = 1;

        while (1) {
            uint8_t pad;

            if (dirty) {
                show_view();
                dirty = 0;
            } else {
                vsync();
            }

            pad = ui_pad_update();
            if (pad & J_B) return;
            if (pad & J_A) break;

            if (pad & (J_UP | J_DOWN)) {
                // 画面の中央を保ったまま拡大・縮小
                uint32_t center = view_start + (view_width() >> 1);
                if ((pad & J_UP) && zoom > 0) zoom--;
                else if ((pad & J_DOWN) && zoom < zoom_max) zoom++;
                else continue;
                view_start = (center > (view_width() >> 1)) ? center - (view_width() >> 1) : 0;
                clamp_view();
                dirty = 1;
            } else if (pad & (J_LEFT | J_RIGHT)) {
                uint32_t step = (uint32_t)SCROLL_COLS << zoom;
                uint32_t before = view_start;
                if (pad & J_LEFT) view_start = (view_start > step) ? view_start - step : 0;
                else view_start += step;
                clamp_view();
                dirty = (view_start != before);
            }
        }
    }
}
