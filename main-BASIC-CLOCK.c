// -----------------------------------------------------------------------------
// CaliperClock main.c
// ATtiny1616 + HT1621B + RV-3028 (soft I2C)
// Shows 12-hour HH:MM on hacked caliper LCD with PM icon and colon blink
// Colon blink is tied to RTC seconds (colon on when sec is odd).
//
// Wiring:
//   HT1621B:
//     DATA = PA1
//     WR   = PA7
//     CS   = PB3
//     LED  = PA6 (active-low heartbeat / debug)
//   RV-3028 soft I2C:
//     SDA  = PA2
//     SCL  = PA3
//
// RTC requirements (RV-3028):
//   - Address 0x52
//   - Read seconds/minutes/hours from 0x00..0x02 (24h mode)
//   - Clear POR flag at 0x0F
//   - Force 24h mode (CTRL1 bit1 = 0 at 0x10)
// -----------------------------------------------------------------------------

#define F_CPU 3333333UL
#include <avr/io.h>
#include <util/delay.h>
#include <stdint.h>
#include <string.h>

#define _BV8(b) ((uint8_t)(1u << (b)))

// -----------------------------------------------------------------------------
// PIN MAP (LCD / LED)
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
// PIN MAP (soft I2C for RV-3028)
// -----------------------------------------------------------------------------
#define SDA_PORT   PORTA
#define SDA_PIN    2   // PA2
#define SCL_PORT   PORTA
#define SCL_PIN    3   // PA3

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
// Bit-banged serial to HT1621: send bits MSB-first
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
// Render a 4-digit number on the glass, with leading-zero suppression
// value: 0..9999 → displayed as D3 D2 D1 D0 (D0 = rightmost)
// colon_on: non-zero to light colon
// pm_on:    non-zero to light PM icon
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

            buf[m.addr] |= (uint8_t)(1u << m.com);
        }
    }

    if (colon_on)
        buf[SEG_COLON.addr] |= (uint8_t)(1u << SEG_COLON.com);

    if (pm_on)
        buf[SEG_PM.addr] |= (uint8_t)(1u << SEG_PM.com);

    // Push buffer to LCD RAM
    for (uint8_t addr = 0; addr < HT_N_ADDR; ++addr)
        ht_write_nibble(addr, buf[addr] & 0x0F);
}

// -----------------------------------------------------------------------------
// LED helpers (optional debug)
// -----------------------------------------------------------------------------
static inline void led_init(void){
    LED_PORT.DIRSET = _BV8(LED_PIN);
    LED_PORT.OUTSET = _BV8(LED_PIN);   // off (active-low)
}
static inline void led_on(void){  LED_PORT.OUTCLR = _BV8(LED_PIN); }
static inline void led_off(void){ LED_PORT.OUTSET = _BV8(LED_PIN); }

static inline void dms(uint16_t ms){
    while (ms--) _delay_ms(1);
}

static void blink_n(uint8_t n){
    if (n == 0){
        dms(400);
        return;
    }
    for(uint8_t i=0; i<n; i++){
        led_on();  dms(120);
        led_off(); dms(120);
    }
    dms(600);
}

// -----------------------------------------------------------------------------
// Soft I2C on PA2/PA3
// -----------------------------------------------------------------------------
static inline void sda_hi(void){ SDA_PORT.DIRCLR = _BV8(SDA_PIN); } // input (pullup -> high)
static inline void sda_lo(void){ SDA_PORT.DIRSET = _BV8(SDA_PIN); SDA_PORT.OUTCLR = _BV8(SDA_PIN); }
static inline void scl_hi(void){ SCL_PORT.DIRCLR = _BV8(SCL_PIN); }
static inline void scl_lo(void){ SCL_PORT.DIRSET = _BV8(SCL_PIN); SCL_PORT.OUTCLR = _BV8(SCL_PIN); }

static inline uint8_t sda_read(void){ return (VPORTA.IN & _BV8(SDA_PIN)) ? 1 : 0; }

static inline void i2c_soft_delay(void){ _delay_us(200); }  // ~5 kHz-ish, nice and slow

static uint8_t i2c_soft_start(void){
    sda_hi();
    scl_hi();
    i2c_soft_delay();

    if (!sda_read()) return 1; // SDA stuck low

    // START: SDA goes low while SCL high
    sda_lo();
    i2c_soft_delay();
    scl_lo();
    i2c_soft_delay();
    return 0;
}

static void i2c_soft_stop(void){
    sda_lo();
    i2c_soft_delay();
    scl_hi();
    i2c_soft_delay();
    sda_hi();
    i2c_soft_delay();
}

static uint8_t i2c_soft_write_byte(uint8_t b){
    for(uint8_t i=0; i<8; i++){
        if (b & 0x80) sda_hi(); else sda_lo();
        i2c_soft_delay();
        scl_hi(); i2c_soft_delay();
        scl_lo(); i2c_soft_delay();
        b <<= 1;
    }
    // ACK bit
    sda_hi(); // release SDA
    i2c_soft_delay();
    scl_hi(); i2c_soft_delay();
    uint8_t ack = (sda_read() == 0);
    scl_lo(); i2c_soft_delay();
    return ack ? 0 : 1; // 0 = ACK, 1 = NACK
}

static uint8_t i2c_soft_read_byte(uint8_t *out, uint8_t ack){
    uint8_t b = 0;
    sda_hi();  // release SDA for input
    for(uint8_t i=0; i<8; i++){
        b <<= 1;
        scl_hi();
        i2c_soft_delay();
        if (sda_read()) b |= 1;
        scl_lo();
        i2c_soft_delay();
    }
    // Send ACK/NACK
    if (ack){
        sda_lo(); // drive low for ACK
    } else {
        sda_hi(); // leave high for NACK
    }
    i2c_soft_delay();
    scl_hi(); i2c_soft_delay();
    scl_lo(); i2c_soft_delay();
    sda_hi();
    *out = b;
    return 0;
}

// Write: addr7, reg, then len bytes of data
static uint8_t i2c_soft_write_reg(uint8_t addr7, uint8_t reg, const uint8_t *data, uint8_t len){
    if (i2c_soft_start()) return 1;
    if (i2c_soft_write_byte( (addr7<<1) | 0 )){ i2c_soft_stop(); return 1; }
    if (i2c_soft_write_byte(reg)){ i2c_soft_stop(); return 1; }
    for(uint8_t i=0; i<len; i++){
        if (i2c_soft_write_byte(data[i])){ i2c_soft_stop(); return 1; }
    }
    i2c_soft_stop();
    return 0;
}

// Read: write reg pointer, repeated start, read len bytes
static uint8_t i2c_soft_read_reg(uint8_t addr7, uint8_t reg, uint8_t *out, uint8_t len){
    if (i2c_soft_start()) return 1;
    if (i2c_soft_write_byte( (addr7<<1) | 0 )){ i2c_soft_stop(); return 1; }
    if (i2c_soft_write_byte(reg)){ i2c_soft_stop(); return 1; }

    // repeated START for read
    if (i2c_soft_start()) return 1;
    if (i2c_soft_write_byte( (addr7<<1) | 1 )){ i2c_soft_stop(); return 1; }

    for(uint8_t i=0; i<len; i++){
        uint8_t ack = (i < (len-1)) ? 1 : 0; // ACK all but last
        if (i2c_soft_read_byte(&out[i], ack)){ i2c_soft_stop(); return 1; }
    }
    i2c_soft_stop();
    return 0;
}

// -----------------------------------------------------------------------------
// RV-3028 helpers (soft I2C)
// -----------------------------------------------------------------------------
#define RV3028_ADDR  0x52

static inline uint8_t bcd2bin(uint8_t v){ return (uint8_t)(10*(v>>4) + (v & 0x0F)); }
static inline uint8_t bin2bcd(uint8_t v){
    return (uint8_t)(((v / 10) << 4) | (v % 10));
}

typedef struct {
    uint8_t sec, min, hour;
} rtc_time_t;

static uint8_t rv3028_get_time_soft(rtc_time_t *t){
    uint8_t raw[3];
    if (i2c_soft_read_reg(RV3028_ADDR, 0x00, raw, 3) != 0) return 1;

    t->sec  = bcd2bin(raw[0] & 0x7F);
    t->min  = bcd2bin(raw[1] & 0x7F);
    t->hour = bcd2bin(raw[2] & 0x3F);   // 24h mode
    return 0;
}

// Clear POR flag and force 24h mode (CTRL1 bit1 = 0)
static uint8_t rv3028_init_soft(void){
    uint8_t status;
    if (i2c_soft_read_reg(RV3028_ADDR, 0x0F, &status, 1) != 0) return 1;
    status &= ~(uint8_t)0x01;  // clear POR bit
    if (i2c_soft_write_reg(RV3028_ADDR, 0x0F, &status, 1) != 0) return 1;

    uint8_t ctrl1;
    if (i2c_soft_read_reg(RV3028_ADDR, 0x10, &ctrl1, 1) != 0) return 1;
    ctrl1 &= ~(uint8_t)(1u << 1);  // bit1 = 0 => 24h
    if (i2c_soft_write_reg(RV3028_ADDR, 0x10, &ctrl1, 1) != 0) return 1;

    return 0;
}

// Set time in 24-hour format (hour24:0–23, min:0–59, sec:0–59)
static uint8_t rv3028_set_time_soft(uint8_t hour24, uint8_t min, uint8_t sec){
    uint8_t data[3];

    if (hour24 > 23) hour24 = 0;
    if (min > 59)    min    = 0;
    if (sec > 59)    sec    = 0;

    data[0] = bin2bcd(sec  & 0x7F);   // seconds, bit7 must be 0
    data[1] = bin2bcd(min);
    data[2] = bin2bcd(hour24);

    return i2c_soft_write_reg(RV3028_ADDR, 0x00, data, 3);
}

// -----------------------------------------------------------------------------
// 24h -> 12h conversion with PM flag
// -----------------------------------------------------------------------------
static void rtc_24h_to_12h(uint8_t hour24, uint8_t *hour12_out, uint8_t *pm_out){
    uint8_t h12;
    uint8_t pm;

    if (hour24 == 0){
        // 00:xx -> 12:xx AM
        h12 = 12;
        pm  = 0;
    } else if (hour24 < 12){
        // 01..11 -> 1..11 AM
        h12 = hour24;
        pm  = 0;
    } else if (hour24 == 12){
        // 12:xx -> 12:xx PM
        h12 = 12;
        pm  = 1;
    } else {
        // 13..23 -> 1..11 PM
        h12 = hour24 - 12;
        pm  = 1;
    }

    *hour12_out = h12;
    *pm_out     = pm;
}

// -----------------------------------------------------------------------------
// MAIN
// -----------------------------------------------------------------------------
int main(void){
    // LED (debug)
    led_init();

    // LCD pins
    pin_out(&HT_DATA_PORT, HT_DATA_PIN);
    pin_out(&HT_WR_PORT,   HT_WR_PIN);
    pin_out(&HT_CS_PORT,   HT_CS_PIN);

    // Soft I2C pull-ups on SDA/SCL
    SDA_PORT.PIN2CTRL = PORT_PULLUPEN_bm;
    SCL_PORT.PIN3CTRL = PORT_PULLUPEN_bm;
    SDA_PORT.DIRCLR   = _BV8(SDA_PIN);   // inputs
    SCL_PORT.DIRCLR   = _BV8(SCL_PIN);

    // Initialize LCD
    lcd_init();
    lcd_clear_all();

    // Initialize RTC (clear POR, force 24h)
    if (rv3028_init_soft() != 0){
        // If RTC init fails, blink 5 forever
        while (1){
            blink_n(5);
        }
    }

    // -----------------------------------------------------------------
    // OPTIONAL: Set RTC time at startup (24-hour format)
    //   Change this line to whatever time you want, or comment it out
    //   after you’ve set the time once.
    //
    // Example: set to 21:58:00 (9:58 PM)
    // -----------------------------------------------------------------
    rv3028_set_time_soft(15, 57, 0);
    // -----------------------------------------------------------------

    rtc_time_t now;

    while (1){
        if (rv3028_get_time_soft(&now) != 0){
            // If read fails, briefly show 0000 and blink 5
            lcd_show_4digit(0, 0, 0);
            blink_n(5);
            continue;
        }

        // Convert to 12-hour + PM flag
        uint8_t hour12, pm;
        rtc_24h_to_12h(now.hour, &hour12, &pm);

        // Build HHMM value
        uint16_t value = (uint16_t)hour12 * 100u + (uint16_t)now.min;

        // Colon blink tied to RTC seconds: on for odd, off for even
        uint8_t colon_on = (now.sec & 1u);

        // Update display
        lcd_show_4digit(value, colon_on, pm);

        // Wait about 1 second before next read / update
        _delay_ms(1000);
    }

    // Should never reach here
    while (1){}
}
