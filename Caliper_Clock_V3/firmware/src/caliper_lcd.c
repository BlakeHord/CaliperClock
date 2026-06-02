/*
 * caliper_lcd.c -- caliper LCD driver for Caliper Clock V3 (MSP430FR4133 LCD_E).
 *
 * The segment layout (DIGIT_SEG) is ported VERBATIM from the V2 firmware
 * (Caliper_Clock_V2/main.c) -- it is the experimentally-determined map of the
 * physical caliper glass, expressed as (HT1621 SEG address, COM index). V3
 * uses the same glass, so DIGIT_SEG is trusted.
 *
 * The V2->V3 translation (V2 addr -> V3 Lxx; V2 com -> V3 backplane via
 * LCDM0W) was unknown at first and resolved on hardware via the BRINGUP=14
 * slow-scan test in Jun 2026. See the "V3 board wiring" block below.
 *
 * Register details cite SLAU445 (../Datasheets/slau445.pdf). Mode 2 bias is
 * datasheet-verified (§17.2.8.1); the L0..L3 -> COM mapping in LCDM0W is
 * §17.2.3.2.1.
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
 * V3 board wiring -- HARDWARE-VERIFIED (Jun 2026, BRINGUP=14 slow scan).
 *
 * The V3 caliper glass is the same physical LCD as V2, so V2's DIGIT_SEG
 * (addr, com) data is correct. What was unknown was how V2's HT1621 (addr,
 * com) maps to V3's MSP430 LCD_E (Lxx, com). The scan resolved both:
 *
 *   V2 addr N  ->  V3 segment line L(N + 8)    (for N = 1..10)
 *   V2 com 1   ->  V3 L2 backplane             (handled via LCDM0W = 0x1248)
 *   V2 com 2   ->  V3 L1 backplane
 *   V2 com 3   ->  V3 L0 backplane
 *   V2 com 0   ->  NC (V3 L3; V2 only uses com 1..3)
 *
 * Two other V2 labels were also off (same glass; V2's main.c just mislabeled
 * the icons): the actual decimal/colon dot is V2 (5, 3), not (8, 3), and the
 * inch indicator that V3 uses as the PM mark is V2 (8, 3), not (3, 3). V2's
 * old (3, 3) lights the battery icon, available but unused here.
 * =========================================================================== */
#define SEG_EMPTY 0xFF
#define LCDE_SEG_NONE 0xFF

/* index = V2 HT1621 address (1..10); value = V3 LCD_E segment line. [0] unused.
 * V3 wires V2 addr N to L(N+8) for N=1..10. L17 (formerly assumed NC) carries
 * V2 addr 9 (D0 d/f/g); only L8 has a stray segment (D4 g) V2 doesn't address. */
static const uint8_t HT1621_ADDR_TO_LCDE_SEG[11] = {
    LCDE_SEG_NONE, /* 0: unused */
    9,  /* addr 1  -> L9  */
    10, /* addr 2  -> L10 */
    11, /* addr 3  -> L11 */
    12, /* addr 4  -> L12 */
    13, /* addr 5  -> L13 */
    14, /* addr 6  -> L14 */
    15, /* addr 7  -> L15 */
    16, /* addr 8  -> L16 */
    17, /* addr 9  -> L17 */
    18  /* addr 10 -> L18 */
};

/* Span of LCD_E segment lines used by digit writes: L9..L18. L8 carries an
 * unaddressed stray segment ("D4 g") that V2 never wrote and we don't either;
 * including L8 in clear()'s sweep is harmless. */
#define SEG_L_MIN 8
#define SEG_L_MAX 18

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

/* V2's original addresses, hardware-verified on V3 (BRINGUP=16/17, Jun 2026):
 *   (8, 3) = the decimal dot between D2 and D1 -- physically positioned right
 *            where a clock colon belongs (between hours and minutes). The L16
 *            SEG line has electrodes at three positions on the glass: D1 a
 *            (com 2 phase), D0 e (com 1 phase), and this decimal (com 3
 *            phase) -- a single SEG line can route to multiple electrodes at
 *            unrelated physical positions, and the COM phase selects which.
 *   (3, 3) = battery icon. V2 uses it as the PM indicator (lit when hour>=12);
 *            we follow.
 * A first-pass BRINGUP=14 scan misread (8, 3) as "Inch icon" and reported a
 * phantom dot at (5, 3); the slow-isolation tests in BRINGUP=16 and the
 * (addr, com=3) sweep in BRINGUP=17 located the real decimal at (8, 3) and
 * confirmed (5, 3) drives nothing visible. V2's original labels were right. */
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

    /* Enable LCD function on the pins we use: COMs L0-L3 and segments L8-L18. */
    LCDPCTL0 = 0xFF0F;                 /* L0-L3 (0x000F) + L8-L15 (0xFF00) */
    LCDPCTL1 = 0x0007;                 /* L16, L17, L18 (all carry segments) */
    LCDPCTL2 = 0x0000;

    LCDCSSEL0 = 0x000F;                /* L0-L3 are common lines */

    /* 4-mux, ACLK source (LCDSSEL_1; SLAU445 Table 17-10: 00b=XT1CLK, 01b=ACLK,
     * 10b=VLOCLK -- so LCDSSEL_1, NOT LCDSSEL_0, picks ACLK). Default ACLK is
     * REFO (~32 kHz, auto-on when requested); Task 4 moves ACLK to XT1.
     * Low-power waveform (LCDLP), segments on (LCDSON), divide-by-3 (LCDDIV_2). */
    LCDCTL0 = LCDMX0 | LCDMX1 | LCDSSEL_1 | LCDLP | LCDSON | LCDDIV_2;

    /* Mode 2 bias (SLAU445 §17.2.8.1): VLCD from internal VDD (the regulated
     * 3.0 V), internal charge pump generates V1/V2/V4/V5 via the flying cap on
     * LCDCAP0/LCDCAP1, no R13 reference, VLCDx unused. R13/R23/R33 stay
     * floating in hardware.
     *
     * Charge-pump frequency: LCDCPFSELx = 0 (all bits cleared) = fastest pump
     * for maximum contrast. Initially set to all-1s (slowest, lowest current)
     * but on the V3 board this produced near-threshold ON levels: lit segments
     * looked faint, OFF segments picked up partial drive from adjacent frame
     * activity (notably the colon toggle), and the on/off difference on the
     * actual colon dot was too small to read as a blink. Revisit for
     * power-tuning once the per-segment contrast is healthy. */
    LCDVCTL = LCDSELVDD | LCDCPEN;

    LCDMEMCTL |= LCDCLRM | LCDCLRBM;   /* clear display + blink memory */

    /* V3 COM mapping. The MSP430's L0..L3 (configured as COMs via LCDCSSEL0)
     * are wired to the caliper backplanes in V2-COM order [3, 2, 1, 0] -- i.e.
     * L0 -> V2 COM3, L1 -> V2 COM2, L2 -> V2 COM1, L3 -> V2 COM0 (NC on this
     * glass; V2 only uses com 1..3). Each L's nibble in LCDM0W is the COM phase
     * index it drives, so a V2-style (addr, com) write with com in {1,2,3}
     * lands on the right backplane:
     *   L0 nibble = 0x8 (phase 3) -> V2 COM3
     *   L1 nibble = 0x4 (phase 2) -> V2 COM2
     *   L2 nibble = 0x2 (phase 1) -> V2 COM1
     *   L3 nibble = 0x1 (phase 0) -> V2 COM0 (NC)
     * Packed low-to-high (L0..L3) into a 16-bit word: 0x1248.
     * (Hardware-verified Jun 2026 via BRINGUP=14 slow scan; earlier BRINGUP=13
     * had this at 0x1428 -- only L0<->L3 swapped -- which mis-routed V2 com 1
     * and 2 because they also need swapping on this board.) */
    LCDM0W  = 0x1248;
    LCDBM0W = 0x1248;

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

void caliper_lcd_segment_scan_slow(void)
{
    /* Slow, note-takeable variant. Each (Lxx, COMy) holds for ~4 s. Each cycle
     * is bookended by an all-on/all-dark marker so you can re-sync: after the
     * 4 s dark gap, the NEXT step is L8 com0.
     *
     * Order of the 44 steps:
     *   L8  com0, L8  com1, L8  com2, L8  com3,
     *   L9  com0, L9  com1, L9  com2, L9  com3,
     *   ...
     *   L18 com0, L18 com1, L18 com2, L18 com3.
     *
     * Total per cycle: 4 s (all on) + 4 s (dark) + 44 * 4 s = 184 s (~3 min).
     */
    for (;;) {
        uint8_t i, lcde_seg, com;

        /* Cycle marker: all on for 4 s. */
        for (i = (SEG_L_MIN >> 1); i <= (SEG_L_MAX >> 1); i++)
            *lcd_byte(i) = 0xFF;
        __delay_cycles(4000000);

        /* Cycle marker: all dark for 4 s. After this, the NEXT step is L8 com0. */
        caliper_lcd_clear();
        __delay_cycles(4000000);

        /* The 44 per-position steps. */
        for (lcde_seg = SEG_L_MIN; lcde_seg <= SEG_L_MAX; lcde_seg++) {
            for (com = 0; com < 4; com++) {
                caliper_lcd_clear();
                set_lcde_segment(lcde_seg, com, 1);
                __delay_cycles(4000000);
            }
        }
    }
}
