NASM      := nasm
CC        := gcc
LD        := ld
OBJCOPY   := objcopy
OBJDUMP   := objdump

CFLAGS    := -m64 -ffreestanding -mcmodel=large -mno-red-zone -fno-stack-protector -fno-pic -fcf-protection=none -nostdlib -nostartfiles -Wall -Wextra -I. -Inet -Iexternal/lwip/src/include -Iexternal/wasm3/source -Iexternal/fatfs -DLWIP_ACD=0 -DLWIP_DHCP_DOES_ACD_CHECK=0 -Dd_m3HasFloat=0 -Dd_m3VerboseErrorMessages=0
LDFLAGS   := -m elf_x86_64 -T linker.ld

BUILD_DIR := build

LWIP_OBJS_RAW := \
	external/lwip/src/core/init.o \
	external/lwip/src/core/mem.o \
	external/lwip/src/core/memp.o \
	external/lwip/src/core/netif.o \
	external/lwip/src/core/pbuf.o \
	external/lwip/src/core/raw.o \
	external/lwip/src/core/stats.o \
	external/lwip/src/core/sys.o \
	external/lwip/src/core/tcp.o \
	external/lwip/src/core/tcp_in.o \
	external/lwip/src/core/tcp_out.o \
	external/lwip/src/core/udp.o \
	external/lwip/src/core/ipv4/autoip.o \
	external/lwip/src/core/ipv4/dhcp.o \
	external/lwip/src/core/ipv4/etharp.o \
	external/lwip/src/core/ipv4/icmp.o \
	external/lwip/src/core/ipv4/ip4.o \
	external/lwip/src/core/ipv4/ip4_addr.o \
	external/lwip/src/core/ipv4/ip4_frag.o \
	external/lwip/src/core/ipv4/igmp.o \
	external/lwip/src/core/def.o \
	external/lwip/src/core/timeouts.o \
	external/lwip/src/core/ip.o \
	external/lwip/src/core/inet_chksum.o \
	external/lwip/src/netif/ethernet.o

LWIP_OBJS := $(addprefix $(BUILD_DIR)/, $(LWIP_OBJS_RAW))

KERNEL_OBJS_RAW := \
	stage2.o \
	idt_asm.o \
	kernel.o \
	fb.o \
	ui.o \
	input.o \
	idt.o \
	timer.o \
	mouse.o \
	pci.o \
	kheap.o \
	pmm.o \
	sched.o \
	storage.o \
	vfs.o \
	net.o \
	lib.o \
	serial.o \
	drivers/virtio_gpu_renderer/virtio_gpu_renderer.o \
	drivers/virtio_net.o \
	vmm.o \
	tss.o \
	syscall.o \
	wasi.o \
	wasm_runtime.o \
	external/fatfs/ff.o \
	external/fatfs/diskio.o

WASM3_OBJS_RAW := \
	external/wasm3/source/m3_bind.o \
	external/wasm3/source/m3_code.o \
	external/wasm3/source/m3_compile.o \
	external/wasm3/source/m3_core.o \
	external/wasm3/source/m3_env.o \
	external/wasm3/source/m3_exec.o \
	external/wasm3/source/m3_function.o \
	external/wasm3/source/m3_info.o \
	external/wasm3/source/m3_module.o \
	external/wasm3/source/m3_parse.o

WASM3_OBJS := $(addprefix $(BUILD_DIR)/, $(WASM3_OBJS_RAW))

KERNEL_OBJS := $(addprefix $(BUILD_DIR)/, $(KERNEL_OBJS_RAW)) $(LWIP_OBJS) $(WASM3_OBJS)

DBL_BUFFER ?= 1
FB_CPPFLAGS := -DWOOS_ENABLE_DBL_BUFFER=$(DBL_BUFFER)

VIRTIO_GPU ?= 0
RENDERER_CPPFLAGS := -DWOOS_ENABLE_VIRTIO_GPU=$(VIRTIO_GPU)

HW_INTERRUPTS ?= 1
KERNEL_CPPFLAGS := -DWOOS_ENABLE_HW_INTERRUPTS=$(HW_INTERRUPTS)
WOFS ?= 1
VFS_CPPFLAGS := -DWOOS_ENABLE_WOFS=$(WOFS)

all: os.img

woosfs.bin: tools/build_fat.py
	python3 tools/build_fat.py

boot.bin: kernel.bin boot.asm
	$(NASM) -f bin -DKERNEL_SECTORS=$(shell expr $$(stat -c%s kernel.bin) / 512) boot.asm -o boot.bin

# Специфические правила для файлов со спецфлагами
$(BUILD_DIR)/kernel.o: kernel.c kernel.h ui.h input.h idt.h timer.h mouse.h kheap.h pmm.h storage.h vfs.h drivers/virtio_gpu_renderer/virtio_gpu_renderer.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(KERNEL_CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/fb.o: fb.c fb.h kernel.h drivers/virtio_gpu_renderer/virtio_gpu_renderer.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(FB_CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/vfs.o: vfs.c vfs.h storage.h kernel.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(VFS_CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/drivers/virtio_gpu_renderer/virtio_gpu_renderer.o: drivers/virtio_gpu_renderer/virtio_gpu_renderer.c drivers/virtio_gpu_renderer/virtio_gpu_renderer.h pci.h kernel.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(RENDERER_CPPFLAGS) -c $< -o $@

# Общие правила компиляции в build/
$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.asm
	@mkdir -p $(dir $@)
	$(NASM) -f elf64 $< -o $@

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
	rm -rf $(BUILD_DIR)
	rm -f *.bin *.elf *.img woosfs.bin

.PHONY: all clean verify-layout
