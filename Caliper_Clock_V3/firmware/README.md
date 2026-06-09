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
make                 # builds the v1.0 PRODUCT (BRINGUP=7) -> bin/firmware.elf
make BRINGUP=1       # build a specific diagnostic instead (see below)
make clean           # removes build/ and bin/
```

The **default build is the product**: `BRINGUP=7`, the full clock on the caliper
glass (timekeeping + display + buttons), versioned `FW_VERSION` in the Makefile
(currently **v1.0**). The build prints a banner saying whether it produced the
product or a diagnostic.

`src/main.c` still selects the build at compile time via `BRINGUP`; the numbered
bring-up/diagnostic builds are kept in-tree and reachable with an override (e.g.
`make BRINGUP=3`). The object files don't track that flag, so **`make clean` when
switching `BRINGUP`**.

## Flash & debug (LaunchPad connected via USB)

```sh
make flash               # build + flash the v1.0 PRODUCT (BRINGUP=7)
make BRINGUP=1 flash     # build+flash a specific diagnostic instead
make debug               # interactive mspdebug session
```

The Makefile prefixes mspdebug with `DYLD_LIBRARY_PATH=/opt/local/lib` so `tilib`
can find `libmsp430.dylib`. If flashing fails to find the FET, unplug/replug the
LaunchPad and retry (a known macOS eZ-FET quirk).

## Bring-up sequence

Each step has a flashable test and a concrete "you should see X" so a failure
isolates to the layer just added — instead of debugging a whole clock at once.
Steps 1–6 verify on the LaunchPad; **step 7 is the v1.0 product** — the full
clock on the real caliper glass — and is now the default `make` build (it
requires a V3 board). The ladder is kept for diagnosing a regression to the
layer that introduced it.

| # | Build | What it proves | Pass criterion | Status |
|---|-------|----------------|----------------|--------|
| 1 | `make BRINGUP=1 flash` | Toolchain + flash path + chip alive | LaunchPad LED2 (P4.0) blinks ~1 Hz | ✓ |
| 2 | `make BRINGUP=2 flash` | LCD_E peripheral: 4-mux, charge-pump bias, ACLK, pin mux | LaunchPad glass shows **HELLO** | ✓ |
| 3 | `make BRINGUP=3 flash` (V3) | The V3→caliper SEG/COM map matches the physical glass | Scan test lights each (Lxx,COM) | ✓ (superseded by BRINGUP=14 slow scan) |
| 4 | `make BRINGUP=4 flash` | XT1 32.768 kHz + RTC 1 Hz tick | Time advances; LED2 toggles every 1 s | ✓ |
| 5 | `make BRINGUP=5 flash` | P1.0–1.2 wake from LPM3; 50 ms debounce | Button-to-GND shows MODE/HOUR/MIN | ✓ |
| 6 | `make BRINGUP=6 flash` | Full clock + set-time UI on LaunchPad | Time advances; long-press MODE → set; HOUR/MIN adjust; MODE commits | ✓ |
| 7 | `make flash` (default; V3) | **v1.0 product:** full clock on the caliper glass | 12:HH with blinking colon, battery icon as PM, buttons set time | ✓ verified on the custom V3 board (display + RTC + buttons) |
| — | power profiling | Meets the ~µA budget | LPM3 + LCD ≈ 1–2 µA on a meter / EnergyTrace | pending |

### Diagnostics (BRINGUP=8..17)

Kept in tree for re-running if the glass or PCB ever changes. These resolved the
V3 caliper LCD wiring empirically:

| Build | Purpose |
|-------|---------|
| `BRINGUP=8`  | Static "8888" on the caliper LCD (full digit + colon + PM lit) |
| `BRINGUP=9`  | Raw all-LCDM-bits-on (bypasses caliper helpers — probes hardware) |
| `BRINGUP=10..13` | Incremental: enable extra LCDPCTL bits, Mode 2 + VLCD_6, COM swap |
| `BRINGUP=14` | Slow per-(Lxx, COMy) scan, ~4 s/step — records empirical V3 segment map |
| `BRINGUP=15` | Static "1200" with colon bit toggled in SW every 3 s — isolates SEG_COLON path |
| `BRINGUP=16` | Light only the SEG_COLON candidate, alternating — confirms which physical segment a single bit drives |
| `BRINGUP=17` | Sweep V2 `(addr, com=3)` for addr=1..10, ~5 s each — locates icons/dots on the com 3 backplane |

Notes:
- **Step 2 caveat:** the FH-1138P glass on the LaunchPad has a *different* pinout
  from the caliper LCD. Step 2 only validates LCD_E itself; the caliper-specific
  segment map is Step 3.
- **Clock source:** Steps 1–2 run on the default post-reset ACLK (~32 kHz REFO).
  Step 4 switches ACLK to the 32.768 kHz **XT1** crystal for accuracy.
- **Sleep mode = LPM3, not LPM3.5.** Per §8.7, LPM3 with the LCD and RTC is
  ~1.1 µA typ; the always-on LCD charge pump dominates the budget, so LPM3.5
  would save only ~0.2 µA at the cost of waking via reset.
- **LaunchPad gotcha:** **pull jumper JP1 (LED1)** for button tests. JP1 puts
  the green LED in series with P1.0 to GND, pulling P1.0 to ~1.8 V and breaking
  the MODE button.

## Status (current)

All seven tasks are implemented and verified on hardware (V3 board for caliper
LCD, buttons, RTC; LaunchPad for LCD_E peripheral check). Outstanding:

- **Task 7 power profiling** — code is at low-power defaults; current draw not
  yet measured. The charge-pump frequency was bumped to *fastest* (`LCDCPFSELx
  = 0`) to fix faint contrast on the caliper glass; this raises LPM3+LCD above
  the §8.7 ~1.07 µA typ. See "Power profiling" below.
- **AM/PM in set-mode** — currently not toggled while setting (mirrors V2).
  Open product question, not a bug.
- **Display refresh artifact** — `caliper_lcd_show_4digit` does `clear()` then
  re-writes the digit bits; byte 6 briefly transitions through 0 each tick,
  visible as a ~1 Hz "glitch" of the whole display. Cosmetic; mitigated by
  computing the new byte values and writing them in one assignment instead of
  clear-then-OR.
- **"in" icon ties to the decimal** — at L16 com 3 the LCD glass shares a trace
  between the decimal dot and the "in" indicator, so they blink together. Not
  separable in software (hardware-tied). Accepted.

### V3 caliper LCD wiring (resolved Jun 2026)

Same physical glass as V2, so V2's `DIGIT_SEG`, `SEG_COLON = (8, 3)`, and
`SEG_PM = (3, 3)` all carry over. The V2→V3 translation needed two fixes
documented in `caliper_lcd.c` and `CLAUDE.md`:

- `HT1621_ADDR_TO_LCDE_SEG`: V2 addr N → V3 L(N+8) for N=1..10 (was earlier
  guessed as L(N+7) with addr 10→L18; L17 is **not** NC, it carries V2 addr 9).
- `LCDM0W = 0x1248`: L0↔L3 *and* L1↔L2 swapped vs. default — V2 com 1 and com 2
  both need swapping on this board, not just com 3 via L0↔L3.

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
- **LCD charge-pump frequency:** currently `LCDCPFSELx = 0` (fastest, *max*
  contrast and *max* current). Original bootstrap target was `LCDCPFSELx = 0xF`
  (slowest) for §8.7 ~1.07 µA, but that put V3's caliper glass at threshold.
  Sweep upward incrementally (1, 3, 7, …, 0xF) until contrast just degrades,
  then back off one step — that's the right tradeoff.
- Floating inputs: every unused pin should be output-low (`board_init()` does
  this; verify nothing re-floats a pin).
- DCO/FLL/REFO left running: should be off in LPM3 (ACLK is XT1). REFO is only
  requested by the FLL during brief active wakes.
- SVS: excluded from the datasheet typicals above; disable if enabled and not
  needed.
- XT1 drive: after startup the drive strength can be reduced (CSCTL6 XT1DRIVE)
  once oscillation is reliable.

## Next steps

In rough priority order:

1. **Timekeeping accuracy on the V3 board.** v1.0 (`make flash`) runs on the
   custom board with display + buttons verified, but the V3 board has its own
   FC-135 XT1 crystal (RTCMOD=31 was tuned on the LaunchPad). **Action:** let it
   run several hours against a reference clock; if it drifts, re-check XT1 load
   caps and RTCMOD.
2. **Power measurement (Task 7).** µA meter in series with VDD on the V3 board,
   MCU sleeping in LPM3. Record baseline, then sweep `LCDCPFSELx` from 0 toward
   0xF and find the lowest setting that keeps segments legible — that's the
   power/contrast operating point.
3. **Display refresh artifact.** The ~1 Hz "glitch" comes from
   `caliper_lcd_show_4digit` clearing bytes 4–9 then re-writing. Replace with a
   compute-locally-then-store-once pattern so segment bytes change in a single
   write per byte, no visible blank step.
4. **AM/PM in set-mode** (product decision). Add a fourth set-mode state that
   toggles PM, or accept V2's behaviour of "set the digits, AM/PM derives from
   24 h rollover."
5. **LPM3.5 (only if power budget fails).** Requires FRAM persistence of the
   clock state + full re-init on every wake. Saves ~0.2 µA over LPM3 with the
   LCD charge pump dominating, so not worth the complexity unless measurement
   forces it.

## References

Datasheets live in `../Datasheets/` (msp430fr4133, slau445 family UG, slaa654
LCD_E app note, tps61099, ht1621b, slau595 LaunchPad UG). The V2 firmware with the
hard-won caliper LCD segment map is in `../../Caliper_Clock_V2/main.c`.
