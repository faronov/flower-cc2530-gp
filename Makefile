# SPDX-License-Identifier: BSD-3-Clause
# FlowerX Green Power Device firmware: a standalone CC2530 image (no Zigbee
# stack) built with LLVM MCS-51; clock and Sleep Timer helpers from the
# cc2530-zigbee submodule.
.DEFAULT_GOAL := test
.DELETE_ON_ERROR:
STACK := cc2530-zigbee
PYTHON ?= python3
LLVM_TOOLS ?= ../llvm-mcs51-build/bin
LLVM_SRC ?= ../llvm-project
OUT ?= build/firmware
# Extra -D overrides for src/gpd_main.c, e.g. GPD_DEFINES=-DGPD_CHANNEL=20
GPD_DEFINES ?=

RT := $(LLVM_SRC)/llvm/lib/Target/MCS51
TARGET := -target mcs51 -mcpu=cc2530
CFLAGS_FW := $(TARGET) -std=c99 -Os -Wall -ffunction-sections -fdata-sections \
	-I$(STACK)/toolchain/clang/include -Iinclude -I$(STACK)/include \
	-DCC2530_BOARD=2 -DCC2530_APP $(GPD_DEFINES)
SOURCES := src/gpd_main.c src/flower.c src/sensors.c src/settings.c $(STACK)/src/clock.c $(STACK)/src/timebase.c
OBJECTS := $(patsubst %.c,$(OUT)/obj/%.o,$(notdir $(SOURCES)))
HEADERS := $(wildcard include/*.h)

.PHONY: test test-quirk firmware clean

$(STACK)/include/clock.h:
	git submodule update --init $(STACK)

# Green Power frame builder (include/gpd_frame.h), ASan/UBSan.
test:
	mkdir -p build/host
	$(CC) -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all \
		-Iinclude tests/test_gpd_frame.c -o build/host/test-gpd-frame
	build/host/test-gpd-frame

# ZHA quirk against zha and zha-quirks (pip install zha zha-quirks pytest).
test-quirk:
	cd quirks && $(PYTHON) -m pytest -q test_zha_quirk_diyruz_flowerx_gp.py

# Intel HEX in $(OUT)/flower-gp.hex.
firmware: $(OUT)/flower-gp.hex

$(OUT)/rt/mcs51/libmcs51builtins.a: $(RT)/mcs51-runtime.c
	mkdir -p $(@D)
	$(LLVM_TOOLS)/clang $(TARGET) -O2 -ffreestanding -fno-builtin -ffunction-sections -fdata-sections \
		-c $< -o $(@D)/mcs51-runtime.o
	rm -f $@ && $(LLVM_TOOLS)/llvm-ar rcs $@ $(@D)/mcs51-runtime.o

# The upstream startup sets SP to 0x7f (stack 0x80-0xff). This image has no
# DATA/IDATA globals, so the stack starts right after the IM registers
# (0x30-0x47) instead: 184 bytes. cc2530-root.ld keeps DATA globals below it.
STACK_SP := 0x47
$(OUT)/rt/mcs51/cc2530_startup.o: $(RT)/cc2530_startup.s
	mkdir -p $(@D)
	grep -q '^  mov sp, #127$$' $<
	sed 's/^  mov sp, #127$$/  mov sp, #$(STACK_SP)/' $< > $(@D)/cc2530_startup.s
	$(LLVM_TOOLS)/clang $(TARGET) -c $(@D)/cc2530_startup.s -o $@

$(OUT)/obj/%.o: src/%.c $(HEADERS) | $(STACK)/include/clock.h
	mkdir -p $(@D)
	$(LLVM_TOOLS)/clang $(CFLAGS_FW) -c $< -o $@

$(OUT)/obj/%.o: $(STACK)/src/%.c | $(STACK)/include/clock.h
	mkdir -p $(@D)
	$(LLVM_TOOLS)/clang $(CFLAGS_FW) -c $< -o $@

$(OUT)/flower-gp.elf: cc2530-root.ld $(OBJECTS) $(OUT)/rt/mcs51/cc2530_startup.o $(OUT)/rt/mcs51/libmcs51builtins.a
	$(LLVM_TOOLS)/clang $(TARGET) -Os -nostartfiles $(OUT)/rt/mcs51/cc2530_startup.o \
		-resource-dir $(OUT)/rt --ld-path=$(LLVM_TOOLS)/ld.lld -Wl,--gc-sections -Wl,--no-check-sections \
		-Wl,-T,cc2530-root.ld -Wl,-Map,$(OUT)/flower-gp.map $(OBJECTS) \
		$(OUT)/rt/mcs51/libmcs51builtins.a -o $@

$(OUT)/flower-gp.hex: $(OUT)/flower-gp.elf
	$(LLVM_TOOLS)/llvm-objcopy --output-target=ihex $< $@
	$(PYTHON) tools/size.py $@
	$(LLVM_TOOLS)/llvm-objdump -d --no-show-raw-insn $< > $(OUT)/flower-gp.dis
	$(PYTHON) tools/stack.py $(OUT)/flower-gp.dis $(STACK_SP) gpd_sleep_isr gpd_button_isr

clean:
	rm -rf build
