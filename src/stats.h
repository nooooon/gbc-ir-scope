// パルス幅の統計・ヒストグラムと、パルス一覧の送り
#ifndef STATS_H
#define STATS_H

#include <stdint.h>

// BURST 表示では、この長さ未満の OFF を搬送波のすき間とみなして前後の ON とつなぐ
// （19 カウント ≒ 100µs。38kHz の搬送波の OFF は約 13µs）
#define STATS_GAP_COUNTS  19

// ヒストグラム：区間 = 2 × log2(カウント) の整数部（1 区間 = √2 倍）。
// 区間 2〜21（2 カウント ≒ 10µs 〜 2896 カウント ≒ 15ms）を表示し、範囲外は両端に入れる
#define STATS_BINS        20
#define STATS_BIN_FIRST   2

typedef struct {
    uint16_t n;
    uint32_t min;    // カウント
    uint32_t max;
    uint32_t sum;
    uint16_t hist[STATS_BINS];
} stats_level_t;

typedef struct {
    stats_level_t on;
    stats_level_t off;
} stats_t;

extern stats_t stats_raw;    // ラン 1 つずつ
extern stats_t stats_burst;  // 短い OFF をつないだ「塊」とその間

// 搬送波：BURST の中の ON + 短い OFF の組の数と、その長さの合計（カウント）
extern uint16_t stats_carrier_n;
extern uint32_t stats_carrier_sum;

// 記録したラン全体から統計を作る（ラン数が多いと 1 秒ほどかかる）
void stats_compute(void);

// カウント → µs（32bit に収まる範囲で計算）
uint32_t stats_counts_to_us(uint32_t counts);

// パルス一覧の 1 項目。RAW ではラン 1 つ、BURST の ON は first〜last のラン（OFF は 1 つ）
typedef struct {
    uint16_t index;   // 一覧での番号
    uint16_t first;   // 最初のラン
    uint16_t last;    // 最後のラン
    uint32_t counts;  // 長さ
} stats_entry_t;

// 先頭の項目にする
void stats_entry_first(stats_entry_t *e, uint8_t burst);

// 次・前の項目へ進む。なければ 0 を返して e は変えない
uint8_t stats_entry_next(stats_entry_t *e, uint8_t burst);
uint8_t stats_entry_prev(stats_entry_t *e, uint8_t burst);

// 項目が ON か（第 0 ランが ON で、以降交互）
#define STATS_ENTRY_ON(e) (((e)->first & 1) == 0)

#endif
