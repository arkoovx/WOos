NASM      := nasm
CC        := gcc
LD        := ld
OBJCOPY   := objcopy

CFLAGS    := -m64 -ffreestanding -mcmodel=large -mno-red-zone -fno-stack-protector -fno-pic -nostdlib -nostartfiles -Wall -Wextra
LDFLAGS   := -m elf_x86_64 -T linker.ld

all: os.img

bootsect.bin: bootsect.asm
	$(NASM) -f bin bootsect.asm -o bootsect.bin

stage2.o: stage2.asm
	$(NASM) -f elf64 stage2.asm -o stage2.o

kernel.o: kernel.c
	$(CC) $(CFLAGS) -c kernel.c -o kernel.o

kernel.elf: stage2.o kernel.o linker.ld
	$(LD) $(LDFLAGS) stage2.o kernel.o -o kernel.elf

kernel.bin: kernel.elf
	$(OBJCOPY) -O binary kernel.elf kernel.bin

os.img: bootsect.bin kernel.bin
	cat bootsect.bin kernel.bin > os.img
	truncate -s 1440k os.img

clean:
	rm -f *.o *.bin *.elf *.img

.PHONY: all clean
