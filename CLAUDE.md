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
6 full clock → 7 V3 final on the caliper LCD). 8–17 are caliper-LCD bring-up
diagnostics kept in tree (segment scan, COM-swap, isolation sweeps). See the
firmware README's "Bring-up sequence" table.

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
- **RTC: `RTCMOD = 31`** for 1 Hz (XT1/1024 = 32 Hz). The FR4133 RTC counter has
  the same `+1` period convention as Timer_A up mode (visits 0..RTCMOD then
  wraps), so period = RTCMOD+1 ticks. *Hardware-verified*: `RTCMOD=32` initially
  ran ~3% slow until corrected. SLAU445 §15.2.1's "reaches RTCMOD then resets"
  wording is misleading — trust the silicon, not the prose.
- **Sleep mode is LPM3, not LPM3.5** — the always-on LCD charge pump (~1 µA)
  dominates, so LPM3 (~1.1 µA typ, §8.7) meets budget and avoids LPM3.5's
  wake-via-reset complexity. Main loops use an atomic disable-test-`__bis_SR`
  pattern to avoid lost wakeups.
- **`LCDSSEL_x` is NOT what its name suggests** in standalone code. Per SLAU445
  Table 17-10: `LCDSSEL_0`=XT1CLK, `LCDSSEL_1`=ACLK, `LCDSSEL_2`=VLOCLK. Energia's
  `LCD_Launchpad.cpp` uses `LCDSSEL_0` because its core init starts XT1 first;
  in standalone bring-up code use `LCDSSEL_1` (ACLK, which defaults to REFO at
  reset and auto-starts when requested). General lesson: even "verbatim from a
  verified working source" can hide framework-implicit preconditions — verify
  *every* register field against the datasheet, not just the ones that *feel*
  uncertain.
- **V2→V3 caliper LCD translation (hardware-verified Jun 2026 via the
  BRINGUP=14 slow scan and the BRINGUP=16/17 isolation tests; same physical
  glass as V2, so V2's `DIGIT_SEG`, `SEG_COLON`, and `SEG_PM` are the source
  of truth):**
  - `V2 addr N → V3 L(N+8)` for N=1..10 (linear; L17 is **not** NC despite
    early guesses, it carries V2 addr 9 = D0 d/f/g; L8 carries an
    unaddressed stray "D4 g" the firmware doesn't write).
  - `LCDM0W = 0x1248`: L0→COM3, L1→COM2, L2→COM1, L3→COM0 — i.e. *both*
    L0↔L3 and L1↔L2 swapped vs. the default. Without the L1↔L2 swap, V2's
    com 1 and com 2 land on the wrong backplane.
  - V2's `SEG_COLON = (8, 3)` is the decimal dot positioned between D2 and
    D1 — exactly where a clock colon belongs (between hours and minutes);
    the L16 SEG line has three electrodes (D1 a at com 2, D0 e at com 1,
    the decimal at com 3) — one SEG line can drive multiple unrelated
    physical electrodes. `SEG_PM = (3, 3)` is the battery icon (lit when
    PM). A first-pass BRINGUP=14 scan misread (8, 3) as "Inch icon" and
    reported a phantom dot at (5, 3); the BRINGUP=16/17 isolation tests
    confirmed V2's addresses were correct.
- **LCD charge-pump frequency**: `LCDCPFSELx = 0` (all bits clear, *fastest*
  pump = max contrast) is set in `caliper_lcd_init`. The bootstrap originally
  used `LCDCPFSELx = 0xF` (slowest, lowest current) per the §8.7 ~1.07 µA typ;
  on this caliper glass that produced ON segments at threshold (visibly faint
  and OFF segments picking up partial drive). Raises LPM3+LCD current above
  §8.7 typ — first power-profiling task is to dial the pump frequency *up*
  (more bits set) incrementally until contrast becomes unacceptable.
- **LaunchPad gotcha**: jumper **JP1 (LED1)** must be **pulled** when testing
  buttons. JP1 puts the green LED in series with P1.0 to GND, pulling P1.0 to
  ~1.8 V and breaking the MODE button input. (V3 board doesn't have this issue.)
- **Open hardware questions**: should set-mode toggle AM/PM (it doesn't,
  mirroring V2). V3 buttons confirmed as short-to-ground GPIO (not capacitive).

## Working norms
- **Verify against the datasheets, don't guess** — registers/pins/timing live in
  `Caliper_Clock_V3/Datasheets/`. Read PDFs with `pdftotext -layout <pdf> -`
  (poppler is installed) and cite the section in code comments.
- Build with `-Wall -Wextra`; keep it warning-clean.
- Environment: macOS Apple Silicon; git is **2.28** (old; `merge.conflictstyle`
  set to `diff3`, not `zdiff3`). Don't push to `master` directly — work branches
  (current work is on `v3-firmware`).
