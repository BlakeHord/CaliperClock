# Caliper Clock V3 Firmware

Firmware for the **MSP430FR4133IPM**-based Caliper Clock V3. Developed and tested
on an **MSP-EXP430FR4133 LaunchPad** while the custom boards are at the fab.

Toolchain: **MSP430-GCC** (compiler) + **mspdebug** with the **tilib** backend
(flashing over Spy-Bi-Wire via the LaunchPad's onboard eZ-FET). No CCS.

> Target platform for this setup: **macOS on Apple Silicon**. The clean path is
> MacPorts, because the `tilib` backend needs TI's proprietary `libmsp430.dylib`,
> which Homebrew core does not package. (Note: no live GDB on Apple Silicon —
> TI's debug-stack GDB path is x86_64 only. `mspdebug` still flashes fine.)

## Layout

```
firmware/
├── Makefile        # build / flash / debug / clean
├── src/            # .c sources (main.c = Task 1 blink stub for now)
├── inc/            # .h headers
├── build/          # object files        (git-ignored)
└── bin/            # firmware.elf output (git-ignored)
```

## One-time toolchain install (MacPorts)

1. Install MacPorts if you don't have it: https://www.macports.org/install.php
2. Install the compiler + flasher (pulls in support files, binutils, and the
   custom `libmsp430` port that provides the TI debug-stack dylib):

   ```sh
   sudo port install msp430-elf-gcc mspdebug
   ```

3. Verify the compiler is on PATH:

   ```sh
   msp430-elf-gcc --version
   ```

4. **Locate the device support files** (headers + linker script). The build
   needs `msp430fr4133.h` and `msp430fr4133.ld`:

   ```sh
   port contents msp430-gcc-support-files | grep -E 'msp430fr4133\.(h|ld)'
   ```

   - If they sit on the compiler's default search path, you're done.
   - Otherwise set `SUPPORT_DIR` to the directory containing them, either by
     editing the `Makefile` or per-invocation: `make SUPPORT_DIR=/path/to/include`.

## Build

```sh
make                 # builds the default bring-up test (BRINGUP=2) -> bin/firmware.elf
make BRINGUP=1       # build a specific bring-up test (see below)
make clean           # removes build/ and bin/
```

`src/main.c` selects one **bring-up test** at compile time via `BRINGUP`. The
object files don't track that flag, so **`make clean` when switching `BRINGUP`**.

## Flash & debug (LaunchPad connected via USB)

```sh
make flash               # build (BRINGUP=2) + mspdebug tilib "prog bin/firmware.elf"
make BRINGUP=1 flash     # build+flash a specific test
make debug               # interactive mspdebug session
```

The Makefile prefixes mspdebug with `DYLD_LIBRARY_PATH=/opt/local/lib` so `tilib`
can find `libmsp430.dylib`. If flashing fails to find the FET, unplug/replug the
LaunchPad and retry (a known macOS eZ-FET quirk).

## Bring-up sequence

The point is to build confidence one layer at a time: each step has a flashable
test and a concrete "you should see X" so a failure is isolated to the layer you
just added — instead of debugging a whole clock at once. Steps 1–6 are implemented
(3 runs only on a V3 board); step 7 is the on-hardware power-measurement task.

Run each on the **LaunchPad** first; once the custom V3 boards arrive, repeat the
hardware-dependent ones (LCD, buttons, power) on the real board.

| # | Build | What it proves | Pass criterion |
|---|-------|----------------|----------------|
| 1 | `make BRINGUP=1 flash` | Toolchain + flash path + the chip is alive | LaunchPad LED2 (P4.0) blinks ~1 Hz |
| 2 | `make BRINGUP=2 flash` | LCD_E peripheral: 4-mux, charge-pump bias, ACLK, pin mux | LaunchPad glass shows **HELLO** (steady, readable contrast) |
| 3 | `make BRINGUP=3 flash` (V3 board only) | The V3→caliper SEG/COM map matches the physical glass | Scan test lights each (Lxx,COM); record the mapping and correct `HT1621_ADDR_TO_LCDE_SEG[]` |
| 4 | `make BRINGUP=4 flash` (LaunchPad) | XT1 32.768 kHz + RTC 1 Hz tick keeps time | LED2 toggles every **1.000 s** (time it vs. a reference to confirm `RTCMOD`); displayed time advances (minutes roll over) |
| 5 | `make BRINGUP=5 flash` | P1.0–1.2 wake from LPM3; 50 ms debounce | Button to GND shows MODE/HOUR/MIN + toggles LED2; idles in LPM3 |
| 6 | `make BRINGUP=6 flash` (LaunchPad) | Full clock + set-time UI (RTC+buttons+display) | Time advances; 5 s MODE-hold enters set (digits flash); HOUR/MIN adjust; MODE commits |
| 7 | power profiling (see below) | Meets the ~µA budget | LPM3 + LCD ≈ 1–2 µA on a meter / EnergyTrace |

Notes:
- **Step 2 caveat:** the FH-1138P glass on the LaunchPad has a *different* pinout
  from the caliper LCD. Step 2 only validates that LCD_E itself works; the
  caliper-specific segment map is Step 3.
- **Clock source:** Steps 1–2 run on the default post-reset ACLK (~32 kHz REFO),
  matching TI's out-of-box demo. Step 4 switches ACLK to the 32.768 kHz **XT1**
  crystal for timekeeping accuracy and lower power.
- **Sleep mode = LPM3, not LPM3.5.** Per the datasheet (§8.7), LPM3 with the LCD
  and RTC is ~1.1 µA typ — inside budget — while the always-on LCD charge pump
  (~1 µA) means LPM3.5 would save only ~0.2 µA at the cost of waking via reset
  (FRAM state persistence, full re-init). LPM3 wakes cleanly per second and on
  button edges. Revisit LPM3.5 in Task 7 only if measurements demand it.

## Current status

- **Task 1 (done):** toolchain + skeleton verified; LED2 blink stub builds clean.
- **Task 2 (done, untested on HW):** `src/hal_lcd.c` drives the LaunchPad
  FH-1138P glass via LCD_E and displays "HELLO". Builds clean; segment tables and
  register sequence ported from verified TI/Energia sources (see file headers).
  Needs a LaunchPad to confirm visually.
- **Task 3 (done, V3-board-only):** `src/caliper_lcd.c` drives the caliper glass
  via LCD_E in Mode 2 bias (datasheet-verified). V2's segment map is ported
  verbatim; the V2→V3 segment-line translation is one flagged table
  (`HT1621_ADDR_TO_LCDE_SEG[]`) resolved by the scan test on real hardware.
  `lcd_show_4digit` + scan test build clean. Can't run until V3 boards exist.
- **Task 4 (done, LaunchPad-testable):** `src/rtc_clock.c` sources ACLK from the
  32.768 kHz XT1 crystal and runs the RTC counter at 1 Hz, maintaining
  sec/min/hour with 24→12 conversion (ported from V2). All register choices
  datasheet-verified (SLAU445 CS ch.3 + RTC ch.15, Table 9-18 for XIN/XOUT).
  `BRINGUP=4` demos it on the LaunchPad (time on the glass + LED2 heartbeat),
  sleeping in LPM3 between ticks.
- **Task 5 (done, LaunchPad-testable):** `src/buttons.c` — P1.0/1.1/1.2 (MODE/
  HOUR/MIN), internal pull-ups, falling-edge IRQ that wakes from LPM3, 50 ms
  Timer_A0 debounce. `BRINGUP=5` shows the pressed button on the glass. Uses
  **LPM3** (see sleep-mode note above), not the bootstrap's LPM3.5. Open question:
  the V3 pads may be *capacitive touch* rather than short-to-ground buttons — if
  so this module needs rework; confirm on hardware.
- **Task 6 (done, LaunchPad-testable):** `src/clock_app.c` — display + time-setting
  state machine ported from V2 (5 s MODE long-press to enter; HOUR/MIN with
  hold-to-repeat; ~100 ms/700 ms digit flash; short MODE commits to the RTC).
  Display-abstracted via a callback; `BRINGUP=6` runs the whole clock on the
  LaunchPad. Timer_A1 provides the 20 ms UI tick, only running during an
  interaction. AM/PM isn't toggled while setting (mirrors V2) — flagged below.
- **Task 7 (low-power init done; measurement pending hardware):** `board_init()`
  now drives all pins output-low (SLAS865 §7.4) so unused pins don't leak; ACLK
  is on XT1 and the DCO/FLL/REFO are off in LPM3. The actual current measurement
  is an on-hardware step — see **Power profiling** below.

Open items for hardware bring-up: the caliper-LCD + colon/PM integration with the
RTC (Tasks 4–6 currently display on the LaunchPad glass); whether the buttons are
capacitive vs short-to-ground; and whether set-mode should toggle AM/PM.

## Power profiling (Task 7)

The goal is ~3 µA average for 1.5–3 yr on an LR44/SR44. This is an on-hardware
measurement; the firmware is already written to hit it (LPM3, XT1 ACLK, all
unused pins driven low). What's left is to confirm and chase any leaks.

**Targets** (datasheet §8.7, 3 V typ): LPM3 + LCD + charge pump ≈ **1.07 µA**;
LPM3 + RTC ≈ 1.08 µA. The always-on LCD charge pump dominates the budget.

**How to measure**
- Easiest: the LaunchPad's onboard **EnergyTrace** (CCS/UniFlash) — but note its
  floor (~hundreds of nA) and that LaunchPad LEDs/jumpers add current; pull the
  `LED`/`RXD`/`TXD` jumpers on J101.
- Most accurate: a µA meter (or DMM in µA range) in series with the coin cell on
  the V3 board, MCU sleeping in LPM3.

**Procedure**
1. Flash a steady-state build and let it idle in LPM3 (no buttons pressed).
2. Read the average current; expect ≈ 1–2 µA. The per-second RTC wake is too
   brief to matter to the average.
3. Press a button and confirm the wake spike is short (< ~10 ms) before it
   returns to LPM3.

**If it's over budget, check (in order)**
- Floating inputs: every unused pin should be output-low (`board_init()` does
  this; verify nothing re-floats a pin).
- DCO/FLL/REFO left running: they should be off in LPM3 (ACLK is XT1). REFO is
  only requested by the FLL during the brief active wakes.
- LCD charge-pump frequency: `LCDCPFSEL=0b1111` (slowest) is already the
  lowest-power setting; raising it costs current.
- SVS: excluded from the datasheet typicals above; disable if enabled and not
  needed.
- XT1 drive: after startup the drive strength can be reduced (CSCTL6 XT1DRIVE)
  for a small saving once oscillation is reliable.

## References

Datasheets live in `../Datasheets/` (msp430fr4133, slau445 family UG, slaa654
LCD_E app note, tps61099, ht1621b, slau595 LaunchPad UG). The V2 firmware with the
hard-won caliper LCD segment map is in `../../Caliper_Clock_V2/main.c`.
