/*
 * hal_lcd.c -- LaunchPad FH-1138P LCD driver (MSP430FR4133 LCD_E). See hal_lcd.h.
 *
 * Provenance (both BSD-3 / verified against the real glass):
 *   - Segment tables + showChar word-write: TI hal_LCD.c, E. Chen, 2014.
 *   - Bare-register Init (no driverlib): Energia LCD_Launchpad (FR4133 path).
 *
 * Register details cite the MSP430FR4xx/FR2xx Family User's Guide (SLAU445,
 * "LCD_E Controller") -- see ../Datasheets/slau445.pdf.
 */

#include <msp430.h>
#include <stdint.h>
#include "hal_lcd.h"

/*
 * Word access to LCD memory. The GCC device header defines
 *   LCDMEM == (volatile char *)&LCDM0
 * i.e. a byte pointer at the base of the LCD memory map, so a word pointer
 * lets us write a whole digit (two adjacent byte-memories) in one access --
 * exactly as TI's hal_LCD does. LCDMEMW[n] is the word at byte offset 2*n.
 * Unsigned so segment math never sign-extends or overflows a signed 16-bit int.
 */
#define LCDMEMW ((volatile unsigned int *)LCDMEM)

/* Pack a {low, high} segment pattern into one 16-bit LCD-memory word.
 * Done in unsigned int to be safe regardless of char signedness and to avoid
 * the signed-overflow UB of `(0xA0 << 8)` on this 16-bit-int target. */
static inline unsigned int seg_word(const uint8_t pat[2])
{
    return (unsigned int)pat[0] | ((unsigned int)pat[1] << 8);
}

/*
 * FH-1138P digit -> LCD-memory byte offset (from TI hal_LCD). Each alphanumeric
 * character A1..A6 is driven by two adjacent byte-memories (one 16-bit word).
 * Index 0 is the leftmost digit (A1). These are NOT sequential because of how
 * the glass is wired to the segment lines.
 */
static const unsigned char lcd_pos[HAL_LCD_NUM_DIGITS] = {
    4,   /* A1 - L4  */
    6,   /* A2 - L6  */
    8,   /* A3 - L8  */
    10,  /* A4 - L10 */
    2,   /* A5 - L2  */
    18   /* A6 - L18 */
};

/* Segment patterns for '0'-'9' (low byte, high byte). From TI hal_LCD. */
static const uint8_t digit[10][2] = {
    {0xFC, 0x28}, {0x60, 0x20}, {0xDB, 0x00}, {0xF3, 0x00}, {0x67, 0x00},
    {0xB7, 0x00}, {0xBF, 0x00}, {0xE4, 0x00}, {0xFF, 0x00}, {0xF7, 0x00}
};

/* Segment patterns for 'A'-'Z'. From TI hal_LCD. */
static const uint8_t alphabetBig[26][2] = {
    {0xEF, 0x00}, {0xF1, 0x50}, {0x9C, 0x00}, {0xF0, 0x50}, {0x9F, 0x00},
    {0x8F, 0x00}, {0xBD, 0x00}, {0x6F, 0x00}, {0x90, 0x50}, {0x78, 0x00},
    {0x0E, 0x22}, {0x1C, 0x00}, {0x6C, 0xA0}, {0x6C, 0x82}, {0xFC, 0x00},
    {0xCF, 0x00}, {0xFC, 0x02}, {0xCF, 0x02}, {0xB7, 0x00}, {0x80, 0x50},
    {0x7C, 0x00}, {0x0C, 0x28}, {0x6C, 0x0A}, {0x00, 0xAA}, {0x00, 0xB0},
    {0x90, 0x28}
};

void hal_lcd_init(void)
{
    LCDCTL0 &= ~LCDON;                 /* turn LCD off while (re)configuring */

    /* Enable the segment-line pins used by the FH-1138P: L0-L26 and L36-L39.
     * (SLAU445 LCDPCTLx: 1 = pin under LCD_E control.) */
    LCDPCTL0 = 0xFFFF;                 /* L0-L15  */
    LCDPCTL1 = 0x07FF;                 /* L16-L26 */
    LCDPCTL2 = 0x00F0;                 /* L36-L39 */

    /* L0-L3 are common (backplane) lines, not segments. */
    LCDCSSEL0 = 0x000F;

    /* 4-mux (LCDMX0|LCDMX1), source = ACLK (LCDSSEL_1; SLAU445 Table 17-10:
     * 00b=XT1CLK, 01b=ACLK, 10b=VLOCLK). Energia uses LCDSSEL_0=XT1CLK because
     * its framework starts XT1 first; we use ACLK so the default REFO (~32 kHz,
     * auto-on when ACLK is requested) drives the LCD with no clock init.
     * Low-power waveform (LCDLP), segments on (LCDSON), divide by 3 (LCDDIV_2). */
    LCDCTL0 = LCDMX0 | LCDMX1 | LCDSSEL_1 | LCDLP | LCDSON | LCDDIV_2;

    /* Charge-pump bias (Mode 3): internal reference enabled (LCDREFEN),
     * charge pump enabled (LCDCPEN), VLCD ~3.0 V (VLCD_6), pump clock freq
     * select bits. This matches the LaunchPad reference config; the V3 board
     * uses the external flying cap (C7) on LCDCAP0/1. SLAU445 "LCDVCTL". */
    LCDVCTL = LCDREFEN | LCDCPEN | VLCD_6
            | LCDCPFSEL3 | LCDCPFSEL2 | LCDCPFSEL1 | LCDCPFSEL0;

    LCDMEMCTL |= LCDCLRM | LCDCLRBM;   /* clear display + blink memory */

    /* Map common lines L0->COM0, L1->COM1, L2->COM2, L3->COM3. The value packs
     * one nibble per pin (0x1,0x2,0x4,0x8). Verified constant from TI/Energia;
     * see SLAU445 LCD memory/COM mapping for the bit layout. */
    LCDM0W  = 0x8421;
    LCDBM0W = 0x8421;

    LCDMEMCTL &= ~LCDDISP;             /* display from main memory (not blink) */

    LCDCTL0 |= LCDON;                  /* turn the LCD on */
}

void hal_lcd_clear(void)
{
    int i;
    for (i = 0; i < HAL_LCD_NUM_DIGITS; i++)
        LCDMEMW[lcd_pos[i] / 2] = 0;
}

void hal_lcd_show_char(char c, int digit_index)
{
    unsigned char pos;

    if (digit_index < 0 || digit_index >= HAL_LCD_NUM_DIGITS)
        return;
    pos = lcd_pos[digit_index];

    if (c == ' ')
        LCDMEMW[pos / 2] = 0;
    else if (c >= '0' && c <= '9')
        LCDMEMW[pos / 2] = seg_word(digit[c - '0']);
    else if (c >= 'A' && c <= 'Z')
        LCDMEMW[pos / 2] = seg_word(alphabetBig[c - 'A']);
    else
        LCDMEMW[pos / 2] = 0xFFFF;     /* unsupported char: all segments on */
}

void hal_lcd_display(const char *s)
{
    int i;
    for (i = 0; i < HAL_LCD_NUM_DIGITS; i++) {
        if (*s) {
            hal_lcd_show_char(*s, i);
            s++;
        } else {
            hal_lcd_show_char(' ', i);   /* pad the rest with blanks */
        }
    }
}

void hal_lcd_scroll(const char *s)
{
    int length = 0;
    int start, i;
    while (s[length])
        length++;

    /* Slide a 6-char window across the message, blank padding on both ends. */
    for (start = HAL_LCD_NUM_DIGITS; start > -length; start--) {
        for (i = 0; i < HAL_LCD_NUM_DIGITS; i++) {
            int idx = i - start;     /* index into s for digit i */
            if (idx >= 0 && idx < length)
                hal_lcd_show_char(s[idx], i);
            else
                hal_lcd_show_char(' ', i);
        }
        __delay_cycles(250000);      /* ~scroll step; default MCLK ~1 MHz */
    }
}
