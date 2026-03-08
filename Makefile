NASM      := nasm
CC        := gcc
LD        := ld

CFLAGS    := -m64 -ffreestanding -mcmodel=large -mno-red-zone -fno-stack-protector -fno-pic -nostdlib -nostartfiles -Wall -Wextra
LDFLAGS   := -m elf_x86_64 -T linker.ld

BOOT_OBJ  := boot.o
KERNEL_OBJ:= kernel.o
OBJS      := $(BOOT_OBJ) $(KERNEL_OBJ)

all: os.img

boot.o: boot.asm
	$(NASM) -f elf64 boot.asm -o boot.o

kernel.o: kernel.c
	$(CC) $(CFLAGS) -c kernel.c -o kernel.o

os.bin: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) $(OBJS) -o os.bin

os.img: os.bin
	dd if=/dev/zero of=os.img bs=512 count=2880 status=none
	dd if=os.bin of=os.img conv=notrunc status=none

clean:
	rm -f *.o *.bin *.img

.PHONY: all clean
