NASM      := nasm
CC        := gcc
LD        := ld

CFLAGS    := -m64 -ffreestanding -mcmodel=large -mno-red-zone -fno-stack-protector -fno-pic -nostdlib -nostartfiles -Wall -Wextra
LDFLAGS   := -m elf_x86_64 -T linker.ld

all: os.img

boot.o: boot.asm
	$(NASM) -f elf64 boot.asm -o boot.o

kernel.o: kernel.c
	$(CC) $(CFLAGS) -c kernel.c -o kernel.o

os.bin: boot.o kernel.o linker.ld
	$(LD) $(LDFLAGS) boot.o kernel.o -o os.bin

os.img: os.bin
	dd if=/dev/zero of=os.img bs=512 count=2880 status=none
	dd if=os.bin of=os.img conv=notrunc status=none

clean:
	rm -f *.o *.bin *.img

.PHONY: all clean
