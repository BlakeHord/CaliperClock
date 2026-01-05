MCU=attiny1616
F_CPU=3333333UL

# Mac (Homebrew):
CC=/opt/homebrew/opt/avr-gcc@14/bin/avr-gcc

# Windows (MSYS2/WSL) - just use avr-gcc in PATH:
# CC=avr-gcc

OBJCOPY=avr-objcopy
CFLAGS=-mmcu=$(MCU) -Os -DF_CPU=$(F_CPU) -std=gnu11 -Wall
LDFLAGS=-mmcu=$(MCU)

all: blink.hex

blink.elf: main.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

blink.hex: blink.elf
	$(OBJCOPY) -O ihex -R .eeprom $< $@

clean:
	rm -f blink.elf blink.hex
