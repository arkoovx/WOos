// Freestanding WoOS kernel (x86_64)

__attribute__((used)) static const char* magic = "KERNEL_START_MARKER";

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;

typedef struct video_info {
    uint64_t framebuffer;
    uint16_t pitch;
    uint16_t width;
    uint16_t height;
    uint8_t  bpp;
    uint8_t  reserved;
} video_info_t;

static void fill_screen(video_info_t* info, uint32_t color) {
    uint8_t* base = (uint8_t*)(uint64_t)info->framebuffer;
    uint16_t y = 0;

    while (y < info->height) {
        uint32_t* row = (uint32_t*)(base + ((uint64_t)y * info->pitch));
        uint16_t x = 0;
        while (x < info->width) {
            row[x] = color;
            x++;
        }
        y++;
    }
}

static void draw_square(video_info_t* info, uint16_t size, uint32_t color) {
    uint8_t* base = (uint8_t*)(uint64_t)info->framebuffer;
    uint16_t y = 0;

    while (y < size && y < info->height) {
        uint32_t* row = (uint32_t*)(base + ((uint64_t)y * info->pitch));
        uint16_t x = 0;
        while (x < size && x < info->width) {
            row[x] = color;
            x++;
        }
        y++;
    }
}

void kmain(video_info_t* video) {
    // 0x000033 = dark blue in 0x00RRGGBB layout.
    fill_screen(video, 0x000033u);

    // Draw 50x50 white square in top-left corner.
    draw_square(video, 50, 0xFFFFFFu);

    while (1) {
        __asm__ __volatile__("hlt");
    }
}
