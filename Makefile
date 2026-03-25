NASM      := nasm
CC        := gcc
LD        := ld
OBJCOPY   := objcopy
OBJDUMP   := objdump

CFLAGS    := -m64 -ffreestanding -mcmodel=large -mno-red-zone -fno-stack-protector -fno-pic -fcf-protection=none -nostdlib -nostartfiles -Wall -Wextra
LDFLAGS   := -m elf_x86_64 -T linker.ld
KERNEL_OBJS := stage2.o idt_asm.o kernel.o fb.o ui.o input.o idt.o timer.o mouse.o pci.o kheap.o pmm.o storage.o vfs.o drivers/virtio_gpu_renderer/virtio_gpu_renderer.o

# По умолчанию держим двойную буферизацию включённой,
# чтобы убрать заметное мерцание UI при частых dirty-update.
DBL_BUFFER ?= 1
FB_CPPFLAGS := -DWOOS_ENABLE_DBL_BUFFER=$(DBL_BUFFER)

# По умолчанию оставляем virtio-gpu renderer выключенным: это
# даёт максимально стабильный fallback на stage2 framebuffer на
# проблемных конфигурациях эмулятора/видеобэкенда.
VIRTIO_GPU ?= 1
RENDERER_CPPFLAGS := -DWOOS_ENABLE_VIRTIO_GPU=$(VIRTIO_GPU)

# Аппаратные IRQ по умолчанию выключены для максимальной
# стабильности boot (polling-путь уже покрывает mouse/timer).
HW_INTERRUPTS ?= 1
KERNEL_CPPFLAGS := -DWOOS_ENABLE_HW_INTERRUPTS=$(HW_INTERRUPTS)
WOFS ?= 1
VFS_CPPFLAGS := -DWOOS_ENABLE_WOFS=$(WOFS)

all: os.img

woosfs.bin: tools/build_woosfs.py
	python3 tools/build_woosfs.py

boot.bin: kernel.bin boot.asm
	$(NASM) -f bin -DKERNEL_SECTORS=$(shell expr $$(stat -c%s kernel.bin) / 512) boot.asm -o boot.bin

stage2.o: stage2.asm
	$(NASM) -f elf64 stage2.asm -o stage2.o

kernel.o: kernel.c kernel.h ui.h input.h idt.h timer.h mouse.h kheap.h pmm.h storage.h vfs.h drivers/virtio_gpu_renderer/virtio_gpu_renderer.h
	$(CC) $(CFLAGS) $(KERNEL_CPPFLAGS) -c kernel.c -o kernel.o

idt_asm.o: idt_asm.asm
	$(NASM) -f elf64 idt_asm.asm -o idt_asm.o

idt.o: idt.c idt.h kernel.h
	$(CC) $(CFLAGS) -c idt.c -o idt.o

timer.o: timer.c timer.h kernel.h
	$(CC) $(CFLAGS) -c timer.c -o timer.o

input.o: input.c input.h kheap.h kernel.h
	$(CC) $(CFLAGS) -c input.c -o input.o

fb.o: fb.c fb.h kernel.h drivers/virtio_gpu_renderer/virtio_gpu_renderer.h
	$(CC) $(CFLAGS) $(FB_CPPFLAGS) -c fb.c -o fb.o

kheap.o: kheap.c kheap.h kernel.h
	$(CC) $(CFLAGS) -c kheap.c -o kheap.o

pmm.o: pmm.c pmm.h kernel.h
	$(CC) $(CFLAGS) -c pmm.c -o pmm.o

storage.o: storage.c storage.h kernel.h
	$(CC) $(CFLAGS) -c storage.c -o storage.o


vfs.o: vfs.c vfs.h storage.h kernel.h
	$(CC) $(CFLAGS) $(VFS_CPPFLAGS) -c vfs.c -o vfs.o



ui.o: ui.c ui.h fb.h kernel.h
	$(CC) $(CFLAGS) -c ui.c -o ui.o


pci.o: pci.c pci.h kernel.h
	$(CC) $(CFLAGS) -c pci.c -o pci.o

drivers/virtio_gpu_renderer/virtio_gpu_renderer.o: drivers/virtio_gpu_renderer/virtio_gpu_renderer.c drivers/virtio_gpu_renderer/virtio_gpu_renderer.h pci.h kernel.h
	$(CC) $(CFLAGS) $(RENDERER_CPPFLAGS) -c drivers/virtio_gpu_renderer/virtio_gpu_renderer.c -o drivers/virtio_gpu_renderer/virtio_gpu_renderer.o

kernel.elf: $(KERNEL_OBJS) linker.ld
	$(LD) $(LDFLAGS) $(KERNEL_OBJS) -o kernel.elf

kernel.bin: kernel.elf
	$(OBJCOPY) -O binary kernel.elf kernel.bin
	truncate -s %512 kernel.bin

os.img: boot.bin kernel.bin woosfs.bin
	dd if=/dev/zero of=os.img bs=1024 count=1440
	dd if=boot.bin of=os.img conv=notrunc
	dd if=kernel.bin of=os.img seek=1 conv=notrunc
ifneq ($(WOFS),0)
	dd if=woosfs.bin of=os.img seek=$$(( 1 + ($$(stat -c%s kernel.bin) / 512) )) conv=notrunc
endif

verify-layout: os.img
	dd if=os.img bs=1 skip=512 count=32 status=none | od -An -tx1
	$(OBJDUMP) -D -b binary -m i386 --start-address=512 --stop-address=640 os.img

clean:
	rm -f *.o *.bin *.elf *.img drivers/virtio_gpu_renderer/*.o

.PHONY: all clean verify-layout

mouse.o: mouse.c mouse.h input.h kernel.h
	$(CC) $(CFLAGS) -c mouse.c -o mouse.o
