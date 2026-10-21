// メニュー画面と HELP 画面
#include <gb/gb.h>
#include <stddef.h>
#include "ui.h"
#include "menu.h"

#define MENU_X        6
#define MENU_Y        7
#define MENU_STEP     2
#define MENU_ITEMS    3

static const char * const menu_items[MENU_ITEMS] = { "LIVE", "CAPTURE", "HELP" };
static const uint8_t menu_modes[MENU_ITEMS] = { MODE_LIVE, MODE_CAPTURE, MODE_HELP };
static uint8_t menu_pos;  // 前回選んだ項目を覚えておく

static void draw_cursor(void) {
    uint8_t i;
    for (i = 0; i < MENU_ITEMS; i++) {
        ui_print(MENU_X - 2, MENU_Y + i * MENU_STEP, (i == menu_pos) ? ">" : " ");
    }
}

uint8_t menu_run(void) {
    uint8_t i;

    ui_clear_screen();
    ui_title("GBC IR SCOPE");
    ui_print(1, 2, "IR RECEIVER TESTER");
    ui_print(1, 3, "FOR GAME BOY COLOR");
    for (i = 0; i < MENU_ITEMS; i++) {
        ui_print(MENU_X, MENU_Y + i * MENU_STEP, menu_items[i]);
    }
    draw_cursor();
    ui_print(1, 16, "UD:SELECT  A:OK");
    ui_print(1, 17, "B:BACK TO MENU");
    ui_dim_rows(16, 2);

    while (1) {
        uint8_t pad;
        vsync();
        pad = ui_pad_update();
        if (pad & J_UP) {
            menu_pos = (menu_pos == 0) ? MENU_ITEMS - 1 : menu_pos - 1;
            draw_cursor();
        } else if (pad & J_DOWN) {
            menu_pos = (menu_pos + 1 == MENU_ITEMS) ? 0 : menu_pos + 1;
            draw_cursor();
        } else if (pad & (J_A | J_START)) {
            return menu_modes[menu_pos];
        }
    }
}

// ---- HELP ----
// 1 ページ = タイトル＋本文 15 行まで。1 行 19 文字まで（x = 1 から書く）

#define HELP_PAGES    4
#define HELP_LINES    15

typedef struct {
    const char *title;
    const char *lines[HELP_LINES];
} help_page_t;

static const help_page_t help_pages[HELP_PAGES] = {
    { "HELP 1/4  LIVE", {
        "LAMP : LIGHT NOW",
        "LIGHT: % OF LIGHT",
        "       IN 1 SECOND",
        "EDGES: DARK->LIGHT",
        "       COUNT",
        "DROP : LATE FRAMES",
        "       (0 IS GOOD)",
        "",
        "WAVE : LAST 312MS",
        " UPPER LINE=LIGHT",
        " LOWER LINE=DARK",
        " BLOCK=FAST BLINK",
        "",
        "A: CAPTURE",
        NULL } },
    { "HELP 2/4  CAPTURE", {
        "A: ARM. RECORDING",
        "   STARTS AT THE",
        "   FIRST LIGHT AND",
        "   STOPS AFTER",
        "   0.34S NO CHANGE",
        "SELECT: WAVE/STATS",
        "        /LIST PAGE",
        "START : BURST/RAW",
        " BURST JOINS GAPS",
        " SHORTER THAN 100US",
        "WAVE UD:ZOOM",
        "     LR:SCROLL",
        "LIST UD/LR:MOVE",
        "A: RECORD AGAIN",
        NULL } },
    { "HELP 3/4  ABOUT IR", {
        "TV REMOTES BLINK",
        "AT ABOUT 38KHZ",
        "WHILE THEY ARE ON.",
        "",
        "UP CLOSE THE GBC",
        "SENSOR SHOWS EACH",
        "BURST AS ONE BLOCK.",
        "FROM FURTHER AWAY",
        "SOME BLINKS SHOW",
        "(STATS RAW CARRIER)",
        "",
        "UNDER STEADY LIGHT",
        "THE SENSOR ADAPTS",
        "AND RETURNS TO DARK",
        "THIS IS NORMAL." } },
    { "HELP 4/4  ACCURACY", {
        "CAPTURE:",
        " 1 COUNT=5.245US",
        " (11 CYCLES AT",
        "  DOUBLE SPEED)",
        " PULSES UNDER",
        " ABOUT 16US ARE NOT",
        " EXACT, BUT THEIR",
        " AVERAGE IS.",
        " MAX 12288 RUNS",
        "",
        "LIVE:",
        " 4096 SAMPLES/S",
        " 1 PIXEL=1.95MS",
        "",
        NULL } },
};

static void draw_help_page(uint8_t page) {
    const help_page_t *hp = &help_pages[page];
    uint8_t i;
    ui_clear_screen();
    ui_title(hp->title);
    for (i = 0; i < HELP_LINES && hp->lines[i]; i++) {
        ui_print(1, 1 + i, hp->lines[i]);
    }
    ui_print(1, 17, "LR:PAGE  B:MENU");
    ui_dim_rows(17, 1);
}

uint8_t help_run(void) {
    uint8_t page = 0;
    draw_help_page(page);
    while (1) {
        uint8_t pad;
        vsync();
        pad = ui_pad_update();
        if (pad & J_B) return MODE_MENU;
        if (pad & J_RIGHT) {
            page = (page + 1 == HELP_PAGES) ? 0 : page + 1;
            draw_help_page(page);
        } else if (pad & J_LEFT) {
            page = (page == 0) ? HELP_PAGES - 1 : page - 1;
            draw_help_page(page);
        }
    }
}
