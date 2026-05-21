/*
 * rtc_clock.h -- 32.768 kHz XT1 + RTC counter timekeeping (Task 4).
 *
 * Board-independent (MCU-internal). Drives ACLK from the XT1 crystal and uses
 * the RTC counter for a 1 Hz tick that maintains wall-clock time. The display
 * layer (LaunchPad or caliper LCD) is separate.
 */
#ifndef RTC_CLOCK_H
#define RTC_CLOCK_H

#include <stdint.h>

/* Timekeeping state, advanced by the RTC ISR at 1 Hz. Read in main context. */
extern volatile uint8_t clock_sec;       /* 0..59 */
extern volatile uint8_t clock_min;       /* 0..59 */
extern volatile uint8_t clock_hour24;    /* 0..23 */
extern volatile uint8_t clock_colon;     /* toggles each second (1 Hz) */
extern volatile uint8_t clock_tick;      /* set each second; clear after handling */

/* Source ACLK from the 32.768 kHz XT1 crystal (waits for it to stabilize). */
void clock_init_xt1(void);

/* Start the RTC counter for a 1 Hz interrupt. Requires clock_init_xt1() first. */
void rtc_init(void);

/* Set the time (24h). Safe against the RTC ISR; restarts a full second. */
void clock_set(uint8_t hour24, uint8_t minute);

/* Atomic snapshot of the current time as 12-hour + minute + PM flag. Reads
 * hour and minute together with the RTC interrupt briefly masked, so the
 * display can't catch a torn value across a minute/hour rollover. The 24->12
 * conversion is ported from V2's rtc_24h_to_12h. */
void clock_read(uint8_t *hour12, uint8_t *minute, uint8_t *pm);

#endif /* RTC_CLOCK_H */
