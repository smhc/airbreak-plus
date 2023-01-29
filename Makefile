all: stm32-unlocked.bin stm32-patched.bin stm32-testing.bin

stm32-unlocked.bin: patch-airsense
	./patch-airsense stm32.bin $@

stm32-patched.bin: patch-airsense patches/graph.bin patches/squarewave.bin
	export PATCH_CODE=1 && ./patch-airsense stm32.bin $@

stm32-testing.bin: patch-airsense patches/graph.bin patches/squarewave_asv.bin
	export PATCH_CODE=1 && export PATCH_TESTING=1 && ./patch-airsense stm32.bin $@

binaries: patches/ventilator.bin patches/graph.bin patches/squarewave.bin patches/squarewave_asv.bin

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

# The graphing is too large to fit directly in the location at 0x8067d2c,
# so it is in high in the flash and the function pointer is fixed up at 0x80f9c88
patches/graph.elf: patches/graph.o patches/stubs.o
graph-offset := 0x80fd000

patches/squarewave.elf: patches/squarewave.o patches/stubs.o
squarewave-offset := 0x80fd300

patches/squarewave_asv.elf: patches/squarewave_asv.o patches/stubs.o
squarewave_asv-offset := 0x80fd300

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
	-nostdlib \
	-nostdinc \
	-ffreestanding \

ASFLAGS ?= $(CFLAGS)

LDFLAGS ?= \
	--nostdlib \
	--no-dynamic-linker \
	--Ttext $($*-offset) \
	--entry start \

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
