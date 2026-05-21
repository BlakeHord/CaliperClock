/*
 * Caliper Clock V3 -- Task 1 toolchain-verification stub.
 *
 * Blinks the MSP-EXP430FR4133 LaunchPad's onboard LED2 (P4.0, green) at ~1 Hz.
 *
 * Purpose: prove the MSP430-GCC build + mspdebug(tilib) flash path works end to
 * end on the LaunchPad. This is NOT the real firmware -- it uses a busy-delay,
 * not the RTC, and does none of the low-power setup. Tasks 2-7 replace it.
 *
 * Pin choice: LED2 = P4.0 on the FR4133 LaunchPad (TI convention; confirm
 * against the board silkscreen / SLAU595 if in doubt). We deliberately avoid
 * P1.0 (LED1) because P1.0-P1.2 are the V3 capacitive buttons -- keeping the
 * blink test off P1 stops it from being confused with the real button pins.
 * The toolchain check only needs the pin to toggle, so a scope on P4.0 proves
 * it even with no LED.
 */

#include <msp430.h>

int main(void)
{
    WDTCTL = WDTPW | WDTHOLD;        /* Stop the watchdog (SLAU445 WDT_A). */

    /*
     * FR4xx I/O powers up locked in a high-impedance state (the same mechanism
     * used to hold pins through LPMx.5). Until LOCKLPM5 is cleared, writes to
     * PxDIR/PxOUT do not reach the pins. (SLAU445, Digital I/O -> "LPMx.5".)
     */
    PM5CTL0 &= ~LOCKLPM5;

    P4DIR |=  BIT0;                  /* P4.0 (LED2) as output */
    P4OUT &= ~BIT0;                  /* start off */

    for (;;) {
        P4OUT ^= BIT0;              /* toggle LED2 */

        /*
         * Crude ~0.5 s delay -> ~1 Hz blink. After PUC the CS module defaults
         * MCLK to ~1 MHz (verify in SLAU445 "Clock System"); at 1 MHz, 500k
         * cycles ~= 0.5 s. Good enough to eyeball. Real timing arrives in
         * Task 4 via XT1 (32.768 kHz) + the RTC. __delay_cycles is a TI GCC
         * builtin and needs a compile-time-constant argument.
         */
        __delay_cycles(500000);
    }
}
