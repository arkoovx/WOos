NASM      := nasm
CC        := gcc
LD        := ld
OBJCOPY   := objcopy
OBJDUMP   := objdump

CFLAGS    := -m64 -ffreestanding -mcmodel=large -mno-red-zone -fno-stack-protector -fno-pic -fcf-protection=none -nostdlib -nostartfiles -Wall -Wextra
LDFLAGS   := -m elf_x86_64 -T linker.ld
KERNEL_OBJS := stage2.o kernel.o fb.o ui.o

DBL_BUFFER ?= 0
FB_CPPFLAGS := -DWOOS_ENABLE_DBL_BUFFER=$(DBL_BUFFER)

all: os.img

boot.bin: kernel.bin boot.asm
	$(NASM) -f bin -DKERNEL_SECTORS=$(shell expr $$(stat -c%s kernel.bin) / 512) boot.asm -o boot.bin

stage2.o: stage2.asm
	$(NASM) -f elf64 stage2.asm -o stage2.o

kernel.o: kernel.c kernel.h ui.h
	$(CC) $(CFLAGS) -c kernel.c -o kernel.o

fb.o: fb.c fb.h kernel.h
	$(CC) $(CFLAGS) $(FB_CPPFLAGS) -c fb.c -o fb.o

ui.o: ui.c ui.h fb.h kernel.h
	$(CC) $(CFLAGS) -c ui.c -o ui.o

kernel.elf: $(KERNEL_OBJS) linker.ld
	$(LD) $(LDFLAGS) $(KERNEL_OBJS) -o kernel.elf

kernel.bin: kernel.elf
	$(OBJCOPY) -O binary kernel.elf kernel.bin
	truncate -s %512 kernel.bin

os.img: boot.bin kernel.bin
	dd if=/dev/zero of=os.img bs=1024 count=1440
	dd if=boot.bin of=os.img conv=notrunc
	dd if=kernel.bin of=os.img seek=1 conv=notrunc

verify-layout: os.img
	dd if=os.img bs=1 skip=512 count=32 status=none | od -An -tx1
	$(OBJDUMP) -D -b binary -m i386 --start-address=512 --stop-address=640 os.img

clean:
	rm -f *.o *.bin *.elf *.img

.PHONY: all clean verify-layout
