// キャプチャの記録ループと記録バッファ（capture.s）
#ifndef CAPTURE_H
#define CAPTURE_H

#include <stdint.h>

// 記録できるラン数（WRAM バンク 2〜7、4KB × 6 / 2 バイト）
#define CAPTURE_MAX_RUNS   12288

// 1 カウント = 11 M サイクル（倍速）= 11 / 2097152 秒 ≒ 5.2452µs
// µs への換算は count * CAPTURE_US_NUM >> CAPTURE_US_SHIFT（5371 / 1024 = 5.2451）
#define CAPTURE_LOOP_CYCLES 11
#define CAPTURE_US_NUM     5371UL
#define CAPTURE_US_SHIFT   10

// capture_run() の戻り値
#define CAPTURE_END_CANCEL  0  // 光を待っているあいだに B ボタン
#define CAPTURE_END_TIMEOUT 1  // 同じレベルが 65536 カウント（約 344ms）続いた
#define CAPTURE_END_FULL    2  // バッファ満杯

// 記録したラン数。第 0 ランが ON（光あり）で、以降 OFF / ON が交互
extern uint16_t capture_runs;

// 最初の OFF→ON を待って記録する。割り込みを禁止し、ir_rx_begin() してから呼ぶ。
// 倍速モード（cpu_fast）が前提
uint8_t capture_run(void);

// 第 i ランの長さ（カウント）を読み書きする。WRAM のバンクを切り替えるので、割り込みを禁止して呼ぶ
uint16_t capture_read(uint16_t i);
void capture_write(uint16_t i, uint16_t v);

#endif
