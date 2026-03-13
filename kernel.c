// Freestanding WoOS kernel (x86_64)

__attribute__((used)) static const char* magic = "KERNEL_START_MARKER";

#include "kernel.h"
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

    while (1) {
        __asm__ __volatile__("hlt");
    }
}
