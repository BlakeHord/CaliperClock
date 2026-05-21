/*
 * clock_app.h -- the clock application: display + time-setting UI (Task 6).
 *
 * Ties together the RTC (rtc_clock), buttons, and a display backend. Behaviour
 * ported from V2's time_setting_mode():
 *   - normal: show time; colon blinks at 1 Hz.
 *   - 5 s long-press of MODE enters time-setting.
 *   - HOUR / MIN adjust with hold-to-repeat (first press steps once, then auto-
 *     repeats after ~400 ms hold); digits flash ~100 ms every ~700 ms.
 *   - a short MODE press commits the new time to the RTC and exits.
 *
 * Display is abstracted so the same logic runs on the LaunchPad (FH-1138P) and
 * the V3 caliper glass.
 */
#ifndef CLOCK_APP_H
#define CLOCK_APP_H

#include <stdint.h>

/* Render the current time. blank=1 means show no digits (used for the set-mode
 * flash) while keeping colon/PM as given. hour12 is 1..12, minute 0..59. */
typedef void (*clock_display_fn)(uint8_t hour12, uint8_t minute,
                                 uint8_t colon, uint8_t pm, uint8_t blank);

/* Run the clock forever. Requires clock_init_xt1() + rtc_init() + buttons_init()
 * and interrupts enabled before calling. */
void clock_app_run(clock_display_fn show);

#endif /* CLOCK_APP_H */
