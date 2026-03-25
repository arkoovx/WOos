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
#include "drivers/virtio_gpu_renderer/virtio_gpu_renderer.h"

typedef enum init_stage {
    INIT_EARLY = 0,
    INIT_PLATFORM,
    INIT_DRIVERS,
    INIT_UI,
} init_stage_t;

// Пауза в основном цикле: нужна, чтобы не загружать CPU на 100%,
// но без «тормозов» интерфейса в эмуляторе.
#define KERNEL_MAIN_LOOP_PAUSE 20000u

#ifndef WOOS_ENABLE_HW_INTERRUPTS
#define WOOS_ENABLE_HW_INTERRUPTS 1
#endif

static void run_vfs_selftest(void);

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

static void run_stage(video_info_t* video, init_stage_t stage) {
    switch (stage) {
        case INIT_EARLY:
            sanitize_boot_info(video);
            break;
        case INIT_PLATFORM:
            fb_init(video);
            idt_init();
            break;
        case INIT_DRIVERS:
            virtio_gpu_renderer_init(video);
            pmm_init(video);
            kheap_init();
            input_init();
            storage_init();
            vfs_init();
            run_vfs_selftest();
            // Heartbeat теперь идёт от аппаратного PIT, а не от числа итераций цикла,
            // поэтому частота UI-обновлений не зависит от скорости CPU/эмулятора.
            timer_init(20u);
            break;
        case INIT_UI:
            ui_render_desktop(video);
            break;
    }
}


static void run_vfs_selftest(void) {
    int32_t root = vfs_open("/");
    if (root >= 0) {
        vfs_dirent_t entry;
        (void)vfs_readdir(root, &entry);
        vfs_close(root);
    }
}

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
            break;
    }
}

static void refresh_runtime_stats(video_info_t* video) {
    const virtio_gpu_renderer_status_t* renderer = virtio_gpu_renderer_status();
    ui_set_runtime_stats(
        video,
        ui_last_dirty_count(),
        kheap_used_bytes(),
        kheap_free_bytes(),
        renderer->detected,
        renderer->active
    );
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
    refresh_runtime_stats(video);

    uint16_t cursor_x = (uint16_t)(video->width / 2);
    uint16_t cursor_y = (uint16_t)(video->height / 2);
    mouse_init(cursor_x, cursor_y);

#if WOOS_ENABLE_HW_INTERRUPTS
    idt_enable_interrupts();
#endif

    while (1) {
        for (volatile uint32_t delay = 0; delay < KERNEL_MAIN_LOOP_PAUSE; delay++) {
            __asm__ __volatile__("pause");
        }

        input_event_t tick_event = {INPUT_EVENT_TIMER_TICK, 0, 0, 0};

        if (timer_poll_tick()) {
            input_push(&tick_event);
        }

        mouse_poll();

        input_event_t next_event;
        while (input_pop(&next_event)) {
            dispatch_input_event(video, &next_event);
        }

        refresh_runtime_stats(video);
        ui_render_dirty(video);
    }
}
