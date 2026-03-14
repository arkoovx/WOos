// Freestanding WoOS kernel (x86_64)

__attribute__((used)) static const char* magic = "KERNEL_START_MARKER";

#include "kernel.h"
#include "fb.h"
#include "input.h"
#include "ui.h"
#include "idt.h"
#include "timer.h"

typedef enum init_stage {
    INIT_EARLY = 0,
    INIT_PLATFORM,
    INIT_DRIVERS,
    INIT_UI,
} init_stage_t;

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
            break;
        case INIT_DRIVERS:
            input_init();
            timer_init(5u);
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
            ui_set_kernel_health(video, idt_is_ready(), timer_ticks());
            break;
    }
}

void kmain(video_info_t* video) {
    run_stage(video, INIT_EARLY);
    run_stage(video, INIT_PLATFORM);
    run_stage(video, INIT_DRIVERS);
    run_stage(video, INIT_UI);
    ui_set_kernel_health(video, idt_is_ready(), timer_ticks());

    uint16_t cursor_x = (uint16_t)(video->width / 2);
    uint16_t cursor_y = (uint16_t)(video->height / 2);
    int16_t dx = 2;
    int16_t dy = 1;
    uint16_t max_x = (video->width > 12) ? (uint16_t)(video->width - 12) : 0;
    uint16_t max_y = (video->height > 18) ? (uint16_t)(video->height - 18) : 0;


    while (1) {
        for (volatile uint32_t delay = 0; delay < 2500000u; delay++) {
            __asm__ __volatile__("pause");
        }


        if (cursor_x <= 2 || cursor_x >= max_x) {
            dx = (int16_t)-dx;
        }

        if (cursor_y <= 2 || cursor_y >= max_y) {
            dy = (int16_t)-dy;
        }

        cursor_x = (uint16_t)(cursor_x + dx);
        cursor_y = (uint16_t)(cursor_y + dy);

        input_event_t tick_event = {INPUT_EVENT_TIMER_TICK, 0, 0, 0};
        input_event_t move_event = {INPUT_EVENT_MOUSE_MOVE, cursor_x, cursor_y, 0};
        input_event_t button_event = {INPUT_EVENT_MOUSE_BUTTON, cursor_x, cursor_y, 0};

        if ((timer_ticks() % 24u) >= 18u) {
            button_event.buttons = 0x1u;
            move_event.buttons = 0x1u;
        }

        if (timer_poll_tick()) {
            input_push(&tick_event);
        }

        input_push(&move_event);
        input_push(&button_event);

        input_event_t next_event;
        while (input_pop(&next_event)) {
            dispatch_input_event(video, &next_event);
        }

        ui_render_dirty(video);
    }
}
