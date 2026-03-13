// Freestanding WoOS kernel (x86_64)

__attribute__((used)) static const char* magic = "KERNEL_START_MARKER";

#include "kernel.h"
#include "ui.h"

void kmain(video_info_t* video) {
    ui_render_desktop(video);

    while (1) {
        __asm__ __volatile__("hlt");
    }
}
