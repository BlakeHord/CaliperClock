/*
 * hal_lcd.h -- LaunchPad FH-1138P LCD via the MSP430FR4133 LCD_E peripheral.
 *
 * Task 2 bring-up driver. Targets the 6-digit alphanumeric glass on the
 * MSP-EXP430FR4133 LaunchPad (NOT the caliper LCD -- that's Task 3). Used only
 * to prove LCD_E is configured correctly. Display logic + segment tables are
 * adapted from TI's hal_LCD.c (E. Chen, 2014, BSD-3); the bare-register init is
 * adapted from Energia's LCD_Launchpad. See hal_lcd.c for register references.
 */
#ifndef HAL_LCD_H
#define HAL_LCD_H

/* Number of alphanumeric digit positions on the FH-1138P glass. */
#define HAL_LCD_NUM_DIGITS 6

/* Configure LCD_E (4-mux, internal charge pump) and turn the display on.
 * Relies on the default ACLK (~32 kHz) being live after reset, as TI's
 * out-of-box demo does. Task 4 switches ACLK to the 32.768 kHz XT1 crystal. */
void hal_lcd_init(void);

/* Blank all six digits. */
void hal_lcd_clear(void);

/* Write one character at digit_index 0..5 (0 = leftmost). Accepts space,
 * '0'-'9', and 'A'-'Z'; anything else lights all segments. */
void hal_lcd_show_char(char c, int digit_index);

/* Show up to the first 6 characters of s, left-aligned; remaining digits blank. */
void hal_lcd_display(const char *s);

/* Scroll s across the display; text enters from the right and moves left
 * (blocking, busy-wait timed). */
void hal_lcd_scroll(const char *s);

#endif /* HAL_LCD_H */
