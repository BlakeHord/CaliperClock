# Caliper Clock V3 firmware -- MSP430FR4133
# Toolchain: MSP430-GCC (MacPorts) + mspdebug(tilib) for flashing.
# See README.md for one-time install/setup.

# ---- Target / toolchain -----------------------------------------------------
MCU       = msp430fr4133
CC        = msp430-elf-gcc
OBJCOPY   = msp430-elf-objcopy
SIZE      = msp430-elf-size

# Device headers (msp430fr4133.h) come from the msp430-gcc-support-files package.
# Verified location on this MacPorts install: /opt/local/msp430-elf/include
# (the matching linker script lives in .../lib and is auto-found via -mmcu).
# If you relocate the toolchain, rediscover with:
#   port contents msp430-gcc-support-files | grep -E 'msp430fr4133\.(h|ld)'
SUPPORT_DIR ?= /opt/local/msp430-elf/include

# mspdebug needs TI's libmsp430.dylib (the MacPorts custom libmsp430 port),
# which lives in /opt/local/lib. tilib won't load it without this on macOS.
MSPDEBUG_ENV = DYLD_LIBRARY_PATH=/opt/local/lib

# ---- Version ----------------------------------------------------------------
# The product firmware. Bump this when cutting a new release.
FW_VERSION = 1.0

# Which build to compile (see src/main.c / README "Bring-up sequence").
#
# DEFAULT = the v1.0 product: BRINGUP=7, the full clock on the caliper glass
# (timekeeping + display + buttons). Plain `make` / `make flash` builds this.
#
# The numbered bring-up/diagnostic builds are kept in-tree and selectable with
# an override, e.g.  make BRINGUP=3 flash   (run `make clean` when changing it):
#   1=blink, 2=LCD HELLO (LaunchPad), 3=caliper scan, 4=RTC, 5=buttons,
#   6=full clock (LaunchPad), 7=V3 PRODUCT (full clock on caliper glass),
#   8..13=caliper LCD diagnostics, 14=slow per-(Lxx,COMy) scan (~4 s each),
#   15=static 1200 w/ SW colon toggle, 16/17=segment isolation sweeps.
BRINGUP ?= 7

# ---- Layout -----------------------------------------------------------------
SRC_DIR   = src
INC_DIR   = inc
BUILD_DIR = build
BIN_DIR   = bin
TARGET    = firmware

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))
ELF  = $(BIN_DIR)/$(TARGET).elf

# ---- Flags ------------------------------------------------------------------
# -Os: optimize for size (FRAM is precious). -g: keep debug symbols.
# -ffunction-sections/-fdata-sections + --gc-sections: drop unused code/data.
# -MMD -MP: emit .d header-dependency files so edits to headers trigger rebuilds.
CFLAGS  = -mmcu=$(MCU) -Os -g -std=gnu11 -Wall -Wextra -DBRINGUP=$(BRINGUP) \
          -DFW_VERSION=\"$(FW_VERSION)\" \
          -ffunction-sections -fdata-sections -MMD -MP -I$(INC_DIR)
LDFLAGS = -mmcu=$(MCU) -Wl,--gc-sections

ifneq ($(strip $(SUPPORT_DIR)),)
CFLAGS  += -I$(SUPPORT_DIR)
LDFLAGS += -L$(SUPPORT_DIR)
endif

# ---- Targets ----------------------------------------------------------------
.PHONY: all flash debug clean size version

# Print what this invocation will build (product vs. a diagnostic).
version:
ifeq ($(BRINGUP),7)
	@echo "Caliper Clock V3 firmware v$(FW_VERSION)  (product: BRINGUP=7)"
else
	@echo "Caliper Clock V3 firmware v$(FW_VERSION)  (DIAGNOSTIC build BRINGUP=$(BRINGUP), not the product)"
endif

all: version $(ELF) size

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(ELF): $(OBJS) | $(BIN_DIR)
	$(CC) $(LDFLAGS) $^ -o $@

$(BUILD_DIR) $(BIN_DIR):
	mkdir -p $@

size: $(ELF)
	$(SIZE) $(ELF)

# Flash the LaunchPad over Spy-Bi-Wire via the onboard eZ-FET.
flash: $(ELF)
	$(MSPDEBUG_ENV) mspdebug tilib "prog $(ELF)"

# Interactive mspdebug session (e.g. `mw`, `md`, `run`, `reset`).
debug: $(ELF)
	$(MSPDEBUG_ENV) mspdebug tilib

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)

# Pull in auto-generated header dependencies (build/*.d), if present.
-include $(OBJS:.o=.d)
