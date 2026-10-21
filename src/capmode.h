// キャプチャモード：A でアーム → 最初の OFF→ON で記録開始 → 波形を拡大・縮小・スクロールして見る
#ifndef CAPMODE_H
#define CAPMODE_H

#include <stdint.h>

// キャプチャモードを実行する。B ボタンで MODE_MENU（menu.h）を返す
uint8_t capmode_run(void);

#endif
