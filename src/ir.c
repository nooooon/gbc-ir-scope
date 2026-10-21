// IR ポート（RP / 0xFF56）へのアクセス
#include <gb/gb.h>
#include <gb/hardware.h>
#include <gb/isr.h>
#include "ir.h"

// 倍速モードでは TAC の 262144Hz 設定が 524288Hz で数える。
// 割り込み周期 = 524288 / (256 - TMA)
#define IR_TIMER_HZ 524288UL
#define IR_TMA      ((uint8_t)(256 - (IR_TIMER_HZ / IR_LIVE_HZ)))

uint8_t ir_ring[256];
volatile uint8_t ir_ring_head;

// 詰めている途中のバイト。番兵ビット（最初に 0x01 を入れておく）が 8 回のシフトで
// キャリーに押し出されたら 1 バイト分たまった合図
static uint8_t live_shift;

void ir_rx_begin(void) {
    // 自分の LED を拾わないよう、発光ビットは 0 のまま読み取りだけ有効にする
    RP_REG = RPF_ENREAD;
}

void ir_off(void) {
    RP_REG = 0;
}

#ifdef IR_FAKE_INPUT
// 疑似入力：4096 サンプル周期（fake_hi:fake_lo の 12 ビット）で次の 3 種類の信号を出す
//   0〜511    ：1 サンプルごとに点滅（速い点滅。波形では塊に見える）
//   1024〜2047：32 サンプル ON / 32 サンプル OFF のパルス
//   2560〜2815：256 サンプル ON しっぱなし
// 割り込みの中で呼ぶので、16bit 演算を避けて軽くしてある
static uint8_t fake_lo;
static uint8_t fake_hi;

static uint8_t fake_level(void) {
    if (fake_hi < 2) return fake_lo & 1;
    if (fake_hi >= 4 && fake_hi < 8) return (fake_lo >> 5) & 1;
    return fake_hi == 10;
}

static uint8_t fake_step(void) {
    uint8_t v = fake_level();
    if (++fake_lo == 0) fake_hi = (fake_hi + 1) & 15;
    return v;
}

uint8_t ir_read(void) {
    return fake_step();
}

uint8_t ir_read_raw(void) {
    // 実機と同じ形（負論理）にそろえる
    return fake_level() ? RPF_ENREAD : (RPF_ENREAD | RPF_DATAIN);
}

// 割り込みから呼ぶ。RP_REG を読んだのと同じ形で返す
uint8_t ir_fake_rp(void);
uint8_t ir_fake_rp(void) {
    return fake_step() ? RPF_ENREAD : (RPF_ENREAD | RPF_DATAIN);
}

// 割り込み内で RP_REG を読む代わりに疑似入力を呼ぶ（呼び出しで壊れる bc / de を退避）
#define IR_ISR_READ \
    push bc \
    push de \
    call _ir_fake_rp \
    pop de \
    pop bc
#else
uint8_t ir_read(void) {
    // RPF_DATAIN は負論理（0 = 受光中）なので反転して返す
    return (RP_REG & RPF_DATAIN) ? 0 : 1;
}

uint8_t ir_read_raw(void) {
    return RP_REG;
}

#define IR_ISR_READ \
    ldh a, (_RP_REG)
#endif

// タイマー割り込み：1 サンプル読んでリングバッファに詰める。
// GBDK のディスパッチャを通さず ISR_VECTOR で直接呼ぶ。16384Hz だと周期が 128 M サイクル
// （倍速時）しかないので、アセンブリで書いて負荷を抑える。
// サイクル数（倍速時の M サイクル、割り込み受付 5 と vector の jp 4 を含む）：
//   通常（7 サンプル / 8）：42
//   1 バイトたまったとき  ：78
//   平均 ≒ 47（16384Hz で CPU の約 37%、4096Hz で約 9%）
void ir_tim_isr(void) __naked {
    __asm
    push af                     ; 4
    push hl                     ; 4
    IR_ISR_READ                 ; 3   bit1 = 0 なら受光中
    cpl                         ; 1   bit1 = 1 なら受光中
    rra                         ; 1
    rra                         ; 1   キャリー = 受光中
    ld   hl, #_live_shift       ; 3
    rl   (hl)                   ; 4   サンプルを押し込み、番兵が出たらキャリー = 1
    jr   c, 1$                  ; 2 / 3
    pop  hl                     ; 3
    pop  af                     ; 3
    reti                        ; 4
1$:
    ld   a, (hl)                ; 2   8 サンプル（古い順に bit7 から）
    ld   (hl), #0x01            ; 3   番兵を入れ直す
    push de                     ; 4
    ld   e, a                   ; 1
    ld   hl, #_ir_ring_head     ; 3
    ld   a, (hl)                ; 2
    inc  (hl)                   ; 3
    add  a, #<_ir_ring          ; 2
    ld   l, a                   ; 1
    ld   a, #>_ir_ring          ; 2
    adc  a, #0                  ; 2
    ld   h, a                   ; 1
    ld   (hl), e                ; 2
    pop  de                     ; 3
    pop  hl                     ; 3
    pop  af                     ; 3
    reti                        ; 4
    __endasm;
}
ISR_VECTOR(VECTOR_TIMER, ir_tim_isr)

void ir_live_start(void) {
    ir_rx_begin();
    CRITICAL {
        live_shift = 0x01;
        TMA_REG = IR_TMA;
        TIMA_REG = IR_TMA;
        TAC_REG = TACF_START | TACF_262KHZ;
        IF_REG &= (uint8_t)~TIM_IFLAG;
        set_interrupts(IE_REG | TIM_IFLAG);
    }
}

void ir_live_stop(void) {
    CRITICAL {
        TAC_REG = TACF_STOP;
        set_interrupts(IE_REG & (uint8_t)~TIM_IFLAG);
    }
    ir_off();
}
