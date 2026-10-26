; send.s — 送信ループ（キャプチャした信号を IR LED で送り直す）
;
; 送信リストは ON / OFF が交互に並んだ 16bit の配列（先頭は ON）。
;   ON  ：搬送波の周期数。1 周期 = 55 M サイクル（倍速）= 5 カウント ≒ 26.2µs（38.13kHz）
;   OFF ：消灯している長さ（カウント。1 カウント = 11 M サイクル ≒ 5.245µs）
; リストは sendmode.c が作る。呼ぶ前に割り込みを禁止し、WRAM はバンク 1 にしておくこと
; （スタックを使うため）。終わると LED を消して RP = 0 にする。
;
; ■ サイクル数（倍速モードの M サイクル）
;   搬送波 1 周期 = 55：点灯 18 / 消灯 37（デューティ約 33%）
;   空白 1 カウント = 11
;   項目の切り替え：最後の点灯が終わってから次の点灯まで = 104 + 11 × OFF カウント。
;   そのうち 37 は搬送波の最後の周期の消灯ぶん（ON の周期数に含めて数えている）なので、
;   OFF が 67 サイクル長くなる。sendmode.c が OFF の長さから SEND_OFF_ADJUST（6 カウント = 66）を引く。
;   各行の右の数字は、その区間の累計サイクル数

        .module send

        .globl  _RP_REG

LED_ON          = 0x01          ; RPF_WRITE_HI（読み取りは無効のまま）
LED_OFF         = 0x00

        .area   _CODE

; void send_play(const uint16_t *list, uint16_t n)  list = de、n = bc
_send_play::
        ld      h, d
        ld      l, e
on_entry:                               ;     （OFF のあとここから点灯まで 31）
        ld      a, b                    ; 1
        or      a, c                    ; 1
        jr      z, done                 ; 2
        dec     bc                      ; 2
        push    bc                      ; 4
        ld      a, (hl+)                ; 2
        ld      c, a                    ; 1
        ld      a, (hl+)                ; 2
        ld      b, a                    ; 1   bc = 周期数
        ld      a, b                    ; 1
        or      a, c                    ; 1
        jr      z, 1$                   ; 2
        call    carrier                 ; 6   → carrier の中の点灯まで 5（計 31）
1$:
        pop     bc                      ; 3
off_entry:
        ld      a, b                    ; 1
        or      a, c                    ; 1
        jr      z, done                 ; 2
        dec     bc                      ; 2
        push    bc                      ; 4
        ld      a, (hl+)                ; 2
        ld      e, a                    ; 1
        ld      a, (hl+)                ; 2
        ld      d, a                    ; 1   de = カウント
        ld      a, d                    ; 1
        or      a, e                    ; 1
        jr      z, 2$                   ; 2
        call    space                   ; 6
2$:
        pop     bc                      ; 3
        jr      on_entry                ; 3
done:
        xor     a, a
        ldh     (_RP_REG), a
        ret

; 搬送波を bc 周期（1 以上）出す。1 周期 55。a, bc, e を壊す
; 点灯の書き込みが終わった時点を 0 として数える
carrier:
        ld      a, #LED_ON              ; 2
        ldh     (_RP_REG), a            ; 3   ← 点灯（0）
        ld      e, #3                   ; 2   2
3$:
        dec     e                       ; 1
        jr      nz, 3$                  ; 3 / 2   ループ全体で 4 × 3 - 1 = 11 → 13
        nop                             ; 1   14
        xor     a, a                    ; 1   15
        ldh     (_RP_REG), a            ; 3   18 ← 消灯
        ld      e, #6                   ; 2   20
4$:
        dec     e                       ; 1
        jr      nz, 4$                  ; 3 / 2   4 × 6 - 1 = 23 → 43
        dec     bc                      ; 2   45
        ld      a, b                    ; 1   46
        or      a, c                    ; 1   47
        jr      nz, carrier             ; 3   50 → 次の点灯（ld 2 + ldh 3）で 55
        ret                             ;     （最後の周期は jr が 2 で、ret 4）

; 消灯したまま de カウント（1 以上）待つ。1 カウント 11
space:
5$:
        dec     de                      ; 2
        ld      a, d                    ; 1
        or      a, e                    ; 1
        nop                             ; 1
        nop                             ; 1
        nop                             ; 1
        nop                             ; 1
        jr      nz, 5$                  ; 3   11
        ret
