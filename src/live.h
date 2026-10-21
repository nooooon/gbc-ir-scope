// ライブモード：タイマー割り込みでサンプリングし、ランプと流れる波形で表示する
#ifndef LIVE_H
#define LIVE_H

// ランプが使う CGB 背景パレット番号（点灯・消灯はこのパレットの色を差し替えて切り替える）
#define LIVE_PAL_LAMP 2

// ライブモードを実行する（戻らない）
void live_run(void);

#endif
