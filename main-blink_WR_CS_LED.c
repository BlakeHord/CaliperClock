#define F_CPU 3333333UL
#include <avr/io.h>
#include <util/delay.h>

// --- Pin map (your confirmed ones) ---
#define HT1621_WR_PORT   PORTA
#define HT1621_WR_PIN    7   // PA7 → WR
#define HT1621_CS_PORT   PORTB
#define HT1621_CS_PIN    3   // PB3 → CS
#define LED_PORT         PORTA
#define LED_PIN          6   // PA6 (heartbeat LED)

#define _BV8(b) ((uint8_t)(1u << (b)))

int main(void) {
    // Make pins outputs
    HT1621_WR_PORT.DIRSET = _BV8(HT1621_WR_PIN);
    HT1621_CS_PORT.DIRSET = _BV8(HT1621_CS_PIN);
    LED_PORT.DIRSET       = _BV8(LED_PIN);

    // Idle high
    HT1621_WR_PORT.OUTSET = _BV8(HT1621_WR_PIN);
    HT1621_CS_PORT.OUTSET = _BV8(HT1621_CS_PIN);
    LED_PORT.OUTSET       = _BV8(LED_PIN); // LED off (sink config)

    while (1) {
        // Toggle WR at ~10 Hz, CS at ~2 Hz, blink LED to show MCU alive
        HT1621_WR_PORT.OUTTGL = _BV8(HT1621_WR_PIN);  // flip WR
        _delay_ms(50);                                 // 10 Hz

        static uint8_t div = 0;
        div++;
        if (div >= 5) {  // every 5 WR toggles → toggle CS, blink LED
            div = 0;
            HT1621_CS_PORT.OUTTGL = _BV8(HT1621_CS_PIN);
            LED_PORT.OUTTGL = _BV8(LED_PIN);
        }
    }
}
