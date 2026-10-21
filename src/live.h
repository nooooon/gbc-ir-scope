// ライブモード：タイマー割り込みでサンプリングし、ランプと流れる波形で表示する
#ifndef LIVE_H
#define LIVE_H

#include <stdint.h>

// ランプが使う CGB 背景パレット番号（点灯・消灯はこのパレットの色を差し替えて切り替える）
#define LIVE_PAL_LAMP 2

// ライブモードを実行する。次のモード（A = キャプチャ、B = メニュー。menu.h の MODE_*）を返す
uint8_t live_run(void);

#endif
