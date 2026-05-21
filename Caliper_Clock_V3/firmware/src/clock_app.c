/*
 * clock_app.c -- clock display + time-setting UI (Task 6). See clock_app.h.
 *
 * Uses Timer_A1 as a ~20 ms UI tick (matching V2's 20 ms set-mode loop),
 * sourced from ACLK so it works while sleeping in LPM3. The UI timer only runs
 * during an interaction (long-press timing + set mode); the rest of the time the
 * clock idles in LPM3 and wakes once per second on the RTC.
 */

#include <msp430.h>
#include <stdint.h>
#include "clock_app.h"
#include "rtc_clock.h"
#include "buttons.h"

/* ~20 ms UI tick at ACLK 32768 Hz: 0.020 * 32768 = 655. */
#define UI_TICK_TICKS   655
#define LONGPRESS_TICKS 250   /* 5 s  / 20 ms (enter set mode) */
#define HOLD_REPEAT     20    /* 400 ms / 20 ms (auto-repeat threshold, per V2) */
#define FLASH_PERIOD    35    /* 700 ms / 20 ms (per V2) */
#define FLASH_BLANK     5     /* 100 ms blank within each flash period */

static volatile uint8_t ui_tick = 0;

static void ui_timer_start(void)
{
    TA1CCR0  = UI_TICK_TICKS;
    TA1CCTL0 = CCIE;
    TA1CTL   = TASSEL__ACLK | MC__UP | TACLR;
}

static void ui_timer_stop(void)
{
    TA1CTL   = MC__STOP;
    TA1CCTL0 = 0;
    ui_tick  = 0;
}

/* Block (in LPM3) until the next UI tick. */
static void ui_wait(void)
{
    while (!ui_tick)
        __bis_SR_register(LPM3_bits | GIE);
    ui_tick = 0;
}

/* MODE was just pressed. Returns 1 if it stays held for the long-press time. */
static uint8_t mode_long_press(void)
{
    uint16_t ticks = 0;
    uint8_t  held  = 1;

    ui_timer_start();
    while (buttons_is_down(BTN_MODE)) {
        ui_wait();
        if (++ticks >= LONGPRESS_TICKS)
            goto done;
    }
    held = 0;                          /* released before the threshold */
done:
    ui_timer_stop();
    return held;
}

/* The time-setting interaction. Edits a local copy and commits to the RTC. */
static void run_set_mode(clock_display_fn show)
{
    uint8_t  h12, pm, mn;
    uint16_t loop = 0, hour_hold = 0, min_hold = 0;
    uint8_t  hour24;

    clock_get_12h(&h12, &pm);
    mn = clock_min;

    ui_timer_start();

    /* Wait for the entry long-press to be released first (V2 does this). */
    while (buttons_is_down(BTN_MODE))
        ui_wait();

    for (;;) {
        ui_wait();
        loop++;

        /* Flash the digits: blank for FLASH_BLANK ticks of each FLASH_PERIOD. */
        show(h12, mn, clock_colon, pm, (loop % FLASH_PERIOD) < FLASH_BLANK);

        /* HOUR: step once on press, then auto-repeat after HOLD_REPEAT ticks. */
        if (buttons_is_down(BTN_HOUR)) {
            if (hour_hold == 0 || hour_hold >= HOLD_REPEAT) {
                if (++h12 > 12) h12 = 1;
            }
            hour_hold++;
        } else {
            hour_hold = 0;
        }

        /* MIN: same hold-to-repeat behaviour. */
        if (buttons_is_down(BTN_MIN)) {
            if (min_hold == 0 || min_hold >= HOLD_REPEAT) {
                if (++mn > 59) mn = 0;
            }
            min_hold++;
        } else {
            min_hold = 0;
        }

        /* A new MODE press commits and exits. */
        if (buttons_is_down(BTN_MODE))
            break;
    }

    ui_timer_stop();

    /* Commit: 12h + PM -> 24h (mirrors V2's conversion at commit). */
    hour24 = pm ? (h12 == 12 ? 12 : h12 + 12)
                : (h12 == 12 ? 0  : h12);
    clock_set(hour24, mn);

    (void)buttons_get_event();          /* drop the exit press */
}

void clock_app_run(clock_display_fn show)
{
    uint8_t h12, pm;

    clock_get_12h(&h12, &pm);
    show(h12, clock_min, clock_colon, pm, 0);

    for (;;) {
        if (clock_tick) {               /* 1 Hz: refresh time + blink colon */
            clock_tick = 0;
            clock_get_12h(&h12, &pm);
            show(h12, clock_min, clock_colon, pm, 0);
        }

        if (buttons_get_event() == BTN_MODE && mode_long_press()) {
            run_set_mode(show);
            clock_get_12h(&h12, &pm);
            show(h12, clock_min, clock_colon, pm, 0);
        }

        __bis_SR_register(LPM3_bits | GIE);
    }
}

/* UI tick. */
void __attribute__((interrupt(TIMER1_A0_VECTOR))) timer1_a0_isr(void)
{
    TA1CCTL0 &= ~CCIFG;
    ui_tick = 1;
    __bic_SR_register_on_exit(LPM3_bits);
}
