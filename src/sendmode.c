// 送信：キャプチャした信号の塊（BURST）を、38kHz の搬送波をつけて IR LED で送り直す
//
// Chromatic の受光部は、近くでは搬送波を塊として記録する（docs/measurements.md）。
// テレビは搬送波の点滅にしか反応しないので、記録をそのまま再生せず、
// BURST と同じ考え方で塊にまとめてから、送るときに搬送波を作り直す。
#include <gb/gb.h>
#include "capture.h"
#include "stats.h"
#include "ui.h"
#include "sendmode.h"

// 送信リストの長さ（ON / OFF 合わせて）。NEC 形式 1 回分は 67 項目
#define SEND_MAX              1024

// 搬送波 1 周期 = 5 カウント（55 M サイクル、send.s）
#define SEND_CARRIER_COUNTS   5

// 項目の切り替えで OFF が長くなるぶん（send.s の説明を参照）
#define SEND_OFF_ADJUST       6

// A を押し続けたときの、送信と送信のあいだ（フレーム数）
#define SEND_REPEAT_GAP       3

// send.s
void send_play(const uint16_t *list, uint16_t n);

static uint16_t send_list[SEND_MAX];  // ON = 搬送波の周期数、OFF = カウント。先頭は ON
static uint16_t send_n;
static uint16_t send_bursts;
static uint32_t send_total;           // 記録全体の長さ（カウント）
static uint8_t send_cut;              // リストに入りきらず途中で切った
static uint16_t send_times;

static void add(uint16_t v) {
    if (send_n < SEND_MAX) {
        send_list[send_n++] = v;
    } else {
        send_cut = 1;
    }
}

static void add_on(uint32_t counts) {
    uint32_t periods = (counts + SEND_CARRIER_COUNTS / 2) / SEND_CARRIER_COUNTS;
    if (periods == 0) periods = 1;
    if (periods > 65535UL) periods = 65535UL;
    add((uint16_t)periods);
    send_bursts++;
}

static void add_off(uint16_t counts) {
    add(counts > SEND_OFF_ADJUST ? counts - SEND_OFF_ADJUST : 1);
}

void send_prepare(void) {
    uint16_t runs = capture_runs;
    uint32_t on = 0;   // つないでいる途中の ON の長さ
    uint16_t i;

    send_n = 0;
    send_bursts = 0;
    send_total = 0;
    send_cut = 0;
    send_times = 0;

    disable_interrupts();
    for (i = 0; i < runs; i++) {
        uint16_t c = capture_read(i);
        send_total += c;
        // ON と、搬送波のすき間とみなす短い OFF は、塊としてつなぐ（stats.c の BURST と同じ）
        if ((i & 1) == 0 || c < STATS_GAP_COUNTS) {
            on += c;
        } else {
            add_on(on);
            add_off(c);
            on = 0;
        }
    }
    if (on) add_on(on);
    enable_interrupts();
}

void send_page_draw(void) {
    ui_title("SEND");
    ui_print(1, 2, "BURSTS :");
    ui_draw_dec(10, 2, send_bursts, 5);
    ui_print(1, 3, "LENGTH :       MS");
    ui_draw_dec(10, 3, (uint16_t)(stats_counts_to_us(send_total) / 1000), 5);
    ui_print(1, 4, "CARRIER: 38.1KHZ");
    ui_print(1, 5, "DUTY   : 33%");
    if (send_cut) ui_print(1, 6, "TOO LONG: CUT");

    ui_print(1, 8, "POINT THE TOP OF");
    ui_print(1, 9, "THE GBC AT THE TV.");
    ui_print(1, 10, "HOLD A TO REPEAT.");

    ui_print(1, 12, "SENT   :");
    ui_draw_dec(10, 12, send_times, 5);

    ui_print(1, 16, "SEL:PAGE  A:SEND");
    ui_print(1, 17, "B:MENU");
    ui_dim_rows(16, 2);
}

void send_page_send(void) {
    if (send_n == 0) return;
    ui_print(1, 13, "SENDING...");
    do {
        uint8_t k;
        // 送信中は割り込み禁止（ループのサイクル数を乱さないため）
        disable_interrupts();
        send_play(send_list, send_n);
        enable_interrupts();
        send_times++;
        ui_draw_dec(10, 12, send_times, 5);
        for (k = 0; k < SEND_REPEAT_GAP; k++) vsync();
    } while (joypad() & J_A);
    ui_print(1, 13, "          ");
}
