// -----------------------------------------------------------------------------
// CaliperClock main.c - Low Power Version with Button Time-Setting
// ATtiny1616 + HT1621B + RV-3028 (soft I2C)
//
// Behavior:
//   - RV-3028 runs in 24h mode, keeps real time.
//   - Minute interrupt wakes ATtiny from sleep.
//   - PB0 long-press enters time-setting mode.
//   - PB1/PB2 increment hour/minute while in time-setting mode.
// -----------------------------------------------------------------------------

#define F_CPU 3333333UL
#include <avr/io.h>
#include <util/delay.h>
#include <stdint.h>
#include <string.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>

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
// PIN MAP (soft I2C for RV-3028 and INT)
// -----------------------------------------------------------------------------
#define SDA_PORT   PORTA
#define SDA_PIN    2   // PA2
#define SCL_PORT   PORTA
#define SCL_PIN    3   // PA3

// RTC INT -> PA4
#define RTC_INT_PORT    PORTA
#define RTC_INT_PIN     4      // PA4

// -----------------------------------------------------------------------------
// BUTTONS
// -----------------------------------------------------------------------------
#define BTN_MODE_PORT   PORTB
#define BTN_MODE_PIN    0   // PB0, enter time-setting mode

#define BTN_HOUR_PORT   PORTB
#define BTN_HOUR_PIN    1   // PB1, increment hour

#define BTN_MIN_PORT    PORTB
#define BTN_MIN_PIN     2   // PB2, increment minute

#define BTN_DEBOUNCE_MS 20   // 20 ms debounce

// -----------------------------------------------------------------------------
// GLOBAL STATE FOR LOW POWER LOGIC
// -----------------------------------------------------------------------------
volatile uint8_t colon_on = 0;                // toggled by PIT ~1 Hz
volatile uint8_t needs_display_refresh = 0;   // set by PIT and/or RTC INT
volatile uint8_t minute_tick_flag = 0;        // set by RTC INT (PA4 ISR)

// Current displayed time (12-hour + PM)
volatile uint8_t cur_hour12 = 12;
volatile uint8_t cur_min    = 0;
volatile uint8_t cur_pm     = 0;

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
    ht_cmd9(CMD_BIASCOM_4COM_13);    // 4-COM mode
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

static const seg_map_t DIGIT_SEG[4][7] = {
    {{10,3}, {10,2}, {10,1}, { 9,1}, { 8,1}, { 9,3}, { 9,2}}, // D0
    {{ 8,2}, { 7,3}, { 7,1}, { 6,1}, { 6,2}, { 6,3}, { 7,2}}, // D1
    {{ 4,3}, { 5,2}, { 5,1}, { 4,1}, { 3,1}, { 3,2}, { 4,2}}, // D2
    {{SEG_EMPTY,1}, { 2,2}, { 2,1}, {SEG_EMPTY,1}, {SEG_EMPTY,1}, {SEG_EMPTY,1}, { 1,2}} // D3
};

static const seg_map_t SEG_COLON = { 8, 3 };
static const seg_map_t SEG_PM    = { 3, 3 };

// 7-seg digit patterns
static const uint8_t DIGIT_PATTERN[10] = {
    0b00111111,0b00000110,0b01011011,0b01001111,
    0b01100110,0b01101101,0b01111101,0b00000111,
    0b01111111,0b01101111
};

// -----------------------------------------------------------------------------
// Clear entire display
// -----------------------------------------------------------------------------
static void lcd_clear_all(void){
    for (uint8_t addr = 0; addr < HT_N_ADDR; ++addr){
        ht_write_nibble(addr, 0x0);
    }
}

// -----------------------------------------------------------------------------
// Render a 4-digit number
// -----------------------------------------------------------------------------
static void lcd_show_4digit(uint16_t value, uint8_t colon_on_local, uint8_t pm_on){
    if (value > 9999) value = 9999;

    uint8_t d[4];
    uint16_t tmp = value;

    d[0] = tmp % 10; tmp /= 10;
    d[1] = tmp % 10; tmp /= 10;
    d[2] = tmp % 10; tmp /= 10;
    d[3] = tmp % 10;

    uint8_t blank[4] = {0,0,0,0};
    if (value == 0){ blank[1]=1; blank[2]=1; blank[3]=1; }
    else {
        if (d[3]==0){ blank[3]=1;
            if (d[2]==0){ blank[2]=1;
                if (d[1]==0) blank[1]=1;
            }
        }
    }

    uint8_t buf[HT_N_ADDR];
    memset(buf,0,sizeof(buf));

    for (uint8_t digit=0;digit<4;digit++){
        if (blank[digit]) continue;
        uint8_t patt = DIGIT_PATTERN[d[digit]];
        for (uint8_t s=0;s<7;s++){
            if (!(patt & (1<<s))) continue;
            seg_map_t m = DIGIT_SEG[digit][s];
            if (m.addr == SEG_EMPTY) continue;
            buf[m.addr] |= (1u << m.com);
        }
    }

    if (colon_on_local) buf[SEG_COLON.addr] |= (1u << SEG_COLON.com);
    if (pm_on)          buf[SEG_PM.addr]    |= (1u << SEG_PM.com);

    for (uint8_t addr=0;addr<HT_N_ADDR;addr++)
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
    if (n==0){ dms(400); return; }
    for(uint8_t i=0;i<n;i++){
        led_on(); dms(120); led_off(); dms(120);
    }
    dms(600);
}

// -----------------------------------------------------------------------------
// Soft I2C helpers
// -----------------------------------------------------------------------------
static inline void sda_hi(void){ SDA_PORT.DIRCLR = _BV8(SDA_PIN); }
static inline void sda_lo(void){ SDA_PORT.DIRSET = _BV8(SDA_PIN); SDA_PORT.OUTCLR = _BV8(SDA_PIN); }
static inline void scl_hi(void){ SCL_PORT.DIRCLR = _BV8(SCL_PIN); }
static inline void scl_lo(void){ SCL_PORT.DIRSET = _BV8(SCL_PIN); SCL_PORT.OUTCLR = _BV8(SCL_PIN); }
static inline uint8_t sda_read(void){ return (VPORTA.IN & _BV8(SDA_PIN)) ? 1 : 0; }
static inline void i2c_soft_delay(void){ _delay_us(200); }  // ~5 kHz

static uint8_t i2c_soft_start(void){
    sda_hi();
    scl_hi();
    i2c_soft_delay();
    if(!sda_read()) return 1;
    sda_lo();
    i2c_soft_delay();
    scl_lo();
    i2c_soft_delay();
    return 0;
}

static void i2c_soft_stop(void){
    sda_lo(); i2c_soft_delay(); scl_hi(); i2c_soft_delay(); sda_hi(); i2c_soft_delay();
}

static uint8_t i2c_soft_write_byte(uint8_t b){
    for(uint8_t i=0;i<8;i++){
        if(b&0x80) sda_hi(); else sda_lo();
        i2c_soft_delay(); scl_hi(); i2c_soft_delay(); scl_lo(); i2c_soft_delay();
        b<<=1;
    }
    sda_hi(); i2c_soft_delay(); scl_hi(); i2c_soft_delay();
    uint8_t ack = (sda_read()==0); scl_lo(); i2c_soft_delay(); sda_hi();
    return ack?0:1;
}

static uint8_t i2c_soft_read_byte(uint8_t *out, uint8_t ack){
    uint8_t b=0;
    sda_hi();
    for(uint8_t i=0;i<8;i++){
        b<<=1; scl_hi(); i2c_soft_delay(); if(sda_read()) b|=1; scl_lo(); i2c_soft_delay();
    }
    if(ack) sda_lo(); else sda_hi();
    i2c_soft_delay(); scl_hi(); i2c_soft_delay(); scl_lo(); i2c_soft_delay(); sda_hi();
    *out=b; return 0;
}

static uint8_t i2c_soft_write_reg(uint8_t addr7,uint8_t reg,const uint8_t *data,uint8_t len){
    if(i2c_soft_start()) return 1;
    if(i2c_soft_write_byte((addr7<<1)|0)){i2c_soft_stop(); return 1;}
    if(i2c_soft_write_byte(reg)){i2c_soft_stop(); return 1;}
    for(uint8_t i=0;i<len;i++){ if(i2c_soft_write_byte(data[i])){i2c_soft_stop(); return 1;} }
    i2c_soft_stop(); return 0;
}

static uint8_t i2c_soft_read_reg(uint8_t addr7,uint8_t reg,uint8_t *out,uint8_t len){
    if(i2c_soft_start()) return 1;
    if(i2c_soft_write_byte((addr7<<1)|0)){i2c_soft_stop(); return 1;}
    if(i2c_soft_write_byte(reg)){i2c_soft_stop(); return 1;}
    if(i2c_soft_start()) return 1;
    if(i2c_soft_write_byte((addr7<<1)|1)){i2c_soft_stop(); return 1;}
    for(uint8_t i=0;i<len;i++){ uint8_t ack=(i<(len-1))?1:0; if(i2c_soft_read_byte(&out[i],ack)){i2c_soft_stop(); return 1;} }
    i2c_soft_stop(); return 0;
}

// -----------------------------------------------------------------------------
// RV-3028 helpers (soft I2C)
// -----------------------------------------------------------------------------
#define RV3028_ADDR 0x52
static inline uint8_t bcd2bin(uint8_t v){ return 10*(v>>4)+(v&0x0F); }
static inline uint8_t bin2bcd(uint8_t v){ return ((v/10)<<4)|(v%10); }

typedef struct { uint8_t sec,min,hour; } rtc_time_t;

static uint8_t rv3028_get_time_soft(rtc_time_t *t){
    uint8_t raw[3];
    if(i2c_soft_read_reg(RV3028_ADDR,0x00,raw,3)!=0) return 1;
    t->sec = bcd2bin(raw[0]&0x7F);
    t->min = bcd2bin(raw[1]&0x7F);
    t->hour= bcd2bin(raw[2]&0x3F);
    return 0;
}

static uint8_t rv3028_init_soft(void){
    uint8_t status;
    if(i2c_soft_read_reg(RV3028_ADDR,0x0E,&status,1)!=0) return 1;
    status &= ~(1<<0); if(i2c_soft_write_reg(RV3028_ADDR,0x0E,&status,1)!=0) return 1;
    uint8_t ctrl2;
    if(i2c_soft_read_reg(RV3028_ADDR,0x10,&ctrl2,1)!=0) return 1;
    ctrl2 &= ~(1<<1); if(i2c_soft_write_reg(RV3028_ADDR,0x10,&ctrl2,1)!=0) return 1;
    return 0;
}

static uint8_t rv3028_set_time_soft(uint8_t hour24,uint8_t min,uint8_t sec){
    uint8_t data[3];
    if(hour24>23) hour24=0;
    if(min>59) min=0;
    if(sec>59) sec=0;
    data[0]=bin2bcd(sec&0x7F); data[1]=bin2bcd(min); data[2]=bin2bcd(hour24);
    return i2c_soft_write_reg(RV3028_ADDR,0x00,data,3);
}

static uint8_t rv3028_enable_minute_update_interrupt(void){
    uint8_t reg;
    if(i2c_soft_read_reg(RV3028_ADDR,0x0E,&reg,1)!=0) return 1;
    reg &= ~(1<<4); if(i2c_soft_write_reg(RV3028_ADDR,0x0E,&reg,1)!=0) return 1;
    if(i2c_soft_read_reg(RV3028_ADDR,0x10,&reg,1)!=0) return 1;
    reg|=(1<<5); if(i2c_soft_write_reg(RV3028_ADDR,0x10,&reg,1)!=0) return 1;
    if(i2c_soft_read_reg(RV3028_ADDR,0x12,&reg,1)!=0) return 1;
    reg|=(1<<0); if(i2c_soft_write_reg(RV3028_ADDR,0x12,&reg,1)!=0) return 1;
    return 0;
}

// -----------------------------------------------------------------------------
// 24h -> 12h conversion
// -----------------------------------------------------------------------------
static void rtc_24h_to_12h(uint8_t hour24,uint8_t *hour12_out,uint8_t *pm_out){
    uint8_t h12,pm;
    if(hour24==0){ h12=12; pm=0; }
    else if(hour24<12){ h12=hour24; pm=0; }
    else if(hour24==12){ h12=12; pm=1; }
    else{ h12=hour24-12; pm=1; }
    *hour12_out=h12; *pm_out=pm;
}

// -----------------------------------------------------------------------------
// RTC PIT (~1 Hz) & INT PA4 Setup
// -----------------------------------------------------------------------------
static void rtc_pit_init_1Hz(void){
    RTC.CTRLA=RTC_RTCEN_bm;
    RTC.PITINTCTRL=RTC_PI_bm;
    RTC.PITCTRLA=RTC_PERIOD_CYC32768_gc|RTC_PITEN_bm;
}

static void porta_pa4_int_init(void){
    RTC_INT_PORT.DIRCLR=_BV8(RTC_INT_PIN);
    RTC_INT_PORT.PIN4CTRL=PORT_PULLUPEN_bm|PORT_ISC_FALLING_gc;
}

// -----------------------------------------------------------------------------
// ISRs
// -----------------------------------------------------------------------------
ISR(RTC_PIT_vect){
    RTC.PITINTFLAGS=RTC_PI_bm;
    colon_on ^= 1;
    needs_display_refresh=1;
}

ISR(PORTA_PORT_vect){
    uint8_t flags=VPORTA.INTFLAGS;
    if(flags & (1<<RTC_INT_PIN)){
        VPORTA.INTFLAGS = (1<<RTC_INT_PIN);
        uint8_t status;
        if(i2c_soft_read_reg(RV3028_ADDR,0x0E,&status,1)==0){
            status &= ~(1<<4);
            i2c_soft_write_reg(RV3028_ADDR,0x0E,&status,1);
        }
        minute_tick_flag=1;
        needs_display_refresh=1;
    }
}

// -----------------------------------------------------------------------------
// Time-Setting Mode (PB0 long-press enters this)
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// Time-Setting Mode (PB0 long-press enters this)
// Flash digits briefly while keeping colon/PM blinking
// -----------------------------------------------------------------------------
static void time_setting_mode(void) {
    uint16_t loop_counter = 0;            // main loop counter for flashing
    uint16_t hour_hold_counter = 0;       // button hold counters
    uint16_t min_hold_counter  = 0;

    // Wait for PB0 release before starting
    while (!(BTN_MODE_PORT.IN & _BV8(BTN_MODE_PIN))) {
        _delay_ms(20);
    }

    while (1) {
        loop_counter++;

        // --- Flash digits briefly ---
        // Flash 100ms every ~0.7s (700ms / 20ms per loop = 35 loops)
        if (loop_counter % 35 < 5) { // first 5 loops = 100ms blank
            lcd_show_4digit(0x0000, colon_on, cur_pm); // blank digits, keep colon/PM
        } else {
            lcd_show_4digit(cur_hour12 * 100 + cur_min, colon_on, cur_pm);
        }

        // --- Hour button (PB1) with debounce + repeat) ---
        if (!(BTN_HOUR_PORT.IN & _BV8(BTN_HOUR_PIN))) {
            hour_hold_counter++;
            if (hour_hold_counter == 1 || hour_hold_counter >= 20) { 
                cur_hour12++;
                if (cur_hour12 > 12) cur_hour12 = 1;
            }
        } else {
            hour_hold_counter = 0;
        }

        // --- Minute button (PB2) with debounce + repeat) ---
        if (!(BTN_MIN_PORT.IN & _BV8(BTN_MIN_PIN))) {
            min_hold_counter++;
            if (min_hold_counter == 1 || min_hold_counter >= 20) {
                cur_min++;
                if (cur_min > 59) cur_min = 0;
            }
        } else {
            min_hold_counter = 0;
        }

        // --- Exit on PB0 new press ---
        if (!(BTN_MODE_PORT.IN & _BV8(BTN_MODE_PIN))) {
            _delay_ms(50); // debounce
            if (!(BTN_MODE_PORT.IN & _BV8(BTN_MODE_PIN))) break;
        }

        _delay_ms(20); // main loop interval
    }

    // --- Commit new time back to RTC ---
    uint8_t hour24 = cur_pm ? (cur_hour12 == 12 ? 12 : cur_hour12 + 12)
                            : (cur_hour12 == 12 ? 0 : cur_hour12);
    rv3028_set_time_soft(hour24, cur_min, 0);
}

// -----------------------------------------------------------------------------
// MAIN
// -----------------------------------------------------------------------------
int main(void){
    led_init();

    pin_out(&HT_DATA_PORT, HT_DATA_PIN);
    pin_out(&HT_WR_PORT, HT_WR_PIN);
    pin_out(&HT_CS_PORT, HT_CS_PIN);

    SDA_PORT.PIN2CTRL = PORT_PULLUPEN_bm;
    SCL_PORT.PIN3CTRL = PORT_PULLUPEN_bm;
    SDA_PORT.DIRCLR=_BV8(SDA_PIN);
    SCL_PORT.DIRCLR=_BV8(SCL_PIN);

    lcd_init();
    lcd_clear_all();

    if(rv3028_init_soft()!=0) while(1){ blink_n(5); }

    rv3028_set_time_soft(10,58,0);

    if(rv3028_enable_minute_update_interrupt()!=0) while(1){ blink_n(3); }

    rtc_pit_init_1Hz();
    porta_pa4_int_init();

    rtc_time_t now;
    if(rv3028_get_time_soft(&now)==0){
        uint8_t h12,pm;
        rtc_24h_to_12h(now.hour,&h12,&pm);
        cur_hour12=h12; cur_min=now.min; cur_pm=pm;
    }

    // Buttons: pull-ups
    BTN_MODE_PORT.PIN0CTRL = PORT_PULLUPEN_bm;
    BTN_HOUR_PORT.PIN1CTRL = PORT_PULLUPEN_bm;
    BTN_MIN_PORT.PIN2CTRL = PORT_PULLUPEN_bm;
    BTN_MODE_PORT.DIRCLR=_BV8(BTN_MODE_PIN);
    BTN_HOUR_PORT.DIRCLR=_BV8(BTN_HOUR_PIN);
    BTN_MIN_PORT.DIRCLR=_BV8(BTN_MIN_PIN);

    sei();
    set_sleep_mode(SLEEP_MODE_PWR_DOWN);

    while(1){
        sleep_mode();

        if(needs_display_refresh){
            needs_display_refresh=0;
            if(minute_tick_flag){
                minute_tick_flag=0;
                if(rv3028_get_time_soft(&now)==0){
                    uint8_t h12,pm;
                    rtc_24h_to_12h(now.hour,&h12,&pm);
                    cur_hour12=h12; cur_min=now.min; cur_pm=pm;
                } else { cur_hour12=12; cur_min=6; cur_pm=0; }
            }
            lcd_show_4digit(cur_hour12*100+cur_min,colon_on,cur_pm);
        }

        // PB0 long press detection
        if(!(BTN_MODE_PORT.IN & _BV8(BTN_MODE_PIN))){
            uint16_t pressed_ms=0;
            while(!(BTN_MODE_PORT.IN & _BV8(BTN_MODE_PIN))){
                _delay_ms(10); pressed_ms+=10;
                if(pressed_ms>=5000){ time_setting_mode(); break; }
            }
        }
    }
    while(1){}
}
