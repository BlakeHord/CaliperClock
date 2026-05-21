/*
 * caliper_lcd.h -- the actual caliper LCD on the Caliper Clock V3 board.
 *
 * Task 3. This is the REAL display (not the LaunchPad FH-1138P from Task 2).
 * The segment->(SEG,COM) map is the hard-won data from the V2 firmware
 * (Caliper_Clock_V2/main.c, DIGIT_SEG/DIGIT_PATTERN); see caliper_lcd.c.
 *
 * Only runnable on the V3 board (different LCD + pins + Mode 2 bias than the
 * LaunchPad), so it is compile- and datasheet-verified only until boards exist.
 */
#ifndef CALIPER_LCD_H
#define CALIPER_LCD_H

#include <stdint.h>

/* Configure LCD_E for the caliper glass: 4-mux, Mode 2 bias (VDD + charge pump
 * + flying cap on LCDCAP0/1), COM0-3 on L0-L3, segments on L8-L17. */
void caliper_lcd_init(void);

/* Blank all segment digits (leaves the COM configuration intact). */
void caliper_lcd_clear(void);

/* Set/clear one segment addressed the V2 way: HT1621 SEG address (1..10) +
 * COM index (0..3). Translated to a V3 LCD_E segment line internally. */
void caliper_lcd_set_segment(uint8_t ht_addr, uint8_t com, uint8_t on);

/* Render a 4-digit value as HH:MM style (e.g. 1234 -> "12:34"), with leading-
 * zero blanking on the hours, plus optional colon and PM indicator. Mirrors
 * V2's lcd_show_4digit. */
void caliper_lcd_show_4digit(uint16_t value, uint8_t colon_on, uint8_t pm_on);

/* Bring-up diagnostic: light each physical (segment line, COM) one at a time,
 * ~0.5 s each, looping forever. Use this with the glass installed to record
 * which element each (Lxx, COMy) drives, then fix HT1621_ADDR_TO_LCDE_SEG[] in
 * caliper_lcd.c. Blocking; never returns. */
void caliper_lcd_segment_scan_test(void);

#endif /* CALIPER_LCD_H */
