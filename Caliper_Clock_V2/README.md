# Caliper Clock

A low-power digital clock built using a salvaged LCD display from a digital caliper, driven by an ATtiny1616 microcontroller with an RV-3028 real-time clock.

![Caliper Clock](https://img.shields.io/badge/MCU-ATtiny1616-blue) ![License](https://img.shields.io/badge/license-MIT-green)

## Features

- **Ultra-low power consumption** — ATtiny1616 sleeps in power-down mode, waking only on RTC minute interrupts
- **12-hour time display** with AM/PM indicator and blinking colon
- **Button-based time setting** — long-press to enter setting mode, adjust hours/minutes with dedicated buttons
- **Accurate timekeeping** — RV-3028 RTC with minute-update interrupt
- **Repurposed LCD** — uses an HT1621B-compatible LCD from a digital caliper (4-digit 7-segment display)

## Hardware

### Components

| Component | Description |
|-----------|-------------|
| ATtiny1616 | 8-bit AVR microcontroller, 20 MHz, 20 pins, supports sleep modes, GPIO, interrupts |
| HT1621B | 32-nibble LCD driver, 4-com 1/3-bias, SPI/parallel interface |
| RV-3028 | I2C RTC, 24-hour mode, minute alarm output, ultra-low-power |
| AP3015AKTR-G1 | Boost Switching Regulator IC |

### Pin Mapping

#### LCD (HT1621B)
| Signal | Pin |
|--------|-----|
| DATA   | PA1 |
| WR     | PA7 |
| /CS    | PB3 |

#### RTC (RV-3028 via Soft I²C)
| Signal | Pin |
|--------|-----|
| SDA    | PA2 |
| SCL    | PA3 |
| INT    | PA4 |

#### Buttons
| Function | Pin |
|----------|-----|
| Mode     | PB0 |
| Hour+    | PB1 |
| Minute+  | PB2 |

#### Debug
| Function | Pin |
|----------|-----|
| LED      | PA6 |

## PCB Designs

The repository is organized into one folder per board revision:

- **Caliper_Clock_V1/** — Original board (includes the `Caliper_Clock_Test_Board/` test/development board)
- **Caliper_Clock_V2/** — Revised board design (also contains this firmware and tooling)
- **Caliper_Clock_V3/** — MSP430FR4133-based redesign (includes the `MSP430FR4133IPM/` symbol library)

Each folder contains:
- KiCad schematic (`.kicad_sch`)
- KiCad PCB layout (`.kicad_pcb`)
- Gerber files for manufacturing
- BOM and pick-and-place files

## Building the Firmware

### Prerequisites

- AVR-GCC toolchain (avr-gcc 14 recommended)
- avr-objcopy

On macOS with Homebrew:
```bash
brew install avr-gcc@14
```

### Compile

```bash
make clean
make
```

This produces `blink.hex` ready for flashing.

### Flash

Use a UPDI programmer (e.g., SerialUPDI, Atmel-ICE, or jtag2updi):

```bash
# Example with pymcuprog
pymcuprog write -t uart -u /dev/tty.usbserial-XXXX -d attiny1616 -f blink.hex
```

## Usage

### Normal Operation

- The clock displays time in 12-hour format with AM/PM indicator
- Colon blinks at ~1 Hz
- MCU sleeps between updates for minimal power consumption

### Setting the Time

1. **Long-press the Mode button (PB0)** for ~5 seconds to enter time-setting mode
2. Display will flash to indicate setting mode
3. **Press Hour button (PB1)** to increment hours (1-12)
4. **Press Minute button (PB2)** to increment minutes (0-59)
5. **Press Mode button (PB0)** again to save and exit

Hold Hour or Minute buttons for auto-repeat.

## Firmware Variants

The repository includes several firmware versions:

| File | Description |
|------|-------------|
| `main.c` | Current version with low-power mode and button time-setting |
| `main-CLOCK-V1.c` | Earlier clock implementation |
| `main-BASIC-CLOCK.c` | Basic clock without power optimization |
| `main-RTC-test.c` | RTC testing/debugging |
| `main-lcd_count.c` | LCD counting test |
| `main-lcd_walk.c` | LCD segment walking test |
| `main-blink_WR_CS_LED.c` | GPIO/LED blink test |

## Python Utilities

- **lcd_driver.py** — ESP32 MicroPython LCD driver (for prototyping)
- **lcd_screen_decoding.py** — LCD segment mapping analysis tool

## License

MIT License — feel free to use, modify, and share!

