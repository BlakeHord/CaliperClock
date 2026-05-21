// -----------------------------------------------------------------------------
// CaliperClock LCD Test
// ATtiny1616 + HT1621B
// Displays a 4-digit number and increments, with colon blink.
// Wiring:
//   DATA = PA1
//   WR   = PA7
//   CS   = PB3
//   LED  = PA6  (active-low)
// -----------------------------------------------------------------------------

#define F_CPU 3333333UL
#include <avr/io.h>
#include <util/delay.h>
#include <string.h>

#define _BV8(b) ((uint8_t)(1u << (b)))

// -----------------------------------------------------------------------------
// PIN MAP
// -----------------------------------------------------------------------------
#define HT_DATA_PORT   PORTA
#define HT_DATA_PIN    1       // PA1

#define HT_WR_PORT     PORTA
#define HT_WR_PIN      7       // PA7

#define HT_CS_PORT     PORTB
#define HT_CS_PIN      3       // PB3 (/CS active-low)

#define LED_PORT       PORTA
#define LED_PIN        6       // PA6 (optional debug LED)

#define HT_N_ADDR      32      // 32 nibbles

// -----------------------------------------------------------------------------
// GPIO helpers
// -----------------------------------------------------------------------------
static inline void pin_out(volatile PORT_t* p, uint8_t pin){ p->DIRSET = _BV8(pin); }
static inline void pin_high(volatile PORT_t* p, uint8_t pin){ p->OUTSET = _BV8(pin); }
static inline void pin_low (volatile PORT_t* p, uint8_t pin){ p->OUTCLR = _BV8(pin); }

static inline void t_hold(void){ _delay_us(4); }

static inline void wr_pulse(void){
    pin_low (&HT_WR_PORT, HT_WR_PIN);
    t_hold();
    pin_high(&HT_WR_PORT, HT_WR_PIN);
    t_hold();
}

// -----------------------------------------------------------------------------
// Bit-banged serial: send bits MSB-first
// -----------------------------------------------------------------------------
static inline void send_bit(uint8_t bit){
    if (bit) pin_high(&HT_DATA_PORT, HT_DATA_PIN);
    else     pin_low (&HT_DATA_PORT, HT_DATA_PIN);
    wr_pulse();
}

static void send_bits(uint32_t v, uint8_t nbits){
    for (int8_t i = nbits - 1; i >= 0; --i)
        send_bit((v >> i) & 1);
}

// -----------------------------------------------------------------------------
// HT1621: Commands
// -----------------------------------------------------------------------------
#define CMD_SYSEN            0b000000010
#define CMD_LCDON            0b000000110
#define CMD_RC256K           0b000110000
#define CMD_BIASCOM_4COM_13  0b000010110   // 4-COM, 1/3-bias mode

static void ht_cmd9(uint16_t payload9){
    pin_low(&HT_CS_PORT, HT_CS_PIN);
    send_bits(0b100, 3);             // command ID = 100
    send_bits(payload9 & 0x1FF, 9);
    pin_high(&HT_CS_PORT, HT_CS_PIN);
    t_hold();
}

// -----------------------------------------------------------------------------
// HT1621: write a single nibble (4 bits) to an address
// -----------------------------------------------------------------------------
static inline void ht_write_nibble(uint8_t addr, uint8_t nibble){
    pin_low(&HT_CS_PORT, HT_CS_PIN);
    send_bits(0b101, 3);              // Write-Data ID
    send_bits(addr & 0x3F, 6);
    send_bits(nibble & 0x0F, 4);
    pin_high(&HT_CS_PORT, HT_CS_PIN);
    t_hold();
}

// -----------------------------------------------------------------------------
// LCD INIT
// -----------------------------------------------------------------------------
static void lcd_init(void){
    pin_high(&HT_DATA_PORT, HT_DATA_PIN);
    pin_high(&HT_WR_PORT,   HT_WR_PIN);
    pin_high(&HT_CS_PORT,   HT_CS_PIN);

    ht_cmd9(CMD_SYSEN);
    ht_cmd9(CMD_RC256K);
    ht_cmd9(CMD_BIASCOM_4COM_13);    // using 4-COM mode
    ht_cmd9(CMD_LCDON);

    _delay_ms(5);
}

// -----------------------------------------------------------------------------
// SEGMENT MAP — from your mapping (addr, COM)
// -----------------------------------------------------------------------------

typedef struct {
    uint8_t addr;   // HT1621 address
    uint8_t com;    // COM index (0..3)
} seg_map_t;

#define SEG_EMPTY 0xFF

// Digit index 0 = rightmost (D0), 3 = leftmost (D3)
// Segment index: a=0, b=1, c=2, d=3, e=4, f=5, g=6

static const seg_map_t DIGIT_SEG[4][7] = {
    // ---------------- D0 (rightmost) ----------------
    {
        {10,3}, {10,2}, {10,1}, { 9,1}, { 8,1}, { 9,3}, { 9,2}
    },
    // ---------------- D1 ----------------------------
    {
        { 8,2}, { 7,3}, { 7,1}, { 6,1}, { 6,2}, { 6,3}, { 7,2}
    },
    // ---------------- D2 ----------------------------
    {
        { 4,3}, { 5,2}, { 5,1}, { 4,1}, { 3,1}, { 3,2}, { 4,2}
    },
    // ---------------- D3 (leftmost; limited segments) ----------------
    {
        {SEG_EMPTY,1}, { 2,2}, { 2,1}, {SEG_EMPTY,1}, 
        {SEG_EMPTY,1}, {SEG_EMPTY,1}, { 1,2}
    }
};

// Optional extras:
static const seg_map_t SEG_COLON = { 8, 3 };
static const seg_map_t SEG_PM    = { 3, 3 };

// 7-seg digit patterns with bit layout: bit0=a, bit1=b, ..., bit6=g
static const uint8_t DIGIT_PATTERN[10] = {
    0b00111111,  // 0
    0b00000110,  // 1
    0b01011011,  // 2
    0b01001111,  // 3
    0b01100110,  // 4
    0b01101101,  // 5
    0b01111101,  // 6
    0b00000111,  // 7
    0b01111111,  // 8
    0b01101111   // 9
};

// -----------------------------------------------------------------------------
// Clear entire display (all addresses, all COM bits)
// -----------------------------------------------------------------------------
static void lcd_clear_all(void){
    for (uint8_t addr = 0; addr < HT_N_ADDR; ++addr){
        ht_write_nibble(addr, 0x0);
    }
}

// -----------------------------------------------------------------------------
// Light exactly one segment given address + COM index (debug helper)
// addr: 0..31
// com : 0..3   (which COM line; bit = 1 << com)
// -----------------------------------------------------------------------------
static void lcd_light_single(uint8_t addr, uint8_t com){
    if (addr >= HT_N_ADDR || com > 3) return;  // simple bounds guard

    lcd_clear_all();                // make sure only one segment is on
    uint8_t mask = (uint8_t)(1u << com);
    ht_write_nibble(addr, mask);    // light that one segment
}

// -----------------------------------------------------------------------------
// Render a 4-digit number on the glass, with leading-zero suppression
// -----------------------------------------------------------------------------
static void lcd_show_4digit(uint16_t value, uint8_t colon_on, uint8_t pm_on){
    if (value > 9999) value = 9999;

    uint8_t d[4];
    uint16_t tmp = value;

    // Split into digits: D0 (rightmost) .. D3 (leftmost)
    d[0] = tmp % 10; tmp /= 10;
    d[1] = tmp % 10; tmp /= 10;
    d[2] = tmp % 10; tmp /= 10;
    d[3] = tmp % 10;

    // Leading-zero suppression: blank left digits until first non-zero,
    // but always show at least D0 (for value == 0).
    uint8_t blank[4] = {0,0,0,0};

    if (value == 0){
        // Show "0" only in rightmost digit
        blank[1] = 1;
        blank[2] = 1;
        blank[3] = 1;
    } else {
        // Start from leftmost; blank zeros until first non-zero
        if (d[3] == 0) {
            blank[3] = 1;
            if (d[2] == 0){
                blank[2] = 1;
                if (d[1] == 0){
                    blank[1] = 1;
                }
            }
        }
        // D0 (d[0]) is never blanked
    }

    uint8_t buf[HT_N_ADDR];
    memset(buf, 0, sizeof(buf));

    for (uint8_t digit = 0; digit < 4; ++digit){
        if (blank[digit]) continue;  // skip blanked digit

        uint8_t patt = DIGIT_PATTERN[d[digit]];

        for (uint8_t s = 0; s < 7; ++s){
            if (!(patt & (1 << s))) continue;  // segment off

            seg_map_t m = DIGIT_SEG[digit][s];
            if (m.addr == SEG_EMPTY) continue;

            buf[m.addr] |= (1 << m.com);
        }
    }

    if (colon_on)
        buf[SEG_COLON.addr] |= (1 << SEG_COLON.com);

    if (pm_on)
        buf[SEG_PM.addr] |= (1 << SEG_PM.com);

    // Push buffer to LCD RAM
    for (uint8_t addr = 0; addr < HT_N_ADDR; ++addr)
        ht_write_nibble(addr, buf[addr] & 0x0F);
}

// -----------------------------------------------------------------------------
// MAIN
// -----------------------------------------------------------------------------
int main(void){
    // Configure pins
    pin_out(&HT_DATA_PORT, HT_DATA_PIN);
    pin_out(&HT_WR_PORT,   HT_WR_PIN);
    pin_out(&HT_CS_PORT,   HT_CS_PIN);
    pin_out(&LED_PORT,     LED_PIN);
    pin_high(&LED_PORT, LED_PIN);

    lcd_init();

    int test = 3;

    if (test == 1) {
        // Example: verify a specific segment
        while (1){
            lcd_light_single(10, 1);   // addr=10, COM=1 (example)
            _delay_ms(1000);
        }
    }

    if (test == 2) {
        uint16_t n = 0;
        lcd_show_4digit(n, 0, 0);
        while (1) { /* hold */ }
    }

    if (test == 3) {
        uint16_t n = 0;
        uint8_t colon = 0;
        uint8_t tick  = 0;  // counts 250ms steps

        while (1){
            lcd_show_4digit(n, colon, 0);  // number, colon on/off, PM off

            n += 1;
            if (n > 9999) n = 0;

            _delay_ms(1000);
            //LED_PORT.OUTTGL = _BV8(LED_PIN); // heartbeat blink (4 Hz)
            
            colon ^= 1;  // toggle colon ~1 Hz
            
        }
    }

    // Should never reach here
    while (1) {}
}
