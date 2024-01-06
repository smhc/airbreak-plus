all: stm32-patched.bin stm32-asv.bin stm32-patched-asv.bin # stm32-pav.bin

# stm32-unlocked.bin: patch-airsense
# 	./patch-airsense stm32.bin $@

stm32-patched.bin: patch-airsense patches/common_code.bin patches/graph.bin patches/squarewave.bin
	export PATCH_CODE=1 && export PATCH_S=1 && ./patch-airsense stm32.bin $@

stm32-asv.bin: patch-airsense patches/common_code.bin patches/graph.bin patches/squarewave_asv.bin patches/asv_task_wrapper.bin
	export PATCH_CODE=1 && export PATCH_S_ASV=1 && export PATCH_ASV_TASK_WRAPPER=1 && ./patch-airsense stm32.bin $@

# stm32-patched-asv.bin: patch-airsense
# 	export PATCH_BACKUP_RATE=1 && ./patch-airsense stm32.bin $@

binaries: patches/common_code.bin patches/ventilator.bin patches/graph.bin patches/squarewave.bin patches/squarewave_asv.bin patches/asv_task_wrapper.bin

serve:
	mkdocs serve
deploy:
	mkdocs gh-deploy

# The ventilator extension replaces the function at 0x80bb734 with
# a simple on/off timer for alternating between two pressures.
# It can't be too long or it will overlap another function.
#
# TODO: add a size check
#
# To add another extension at a different address in the firmware,
# define a .elf target and a variable with the offset that it will
# be patched into the image.

patches/ventilator.elf: patches/ventilator.o patches/stubs.o
ventilator-offset := 0x80bb734
ventilator-extra := --just-symbols=patches/common_code.elf

patches/common_code.elf: patches/common_code.o patches/stubs.o
common_code-offset := 0x80fe000

# The graphing is too large to fit directly in the location at 0x8067d2c,
# so it is in high in the flash and the function pointer is fixed up at 0x80f9c88
patches/graph.elf: patches/graph.o patches/stubs.o
graph-offset := 0x80fcd40
graph-extra := --just-symbols=patches/common_code.elf

patches/squarewave.elf: patches/squarewave.o patches/stubs.o
squarewave-offset := 0x80fd000
squarewave-extra := --just-symbols=patches/common_code.elf

patches/squarewave_asv.elf: patches/squarewave_asv.o patches/stubs.o
squarewave_asv-offset := 0x80fd000
squarewave_asv-extra := --just-symbols=patches/common_code.elf

# patches/squarewave_pav.elf: patches/squarewave_pav.o patches/common_code.o patches/stubs.o
# squarewave_pav-offset := 0x80fd000

# patches/easybreathe.elf: patches/easybreathe.o patches/common_code.o patches/stubs.o
# easybreathe-offset := 0x80fdf00

patches/asv_task_wrapper.elf: patches/asv_task_wrapper.o patches/stubs.o
asv_task_wrapper-offset := 0x80fdf00
asv_task_wrapper-extra := --just-symbols=patches/common_code.elf

# If there is a new version of the ghidra XML, the stubs.S
# file will be regenerated so that the addresses and functions
# are at the correct address in the ELF image.
#stubs.S: stm32.bin.xml
#	./ghidra2stubs < $< > $@


CROSS ?= arm-none-eabi-
CC := $(CROSS)gcc
AS := $(CC)
LD := $(CROSS)ld
OBJCOPY := $(CROSS)objcopy

CFLAGS ?= \
	-g \
	-Os \
	-mcpu=cortex-m4 \
	-mhard-float \
	-mfp16-format=ieee \
	-mthumb \
	-W \
	-Wall \
	-Wno-unused-result \
	-Wno-unused-parameter \
	-Wno-unused-variable \
	-nostdlib \
	-nostdinc \
	-ffreestanding \

ASFLAGS ?= $(CFLAGS)

LDFLAGS ?= \
	--nostdlib \
	--no-dynamic-linker \
	--Ttext $($*-offset) \
	$($*-extra) \
	--entry start \
	--sort-section=name \

# TODO: Sort sections by name, lay out main before the rest, to avoid inlining everything

# patches/shared_code.o: patches/shared_code.c
# 	$(CC) $(CFLAGS) -static -shared -c -o $@ $<
patches/%.o: patches/%.c
	$(CC) $(CFLAGS) -c -o $@ $<
patches/%.o: patches/%.S
	$(AS) $(ASFLAGS) -c -o $@ $<
patches/%.elf:
	$(LD) $(LDFLAGS) -o $@ $^

patches/%.bin: patches/%.elf
	$(OBJCOPY) -Obinary $< $@

clean:
	$(RM) patches/*.o patches/*.elf patches/*.bin stm32-unlocked.bin stm32-patched.bin
