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
make            # compiles src/*.c -> bin/firmware.elf, prints size
make clean      # removes build/ and bin/
```

## Flash & debug (LaunchPad connected via USB)

```sh
make flash      # mspdebug tilib "prog bin/firmware.elf"
make debug      # interactive mspdebug session
```

The Makefile prefixes mspdebug with `DYLD_LIBRARY_PATH=/opt/local/lib` so `tilib`
can find `libmsp430.dylib`. If flashing fails to find the FET, unplug/replug the
LaunchPad and retry (a known macOS eZ-FET quirk).

## Current status

- **Task 1 (in progress):** toolchain + skeleton. `src/main.c` blinks LaunchPad
  LED2 (P4.0) at ~1 Hz to prove build+flash works end to end.

Remaining tasks (LCD_E "hello", caliper segment map + scan test, RTC, buttons +
LPM3.5, time-setting UI, power profiling) build on this skeleton.

## References

Datasheets live in `../Datasheets/` (msp430fr4133, slau445 family UG, slaa654
LCD_E app note, tps61099, ht1621b). The V2 firmware with the hard-won caliper LCD
segment map is in `../../Caliper_Clock_V2/main.c`.
