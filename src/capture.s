; capture.s — キャプチャモードの記録ループ（ランレングス）
;
; 「同じレベルが何ループ続いたか」を 16bit で記録する。最初のランは必ず ON（光あり）で、
; 以降 OFF, ON, OFF... と交互に並ぶ。
;
; 記録先は CGB の WRAM バンク 2〜7（0xD000〜0xDFFF を SVBK で切り替え。4KB × 6 = 12288 ラン）。
; スタックはバンク 1 の 0xD000〜0xDFFF にあるので、バンク 1 以外を選んでいるあいだは
; push / pop / call を使わない。割り込みも呼び出し側で禁止しておくこと。
;
; ■ サイクル数（倍速モードの M サイクル。1 M サイクル = 1 / 2097152 秒）
;   1 ループ = 11 M サイクル = 1 カウント ≒ 5.245µs
;   ループの形と違う経路（エッジ・カウンタ桁上がり・バンク切り替え）は 11 の倍数に nop でそろえ、
;   かかった分を次のランのカウントに入れる。こうすると、どの経路を通っても
;   「サンプル（ldh a, (RP)）から次のサンプルまで = 11 × 増えたカウント」が成り立つ。
;   各行の右の数字は、そのランのサンプルからの累計サイクル数。
;
;   経路                       サイクル  カウント
;   ループ（同じレベル）           11       1
;   下位バイトの桁上がり           22       2
;   エッジ ON→OFF                  22       2（新しいランの初期値）
;   エッジ OFF→ON                  33       3
;   エッジ＋バンク切り替え         44       4
;
;   短いランの限界：エッジの直後は次のサンプルまで 22〜33 サイクル（約 10〜16µs）あくので、
;   それより短い ON / OFF は正しく測れない。
;
; ■ 終了
;   タイムアウト：同じレベルが 65536 カウント（約 344ms）続いた。そのランは記録しない
;   バッファ満杯：12288 ラン記録した
;   キャンセル  ：最初の光を待っているあいだに B ボタン

        .module capture

        .globl  _RP_REG
        .globl  _P1_REG
        .globl  _SVBK_REG

RPF_DATAIN      = 0x02          ; 0 = 受光中
P1F_GET_BTN     = 0x10          ; ボタン側を選ぶ
P1F_B           = 0x02          ; B ボタン（0 = 押されている）

BUF_START       = 0xD000
BUF_END_HI      = 0xE0          ; h がこの値になったらバンクの終わり
BANK_FIRST      = 2
BANK_LAST       = 7
BANK_DEFAULT    = 1             ; GBDK が使う（スタックがある）バンク

; capture_run() の戻り値
END_CANCEL      = 0
END_TIMEOUT     = 1
END_FULL        = 2

        .area   _DATA
_capture_runs::                 ; 記録したラン数
        .ds     2

        .area   _CODE

; uint8_t capture_run(void)
; 戻り値：END_*。呼ぶ前に割り込みを禁止し、ir_rx_begin() で受光を有効にしておく
_capture_run::
        ld      a, #P1F_GET_BTN
        ldh     (_P1_REG), a

        ; すでに光っていたら、いったん消えるのを待つ（OFF→ON のエッジで始めたいので）
1$:
        ldh     a, (_P1_REG)
        and     #P1F_B
        jp      z, cancel
        ldh     a, (_RP_REG)
        and     #RPF_DATAIN
        jr      z, 1$

        ld      hl, #BUF_START
        ld      c, #BANK_FIRST
        ld      a, c
        ldh     (_SVBK_REG), a

        ; 最初の光を待つ（ここは時間を測らないので、ループの長さは関係ない）
wait:
        ldh     a, (_RP_REG)            ; 3
        and     #RPF_DATAIN             ; 2
        jr      z, trig                 ; 3 → 8
        ldh     a, (_P1_REG)
        and     #P1F_B
        jr      nz, wait
        jp      cancel_restore
trig:
        ld      de, #1                  ; 3   11 → 1 カウント
        ; そのまま loop_on へ

; ---- ON（光あり）のあいだ回るループ ----
loop_on:
        ldh     a, (_RP_REG)            ; 3   3
        and     #RPF_DATAIN             ; 2   5
        jr      nz, on_end              ; 2   7    （分岐すると 3 → 8）
        inc     e                       ; 1   8
        jr      nz, loop_on             ; 3   11
        ; 下位バイトが一周した                  （上の jr は 2 → 10）
        inc     d                       ; 1   11
        jr      z, timeout              ; 2   13
        inc     e                       ; 1   14   e = 1（この 22 サイクルで 2 カウント）
        nop                             ; 1   15
        nop                             ; 1   16
        nop                             ; 1   17
        nop                             ; 1   18
        nop                             ; 1   19
        jr      loop_on                 ; 3   22

on_end:                                 ;     8
        ld      a, e                    ; 1   9
        ld      (hl+), a                ; 2   11
        ld      a, d                    ; 1   12
        ld      (hl+), a                ; 2   14
        ld      a, h                    ; 1   15
        cp      #BUF_END_HI             ; 2   17
        jr      z, on_bank              ; 2   19   （分岐すると 3 → 20）
        ld      de, #2                  ; 3   22 → 2 カウント
        ; そのまま loop_off へ

; ---- OFF（光なし）のあいだ回るループ ----
loop_off:
        ldh     a, (_RP_REG)            ; 3   3
        and     #RPF_DATAIN             ; 2   5
        jr      z, off_end              ; 2   7    （分岐すると 3 → 8）
        inc     e                       ; 1   8
        jr      nz, loop_off            ; 3   11
        inc     d                       ; 1   11
        jr      z, timeout              ; 2   13
        inc     e                       ; 1   14
        nop                             ; 1   15
        nop                             ; 1   16
        nop                             ; 1   17
        nop                             ; 1   18
        nop                             ; 1   19
        jr      loop_off                ; 3   22

off_end:                                ;     8
        ld      a, e                    ; 1   9
        ld      (hl+), a                ; 2   11
        ld      a, d                    ; 1   12
        ld      (hl+), a                ; 2   14
        ld      a, h                    ; 1   15
        cp      #BUF_END_HI             ; 2   17
        jr      z, off_bank             ; 2   19   （分岐すると 3 → 20）
        ld      de, #3                  ; 3   22
        nop                             ; 1   23
        nop                             ; 1   24
        nop                             ; 1   25
        nop                             ; 1   26
        nop                             ; 1   27
        nop                             ; 1   28
        nop                             ; 1   29
        nop                             ; 1   30
        jr      loop_on                 ; 3   33 → 3 カウント

; ---- バンクの終わり：次のバンクへ ----
on_bank:                                ;     20
        inc     c                       ; 1   21
        ld      a, c                    ; 1   22
        cp      #BANK_LAST + 1          ; 2   24
        jr      z, full                 ; 2   26
        ldh     (_SVBK_REG), a          ; 3   29
        ld      hl, #BUF_START          ; 3   32
        ld      de, #4                  ; 3   35
        nop                             ; 1   36
        nop                             ; 1   37
        nop                             ; 1   38
        nop                             ; 1   39
        nop                             ; 1   40
        nop                             ; 1   41
        jr      loop_off                ; 3   44 → 4 カウント

off_bank:                               ;     20
        inc     c                       ; 1   21
        ld      a, c                    ; 1   22
        cp      #BANK_LAST + 1          ; 2   24
        jr      z, full                 ; 2   26
        ldh     (_SVBK_REG), a          ; 3   29
        ld      hl, #BUF_START          ; 3   32
        ld      de, #4                  ; 3   35
        nop                             ; 1   36
        nop                             ; 1   37
        nop                             ; 1   38
        nop                             ; 1   39
        nop                             ; 1   40
        nop                             ; 1   41
        jr      loop_on                 ; 3   44 → 4 カウント

; ---- 終了処理 ----
timeout:
        ld      b, #END_TIMEOUT
        jr      finish
full:
        ld      b, #END_FULL
finish:
        ; ラン数 = ((バンク - 2) * 4096 + (hl - 0xD000)) / 2
        ; c はバンク（満杯のときは 8）。満杯なら hl = 0xE000 で、計算は同じ式で合う
        ld      a, c
        cp      #BANK_LAST + 1
        jr      nz, 2$
        dec     c                       ; 満杯：バンク 7 の終わりとして数える
2$:
        ld      a, c
        sub     a, #BANK_FIRST          ; 0〜5
        add     a, a
        add     a, a
        add     a, a
        add     a, a                    ; × 16（= 4096 バイト / 256）
        ld      d, a
        ld      a, h
        sub     a, #>BUF_START          ; 0〜16
        add     a, d
        ld      d, a                    ; d:l = 記録したバイト数
        srl     d
        rr      l                       ; ÷ 2
        ld      a, l
        ld      (_capture_runs), a
        ld      a, d
        ld      (_capture_runs + 1), a
        ld      a, #BANK_DEFAULT
        ldh     (_SVBK_REG), a
        ld      a, b
        ret

cancel_restore:
        ld      a, #BANK_DEFAULT
        ldh     (_SVBK_REG), a
cancel:
        xor     a, a
        ld      (_capture_runs), a
        ld      (_capture_runs + 1), a
        ld      a, #END_CANCEL
        ret

; バッファの読み書き。第 i ランは バンク = 2 + i / 2048、番地 = 0xD000 + (i % 2048) * 2。
; バンクを切り替えているあいだはスタックが見えないので、切り替えから戻すまで push / pop / call をしない。
; 呼び出し側で割り込みを禁止しておくこと（割り込みもスタックを使う）

; uint16_t capture_read(uint16_t i)  i = de、戻り値 = bc
_capture_read::
        ld      a, d
        rrca
        rrca
        rrca
        and     #0x1F
        add     a, #BANK_FIRST
        ld      b, a                    ; 切り替え先バンク
        ld      a, d
        and     #0x07
        ld      h, a
        ld      l, e
        add     hl, hl
        ld      a, h
        add     a, #>BUF_START
        ld      h, a
        ld      a, b
        ldh     (_SVBK_REG), a
        ld      a, (hl+)
        ld      c, a
        ld      b, (hl)
        ld      a, #BANK_DEFAULT
        ldh     (_SVBK_REG), a
        ret

; void capture_write(uint16_t i, uint16_t v)  i = de、v = bc（疑似入力でデータを作るとき用）
_capture_write::
        ld      a, d
        rrca
        rrca
        rrca
        and     #0x1F
        add     a, #BANK_FIRST
        push    af                      ; 切り替え先バンク（ここはまだバンク 1 なので push してよい）
        ld      a, d
        and     #0x07
        ld      h, a
        ld      l, e
        add     hl, hl
        ld      a, h
        add     a, #>BUF_START
        ld      h, a
        pop     af
        ldh     (_SVBK_REG), a
        ld      a, c
        ld      (hl+), a
        ld      (hl), b
        ld      a, #BANK_DEFAULT
        ldh     (_SVBK_REG), a
        ret
