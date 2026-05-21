// CaliperClock bring-up: ATtiny1616 + HT1621B
// Mode: "ALL SEGMENTS ON" (continuous burst refresh) + LED heartbeat
// Pins (confirmed):
//   DATA = PA1, WR = PA7, /CS = PB3, LED(sink) = PA6
// Toolchain: avr-gcc; Program via pyupdi

#define F_CPU 3333333UL
#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include <string.h>


// ---------- Pin mapping ----------
#define HT_DATA_PORT   PORTA
#define HT_DATA_PIN    1       // PA1
#define HT_WR_PORT     PORTA
#define HT_WR_PIN      7       // PA7
#define HT_CS_PORT     PORTB
#define HT_CS_PIN      3       // PB3 (/CS active-low)
#define LED_PORT       PORTA
#define LED_PIN        6       // PA6 (active-low LED sink)

// ---------- LCD geometry / timings ----------
#define HT_N_ADDR      32      // 32 nibbles (SEG addresses)
#define LED_BLINK_MS   500     // ~1 Hz blink

#define _BV8(b) ((uint8_t)(1u << (b))) // creates a bitmask with bit number b set to 1, and all others set to 0

// ---------- GPIO helpers ----------
static inline void pin_out (volatile PORT_t* p, uint8_t pin){ p->DIRSET = _BV8(pin); } // set the pin as an output
static inline void pin_high(volatile PORT_t* p, uint8_t pin){ p->OUTSET = _BV8(pin); } // set the pin high
static inline void pin_low (volatile PORT_t* p, uint8_t pin){ p->OUTCLR = _BV8(pin); } // set the pin low

static inline void t_hold(void){ _delay_us(4); } // delay for 4 microseconds
static inline void wr_pulse(void){ // send a LOW then HIGH pulse to the WR pin 
    pin_low (&HT_WR_PORT, HT_WR_PIN);  t_hold();
    pin_high(&HT_WR_PORT, HT_WR_PIN);  t_hold();
}

// ---------- Bit-bang serial (MSB-first) ----------
static inline void send_bit(uint8_t bit){ // send a single bit to the DATA pin
    if (bit) pin_high(&HT_DATA_PORT, HT_DATA_PIN);
    else     pin_low (&HT_DATA_PORT, HT_DATA_PIN);
    wr_pulse(); // send a pulse to the WR pin
}
static void send_bits(uint32_t v, uint8_t nbits){ // send a bunch of bits to the DATA pin
    for (int8_t i = nbits - 1; i >= 0; --i) send_bit( (v >> i) & 1 );
}

// ---------- 9-bit command payloads (after ID=100) ----------

#define CMD_SYSEN            0b000000010    // System enable
#define CMD_LCDON            0b000000110    // LCD on
#define CMD_RC256K           0b000110000    // Internal RC 256 kHz
#define CMD_BIASCOM_3COM_13  0b000010010    // 3-COM, 1/3-bias
#define CMD_BIASCOM_4COM_13  0b000010110    // 4-COM, 1/3-bias

// ---------- HT1621 primitives ----------
static void ht_cmd9(uint16_t payload9){ // Send a 9-bit command payload after the 100 command ID.
    pin_low(&HT_CS_PORT, HT_CS_PIN);    // pull the CS pin low to start the command
    send_bits(0b100, 3);                // send the command ID (100) on the DATA pin
    send_bits(payload9 & 0x1FF, 9);     // send the 9-bit payload on the DATA pin - protect against overflow
    pin_high(&HT_CS_PORT, HT_CS_PIN);   // pull the CS pin high to end the command
    t_hold();                           // hold the CS pin high for 1 microsecond
}


// ---------- Init & main pattern ----------
static void lcd_init(void){
    // Make sure all pins are idle high
    pin_high(&HT_DATA_PORT, HT_DATA_PIN);
    pin_high(&HT_WR_PORT,   HT_WR_PIN);
    pin_high(&HT_CS_PORT,   HT_CS_PIN);

    // SYS EN → RC clock → bias/com → LCD ON
    ht_cmd9(CMD_SYSEN); // power up internal logic/oscillator gate
    ht_cmd9(CMD_RC256K); // select internal 256 kHz RC clock

    // Try 3-COM first; if glass uses COM1–COM3, use 4-COM instead.
    //ht_cmd9(CMD_BIASCOM_3COM_13);
    ht_cmd9(CMD_BIASCOM_4COM_13); // uncomment to force 4-COM

    ht_cmd9(CMD_LCDON); // turn on the LCD
    _delay_ms(5); // wait 5 milliseconds to stabilize the LCD
}


// TEST 1 FUNCTIONS

// Burst write: ID=101 + 6-bit start address + N×(4-bit data)
static void ht_burst_write_all(uint8_t start_addr, uint8_t nibble){
    pin_low(&HT_CS_PORT, HT_CS_PIN);            // pull the CS pin low to start the command
    send_bits(0b101, 3);                        // send the command ID (101) on the DATA pin
    send_bits(start_addr & 0x3F, 6);            // send the 6-bit start address on the DATA pin - protect against overflow
    for (uint8_t i = 0; i < HT_N_ADDR; ++i)     // 32 nibbles - send the data for all 32 segments
        send_bits(nibble & 0x0F, 4);            // send the 4-bit data on the DATA pin - protect against overflow
    pin_high(&HT_CS_PORT, HT_CS_PIN);           // pull the CS pin high to end the command
    t_hold();                                   // hold the CS pin high for 1 microsecond
}

//ht_burst_write_all(0, 0x0);  // clear all segments
//ht_burst_write_all(0, 0xF);  // turn on all segments (test)


// TEST 2 FUNCTIONS

// ---------- Single-nibble write (addr = 6b, data = 4b) ----------
static inline void ht_write_nibble(uint8_t addr, uint8_t nibble){
    pin_low(&HT_CS_PORT, HT_CS_PIN);
    send_bits(0b101, 3);                 // Write Data ID
    send_bits(addr & 0x3F, 6);           // 6-bit address
    send_bits(nibble & 0x0F, 4);         // 4-bit data
    pin_high(&HT_CS_PORT, HT_CS_PIN);
    t_hold();
}

// ---------- One-at-a-time segment walker ----------
// Set how many COM bits to exercise and (optionally) shift which nibble bit is used.
// For normal 3-COM wiring on controller COM0..COM2: HT_COM_BITS=3, HT_COM_SHIFT=0
// For your "glass on controller COM1..COM3" case in 4-COM mode: HT_COM_BITS=3, HT_COM_SHIFT=1
#ifndef HT_COM_BITS
#define HT_COM_BITS   4
#endif
#ifndef HT_COM_SHIFT
#define HT_COM_SHIFT  0
#endif

static void lcd_walk_one_at_a_time(uint16_t dwell_ms){
    for (uint8_t addr = 0; addr < HT_N_ADDR; ++addr){
        for (uint8_t c = 0; c < HT_COM_BITS; ++c){
            // Clear everything so only one segment is on
            ht_burst_write_all(0x00, 0x0);

            // Turn on exactly one COM bit at this address
            uint8_t nibble = (uint8_t)(1u << (c + HT_COM_SHIFT)); // map to COM bit
            ht_write_nibble(addr, nibble);

            // Dwell so you can see it
            uint16_t remain = dwell_ms;
            while (remain >= 10){
                _delay_ms(10);  // Fixed constant for _delay_ms
                remain -= 10;
            }
            if (remain > 0){
                _delay_ms(1);  // Handle any remainder with 1ms delays
            }
        }
    }
}


// TEST 3 FUNCTIONS

// ---------- Address-ranged, no-pause walker ----------
typedef struct { uint8_t start, count; } addr_range_t;

// EDIT THESE to match your panel mapping.
// Example: only 0x00..0x1F used:
static const addr_range_t ACTIVE_RANGES[] = {
    { 0x00, 11 },  // first bank
    //{ 0x20, 32 }, // uncomment if your panel uses a second bank
};
static const uint8_t NUM_ACTIVE_RANGES = sizeof(ACTIVE_RANGES)/sizeof(ACTIVE_RANGES[0]);

static inline void lcd_delay_ms(uint16_t ms){
    while (ms >= 10){ _delay_ms(10); ms -= 10; }
    while (ms--)     _delay_ms(1);
}

static void lcd_walk_stream_ranged(uint16_t dwell_ms){
    static uint8_t inited = 0;
    static uint8_t prev_addr = 0, prev_mask = 0;

    if (!inited){
        ht_burst_write_all(0x00, 0x0);  // one-time clear
        inited = 1;
    }

    for (uint8_t r = 0; r < NUM_ACTIVE_RANGES; ++r){
        uint8_t start = ACTIVE_RANGES[r].start;
        uint8_t count = ACTIVE_RANGES[r].count;

        for (uint8_t i = 0; i < count; ++i){
            uint8_t addr = (uint8_t)(start + i);
            for (uint8_t c = 0; c < HT_COM_BITS; ++c){
                uint8_t mask = (uint8_t)(1u << (c + HT_COM_SHIFT)); // which COM bit

                if (prev_mask) ht_write_nibble(prev_addr, 0x0); // turn off previous
                ht_write_nibble(addr, mask);                    // turn on next

                prev_addr = addr;
                prev_mask = mask;

                lcd_delay_ms(dwell_ms);

                // *** LED heartbeat for Test 3: toggle LED each segment step ***
                LED_PORT.OUTTGL = _BV8(LED_PIN);
            }
            lcd_delay_ms(dwell_ms*3);
        }
    }
}



int main(void){
    // Configure pins
    pin_out(&HT_DATA_PORT, HT_DATA_PIN); // set the DATA pin as an output
    pin_out(&HT_WR_PORT,   HT_WR_PIN);   // set the WR pin as an output
    pin_out(&HT_CS_PORT,   HT_CS_PIN);   // set the CS pin as an output
    pin_out(&LED_PORT,     LED_PIN);     // set the LED pin as an output
    pin_high(&LED_PORT, LED_PIN);        // turn off the LED (active-low)

    lcd_init(); // initialize the LCD

    int test_number = 3; //Number of the test to run

    // TEST #1
    // --- ALL-ON refresh + LED heartbeat ---
    if (test_number == 1){
        uint16_t led_accum = 0;
        for(;;){ // infinite loop
            ht_burst_write_all(0x00, 0xF);   // light all SEG×COM - send the data for all 32 segments
            //ht_burst_write_all(0x20, 0xF);
            for (uint16_t t = 0; t < LED_BLINK_MS; t += 10){
                _delay_ms(10); // wait 10 milliseconds
                led_accum += 10; // increment the LED accumulator by 10 milliseconds
                if (led_accum >= LED_BLINK_MS){
                    LED_PORT.OUTTGL = _BV8(LED_PIN); // toggle the LED pin
                    led_accum = 0; // reset the LED accumulator
                }
            }
        }
    }

    // TEST #2
    // --- Single-nibble write (addr = 6b, data = 4b) ---
    if (test_number == 2){
        while(1){
            lcd_walk_one_at_a_time(250);
        }
    }

    // TEST #3
    // --- Address-ranged, no-pause walker ---
    if (test_number == 3){
        while(1){
            lcd_walk_stream_ranged(500);
        }
    }
}
