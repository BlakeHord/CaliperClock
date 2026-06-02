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
#define LONGPRESS_TICKS 250   /* 5 s   / 20 ms (MODE hold to enter set mode) */
#define HOLD_DELAY      20    /* 400 ms / 20 ms before auto-repeat begins */
#define REPEAT_EVERY    6     /* then step every 6 ticks (~120 ms), not every tick */
#define FLASH_PERIOD    35    /* 700 ms / 20 ms (per V2) */
#define FLASH_BLANK     5     /* 100 ms blank within each flash period */
#define SETMODE_TIMEOUT 1500  /* 30 s with no button held -> auto-commit and exit */

static volatile uint8_t ui_tick = 0;

/* Whether a held HOUR/MIN button should advance its digit this tick: once on the
 * initial press (hold==0), then every REPEAT_EVERY ticks after HOLD_DELAY. */
static uint8_t hold_step(uint16_t hold)
{
    return (hold == 0) ||
           (hold >= HOLD_DELAY && ((hold - HOLD_DELAY) % REPEAT_EVERY) == 0);
}

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

/* MODE was just pressed. Returns 1 if it stays held for the long-press time.
 * Once the threshold is reached, the digits start flashing while still held to
 * tell the user "you can let go now" -- the visual feedback continues until
 * MODE is released, so this function returns only after release. */
static uint8_t mode_long_press(clock_display_fn show)
{
    uint16_t ticks = 0;

    ui_timer_start();

    /* Phase 1: count to threshold while held. */
    while (buttons_is_down(BTN_MODE)) {
        ui_wait();
        if (++ticks >= LONGPRESS_TICKS)
            break;
    }
    if (!buttons_is_down(BTN_MODE)) {
        ui_timer_stop();
        return 0;                       /* released early -- short press */
    }

    /* Phase 2: threshold reached. Flash digits as a "let go now" signal and
     * spin until the user actually releases MODE before entering set mode. */
    {
        uint8_t h12, mn, pm;
        uint16_t flash = 0;
        clock_read(&h12, &mn, &pm);
        while (buttons_is_down(BTN_MODE)) {
            ui_wait();
            flash++;
            show(h12, mn, clock_colon, pm,
                 (flash % FLASH_PERIOD) < FLASH_BLANK);
        }
    }
    ui_timer_stop();
    return 1;
}

/* The time-setting interaction. Edits a local copy and commits to the RTC. */
static void run_set_mode(clock_display_fn show)
{
    uint8_t  h12, pm, mn;
    uint16_t loop = 0, hour_hold = 0, min_hold = 0, idle = 0;
    uint8_t  hour24;

    clock_read(&h12, &mn, &pm);

    /* mode_long_press() already flashed the digits and waited for the entry
     * MODE release; we land here with MODE up and the user expecting set mode. */
    ui_timer_start();

    for (;;) {
        uint8_t mode_down, hour_down, min_down;
        ui_wait();
        loop++;

        /* Flash the digits: blank for FLASH_BLANK ticks of each FLASH_PERIOD. */
        show(h12, mn, clock_colon, pm, (loop % FLASH_PERIOD) < FLASH_BLANK);

        mode_down = buttons_is_down(BTN_MODE);
        hour_down = buttons_is_down(BTN_HOUR);
        min_down  = buttons_is_down(BTN_MIN);

        /* HOUR: step once on press, then auto-repeat at REPEAT_EVERY after the
         * initial HOLD_DELAY (so a held button steps a few times/second, not 50). */
        if (hour_down) {
            if (hold_step(hour_hold) && ++h12 > 12) h12 = 1;
            hour_hold++;
        } else {
            hour_hold = 0;
        }

        /* MIN: same hold-to-repeat behaviour. */
        if (min_down) {
            if (hold_step(min_hold) && ++mn > 59) mn = 0;
            min_hold++;
        } else {
            min_hold = 0;
        }

        /* A new MODE press commits and exits. */
        if (mode_down)
            break;

        /* Auto-exit if abandoned: no button held for SETMODE_TIMEOUT ticks
         * (guards against a stuck/abandoned set mode, esp. if the pads are
         * capacitive). Any held button keeps the session alive. */
        if (mode_down || hour_down || min_down)
            idle = 0;
        else if (++idle >= SETMODE_TIMEOUT)
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
    uint8_t h12, mn, pm;

    clock_read(&h12, &mn, &pm);
    show(h12, mn, clock_colon, pm, 0);

    /*
     * Sleep/wake with no lost-wakeup race: disable interrupts, then test the
     * pending work with interrupts off. If there's work, re-enable and handle
     * it; otherwise __bis_SR_register(LPM3|GIE) atomically enters LPM3 and sets
     * GIE, so an interrupt that becomes pending exactly then still wakes us.
     */
    for (;;) {
        button_t e;

        __disable_interrupt();

        if (clock_tick) {                 /* 1 Hz: refresh time + colon */
            clock_tick = 0;
            __enable_interrupt();
            clock_read(&h12, &mn, &pm);
            show(h12, mn, clock_colon, pm, 0);
            continue;
        }

        e = buttons_get_event();          /* leaves GIE off (saved off here) */
        if (e != BTN_NONE) {
            __enable_interrupt();
            if (e == BTN_MODE && mode_long_press(show))
                run_set_mode(show);
            clock_read(&h12, &mn, &pm);
            show(h12, mn, clock_colon, pm, 0);
            continue;
        }

        __bis_SR_register(LPM3_bits | GIE);   /* nothing pending: sleep */
    }
}

/* UI tick. */
void __attribute__((interrupt(TIMER1_A0_VECTOR))) timer1_a0_isr(void)
{
    TA1CCTL0 &= ~CCIFG;
    ui_tick = 1;
    __bic_SR_register_on_exit(LPM3_bits);
}
