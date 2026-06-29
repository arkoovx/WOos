// Freestanding WoOS kernel (x86_64)

__attribute__((used)) static const char* magic = "KERNEL_START_MARKER";

#include "kernel.h"
#include "fb.h"
#include "input.h"
#include "ui.h"
#include "idt.h"
#include "timer.h"
#include "mouse.h"
#include "kheap.h"
#include "pmm.h"
#include "storage.h"
#include "vfs.h"
#include "net_socket.h"
#include "net.h"
#include "serial.h"
#include "sched.h"
#include "drivers/virtio_gpu_renderer/virtio_gpu_renderer.h"
#include "vmm.h"
#include "tss.h"
#include "syscall.h"
#include "wasm_runtime.h"
#include "test_wasm.h"

uint64_t g_tsc_per_ms = 2000000ULL;

void calibrate_tsc(void) {
    uint32_t start_ticks = timer_ticks();
    // Ждем изменения тика
    while (timer_ticks() == start_ticks) {
        __asm__ __volatile__("pause");
    }
    
    start_ticks = timer_ticks();
    uint64_t start_tsc = rdtsc();
    
    // Ждем 5 тиков (100 мс)
    while (timer_ticks() < start_ticks + 5) {
        __asm__ __volatile__("pause");
    }
    
    uint64_t end_tsc = rdtsc();
    // 5 тиков при частоте PIT 20 Гц — это 250 мс
    g_tsc_per_ms = (end_tsc - start_tsc) / 250;
    if (g_tsc_per_ms == 0) {
        g_tsc_per_ms = 2000000ULL;
    }
    serial_printf("[WoOS Kernel] TSC calibrated: %u ticks/ms\n", (uint32_t)g_tsc_per_ms);
}

typedef enum init_stage {
    INIT_EARLY = 0,
    INIT_PLATFORM,
    INIT_DRIVERS,
    INIT_UI,
} init_stage_t;

// Короткая пауза в idle-path. Большая задержка здесь напрямую увеличивает
// input-latency (курсор «плывёт» и клики приходят с опозданием).
#define KERNEL_MAIN_LOOP_PAUSE 512u

#ifndef WOOS_ENABLE_HW_INTERRUPTS
#define WOOS_ENABLE_HW_INTERRUPTS 1
#endif

static uint8_t g_vfs_probe_done = 0u;

static void sanitize_boot_info(video_info_t* video) {
    if (video->magic != BOOT_INFO_MAGIC_EXPECTED) {
        video->magic = BOOT_INFO_MAGIC_EXPECTED;
        video->version = BOOT_INFO_VERSION_V1;
        video->size = 24u;
        video->memory_region_count = 0;
        video->memory_region_capacity = BOOT_INFO_E820_MAX_ENTRIES;
    }

    if (video->version != BOOT_INFO_VERSION_V1 && video->version != BOOT_INFO_VERSION_V2) {
        video->version = BOOT_INFO_VERSION_V1;
        video->size = 24u;
        video->memory_region_count = 0;
        video->memory_region_capacity = BOOT_INFO_E820_MAX_ENTRIES;
    }

    if (video->version == BOOT_INFO_VERSION_V2 && video->size < 28u) {
        video->version = BOOT_INFO_VERSION_V1;
        video->size = 24u;
        video->memory_region_count = 0;
    }

    if (video->version == BOOT_INFO_VERSION_V1) {
        video->memory_region_count = 0;
        video->memory_region_capacity = BOOT_INFO_E820_MAX_ENTRIES;
    } else if (video->memory_region_capacity == 0 || video->memory_region_capacity > BOOT_INFO_E820_MAX_ENTRIES) {
        video->memory_region_capacity = BOOT_INFO_E820_MAX_ENTRIES;
    }

    if (video->framebuffer == 0) {
        video->framebuffer = 0xE0000000ull;
    }

    if (video->pitch == 0) {
        video->pitch = (uint16_t)(video->width * 4);
    }
}

void task1(void) {
    while (1) {
        serial_printf("[Task 1] Hello from thread 1! ticks=%u\n", (uint32_t)timer_ticks());
        for (volatile int i = 0; i < 20000000; i++);
    }
}

void task2(void) {
    while (1) {
        serial_printf("[Task 2] Hello from thread 2! ticks=%u\n", (uint32_t)timer_ticks());
        for (volatile int i = 0; i < 20000000; i++);
    }
}

void wasm_runner_thread(void) {
    serial_printf("[WASM Runner] Waiting for network to be active...\n");
    while (1) {
        const net_status_t* st = net_get_status();
        if (st->active && st->link_up) break;
        net_poll();
        for (volatile int d = 0; d < 500000; d++);
        thread_yield();
    }
    serial_printf("[WASM Runner] Network active. Starting WASM HTTP Server...\n");
    wasm_runtime_run(test_wasm, test_wasm_len);
    while (1) {
        thread_yield();
    }
}

static void run_stage(video_info_t* video, init_stage_t stage) {
    serial_printf("[WoOS Kernel] Starting stage: %d...\n", (int)stage);
    switch (stage) {
        case INIT_EARLY:
            sanitize_boot_info(video);
            serial_init();
            serial_printf("[WoOS Kernel] Serial logging initialized.\n");
            serial_printf("[WoOS Kernel] Framebuffer info: addr=%p, w=%u, h=%u, pitch=%u, bpp=%u\n",
                          (void*)video->framebuffer, video->width, video->height, video->pitch, video->bpp);
            break;
        case INIT_PLATFORM:
            serial_printf("[WoOS Kernel] Initializing platform components...\n");
            fb_init(video);
            fb_enable_write_combining(video);
            serial_printf("[WoOS Kernel] Framebuffer driver initialized with Write-Combining.\n");
            idt_init();
            serial_printf("[WoOS Kernel] IDT loaded.\n");
            break;
        case INIT_DRIVERS:
            serial_printf("[WoOS Kernel] Initializing drivers...\n");
            virtio_gpu_renderer_init(video);
            serial_printf("[WoOS Kernel] Virtio-GPU renderer initialized.\n");
            pmm_init(video);
            serial_printf("[WoOS Kernel] PMM initialized.\n");
            kheap_init();
            serial_printf("[WoOS Kernel] Heap initialized.\n");
            // Инициализируем сетевой стек ДО создания потоков —
            // чтобы tcp_server_thread мог безопасно вызывать net_socket_create
            net_init();
            serial_printf("[WoOS Kernel] Net stack initialized.\n");
            storage_init();
            serial_printf("[WoOS Kernel] Storage driver initialized.\n");
            vfs_init();
            serial_printf("[WoOS Kernel] VFS / FAT12 initialized.\n");
            timer_init(20u);
            serial_printf("[WoOS Kernel] Timer initialized.\n");
            vmm_init();
            tss_init((void*)0x200000);
            syscall_init();
            wasm_runtime_init();
            input_init();
            serial_printf("[WoOS Kernel] Input queue initialized.\n");
            sched_init();
            thread_create(task1);
            thread_create(task2);
            thread_create(wasm_runner_thread);
            serial_printf("[WoOS Kernel] Scheduler started with 3 threads.\n");
            break;
        case INIT_UI:
            serial_printf("[WoOS Kernel] Launching UI...\n");
            ui_render_desktop(video);
            serial_printf("[WoOS Kernel] UI rendering finished. Kernel entering loop.\n");
            break;
    }
}


static void refresh_runtime_stats(video_info_t* video, uint16_t dirty_count);

static void dispatch_input_event(video_info_t* video, const input_event_t* event) {

    switch (event->type) {
        case INPUT_EVENT_MOUSE_MOVE:
            ui_handle_mouse_move(video, event->x, event->y, event->buttons);
            break;
        case INPUT_EVENT_MOUSE_BUTTON:
            ui_handle_mouse_button(video, event->buttons);
            break;
        case INPUT_EVENT_TIMER_TICK:
            ui_set_kernel_health(video, idt_is_ready(), timer_ticks());
            ui_set_irq_stats(video, idt_keyboard_irq_count(), idt_mouse_irq_count());
            ui_set_memory_stats(video, pmm_is_ready(), pmm_total_pages(), pmm_free_pages());
            ui_set_storage_stats(video, storage_is_ready(), storage_last_read_ok(), storage_last_lba(), storage_boot_signature_valid());
            const net_status_t* net = net_get_status();
            ui_set_net_status(video, net->ready, net->active, net->link_up, net->ip_addr);
            refresh_runtime_stats(video, ui_last_dirty_count());
            break;

    }
}

static void refresh_runtime_stats(video_info_t* video, uint16_t dirty_count) {
    const virtio_gpu_renderer_status_t* renderer = virtio_gpu_renderer_status();
    ui_set_runtime_stats(
        video,
        dirty_count,
        kheap_used_bytes(),
        kheap_free_bytes(),
        renderer->detected,
        renderer->active
    );
}

static void run_deferred_vfs_probe(void) {
    if (g_vfs_probe_done || timer_ticks() < 20u) {
        return;
    }

    g_vfs_probe_done = 1u;

    int32_t root = vfs_open("/", VFS_MODE_READ);
    if (root >= 0) {
        vfs_dirent_t entry;
        (void)vfs_readdir(root, &entry);
        vfs_close(root);
    }

    int32_t hello = vfs_open("/HELLO.TXT", VFS_MODE_READ);
    if (hello >= 0) {
        uint8_t preview[16];
        (void)vfs_read(hello, preview, sizeof(preview));
        vfs_close(hello);
    }
}

void kmain(video_info_t* video) {
    run_stage(video, INIT_EARLY);
    run_stage(video, INIT_PLATFORM);
    run_stage(video, INIT_DRIVERS);
    run_stage(video, INIT_UI);
    ui_set_kernel_health(video, idt_is_ready(), timer_ticks());
    ui_set_irq_stats(video, idt_keyboard_irq_count(), idt_mouse_irq_count());
    ui_set_memory_stats(video, pmm_is_ready(), pmm_total_pages(), pmm_free_pages());
    ui_set_storage_stats(video, storage_is_ready(), storage_last_read_ok(), storage_last_lba(), storage_boot_signature_valid());
    const net_status_t* net = net_get_status();
    ui_set_net_status(video, net->ready, net->active, net->link_up, net->ip_addr);
    refresh_runtime_stats(video, 0u);

    uint16_t cursor_x = (uint16_t)(video->width / 2);
    uint16_t cursor_y = (uint16_t)(video->height / 2);
    mouse_init(cursor_x, cursor_y);

#if WOOS_ENABLE_HW_INTERRUPTS
    idt_enable_interrupts();
#endif

    calibrate_tsc();

    while (1) {
        uint64_t start, end;

        start = rdtsc();
        net_poll();
        end = rdtsc();
        uint64_t net_time = (end - start) / g_tsc_per_ms;
        if (net_time >= 2) {
            serial_printf("[Perf Warning] net_poll took %u ms\n", (uint32_t)net_time);
        }

        start = rdtsc();
        input_event_t next_event;
        uint32_t event_count = 0;
        while (input_pop(&next_event)) {
            dispatch_input_event(video, &next_event);
            event_count++;
        }
        end = rdtsc();
        uint64_t input_time = (end - start) / g_tsc_per_ms;
        if (input_time >= 2) {
            serial_printf("[Perf Warning] input dispatch (%u events) took %u ms\n", event_count, (uint32_t)input_time);
        }

        start = rdtsc();
        run_deferred_vfs_probe();
        end = rdtsc();
        uint64_t vfs_time = (end - start) / g_tsc_per_ms;
        if (vfs_time >= 2) {
            serial_printf("[Perf Warning] vfs_probe took %u ms\n", (uint32_t)vfs_time);
        }

        start = rdtsc();
        ui_render_dirty(video);
        end = rdtsc();
        uint64_t render_time = (end - start) / g_tsc_per_ms;
        if (render_time >= 2) {
            serial_printf("[Perf Warning] ui_render_dirty took %u ms\n", (uint32_t)render_time);
        }

        // Усыпляем процессор до следующего прерывания
        __asm__ __volatile__("hlt");
    }
}

