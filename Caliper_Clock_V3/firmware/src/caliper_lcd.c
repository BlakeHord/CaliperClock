/*
 * caliper_lcd.c -- caliper LCD driver for Caliper Clock V3 (MSP430FR4133 LCD_E).
 *
 * The segment layout below is ported VERBATIM from the V2 firmware
 * (Caliper_Clock_V2/main.c) -- it is the experimentally-determined map of the
 * physical caliper glass, expressed as (HT1621 SEG address, COM index). That
 * data is trusted. What is NOT yet trusted is how a V2 "HT1621 address" lands
 * on a V3 MSP430 LCD_E segment line; that lives in ONE clearly-marked table
 * (HT1621_ADDR_TO_LCDE_SEG) and is resolved on hardware via
 * caliper_lcd_segment_scan_test().
 *
 * Register details cite SLAU445 (../Datasheets/slau445.pdf). Mode 2 bias and
 * the L0-L3 -> COM0-3 mapping (LCDM0W=0x8421) are datasheet-verified
 * (§17.2.8.1 and §17.2.3.2.1 respectively).
 */

#include <msp430.h>
#include <stdint.h>
#include <string.h>
#include "caliper_lcd.h"

/* Byte access to LCD memory. The LCDM0..LCDMx registers are contiguous, but each
 * is declared as a 1-byte SFR, so indexing a char* taken from &LCDM0 trips
 * -Warray-bounds. Forming the address in integer space sidesteps that without
 * changing the (identical) generated access. */
static inline volatile unsigned char *lcd_byte(uint8_t offset)
{
    return (volatile unsigned char *)((uintptr_t)&LCDM0 + offset);
}

/* ===========================================================================
 * V3 board wiring -- THE ONE PART THAT IS UNVERIFIED.
 *
 * LCD_E addresses memory by segment line (Lxx). On the V3 board the caliper
 * glass connects through J1 to these MSP430 lines (per the V3 pin plan):
 *     COM0..COM3  -> L0..L3
 *     "SEG1".."SEG10" -> L8..L17
 *
 * The map below assumes V2 HT1621 address N drives the same physical segment
 * as V3 segment line L(7+N), i.e. addr 1 -> L8 ... addr 10 -> L17, and that
 * COM index 0..3 is preserved between V2 and V3. BOTH assumptions are unproven
 * (the V2->V3 SEG order and the SEG0/SEG1 off-by-one were never confirmed).
 *
 * >>> When boards arrive: run caliper_lcd_segment_scan_test(), watch which
 * >>> element lights for each (Lxx, COM), and correct this table. Nothing else
 * >>> in this file should need to change.
 * =========================================================================== */
#define SEG_EMPTY 0xFF
#define LCDE_SEG_NONE 0xFF

/* index = V2 HT1621 address (1..10); value = V3 LCD_E segment line. [0] unused. */
static const uint8_t HT1621_ADDR_TO_LCDE_SEG[11] = {
    LCDE_SEG_NONE, /* 0: unused */
    8,  /* addr 1  -> L8  */
    9,  /* addr 2  -> L9  */
    10, /* addr 3  -> L10 */
    11, /* addr 4  -> L11 */
    12, /* addr 5  -> L12 */
    13, /* addr 6  -> L13 */
    14, /* addr 7  -> L14 */
    15, /* addr 8  -> L15 */
    16, /* addr 9  -> L16 */
    17  /* addr 10 -> L17 */
};

/* The contiguous span of LCD_E segment lines used for digits (L8..L17), and the
 * LCD-memory byte indices they occupy (Lxx -> byte Lxx/2 -> bytes 4..8). */
#define SEG_L_MIN 8
#define SEG_L_MAX 17

/* ===========================================================================
 * V2 segment map -- ported verbatim from Caliper_Clock_V2/main.c (trusted).
 * =========================================================================== */
typedef struct { uint8_t addr; uint8_t com; } seg_map_t;

/* DIGIT_SEG[digit][segment a..g] = (HT1621 address, COM). Digit 0 = rightmost
 * (ones of minutes); digit 3 = leftmost (tens of hours, only b,c,g wired). */
static const seg_map_t DIGIT_SEG[4][7] = {
    {{10,3},{10,2},{10,1},{ 9,1},{ 8,1},{ 9,3},{ 9,2}},                          /* D0 */
    {{ 8,2},{ 7,3},{ 7,1},{ 6,1},{ 6,2},{ 6,3},{ 7,2}},                          /* D1 */
    {{ 4,3},{ 5,2},{ 5,1},{ 4,1},{ 3,1},{ 3,2},{ 4,2}},                          /* D2 */
    {{SEG_EMPTY,1},{ 2,2},{ 2,1},{SEG_EMPTY,1},{SEG_EMPTY,1},{SEG_EMPTY,1},{ 1,2}} /* D3 */
};

static const seg_map_t SEG_COLON = { 8, 3 };
static const seg_map_t SEG_PM    = { 3, 3 };

/* 7-seg patterns for 0..9, bit0=a .. bit6=g. */
static const uint8_t DIGIT_PATTERN[10] = {
    0b00111111, 0b00000110, 0b01011011, 0b01001111,
    0b01100110, 0b01101101, 0b01111101, 0b00000111,
    0b01111111, 0b01101111
};

/* ===========================================================================
 * Low-level LCD_E helpers
 * =========================================================================== */

/* Write segment line L (must be in SEG_L_MIN..SEG_L_MAX or 0..3 for COMs) at
 * COM index com. In 4-mux, each LCD-memory byte holds two pins: even line in
 * the low nibble, odd line in the high nibble; COMx = bit x within the nibble
 * (SLAU445 §17.2.1 / Figure 17-4). */
static void set_lcde_segment(uint8_t lcde_seg, uint8_t com, uint8_t on)
{
    uint8_t idx = lcde_seg >> 1;
    uint8_t bit = (lcde_seg & 1) ? (4 + com) : com;
    if (on)
        *lcd_byte(idx) |=  (uint8_t)(1u << bit);
    else
        *lcd_byte(idx) &= (uint8_t)~(1u << bit);
}

void caliper_lcd_init(void)
{
    LCDCTL0 &= ~LCDON;                 /* off while configuring */

    /* Enable LCD function on the pins we use: COMs L0-L3 and segments L8-L17. */
    LCDPCTL0 = 0xFF0F;                 /* L0-L3 (0x000F) + L8-L15 (0xFF00) */
    LCDPCTL1 = 0x0003;                 /* L16, L17 */
    LCDPCTL2 = 0x0000;

    LCDCSSEL0 = 0x000F;                /* L0-L3 are common lines */

    /* 4-mux, ACLK source (LCDSSEL_1; SLAU445 Table 17-10: 00b=XT1CLK, 01b=ACLK,
     * 10b=VLOCLK -- so LCDSSEL_1, NOT LCDSSEL_0, picks ACLK). Default ACLK is
     * REFO (~32 kHz, auto-on when requested); Task 4 moves ACLK to XT1.
     * Low-power waveform (LCDLP), segments on (LCDSON), divide-by-3 (LCDDIV_2). */
    LCDCTL0 = LCDMX0 | LCDMX1 | LCDSSEL_1 | LCDLP | LCDSON | LCDDIV_2;

    /* Mode 2 bias (SLAU445 §17.2.8.1): VLCD from internal VDD (the regulated
     * 3.0 V), internal charge pump generates V1/V2/V4/V5 via the flying cap on
     * LCDCAP0/LCDCAP1, no R13 reference, VLCDx unused. Slowest charge-pump
     * frequency (all LCDCPFSEL bits) for lowest current. R13/R23/R33 stay
     * floating in hardware. */
    LCDVCTL = LCDSELVDD | LCDCPEN
            | LCDCPFSEL3 | LCDCPFSEL2 | LCDCPFSEL1 | LCDCPFSEL0;

    LCDMEMCTL |= LCDCLRM | LCDCLRBM;   /* clear display + blink memory */

    /* L0->COM0 .. L3->COM3 (datasheet-derived; see file header). */
    LCDM0W  = 0x8421;
    LCDBM0W = 0x8421;

    LCDMEMCTL &= ~LCDDISP;             /* show main memory */

    LCDCTL0 |= LCDON;
}

void caliper_lcd_clear(void)
{
    /* Zero only the segment bytes (L8..L17 -> bytes 4..8); keep COM config in
     * bytes 0-1 intact. */
    uint8_t i;
    for (i = (SEG_L_MIN >> 1); i <= (SEG_L_MAX >> 1); i++)
        *lcd_byte(i) = 0;
}

void caliper_lcd_set_segment(uint8_t ht_addr, uint8_t com, uint8_t on)
{
    uint8_t lcde_seg;
    if (com > 3)                        /* 4-mux: only COM0..3 exist */
        return;
    if (ht_addr == SEG_EMPTY || ht_addr >= sizeof(HT1621_ADDR_TO_LCDE_SEG))
        return;
    lcde_seg = HT1621_ADDR_TO_LCDE_SEG[ht_addr];
    if (lcde_seg == LCDE_SEG_NONE)
        return;
    set_lcde_segment(lcde_seg, com, on);
}

void caliper_lcd_show_4digit(uint16_t value, uint8_t colon_on, uint8_t pm_on)
{
    uint8_t d[4];
    uint8_t blank[4] = {0, 0, 0, 0};
    uint16_t tmp;
    uint8_t digit, s;

    if (value > 9999) value = 9999;

    tmp = value;
    d[0] = tmp % 10; tmp /= 10;        /* ones of minutes (rightmost) */
    d[1] = tmp % 10; tmp /= 10;        /* tens of minutes */
    d[2] = tmp % 10; tmp /= 10;        /* ones of hours */
    d[3] = tmp % 10;                   /* tens of hours (leftmost) */

    /* Leading-zero blanking on the hours (mirrors V2). */
    if (value == 0) {
        blank[1] = blank[2] = blank[3] = 1;
    } else {
        if (d[3] == 0) { blank[3] = 1;
            if (d[2] == 0) { blank[2] = 1;
                if (d[1] == 0) blank[1] = 1;
            }
        }
    }

    caliper_lcd_clear();

    for (digit = 0; digit < 4; digit++) {
        uint8_t patt;
        if (blank[digit]) continue;
        patt = DIGIT_PATTERN[d[digit]];
        for (s = 0; s < 7; s++) {
            seg_map_t m;
            if (!(patt & (1u << s))) continue;
            m = DIGIT_SEG[digit][s];
            if (m.addr == SEG_EMPTY) continue;
            caliper_lcd_set_segment(m.addr, m.com, 1);
        }
    }

    if (colon_on) caliper_lcd_set_segment(SEG_COLON.addr, SEG_COLON.com, 1);
    if (pm_on)    caliper_lcd_set_segment(SEG_PM.addr,    SEG_PM.com,    1);
}

void caliper_lcd_segment_scan_test(void)
{
    /* Walk the RAW V3 segment lines (not the V2 translation) so you can map the
     * physical glass directly: note which element lights for each (Lxx, COM). */
    for (;;) {
        uint8_t lcde_seg, com;
        for (lcde_seg = SEG_L_MIN; lcde_seg <= SEG_L_MAX; lcde_seg++) {
            for (com = 0; com < 4; com++) {
                caliper_lcd_clear();
                set_lcde_segment(lcde_seg, com, 1);
                __delay_cycles(400000);   /* ~0.5 s at ~1 MHz default MCLK */
            }
        }
    }
}
