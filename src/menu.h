// メニュー画面と HELP 画面
#ifndef MENU_H
#define MENU_H

#include <stdint.h>

// 次に入るモード（各モードの関数がこれを返し、main.c が切り替える）
#define MODE_MENU     0
#define MODE_LIVE     1
#define MODE_CAPTURE  2
#define MODE_HELP     3

// メニューを表示し、選ばれたモードを返す
uint8_t menu_run(void);

// HELP を表示する。B で戻る（MODE_MENU を返す）
uint8_t help_run(void);

#endif
