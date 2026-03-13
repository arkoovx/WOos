// Freestanding WoOS kernel (x86_64)

__attribute__((used)) static const char* magic = "KERNEL_START_MARKER";

#include "kernel.h"
#include "fb.h"
#include "ui.h"

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
            break;
        case INIT_DRIVERS:
            break;
        case INIT_UI:
            ui_render_desktop(video);
            break;
    }
}

void kmain(video_info_t* video) {
    run_stage(video, INIT_EARLY);
    run_stage(video, INIT_PLATFORM);
    run_stage(video, INIT_DRIVERS);
    run_stage(video, INIT_UI);

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
        ui_set_cursor(video, cursor_x, cursor_y, 0);
    }
}
