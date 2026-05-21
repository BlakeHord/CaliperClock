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

    for (;;) {
        if (clock_tick) {
            uint8_t h12, pm;
            char buf[5];
            clock_tick = 0;
            P4OUT ^= BIT0;            /* heartbeat */

            clock_get_12h(&h12, &pm);
            buf[0] = (h12 >= 10) ? '0' + h12 / 10 : ' ';  /* blank leading zero */
            buf[1] = '0' + h12 % 10;
            buf[2] = '0' + clock_min / 10;
            buf[3] = '0' + clock_min % 10;
            buf[4] = '\0';
            hal_lcd_display(buf);
        }
        __bis_SR_register(LPM3_bits | GIE);   /* sleep until the next RTC tick */
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

    for (;;) {
        button_t e = buttons_get_event();
        if (e != BTN_NONE) {
            P4OUT ^= BIT0;
            switch (e) {
            case BTN_MODE: hal_lcd_display("MODE"); break;
            case BTN_HOUR: hal_lcd_display("HOUR"); break;
            case BTN_MIN:  hal_lcd_display("MIN");  break;
            default: break;
            }
        }
        __bis_SR_register(LPM3_bits | GIE);   /* sleep until a button wakes us */
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

#else
#error "Unknown BRINGUP value (expected 1, 2, 3, 4, 5, or 6)"
#endif
