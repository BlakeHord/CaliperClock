/*
 * Caliper Clock V3 -- bring-up entry point.
 *
 * Selects one bring-up test at compile time so each layer can be flashed and
 * verified on its own (see README "Bring-up sequence"):
 *
 *   make BRINGUP=1 flash   -> Task 1: blink LaunchPad LED2 (toolchain check)
 *   make BRINGUP=2 flash   -> Task 2: LCD_E displays "HELLO" on the LaunchPad
 *   make BRINGUP=3 flash   -> Task 3: caliper LCD segment scan (V3 board only)
 *   make BRINGUP=4 flash   -> Task 4: XT1+RTC timekeeping demo on the LaunchPad
 *   make BRINGUP=5 flash   -> Task 5: buttons + LPM3 wake (P1.0-1.2 to GND)
 *   make BRINGUP=6 flash   -> Task 6: full clock + set-time UI on the LaunchPad
 *   make BRINGUP=7 flash   -> V3 final: full clock + set-time on the caliper LCD
 *   make BRINGUP=8 flash   -> diagnostic: caliper LCD statically shows "8888"
 *                            (every segment of every digit + colon + PM)
 *   make BRINGUP=9 flash   -> diagnostic: light EVERY LCD memory bit raw
 *                            (no V2 map, no caliper_lcd helpers -- proves
 *                             whether COM3 + L17 hardware actually works)
 *   make BRINGUP=10 flash  -> diagnostic: show_4digit + enable L18/L19 as LCD pins
 *                            (isolates whether enabling extra LCDPCTL bits is what
 *                             made BRINGUP=9 work, vs. the actual byte-9 writes)
 *   make BRINGUP=11 flash  -> diagnostic: show_4digit + LCDPCTL1 + write byte 9 = 0xFF
 *                            (isolates whether driving L18/L19 is what fixes things)
 *   make BRINGUP=12 flash  -> diagnostic: as BRINGUP=11 but with Mode 2 + VLCD_6
 *                            (test if charge pump needs an explicit VLCDx > 0)
 *   make BRINGUP=13 flash  -> diagnostic: as BRINGUP=11 + COM swap LCDM0W=0x1428
 *                            (swaps L0<->L3 COM assignment so firmware "COM3"
 *                             phase comes out on L0 = J1.2 = V2's actual COM3
 *                             backplane on the caliper LCD)
 *   make BRINGUP=14 flash  -> diagnostic: slow per-(Lxx,COMy) scan (~4 s each).
 *                            Records empirical (Lxx, COMy) -> physical caliper
 *                            segment map for rebuilding the V2->V3 translation.
 *                            Cycle starts: all-on 4 s + all-dark 4 s, then
 *                            L8 com0, L8 com1, ..., L18 com3 (44 steps).
 *   make BRINGUP=15 flash  -> diagnostic: static "1200" with the colon bit
 *                            alternated on/off every 3 s by software (no RTC).
 *                            Isolates the SEG_COLON write path: if the decimal
 *                            blinks here, BRINGUP=7's "no-blink" is upstream.
 *   make BRINGUP=16 flash  -> diagnostic: ONLY the SEG_COLON candidate (V2 5,3)
 *                            is lit, nothing else, toggled every 4 s. Pin down
 *                            which physical element the SEG_COLON write hits.
 *   make BRINGUP=17 flash  -> diagnostic: walks V2 (addr, com=3) for addr=1..10,
 *                            ~5 s per step, nothing else lit. Note which addr's
 *                            step lights the decimal between D2 and D1. Cycle
 *                            marker = 5 s blank between sweeps.
 *
 * Higher tasks (RTC, buttons, LPM3.5, set-time, the real caliper LCD) get added
 * as they're implemented. BRINGUP defaults to the highest test in the Makefile.
 */

#include <msp430.h>

#ifndef BRINGUP
#define BRINGUP 2
#endif

/* Common board init: stop watchdog, drive all GPIO low to minimize leakage,
 * then release the power-up high-Z state.
 *
 * Driving every pin as an output low is TI's recommended treatment for unused
 * pins (datasheet SLAS865 §7.4 "Connection of Unused Pins"); each module's init
 * then reclaims the specific pins it needs (LCD via LCDPCTL, XT1 via P4SEL0,
 * buttons as inputs). LOCKLPM5 must be cleared for the I/O config to take effect
 * (FR4xx LPMx.5 lock; SLAU445 Digital I/O). The FR4133 exposes GPIO as 16-bit
 * port pairs PA=P1:P2, PB=P3:P4, PC=P5:P6, PD=P7:P8. */
static void board_init(void)
{
    WDTCTL = WDTPW | WDTHOLD;

    PADIR = 0xFFFF; PAOUT = 0x0000;   /* P1, P2 */
    PBDIR = 0xFFFF; PBOUT = 0x0000;   /* P3, P4 */
    PCDIR = 0xFFFF; PCOUT = 0x0000;   /* P5, P6 */
    PDDIR = 0xFFFF; PDOUT = 0x0000;   /* P7, P8 */

    PM5CTL0 &= ~LOCKLPM5;
}

#if BRINGUP == 1            /* ---- Task 1: blink LED2 (P4.0) ~1 Hz ---- */

int main(void)
{
    board_init();
    P4DIR |=  BIT0;
    P4OUT &= ~BIT0;
    for (;;) {
        P4OUT ^= BIT0;
        __delay_cycles(500000);   /* ~0.5 s at ~1 MHz default MCLK */
    }
}

#elif BRINGUP == 2          /* ---- Task 2: LCD_E "HELLO" ---- */

#include "hal_lcd.h"

int main(void)
{
    board_init();
    hal_lcd_init();
    hal_lcd_display("HELLO");
    for (;;)
        __delay_cycles(1000000);  /* hold the display; nothing else yet */
}

#elif BRINGUP == 3          /* ---- Task 3: caliper LCD segment scan (V3 board) ---- */

#include "caliper_lcd.h"

int main(void)
{
    board_init();
    caliper_lcd_init();
    caliper_lcd_segment_scan_test();  /* lights each (Lxx,COM) in turn; never returns */
    return 0;
}

#elif BRINGUP == 4          /* ---- Task 4: XT1+RTC timekeeping (LaunchPad) ---- */

#include "hal_lcd.h"
#include "rtc_clock.h"

/* Validates the timekeeping engine on the LaunchPad: time on the FH-1138P glass
 * (HH MM), LED2 toggling at 1 Hz as the "RTC is ticking" heartbeat. The caliper
 * glass + colon/PM integration happens on the V3 board. */
int main(void)
{
    board_init();
    clock_init_xt1();                 /* ACLK <- 32.768 kHz crystal */
    rtc_init();                       /* 1 Hz RTC interrupt */
    hal_lcd_init();
    clock_set(12, 0);                 /* start at 12:00 */

    P4DIR |= BIT0;                    /* LED2 (P4.0) = 1 Hz heartbeat */
    __enable_interrupt();

    /* Disable interrupts to test the flag atomically with entering LPM3, so an
     * RTC tick that fires just before sleep isn't slept through (lost wakeup). */
    for (;;) {
        __disable_interrupt();
        if (clock_tick) {
            uint8_t h12, mn, pm;
            char buf[5];
            clock_tick = 0;
            __enable_interrupt();
            P4OUT ^= BIT0;            /* heartbeat */

            clock_read(&h12, &mn, &pm);
            buf[0] = (h12 >= 10) ? '0' + h12 / 10 : ' ';  /* blank leading zero */
            buf[1] = '0' + h12 % 10;
            buf[2] = '0' + mn / 10;
            buf[3] = '0' + mn % 10;
            buf[4] = '\0';
            hal_lcd_display(buf);
        } else {
            __bis_SR_register(LPM3_bits | GIE);   /* sleep until next RTC tick */
        }
    }
}

#elif BRINGUP == 5          /* ---- Task 5: buttons + LPM3 wake ---- */

#include "hal_lcd.h"
#include "buttons.h"

/* Sleeps in LPM3; a button (P1.0=MODE, P1.1=HOUR, P1.2=MIN, each shorted to GND)
 * wakes the MCU, the press is debounced, its name shows on the glass, and LED2
 * toggles. Proves the button IRQ wakes from LPM3 and debounce works. */
int main(void)
{
    board_init();
    hal_lcd_init();
    buttons_init();
    hal_lcd_display("PUSH");
    P4DIR |= BIT0;
    __enable_interrupt();

    /* Atomic test-and-sleep (see Task 4) so a press just before LPM3 entry is
     * not slept through. buttons_get_event() preserves the (disabled) interrupt
     * state, so it can be called inside the critical section. */
    for (;;) {
        button_t e;
        __disable_interrupt();
        e = buttons_get_event();
        if (e != BTN_NONE) {
            __enable_interrupt();
            P4OUT ^= BIT0;
            switch (e) {
            case BTN_MODE: hal_lcd_display("MODE"); break;
            case BTN_HOUR: hal_lcd_display("HOUR"); break;
            case BTN_MIN:  hal_lcd_display("MIN");  break;
            default: break;
            }
        } else {
            __bis_SR_register(LPM3_bits | GIE);   /* sleep until a button wakes us */
        }
    }
}

#elif BRINGUP == 6          /* ---- Task 6: full clock + set-time UI (LaunchPad) ---- */

#include "hal_lcd.h"
#include "rtc_clock.h"
#include "buttons.h"
#include "clock_app.h"

/* LaunchPad display backend: show " H:MM" style on the FH-1138P glass (no colon
 * segment exposed here, so the set-mode flash is the visual feedback). */
static void launchpad_show(uint8_t h12, uint8_t mn, uint8_t colon, uint8_t pm,
                           uint8_t blank)
{
    char buf[5];
    (void)colon; (void)pm;
    if (blank) { hal_lcd_display("    "); return; }
    buf[0] = (h12 >= 10) ? '0' + h12 / 10 : ' ';
    buf[1] = '0' + h12 % 10;
    buf[2] = '0' + mn / 10;
    buf[3] = '0' + mn % 10;
    buf[4] = '\0';
    hal_lcd_display(buf);
}

int main(void)
{
    board_init();
    clock_init_xt1();
    rtc_init();
    hal_lcd_init();
    buttons_init();
    clock_set(12, 0);
    __enable_interrupt();

    clock_app_run(launchpad_show);    /* never returns */
    return 0;
}

#elif BRINGUP == 7          /* ---- V3 final: full clock on the caliper LCD ---- */

#include "caliper_lcd.h"
#include "rtc_clock.h"
#include "buttons.h"
#include "clock_app.h"

/* V3 display backend: hand the 4-digit value + colon + PM straight to the
 * caliper-LCD driver, which already does V2's leading-zero blanking for the
 * hours. Blank means clear all four digits for the set-mode flash. */
static void caliper_show(uint8_t h12, uint8_t mn, uint8_t colon, uint8_t pm,
                         uint8_t blank)
{
    if (blank) {
        caliper_lcd_clear();
        return;
    }
    caliper_lcd_show_4digit((uint16_t)h12 * 100 + mn, colon, pm);
}

int main(void)
{
    board_init();
    clock_init_xt1();
    rtc_init();
    caliper_lcd_init();               /* the real caliper glass via LCD_E */
    buttons_init();
    clock_set(12, 0);
    __enable_interrupt();

    clock_app_run(caliper_show);      /* never returns */
    return 0;
}

#elif BRINGUP == 8          /* ---- diagnostic: caliper LCD static "8888" ---- */

#include "caliper_lcd.h"

/* Statically display 8888 with colon + PM lit. Every segment of D0/D1/D2 should
 * light (a full 8), plus the b/c/g segments of D3 (the partial digit). If a
 * digit shows fewer or different segments than this, the segment-line map
 * (HT1621_ADDR_TO_LCDE_SEG) is off. If everything looks right here but the
 * BRINGUP=7 clock is gibberish, the COM ordering between V2 and V3 differs. */
int main(void)
{
    board_init();
    caliper_lcd_init();
    caliper_lcd_show_4digit(8888, 1, 1);
    for (;;)
        __delay_cycles(1000000);
}

#elif BRINGUP == 9          /* ---- raw all-on diagnostic ---- */

#include <stdint.h>
#include "caliper_lcd.h"

/* Light every LCD-memory bit for L8..L19 (and re-confirm COM mapping). Bypasses
 * caliper_lcd_set_segment / set_lcde_segment / V2 map entirely -- writes 0xFF to
 * each LCD memory byte by raw pointer. If a (Lxx, COM) physically reaches a
 * working backplane on the caliper LCD, its segment must light here. If a
 * segment is dark in BRINGUP=8 but lights here, the issue is in caliper_lcd's
 * helper path; if it's dark in both, that signal isn't reaching the glass. */
int main(void)
{
    uint8_t i;

    board_init();
    caliper_lcd_init();                 /* same init as BRINGUP=3 and =8 */

    /* Also enable LCD function on L18, L19 (LCDPCTL1 bits 2,3), since they
     * carry LCD_SEG10 and (possibly NC) and aren't enabled by caliper_lcd_init. */
    LCDPCTL1 |= 0x000C;

    /* LCDMEM bytes: 0,1 = COM config (don't touch); 4..9 = L8..L19. Form each
     * byte's address from &LCDM0 in integer arithmetic to dodge GCC's
     * -Warray-bounds false positive on the 1-byte SFR (same trick caliper_lcd.c
     * uses via lcd_byte()). */
    for (i = 4; i <= 9; i++)
        *(volatile uint8_t *)((uintptr_t)&LCDM0 + i) = 0xFF;

    for (;;)
        __delay_cycles(1000000);
}

#elif BRINGUP == 10         /* ---- diagnostic: 8888 + LCDPCTL1 L18/L19 enabled ---- */

#include "caliper_lcd.h"

int main(void)
{
    board_init();
    caliper_lcd_init();
    /* Enable L18 and L19 as LCD pins (LCDPCTL1 bits 2, 3), but do NOT write
     * any data to byte 9 (LCDM9). Then drive 8888 via show_4digit. If this is
     * enough to make the previously-dark segments (PM, colon, D2-a, D1-b/f,
     * D0-a/b/c/f) light, then enabling extra LCDPCTL bits is what mattered in
     * BRINGUP=9, not the byte-9 writes. */
    LCDPCTL1 |= 0x000C;
    caliper_lcd_show_4digit(8888, 1, 1);
    for (;;)
        __delay_cycles(1000000);
}

#elif BRINGUP == 11         /* ---- diagnostic: 8888 + enable + drive L18/L19 ---- */

#include "caliper_lcd.h"
#include <stdint.h>

int main(void)
{
    board_init();
    caliper_lcd_init();
    LCDPCTL1 |= 0x000C;                                    /* enable L18, L19 */
    caliper_lcd_show_4digit(8888, 1, 1);
    *(volatile uint8_t *)((uintptr_t)&LCDM0 + 9) = 0xFF;  /* drive L18/L19 all COMs */
    for (;;)
        __delay_cycles(1000000);
}

#elif BRINGUP == 12         /* ---- diagnostic: like BRINGUP=11 + VLCD_6 ---- */

#include <msp430.h>
#include "caliper_lcd.h"
#include <stdint.h>

int main(void)
{
    board_init();
    caliper_lcd_init();

    /* Re-write LCDVCTL with VLCD_6 added to Mode 2 (LCDCPEN supposedly requires
     * VLCDx > 0 OR VLCDREFx > 0 to actually enable the charge pump, per SLAU445
     * Table 17-14). LCDON must be off while changing LCDVCTL. */
    LCDCTL0 &= ~LCDON;
    LCDVCTL = LCDSELVDD | LCDCPEN | VLCD_6
            | LCDCPFSEL3 | LCDCPFSEL2 | LCDCPFSEL1 | LCDCPFSEL0;
    LCDCTL0 |= LCDON;

    LCDPCTL1 |= 0x000C;                                    /* enable L18, L19 */
    caliper_lcd_show_4digit(8888, 1, 1);
    *(volatile uint8_t *)((uintptr_t)&LCDM0 + 9) = 0xFF;  /* drive L18/L19 */
    for (;;)
        __delay_cycles(1000000);
}

#elif BRINGUP == 13         /* ---- diagnostic: COM-swap test (LCDM0W = 0x1428) ---- */

#include <msp430.h>
#include "caliper_lcd.h"
#include <stdint.h>

int main(void)
{
    board_init();
    caliper_lcd_init();

    /* Swap L0 <-> L3 COM assignment: L0 = COM3, L1 = COM1, L2 = COM2, L3 = COM0.
     * Hypothesis: J1.1 (= L3) is wired to V2's unused COM0 backplane (NC), and
     * J1.2 (= L0) is wired to V2's COM3 backplane. So routing firmware "COM3"
     * phase to L0 makes V2-COM3 segments light when the firmware writes bit 3
     * of the lower nibble (its normal V2-com=3 idiom). LCDON must be off while
     * changing the COM-assignment registers. */
    LCDCTL0 &= ~LCDON;
    LCDM0W  = 0x1428;
    LCDBM0W = 0x1428;
    LCDCTL0 |= LCDON;

    LCDPCTL1 |= 0x000C;                                    /* enable L18, L19 */
    caliper_lcd_show_4digit(8888, 1, 1);
    *(volatile uint8_t *)((uintptr_t)&LCDM0 + 9) = 0xFF;  /* drive L18/L19 */
    for (;;)
        __delay_cycles(1000000);
}

#elif BRINGUP == 14         /* ---- diagnostic: slow (Lxx, COMy) scan ---- */

#include "caliper_lcd.h"

/* Walks every (Lxx, COMy) for 4 s each so you can write down which physical
 * caliper segment lights at each step. Cycle marker = all-on (4 s) + all-dark
 * (4 s), then the 44 positions start in fixed order. The recorded table inverts
 * against V2's DIGIT_SEG to give the V2->V3 (addr,com) translation. */
int main(void)
{
    board_init();
    caliper_lcd_init();                  /* same init as BRINGUP=7/8/13 */
    LCDPCTL1 |= 0x000C;                  /* keep L18 enabled (used for addr 10) */
    caliper_lcd_segment_scan_slow();     /* never returns */
    return 0;
}

#elif BRINGUP == 15         /* ---- diagnostic: static 1200, colon toggled in SW ---- */

#include "caliper_lcd.h"
#include <stdint.h>

/* Show "1200" PM with the colon bit deliberately alternated on/off every 3 s
 * by software (no RTC, no clock_app). If the decimal blinks here, the SEG_COLON
 * write path is correct and any "no-blink" in BRINGUP=7 is upstream (RTC ISR,
 * clock_colon variable, clock_app refresh). If it stays steady (on OR off),
 * then SEG_COLON's (addr, com) isn't actually wired to the dot the eye reads
 * as the decimal -- the scan's "L13 com 3 = colon/decimal" was a misread. */
int main(void)
{
    uint8_t colon = 0;

    board_init();
    caliper_lcd_init();

    for (;;) {
        caliper_lcd_show_4digit(1200, colon, 1);
        __delay_cycles(3000000);          /* ~3 s at ~1 MHz default MCLK */
        colon ^= 1;
    }
}

#elif BRINGUP == 16         /* ---- diagnostic: ONLY SEG_COLON candidate ---- */

#include "caliper_lcd.h"

/* Light ONLY V2 (5, 3) -- the SEG_COLON candidate -- alternating with all
 * segments off, ~4 s per state. No digits, no PM, no other writes. Whatever
 * physical segment appears/disappears in sync is what SEG_COLON drives.
 * Use this to definitively confirm or refute the "L13 com 3 = decimal" call
 * in the BRINGUP=14 scan. */
int main(void)
{
    board_init();
    caliper_lcd_init();

    for (;;) {
        caliper_lcd_clear();
        caliper_lcd_set_segment(5, 3, 1);
        __delay_cycles(4000000);             /* ~4 s lit */
        caliper_lcd_clear();
        __delay_cycles(4000000);             /* ~4 s dark */
    }
}

#elif BRINGUP == 17         /* ---- diagnostic: walk com=3 across V2 addrs ---- */

#include "caliper_lcd.h"
#include <stdint.h>

/* Walk V2 (addr=1..10, com=3), one segment at a time, ~5 s each. Order:
 *   (blank 5 s) -> (1,3) -> (2,3) -> ... -> (10,3) -> (blank 5 s) -> repeat.
 * Notes the addr at which the decimal between D2 and D1 lights, so SEG_COLON
 * can be relabeled to that (addr, 3). */
int main(void)
{
    board_init();
    caliper_lcd_init();

    for (;;) {
        uint8_t addr;
        caliper_lcd_clear();
        __delay_cycles(5000000);             /* ~5 s cycle marker */
        for (addr = 1; addr <= 10; addr++) {
            caliper_lcd_clear();
            caliper_lcd_set_segment(addr, 3, 1);
            __delay_cycles(5000000);
        }
    }
}

#else
#error "Unknown BRINGUP value (expected 1..17)"
#endif
