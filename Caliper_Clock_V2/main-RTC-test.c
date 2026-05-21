// -----------------------------------------------------------------------------
// ATtiny1616 + RV-3028 via soft I2C ONLY
// LED on PA6 (active-low), SDA on PA2, SCL on PA3
//
// Behavior:
//   - Soft I2C reads 3 bytes starting at 0x00 (sec, min, hour) from 0x52.
//   - If read fails: 5 blinks forever.
//   - If read works: in a loop, blink H, then M, then S counts on LED.
// ----------------------------------------------------------------------------- 

#define F_CPU 3333333UL
#include <avr/io.h>
#include <util/delay.h>
#include <stdint.h>

#define _BV8(b) ((uint8_t)(1u << (b)))

// --- Pin mapping ---
#define LED_PORT   PORTA
#define LED_PIN    6   // PA6 active-low LED

#define SDA_PORT   PORTA
#define SDA_PIN    2   // PA2
#define SCL_PORT   PORTA
#define SCL_PIN    3   // PA3

// --- LED helpers ---
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
        // represent 0 as a small pause
        dms(400);
        return;
    }
    for(uint8_t i=0; i<n; i++){
        led_on();  dms(120);
        led_off(); dms(120);
    }
    dms(600);
}

// --- Soft I2C on PA2/PA3 ---
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

// Write: addr7, reg, then len bytes of data (not used here but handy)
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

// --- RV-3028 helpers (soft I2C) ---
#define RV3028_ADDR  0x52

static inline uint8_t bcd2bin(uint8_t v){ return (uint8_t)(10*(v>>4) + (v & 0x0F)); }

typedef struct {
    uint8_t sec, min, hour;
} rtc_time_t;

static uint8_t rv3028_get_time_soft(rtc_time_t *t){
    uint8_t raw[3];
    if (i2c_soft_read_reg(RV3028_ADDR, 0x00, raw, 3) != 0) return 1;

    t->sec  = bcd2bin(raw[0] & 0x7F);
    t->min  = bcd2bin(raw[1] & 0x7F);
    t->hour = bcd2bin(raw[2] & 0x3F);
    return 0;
}

// --- main ---
int main(void){
    led_init();

    // enable pull-ups on SDA/SCL
    SDA_PORT.PIN2CTRL = PORT_PULLUPEN_bm;
    SCL_PORT.PIN3CTRL = PORT_PULLUPEN_bm;
    SDA_PORT.DIRCLR = _BV8(SDA_PIN);
    SCL_PORT.DIRCLR = _BV8(SCL_PIN);

    for(;;){
        rtc_time_t now;
        if (rv3028_get_time_soft(&now) != 0){
            // error - can't read time
            blink_n(5);
            continue;
        }

        // Blink hour, minute, second
        blink_n(now.hour);
        dms(800);
        blink_n(now.min);
        dms(800);
        blink_n(now.sec);
        dms(1500);
    }
}
