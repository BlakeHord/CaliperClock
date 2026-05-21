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
just added — instead of debugging a whole clock at once. Steps 1–2 exist today;
3–7 are the planned checkpoints (this list is the roadmap for when boards arrive).

Run each on the **LaunchPad** first; once the custom V3 boards arrive, repeat the
hardware-dependent ones (LCD, buttons, power) on the real board.

| # | Build | What it proves | Pass criterion |
|---|-------|----------------|----------------|
| 1 | `make BRINGUP=1 flash` | Toolchain + flash path + the chip is alive | LaunchPad LED2 (P4.0) blinks ~1 Hz |
| 2 | `make BRINGUP=2 flash` | LCD_E peripheral: 4-mux, charge-pump bias, ACLK, pin mux | LaunchPad glass shows **HELLO** (steady, readable contrast) |
| 3 | *(planned)* segment-scan + caliper map | The V3→caliper SEG/COM map matches the physical glass | Each segment lights where expected; digits render correctly |
| 4 | *(planned)* RTC on XT1 | 32.768 kHz crystal + RTC keep time; ACLK moved off REFO | Colon blinks 1 Hz; minutes advance |
| 5 | *(planned)* buttons + LPM3.5 | P1.0–1.2 wake from deep sleep; debounce | Button press wakes and registers; idles in LPM3.5 |
| 6 | *(planned)* set-time UI | Long-press MODE, hour/min adjust, commit | Time can be set and is retained |
| 7 | *(planned)* power profiling | Meets the ~µA budget | LPM3.5+LCD < 2 µA on a meter |

Notes:
- **Step 2 caveat:** the FH-1138P glass on the LaunchPad has a *different* pinout
  from the caliper LCD. Step 2 only validates that LCD_E itself works; the
  caliper-specific segment map is Step 3.
- **Clock source:** Steps 1–2 run on the default post-reset ACLK (~32 kHz REFO),
  matching TI's out-of-box demo. Step 4 switches ACLK to the 32.768 kHz **XT1**
  crystal for timekeeping accuracy and lower power.

## Current status

- **Task 1 (done):** toolchain + skeleton verified; LED2 blink stub builds clean.
- **Task 2 (done, untested on HW):** `src/hal_lcd.c` drives the LaunchPad
  FH-1138P glass via LCD_E and displays "HELLO". Builds clean; segment tables and
  register sequence ported from verified TI/Energia sources (see file headers).
  Needs a LaunchPad to confirm visually.

Remaining: Task 3 (caliper segment map + scan test), Task 4 (RTC), Task 5
(buttons + LPM3.5), Task 6 (set-time UI), Task 7 (power profiling).

## References

Datasheets live in `../Datasheets/` (msp430fr4133, slau445 family UG, slaa654
LCD_E app note, tps61099, ht1621b). The V2 firmware with the hard-won caliper LCD
segment map is in `../../Caliper_Clock_V2/main.c`.
