/*
 * rtc_clock.c -- XT1 + RTC counter timekeeping (Task 4). See rtc_clock.h.
 *
 * Register choices cite SLAU445 (CS, ch.3; RTC counter, ch.15) and the device
 * datasheet (XIN/XOUT pin select). All verified against ../Datasheets/.
 */

#include <msp430.h>
#include <stdint.h>
#include "rtc_clock.h"

volatile uint8_t clock_sec    = 0;
volatile uint8_t clock_min    = 0;
volatile uint8_t clock_hour24 = 12;     /* arbitrary power-on default (12:00) */
volatile uint8_t clock_colon  = 1;
volatile uint8_t clock_tick   = 0;

void clock_init_xt1(void)
{
    /* P4.1 = XIN, P4.2 = XOUT. Datasheet Table 9-18: the crystal function is
     * selected by P4SEL0.1 / P4SEL0.2 = 1 (PSEL1 not involved). */
    P4SEL0 |= BIT1 | BIT2;

    /* CSCTL6 reset (0x08C1) already gives LF mode (XTS=0), internal crystal
     * (XT1BYPASS=0), and highest drive for reliable startup. Clear XT1AUTOOFF
     * so XT1 stays on -- the LCD and RTC need ACLK continuously anyway.
     * (SLAU445 §3.3.7) */
    CSCTL6 &= ~XT1AUTOOFF;

    /* Start XT1 and wait until it is fault-free: repeatedly clear the oscillator
     * fault flags until OFIFG stays low (SLAU445 §3.2.11 Fault Handling). */
    do {
        CSCTL7 &= ~(XT1OFFG | DCOFFG);
        SFRIFG1 &= ~OFIFG;
    } while (SFRIFG1 & OFIFG);

    /* ACLK <- XT1 (SELA = XT1 = 0; reset default is REFO). DIVA is bypassed for
     * XT1 in LF mode, so ACLK = 32.768 kHz. (SLAU445 §3.3.5) */
    CSCTL4 &= ~SELA__REFOCLK;
}

void rtc_init(void)
{
    /* 1 Hz: XT1 (32768 Hz) / 1024 (RTCPS) = 32 Hz. Hardware says the FR4133 RTC
     * counter has the same +1 period convention as Timer_A up-mode -- i.e. the
     * counter visits 0,1,..,RTCMOD before wrapping, so the period is RTCMOD+1
     * ticks, NOT RTCMOD. We want 32 ticks per overflow, so RTCMOD = 31.
     * (Verified empirically on the LaunchPad: RTCMOD=32 gave ~1.03 s/heartbeat;
     * SLAU445 §15.2.1's "counter reaches RTCMOD then resets" wording sounded
     * like period=RTCMOD but the silicon disagrees -- trust the heartbeat.) */
    RTCMOD = 31;

    /* Source = XT1, predivide /1024, interrupt enabled. RTCSR loads the modulo
     * into the shadow register and resets the counter (TI-recommended after
     * selecting the source). */
    RTCCTL = RTCSS__XT1CLK | RTCPS__1024 | RTCIE | RTCSR;
}

void clock_set(uint8_t hour24, uint8_t minute)
{
    /* Pause just the RTC interrupt so the second/minute/hour cascade can't run
     * mid-update; no effect on the global interrupt state. */
    RTCCTL &= ~RTCIE;
    clock_hour24 = hour24 % 24;
    clock_min    = minute % 60;
    clock_sec    = 0;
    /* Reset the counter so the first second after a set is a full second (not
     * whatever fraction was left), reloading the modulo. RTCSR does not raise an
     * interrupt (SLAU445 §15.2.1). */
    RTCCTL |= RTCSR;
    (void)RTCIV;                /* drop any overflow that latched while masked */
    RTCCTL |= RTCIE;
}

/* 24h -> 12h, as in V2. */
static void to_12h(uint8_t h24, uint8_t *hour12, uint8_t *pm)
{
    if (h24 == 0)        { *hour12 = 12; *pm = 0; }
    else if (h24 < 12)   { *hour12 = h24; *pm = 0; }
    else if (h24 == 12)  { *hour12 = 12; *pm = 1; }
    else                 { *hour12 = h24 - 12; *pm = 1; }
}

void clock_read(uint8_t *hour12, uint8_t *minute, uint8_t *pm)
{
    uint8_t h24, mn;

    /* Snapshot hour+minute together with the cascade ISR masked, so we never
     * read a new minute against an old hour at the :00 rollover. */
    RTCCTL &= ~RTCIE;
    h24 = clock_hour24;
    mn  = clock_min;
    RTCCTL |= RTCIE;

    *minute = mn;
    to_12h(h24, hour12, pm);
}

/* RTC overflow -> 1 Hz. Reading RTCIV clears the flag. */
void __attribute__((interrupt(RTC_VECTOR))) rtc_isr(void)
{
    (void)RTCIV;                        /* clear RTCIFG */

    if (++clock_sec >= 60) {
        clock_sec = 0;
        if (++clock_min >= 60) {
            clock_min = 0;
            if (++clock_hour24 >= 24)
                clock_hour24 = 0;
        }
    }
    clock_colon ^= 1;
    clock_tick = 1;

    __bic_SR_register_on_exit(LPM3_bits);   /* wake main to refresh the display */
}
