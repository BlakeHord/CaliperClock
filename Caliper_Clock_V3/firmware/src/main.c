/*
 * Caliper Clock V3 -- bring-up entry point.
 *
 * Selects one bring-up test at compile time so each layer can be flashed and
 * verified on its own (see README "Bring-up sequence"):
 *
 *   make BRINGUP=1 flash   -> Task 1: blink LaunchPad LED2 (toolchain check)
 *   make BRINGUP=2 flash   -> Task 2: LCD_E displays "HELLO" on the LaunchPad
 *
 * Higher tasks (RTC, buttons, LPM3.5, set-time, the real caliper LCD) get added
 * as they're implemented. BRINGUP defaults to the highest test in the Makefile.
 */

#include <msp430.h>

#ifndef BRINGUP
#define BRINGUP 2
#endif

/* Common board init: stop watchdog, unlock GPIO/LCD pins out of the
 * power-up high-Z state (FR4xx LPMx.5 lock; SLAU445 Digital I/O). */
static void board_init(void)
{
    WDTCTL = WDTPW | WDTHOLD;
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

#else
#error "Unknown BRINGUP value (expected 1 or 2)"
#endif
