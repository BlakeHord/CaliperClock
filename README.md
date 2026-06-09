# Caliper Clock

A low-power digital clock that uses the LCD salvaged from a cheap digital
caliper. The custom board drops into the caliper body, drives its 4-mux glass
directly, and is designed to operate for >1 year off a single 1.5V LR44 button cell.

Three major hardware revisions live in this repo.

## Revisions

| Dir | Board | Status |
|-----|-------|--------|
| [`Caliper_Clock_V1/`](Caliper_Clock_V1/) | ATtiny1616 + HT1621B LCD driver + RV-3028 RTC (incl. a test board) | original design |
| [`Caliper_Clock_V2/`](Caliper_Clock_V2/) | revised V1-class board **+ the working V2 firmware** | source of truth for the caliper LCD segment map & set-time UX. Battery depleted in days due to constant boost switching (wrong component choice) |
| [`Caliper_Clock_V3/`](Caliper_Clock_V3/) | **active redesign** around an **MSP430FR4133** (FRAM, integrated LCD_E + RTC, no HT1621) | hardware-verified, firmware **v1.0** running on the custom board |

**Active work is V3.** Its firmware ports the V2 segment map and time-setting
state machine onto the MSP430, displaying time on the real caliper glass via the
chip's built-in LCD controller and keeping time with the integrated RTC.

## V3 at a glance

- **MCU:** MSP430FR4133 (LCD_E drives the 4-mux caliper glass directly).
- **Power:** boosts a 1.5 V LR44 cell to 3.0 V (TPS61099); sleeps in LPM3 with
  the always-on LCD, targeting ~µA average for >year battery life.
- **Firmware:** `Caliper_Clock_V3/firmware/` — MSP430-GCC + `mspdebug`. The
  default build is the v1.0 product (full clock + buttons on the caliper glass);
  numbered diagnostic builds remain for bring-up. See its
  [README](Caliper_Clock_V3/firmware/README.md).
- **Hardware:** KiCad design, datasheets, and gerbers under `Caliper_Clock_V3/`.

```sh
cd Caliper_Clock_V3/firmware
make            # build v1.0 (the product)
make flash      # build + flash via the LaunchPad's eZ-FET (Spy-Bi-Wire)
```
