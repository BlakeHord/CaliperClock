/*
 * buttons.c -- P1.0-1.2 button input with debounce (Task 5). See buttons.h.
 *
 * Wakes from LPM3 on a falling edge (P1 port interrupt). The Timer_A0 debounce
 * runs on ACLK, which stays active in LPM3, so the whole thing works while the
 * MCU sleeps. Register choices: SLAU445 Digital I/O (ch.8) and Timer_A (ch.13).
 */

#include <msp430.h>
#include <stdint.h>
#include "buttons.h"

#define BTN_MODE_BIT BIT0
#define BTN_HOUR_BIT BIT1
#define BTN_MIN_BIT  BIT2
#define BTN_ALL      (BTN_MODE_BIT | BTN_HOUR_BIT | BTN_MIN_BIT)

/* 50 ms debounce window at ACLK = 32768 Hz: 0.050 * 32768 = 1638. */
#define DEBOUNCE_TICKS 1638

static volatile button_t g_event = BTN_NONE;

static uint8_t bit_for(button_t b)
{
    switch (b) {
    case BTN_MODE: return BTN_MODE_BIT;
    case BTN_HOUR: return BTN_HOUR_BIT;
    case BTN_MIN:  return BTN_MIN_BIT;
    default:       return 0;
    }
}

void buttons_init(void)
{
    /* Inputs with pull-ups; pressed = pad shorted to GND = falling edge. */
    P1DIR &= ~BTN_ALL;
    P1OUT |=  BTN_ALL;            /* with REN set, selects pull-UP */
    P1REN |=  BTN_ALL;
    P1IES |=  BTN_ALL;            /* interrupt on high->low (press) */
    P1IFG &= ~BTN_ALL;            /* clear any stale edge flags */
    P1IE  |=  BTN_ALL;

    /* Timer_A0 as a 50 ms one-shot, sourced from ACLK, initially stopped. */
    TA0CCR0  = DEBOUNCE_TICKS;
    TA0CCTL0 = CCIE;
    TA0CTL   = TASSEL__ACLK | MC__STOP | TACLR;
}

button_t buttons_get_event(void)
{
    button_t e;
    uint16_t gie = __get_SR_register() & GIE;   /* save interrupt state */
    __disable_interrupt();
    e = g_event;
    g_event = BTN_NONE;
    if (gie) __enable_interrupt();              /* restore (don't force-enable) */
    return e;
}

uint8_t buttons_is_down(button_t b)
{
    uint8_t bit = bit_for(b);
    return (bit != 0) && !(P1IN & bit);         /* active low */
}

/* Falling edge on a button: latch the press, then mask the buttons and start
 * the 50 ms debounce window. */
void __attribute__((interrupt(PORT1_VECTOR))) port1_isr(void)
{
    uint8_t flags = P1IFG & BTN_ALL;

    if      (flags & BTN_MODE_BIT) g_event = BTN_MODE;
    else if (flags & BTN_HOUR_BIT) g_event = BTN_HOUR;
    else if (flags & BTN_MIN_BIT)  g_event = BTN_MIN;

    P1IFG &= ~BTN_ALL;                          /* clear the edge(s) */
    P1IE  &= ~BTN_ALL;                          /* ignore further edges... */
    TA0CTL = TASSEL__ACLK | MC__UP | TACLR;     /* ...for the next 50 ms */

    __bic_SR_register_on_exit(LPM3_bits);       /* wake main to handle it */
}

/* Debounce window elapsed: drop any bounce edges and re-arm the buttons. */
void __attribute__((interrupt(TIMER0_A0_VECTOR))) timer0_a0_isr(void)
{
    TA0CTL   = MC__STOP;                        /* one-shot: stop the timer */
    TA0CCTL0 &= ~CCIFG;
    P1IFG &= ~BTN_ALL;
    P1IE  |=  BTN_ALL;
}
