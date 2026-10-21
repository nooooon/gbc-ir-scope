// IR ポート（RP / 0xFF56）へのアクセス
// RP_REG はこのモジュール以外から触らない
#ifndef IR_H
#define IR_H

#include <stdint.h>

// ライブモードのサンプリング周波数（4096 または 16384）
// 倍速モード（cpu_fast）で動かす前提でタイマーを設定する
#ifndef IR_LIVE_HZ
#define IR_LIVE_HZ 4096
#endif

#if IR_LIVE_HZ != 4096 && IR_LIVE_HZ != 16384
#error "IR_LIVE_HZ は 4096 か 16384 にする"
#endif

// サンプルのリングバッファ（256 バイト = 2048 サンプル）
// 1 バイトに 8 サンプルを詰める。古いサンプルが bit7、新しいサンプルが bit0、1 = 光あり
// ir_ring_head は割り込み側だけが進める。読む側は自分の読み位置を持つ
extern uint8_t ir_ring[256];
extern volatile uint8_t ir_ring_head;

// 受信を開始する（読み取り有効・LED 消灯）
void ir_rx_begin(void);

// IR ポートを無効にする（電池の消耗を抑える）
void ir_off(void);

// 受光状態を正論理で返す（1 = 光あり、0 = 光なし）
uint8_t ir_read(void);

// RP レジスタの生の値（デバッグ表示用）
uint8_t ir_read_raw(void);

// タイマー割り込みによるサンプリングを開始・停止する（ir_rx_begin も行う）
void ir_live_start(void);
void ir_live_stop(void);

#endif
