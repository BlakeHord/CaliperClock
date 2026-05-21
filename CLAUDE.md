# CLAUDE.md — Caliper Clock

A low-power digital clock that repurposes the LCD salvaged from a digital
caliper. Three hardware revisions live side by side in this repo.

## Repo layout

- **`Caliper_Clock_V1/`** — original board (ATtiny1616 + HT1621B LCD driver +
  RV-3028 RTC). Contains the `Caliper_Clock_Test_Board/` dev board.
- **`Caliper_Clock_V2/`** — revised V1-class board **and the working V2
  firmware** (`main.c`, ATtiny + HT1621). This `main.c` is the **source of truth
  for the caliper LCD segment map** (`DIGIT_SEG`, `DIGIT_PATTERN`, colon/PM) and
  the time-setting UX — all later work ports from it.
- **`Caliper_Clock_V3/`** — the active redesign around an **MSP430FR4133**
  (FRAM, integrated LCD_E + RTC, runs the same caliper glass directly, no HT1621).
  - `firmware/` — the V3 firmware (see its own `README.md` for full detail).
  - `Datasheets/` — msp430fr4133, slau445 (FR4xx family UG), slaa654 (LCD_E),
    tps61099, ht1621b, slau595 (LaunchPad UG).
  - `*.kicad_*`, `Gerbers1/`, `MSP430FR4133IPM/` — the KiCad design.

**Active work is the V3 firmware.** It is developed/verified on an
MSP-EXP430FR4133 LaunchPad while the custom boards are at the fab.

## V3 firmware — build & flash

Toolchain: **MSP430-GCC (MacPorts `msp430-elf-gcc`)** + **`mspdebug` `tilib`**.
PATH includes `/opt/local/bin`. From `Caliper_Clock_V3/firmware/`:

```sh
make                 # build the default bring-up test
make BRINGUP=N flash # build+flash test N (1..6); run `make clean` when changing N
make clean
```

`src/main.c` has a compile-time **`BRINGUP` selector** so each layer is a
separate flashable test (1 blink → 2 LCD → 3 caliper scan → 4 RTC → 5 buttons →
6 full clock). See the firmware README's "Bring-up sequence" table.

### Firmware modules (`src/`, `inc/`)
- `hal_lcd` — LaunchPad FH-1138P glass via LCD_E (Task 2 validation only).
- `caliper_lcd` — the real caliper glass via LCD_E, Mode 2 bias (V3 board only).
- `rtc_clock` — XT1 32.768 kHz → ACLK, RTC counter @ 1 Hz, timekeeping.
- `buttons` — P1.0/1.1/1.2 (MODE/HOUR/MIN), debounced, wake from LPM3.
- `clock_app` — display + time-setting state machine (ties it together).
- `main` — board init + the BRINGUP test mains.

## Key facts & gotchas (verify before changing)
- **`char` is unsigned** and **`int` is 16-bit** on msp430-gcc. Watch shifts:
  `(0xA0 << 8)` overflows a signed 16-bit int (UB) — use unsigned. LCD segment
  packing already accounts for this.
- **LCD memory** must be addressed via integer arithmetic from `&LCDM0`
  (`lcd_byte()` in caliper_lcd.c) to avoid a `-Warray-bounds` false positive.
- **RTC: `RTCMOD = 32`** for 1 Hz (XT1/1024 = 32 Hz). The counter overflows when
  it *reaches* RTCMOD → period = RTCMOD ticks, NOT +1 (SLAU445 §15.2.1). Confirm
  on hardware by timing the BRINGUP=4 LED heartbeat.
- **Sleep mode is LPM3, not LPM3.5** — the always-on LCD charge pump (~1 µA)
  dominates, so LPM3 (~1.1 µA typ, §8.7) meets budget and avoids LPM3.5's
  wake-via-reset complexity. Main loops use an atomic disable-test-`__bis_SR`
  pattern to avoid lost wakeups.
- **The V2→V3 segment-line map is UNVERIFIED**: isolated in
  `HT1621_ADDR_TO_LCDE_SEG[]` (caliper_lcd.c). Resolve it on hardware with the
  BRINGUP=3 segment-scan test, then correct that one table.
- **Open hardware questions**: are the V3 buttons capacitive-touch or
  short-to-ground (current code assumes the latter)? Should set-mode toggle
  AM/PM (it doesn't, mirroring V2)?

## Working norms
- **Verify against the datasheets, don't guess** — registers/pins/timing live in
  `Caliper_Clock_V3/Datasheets/`. Read PDFs with `pdftotext -layout <pdf> -`
  (poppler is installed) and cite the section in code comments.
- Build with `-Wall -Wextra`; keep it warning-clean. **No hardware yet**, so all
  firmware is compile- and datasheet-verified only — say so plainly.
- Environment: macOS Apple Silicon; git is **2.28** (old; `merge.conflictstyle`
  set to `diff3`, not `zdiff3`). Don't push to `master` directly — work branches
  (current work is on `v3-firmware`).
