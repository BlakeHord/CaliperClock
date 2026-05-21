/*
 * buttons.h -- the three Caliper Clock V3 buttons (Task 5).
 *
 * MODE = P1.0, HOUR = P1.1, MIN = P1.2. Active-low with internal pull-ups;
 * falling-edge interrupts wake the MCU from LPM3. A 50 ms Timer_A0 window
 * debounces (first edge latches the press, further edges ignored for 50 ms).
 *
 * NOTE: the V3 hardware overview calls these "capacitive touch pads," but the
 * Task 5 spec treats them as simple short-to-ground buttons (pull-ups +
 * falling edge), which is what this implements. If the pads turn out to need
 * real capacitive sensing, this module must be revisited. Confirm on hardware.
 */
#ifndef BUTTONS_H
#define BUTTONS_H

#include <stdint.h>

typedef enum {
    BTN_NONE = 0,
    BTN_MODE,
    BTN_HOUR,
    BTN_MIN
} button_t;

/* Configure P1.0-1.2 and the debounce timer. Call once after board init. */
void buttons_init(void);

/* Return (and clear) the most recent debounced press, or BTN_NONE. */
button_t buttons_get_event(void);

/* Current raw state: 1 if the button is being held down (for long-press /
 * hold-to-repeat in Task 6). */
uint8_t buttons_is_down(button_t b);

#endif /* BUTTONS_H */
