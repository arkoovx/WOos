// Freestanding WoOS kernel (x86_64)

__attribute__((used)) static const char* magic = "KERNEL_START_MARKER";

#include "kernel.h"
#include "fb.h"
#include "input.h"
#include "ui.h"
#include "idt.h"
#include "timer.h"
#include "mouse.h"
#include "keyboard.h"
#include "pmm.h"
#include "kheap.h"
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

static uint8_t g_kernel_ready = 0u;

static void sanitize_boot_info(video_info_t* video) {
    if (video->magic != BOOT_INFO_MAGIC_EXPECTED) {
        video->magic = BOOT_INFO_MAGIC_EXPECTED;
        video->version = BOOT_INFO_VERSION_V1;
        video->size = (uint16_t)sizeof(video_info_t);
    }

    if (video->version != BOOT_INFO_VERSION_V1 || video->size < (uint16_t)sizeof(video_info_t)) {
        video->version = BOOT_INFO_VERSION_V1;
        video->size = (uint16_t)sizeof(video_info_t);
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
            pmm_init();
            kheap_init();
            break;
        case INIT_DRIVERS:
            virtio_gpu_renderer_init(video);
            input_init();
            keyboard_init();
            // Heartbeat обновляется заметно медленнее кадрового цикла,
            // чтобы UI оставался отзывчивым, а счётчик не «улетал» слишком быстро.
            timer_init(120u);
            break;
        case INIT_UI:
            ui_render_desktop(video);
            break;
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
            ui_set_kernel_health(video, g_kernel_ready, timer_ticks());
            break;
        case INPUT_EVENT_KEY_PRESS:
            // Пока отображаем только факт жизни IRQ-пути клавиатуры через heartbeat refresh.
            ui_set_kernel_health(video, g_kernel_ready, timer_ticks());
            break;
    }
}

static void irq_keyboard_handler(void) {
    keyboard_handle_irq();
}

static void irq_mouse_handler(void) {
    mouse_handle_irq();
}

static uint8_t kheap_smoke_test(void) {
    void* a = kmalloc(64u);
    void* b = kmalloc(128u);

    if (a == (void*)0 || b == (void*)0 || a == b) {
        return 0u;
    }

    kfree(a);
    kfree(b);
    return 1u;
}

void kmain(video_info_t* video) {
    run_stage(video, INIT_EARLY);
    run_stage(video, INIT_PLATFORM);
    run_stage(video, INIT_DRIVERS);
    run_stage(video, INIT_UI);

    // Базовая проверка heap в рантайме: должна пройти до входа в event loop.
    uint8_t heap_ok = kheap_is_ready() && kheap_smoke_test();
    g_kernel_ready = (uint8_t)(idt_is_ready() && heap_ok);
    ui_set_kernel_health(video, g_kernel_ready, timer_ticks());

    uint16_t cursor_x = (uint16_t)(video->width / 2);
    uint16_t cursor_y = (uint16_t)(video->height / 2);
    mouse_init(cursor_x, cursor_y);

    idt_set_irq_handler(1u, irq_keyboard_handler);
    idt_set_irq_handler(12u, irq_mouse_handler);
    idt_enable_interrupts();

    while (1) {
        for (volatile uint32_t delay = 0; delay < KERNEL_MAIN_LOOP_PAUSE; delay++) {
            __asm__ __volatile__("pause");
        }

        input_event_t tick_event = {INPUT_EVENT_TIMER_TICK, 0u, 0u, 0u, 0u};

        if (timer_poll_tick()) {
            input_push(&tick_event);
        }

        // Fallback: polling остаётся как защитный путь для окружений с нестабильной IRQ-маршрутизацией.
        mouse_poll();

        input_event_t next_event;
        while (input_pop(&next_event)) {
            dispatch_input_event(video, &next_event);
        }

        ui_render_dirty(video);
    }
}
