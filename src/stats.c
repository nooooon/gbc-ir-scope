// パルス幅の統計・ヒストグラムと、パルス一覧の送り
#include <gb/gb.h>
#include <string.h>
#include "capture.h"
#include "stats.h"

stats_t stats_raw;
stats_t stats_burst;
uint16_t stats_carrier_n;
uint32_t stats_carrier_sum;

// ヒストグラムの区間 = 2 × log2(c) の整数部。
// 最上位ビットの位置 m と、その次のビット（√2 倍を超えるか）で決める
static uint8_t bin_of(uint32_t c) {
    uint8_t m = 1;
    uint8_t b;
    if (c < 2) return 0;
    while (c >= 4) {
        c >>= 1;
        m++;
    }
    b = (uint8_t)(m * 2 + ((uint8_t)c & 1));  // c は 2 か 3
    if (b < STATS_BIN_FIRST) return 0;
    b -= STATS_BIN_FIRST;
    return (b >= STATS_BINS) ? STATS_BINS - 1 : b;
}

static void level_add(stats_level_t *s, uint32_t c) {
    if (s->n == 0 || c < s->min) s->min = c;
    if (c > s->max) s->max = c;
    s->sum += c;
    s->n++;
    s->hist[bin_of(c)]++;
}

void stats_compute(void) {
    uint16_t runs = capture_runs;
    uint32_t burst = 0;   // つないでいる途中の ON の長さ
    uint16_t last_on = 0;
    uint16_t i;

    memset(&stats_raw, 0, sizeof(stats_raw));
    memset(&stats_burst, 0, sizeof(stats_burst));
    stats_carrier_n = 0;
    stats_carrier_sum = 0;

    disable_interrupts();
    for (i = 0; i < runs; i++) {
        uint16_t c = capture_read(i);
        if ((i & 1) == 0) {
            level_add(&stats_raw.on, c);
            burst += c;
            last_on = c;
        } else {
            level_add(&stats_raw.off, c);
            if (c < STATS_GAP_COUNTS) {
                // 搬送波のすき間：ON とつなぎ、1 周期として数える
                burst += c;
                stats_carrier_n++;
                stats_carrier_sum += (uint32_t)last_on + c;
            } else {
                level_add(&stats_burst.on, burst);
                level_add(&stats_burst.off, c);
                burst = 0;
            }
        }
    }
    if (burst) level_add(&stats_burst.on, burst);
    enable_interrupts();
}

// count × 5371 / 1024。上位と下位に分けて 32bit を超えないようにする
uint32_t stats_counts_to_us(uint32_t counts) {
    uint32_t hi = counts >> CAPTURE_US_SHIFT;
    uint32_t lo = counts & ((1UL << CAPTURE_US_SHIFT) - 1);
    return hi * CAPTURE_US_NUM + ((lo * CAPTURE_US_NUM) >> CAPTURE_US_SHIFT);
}

// ---- パルス一覧の送り（以下の static 関数は割り込み禁止の中で呼ぶ） ----

// ON のラン s から始まる塊の最後の ON ランを返し、長さを *sum に入れる
static uint16_t burst_last(uint16_t s, uint32_t *sum) {
    uint16_t e = s;
    uint32_t t = capture_read(s);
    while (e + 2 < capture_runs) {
        uint16_t off = capture_read(e + 1);
        if (off >= STATS_GAP_COUNTS) break;
        t += off + capture_read(e + 2);
        e += 2;
    }
    *sum = t;
    return e;
}

// ON のラン e で終わる塊の最初の ON ランを返す
static uint16_t burst_first(uint16_t e) {
    uint16_t s = e;
    while (s >= 2 && capture_read(s - 1) < STATS_GAP_COUNTS) s -= 2;
    return s;
}

// ラン i から始まる項目にする（BURST の ON なら塊全体）
static void entry_at(stats_entry_t *e, uint16_t i, uint8_t burst) {
    e->first = i;
    if (burst && (i & 1) == 0) {
        e->last = burst_last(i, &e->counts);
    } else {
        e->last = i;
        e->counts = capture_read(i);
    }
}

void stats_entry_first(stats_entry_t *e, uint8_t burst) {
    e->index = 0;
    if (capture_runs == 0) {
        e->first = 0;
        e->last = 0;
        e->counts = 0;
        return;
    }
    disable_interrupts();
    entry_at(e, 0, burst);
    enable_interrupts();
}

uint8_t stats_entry_next(stats_entry_t *e, uint8_t burst) {
    uint16_t n = e->last + 1;
    if (n >= capture_runs) return 0;
    disable_interrupts();
    entry_at(e, n, burst);
    enable_interrupts();
    e->index++;
    return 1;
}

uint8_t stats_entry_prev(stats_entry_t *e, uint8_t burst) {
    uint16_t p;
    if (e->first == 0) return 0;
    p = e->first - 1;
    disable_interrupts();
    if (burst && (p & 1) == 0) p = burst_first(p);
    entry_at(e, p, burst);
    enable_interrupts();
    e->index--;
    return 1;
}
