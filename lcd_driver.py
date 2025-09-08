"""
ESP32 bare-glass LCD driver for CaliperClock — COM Identification Edition
COM pins: GPIO2, GPIO4, GPIO17, GPIO18 (supports 1/2, 1/3, 1/4 duty)
SEG pin: GPIO22 (single test segment)
"""

import time
import machine

class LCDDriver:
    def __init__(self):
        # --- Hardware pins ---
        self.ALL_COM_PINS = [
            machine.Pin(2, machine.Pin.OUT),   # index 0
            machine.Pin(4, machine.Pin.OUT),   # index 1
            machine.Pin(17, machine.Pin.OUT),  # index 2
            machine.Pin(18, machine.Pin.OUT),  # index 3
        ]
        self.SEG_PIN = machine.Pin(22, machine.Pin.OUT)

        # --- Timing defaults (tweakable at runtime) ---
        self.FRAME_HZ = 60         # full frame refresh (per COM returns once per frame)
        self.DUTY = 4              # 2, 3, or 4 (number of COM slots used)
        self.SLOT_TIME = 1.0 / (self.FRAME_HZ * self.DUTY * 2)  # half-slot for AC

        # Human-visible dwell per highlighted COM in identify modes
        self.STROBE_SECONDS = 1.0

        # Initialize low
        for p in self.ALL_COM_PINS:
            p.off()
        self.SEG_PIN.off()

        print("LCD Driver initialized")
        self._print_params()

    # ---------- Low-level slot drive ----------
    def _drive_one_slot_halfcycle(self, duty, slot_idx, polarity, seg_on_for_slot):
        """
        Drive one half-cycle of a single COM 'slot':
        - COMs: selected slot is opposite polarity to others.
        - SEG: ON -> invert vs active COM; OFF -> match active COM.
        """
        # Drive COMs
        for i, pin in enumerate(self.ALL_COM_PINS):
            if i < duty:
                val = (1 - polarity) if i == slot_idx else polarity
                pin.value(val)
            else:
                pin.value(0)

        # Active COM logic (value on the selected COM before the flip)
        active_com_val = (1 - polarity)

        # SEG behavior
        if seg_on_for_slot:
            self.SEG_PIN.value(1 - active_com_val)  # ON: invert vs active COM
        else:
            self.SEG_PIN.value(active_com_val)      # OFF: match active COM

        time.sleep(self.SLOT_TIME)

    # ---------- Utilities ----------
    def _print_params(self):
        print(f"- Frame rate: {self.FRAME_HZ} Hz")
        print(f"- Duty: 1/{self.DUTY}")
        print(f"- Half-slot time: {self.SLOT_TIME:.6f} s")
        print(f"- Strobe per slot (identify): {self.STROBE_SECONDS:.2f} s")
        print(f"- COM GPIOs (slot order): [2, 4, 17, 18]")
        print(f"- SEG GPIO: 22")

    def set_frame_rate(self, hz):
        self.FRAME_HZ = int(hz)
        self._recompute_slot_time()

    def set_duty(self, duty):
        if duty not in (2, 3, 4):
            raise ValueError("Duty must be 2, 3, or 4")
        self.DUTY = duty
        self._recompute_slot_time()

    def set_strobe_seconds(self, s):
        self.STROBE_SECONDS = float(s)

    def _recompute_slot_time(self):
        self.SLOT_TIME = 1.0 / (self.FRAME_HZ * self.DUTY * 2)
        print(f"[Timing] Recomputed half-slot time: {self.SLOT_TIME:.6f} s")

    def cleanup(self):
        for pin in self.ALL_COM_PINS:
            pin.off()
        self.SEG_PIN.off()
        print("All pins off.")

    def _run_for_seconds(self, duty, slot_idx, seconds, seg_on):
        """
        Helper: drive a single slot for 'seconds' with SEG either ON or OFF,
        flipping polarity every half-slot for AC.
        """
        half_cycles = max(1, int(seconds / self.SLOT_TIME))
        # make even so we end a slot with the same polarity we started
        if (half_cycles % 2) != 0:
            half_cycles += 1
        polarity = 0
        for _ in range(half_cycles):
            self._drive_one_slot_halfcycle(duty, slot_idx, polarity, seg_on_for_slot=seg_on)
            polarity ^= 1

    def lock_com_ref_guarded(self, duty, com_ref_index=0, seg_on=True, seconds=None):
        """
        Hard isolation with proper guarding:
        - Selected COM (com_ref_index) is driven opposite to SEG (if seg_on=True) so ONLY that pixel lights.
        - All other COMs are forced to MATCH SEG each half-cycle, so they see ~0V vs SEG and stay OFF.
        - Polarity flips every half-slot to keep zero DC bias.
        - If 'seconds' is None, runs until Ctrl-C; otherwise runs for the given duration.
        """
        if duty not in (2, 3, 4):
            raise ValueError("duty must be 2, 3, or 4")
        if not (0 <= com_ref_index < duty):
            raise ValueError("com_ref_index must be within 0..duty-1")

        print(f"[LockCOM-Guarded] duty=1/{duty}, COM_ref=COM{com_ref_index} (GPIO{[2,4,17,18][com_ref_index]}) "
            f"SEG {'ON' if seg_on else 'OFF'}")

        infinite = seconds is None
        half_cycles = max(2, int((seconds or 0)/self.SLOT_TIME))
        if not infinite and (half_cycles % 2): half_cycles += 1

        polarity = 0
        try:
            n = 0
            while infinite or n < half_cycles:
                # Choose a base level for this half-cycle
                # We'll define SEG level first, then drive COMs accordingly.
                # For ON: make SEG the inverse of the selected COM.
                # For OFF: make SEG equal to the selected COM.
                # Let sel = current level we *want* on the selected COM this half-cycle.
                sel = (1 - polarity)  # just a toggling bit

                if seg_on:
                    seg_level = 1 - sel    # ON: SEG opposite the selected COM
                else:
                    seg_level = sel        # OFF: SEG matches the selected COM

                # Drive SEG
                self.SEG_PIN.value(seg_level)

                # Drive COMs:
                for i, pin in enumerate(self.ALL_COM_PINS):
                    if i >= duty:
                        pin.value(0)       # unused pins low
                        continue
                    if i == com_ref_index:
                        pin.value(sel)     # selected COM = 'sel'
                    else:
                        pin.value(seg_level)  # guard COMs: match SEG to force ~0V on non-selected pixels

                time.sleep(self.SLOT_TIME)
                polarity ^= 1
                n += 1
        except KeyboardInterrupt:
            print("\nStopping LockCOM-Guarded.")
        finally:
            self.cleanup()





# ---------------- Main menu ----------------
def main():
    lcd = LCDDriver()

    # --- Quick config ---
    lcd.set_duty(3)                # set to 2, 3, or 4 for your panel
    lcd.set_frame_rate(60)         # 30–80 works well
    lcd.set_strobe_seconds(1.0)    # 0.5–2.0 seconds is fine

    # Single COM checker (using the guarded version)
    # lcd.lock_com_ref_guarded(duty=3, com_ref_index=2, seg_on=True)

    # Try to find a COM3????
    #lcd.lock_com_ref_guarded(duty=lcd.DUTY, com_ref_index=2, seg_on=True)


    # Run through each COM slot with SEG ON then OFF to figure out which SEG/COM combo lights up which segment
    while(True):
        lcd.lock_com_ref_guarded(duty=lcd.DUTY, com_ref_index=0, seg_on=True, seconds=1)
        lcd.lock_com_ref_guarded(duty=lcd.DUTY, com_ref_index=0, seg_on=False, seconds=0.5)
        lcd.lock_com_ref_guarded(duty=lcd.DUTY, com_ref_index=1, seg_on=True, seconds=1)
        lcd.lock_com_ref_guarded(duty=lcd.DUTY, com_ref_index=1, seg_on=False, seconds=0.5)
        lcd.lock_com_ref_guarded(duty=lcd.DUTY, com_ref_index=2, seg_on=True, seconds=1)
        lcd.lock_com_ref_guarded(duty=lcd.DUTY, com_ref_index=2, seg_on=False, seconds=2)


if __name__ == "__main__":
    main()
