// 送信：キャプチャした信号の塊（BURST）を、38kHz の搬送波をつけて IR LED で送り直す
#ifndef SENDMODE_H
#define SENDMODE_H

#include <stdint.h>

// 記録したランから送信リストを作る。キャプチャのたびに 1 回呼ぶ（ラン数が多いと 1 秒ほどかかる）
void send_prepare(void);

// SEND 画面を描く（画面は ui_clear_screen() 済みであること）
void send_page_draw(void);

// 送信する。A を押しているあいだは繰り返し送る
void send_page_send(void);

#endif
