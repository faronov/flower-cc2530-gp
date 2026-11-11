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
# flower (FlowerX, flower-gp) or bgt1 (EfektaLab BGT1, bgt1-gp).
BOARD ?= flower
ifeq ($(BOARD),flower)
IMAGE := flower-gp
BOARD_SOURCE := src/flower.c
BOARD_DEFINES :=
OUT ?= build/firmware
else ifeq ($(BOARD),bgt1)
IMAGE := bgt1-gp
BOARD_SOURCE := src/bgt1.c
BOARD_DEFINES := -DBOARD_BGT1
OUT ?= build/bgt1
else
$(error BOARD must be flower or bgt1)
endif
# Extra -D overrides for src/gpd_main.c, e.g. GPD_DEFINES=-DGPD_CHANNEL=20
GPD_DEFINES ?=

RT := $(LLVM_SRC)/llvm/lib/Target/MCS51
TARGET := -target mcs51 -mcpu=cc2530
CFLAGS_FW := $(TARGET) -std=c99 -Os -Wall -ffunction-sections -fdata-sections \
	-I$(STACK)/toolchain/clang/include -Iinclude -I$(STACK)/include \
	-DCC2530_BOARD=2 -DCC2530_APP $(BOARD_DEFINES) $(GPD_DEFINES)
# aes.c: the lean hardware AES block (GP security; dropped by --gc-sections
# with GPD_DEFINES=-DGPD_SECURITY=0).
SOURCES := src/gpd_main.c $(BOARD_SOURCE) src/sensors.c src/settings.c $(STACK)/src/clock.c $(STACK)/src/timebase.c \
	$(STACK)/src/aes.c
OBJECTS := $(patsubst %.c,$(OUT)/obj/%.o,$(notdir $(SOURCES)))
HEADERS := $(wildcard include/*.h)

.PHONY: test test-quirk firmware clean

$(STACK)/include/clock.h:
	git submodule update --init $(STACK)

# Green Power frame builder (include/gpd_frame.h) for both boards, ASan/UBSan.
HOST_CFLAGS := -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -Iinclude
# With GPD_SECURITY the CCM* frames against zigpy's vectors, AES from the
# stack's host-only FIPS 197 reference (its main renamed).
AES_REFERENCE := $(STACK)/tests/aes_reference.c
test: | $(STACK)/include/clock.h
	mkdir -p build/host
	$(CC) $(HOST_CFLAGS) -I$(STACK)/tests -I$(STACK)/include -DCC2530_HOST_TEST -Dmain=aes_reference_main -c $(AES_REFERENCE) -o build/host/aes_reference.o
	$(CC) $(HOST_CFLAGS) -I$(STACK)/tests tests/test_gpd_frame.c build/host/aes_reference.o -o build/host/test-gpd-frame
	$(CC) $(HOST_CFLAGS) -I$(STACK)/tests -DBOARD_BGT1 tests/test_gpd_frame.c build/host/aes_reference.o -o build/host/test-gpd-frame-bgt1
	$(CC) $(HOST_CFLAGS) -DGPD_SECURITY=0 tests/test_gpd_frame.c -o build/host/test-gpd-frame-open
	$(CC) $(HOST_CFLAGS) -DGPD_SECURITY=0 -DBOARD_BGT1 tests/test_gpd_frame.c -o build/host/test-gpd-frame-bgt1-open
	build/host/test-gpd-frame
	build/host/test-gpd-frame-bgt1
	build/host/test-gpd-frame-open
	build/host/test-gpd-frame-bgt1-open

# ZHA quirk against zha and zha-quirks (pip install zha zha-quirks pytest).
test-quirk:
	cd quirks && $(PYTHON) -m pytest -q test_zha_quirk_diyruz_flowerx_gp.py test_zha_quirk_efektalab_bgt1_gp.py

# Intel HEX in $(OUT)/$(IMAGE).hex.
firmware: $(OUT)/$(IMAGE).hex

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
	$(LLVM_TOOLS)/clang $(CFLAGS_FW) -DCC2530_LEAN -I$(STACK)/src -c $< -o $@

$(OUT)/$(IMAGE).elf: cc2530-root.ld $(OBJECTS) $(OUT)/rt/mcs51/cc2530_startup.o $(OUT)/rt/mcs51/libmcs51builtins.a
	$(LLVM_TOOLS)/clang $(TARGET) -Os -nostartfiles $(OUT)/rt/mcs51/cc2530_startup.o \
		-resource-dir $(OUT)/rt --ld-path=$(LLVM_TOOLS)/ld.lld -Wl,--gc-sections -Wl,--no-check-sections \
		-Wl,-T,cc2530-root.ld -Wl,-Map,$(OUT)/$(IMAGE).map $(OBJECTS) \
		$(OUT)/rt/mcs51/libmcs51builtins.a -o $@

$(OUT)/$(IMAGE).hex: $(OUT)/$(IMAGE).elf
	$(LLVM_TOOLS)/llvm-objcopy --output-target=ihex $< $@
	$(PYTHON) tools/size.py $@
	$(LLVM_TOOLS)/llvm-objdump -d --no-show-raw-insn $< > $(OUT)/$(IMAGE).dis
	$(PYTHON) tools/stack.py $(OUT)/$(IMAGE).dis $(STACK_SP) gpd_sleep_isr gpd_button_isr

clean:
	rm -rf build
