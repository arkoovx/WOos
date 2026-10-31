#include "idt.h"
#include "serial.h"

#define PIC1_COMMAND 0x20u
#define PIC1_DATA    0x21u
#define PIC2_COMMAND 0xA0u
#define PIC2_DATA    0xA1u
#define PIC_EOI      0x20u

#define PIC_ICW1_ICW4 0x01u
#define PIC_ICW1_INIT 0x10u
#define PIC_ICW4_8086 0x01u

#define IRQ_VECTOR_BASE_MASTER 32u
#define IRQ_VECTOR_BASE_SLAVE  40u
#define IRQ_KEYBOARD_VECTOR    (IRQ_VECTOR_BASE_MASTER + 1u)
#define IRQ_MOUSE_VECTOR       (IRQ_VECTOR_BASE_SLAVE + 4u)

typedef struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed)) idt_entry_t;

typedef struct idtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) idtr_t;

extern void idt_load(const idtr_t* idtr);
extern void idt_stub_ignore(void);
extern void idt_stub_ignore_errcode(void);

// CPU exception stubs
extern void idt_stub_exception0(void);
extern void idt_stub_exception1(void);
extern void idt_stub_exception2(void);
extern void idt_stub_exception3(void);
extern void idt_stub_exception4(void);
extern void idt_stub_exception5(void);
extern void idt_stub_exception6(void);
extern void idt_stub_exception7(void);
extern void idt_stub_exception8(void);
extern void idt_stub_exception9(void);
extern void idt_stub_exception10(void);
extern void idt_stub_exception11(void);
extern void idt_stub_exception12(void);
extern void idt_stub_exception13(void);
extern void idt_stub_exception14(void);
extern void idt_stub_exception15(void);
extern void idt_stub_exception16(void);
extern void idt_stub_exception17(void);
extern void idt_stub_exception18(void);
extern void idt_stub_exception19(void);
extern void idt_stub_exception20(void);
extern void idt_stub_exception21(void);
extern void idt_stub_exception22(void);
extern void idt_stub_exception23(void);
extern void idt_stub_exception24(void);
extern void idt_stub_exception25(void);
extern void idt_stub_exception26(void);
extern void idt_stub_exception27(void);
extern void idt_stub_exception28(void);
extern void idt_stub_exception29(void);
extern void idt_stub_exception30(void);
extern void idt_stub_exception31(void);

// IRQ stubs
extern void idt_stub_irq0(void);
extern void idt_stub_irq1(void);
extern void idt_stub_irq2(void);
extern void idt_stub_irq3(void);
extern void idt_stub_irq4(void);
extern void idt_stub_irq5(void);
extern void idt_stub_irq6(void);
extern void idt_stub_irq7(void);
extern void idt_stub_irq8(void);
extern void idt_stub_irq9(void);
extern void idt_stub_irq10(void);
extern void idt_stub_irq11(void);
extern void idt_stub_irq12(void);
extern void idt_stub_irq13(void);
extern void idt_stub_irq14(void);
extern void idt_stub_irq15(void);
extern void idt_stub_syscall(void);

static idt_entry_t g_idt[256];
static uint8_t g_idt_ready = 0;
static uint32_t g_keyboard_irq_count = 0u;
static uint32_t g_mouse_irq_count = 0u;

static void (*const g_exception_stubs[32])(void) = {
    idt_stub_exception0, idt_stub_exception1, idt_stub_exception2, idt_stub_exception3,
    idt_stub_exception4, idt_stub_exception5, idt_stub_exception6, idt_stub_exception7,
    idt_stub_exception8, idt_stub_exception9, idt_stub_exception10, idt_stub_exception11,
    idt_stub_exception12, idt_stub_exception13, idt_stub_exception14, idt_stub_exception15,
    idt_stub_exception16, idt_stub_exception17, idt_stub_exception18, idt_stub_exception19,
    idt_stub_exception20, idt_stub_exception21, idt_stub_exception22, idt_stub_exception23,
    idt_stub_exception24, idt_stub_exception25, idt_stub_exception26, idt_stub_exception27,
    idt_stub_exception28, idt_stub_exception29, idt_stub_exception30, idt_stub_exception31
};

static void (*const g_irq_stubs[16])(void) = {
    idt_stub_irq0,
    idt_stub_irq1,
    idt_stub_irq2,
    idt_stub_irq3,
    idt_stub_irq4,
    idt_stub_irq5,
    idt_stub_irq6,
    idt_stub_irq7,
    idt_stub_irq8,
    idt_stub_irq9,
    idt_stub_irq10,
    idt_stub_irq11,
    idt_stub_irq12,
    idt_stub_irq13,
    idt_stub_irq14,
    idt_stub_irq15,
};


static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ __volatile__("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t ps2_status(void) {
    return inb(0x64u);
}

static inline uint8_t ps2_data(void) {
    return inb(0x60u);
}

static inline void io_wait(void) {
    __asm__ __volatile__("outb %%al, $0x80" : : "a"(0u));
}

static void pic_remap(void) {
    uint8_t mask_master = inb(PIC1_DATA);
    uint8_t mask_slave = inb(PIC2_DATA);

    outb(PIC1_COMMAND, PIC_ICW1_INIT | PIC_ICW1_ICW4);
    io_wait();
    outb(PIC2_COMMAND, PIC_ICW1_INIT | PIC_ICW1_ICW4);
    io_wait();

    outb(PIC1_DATA, IRQ_VECTOR_BASE_MASTER);
    io_wait();
    outb(PIC2_DATA, IRQ_VECTOR_BASE_SLAVE);
    io_wait();

    outb(PIC1_DATA, 4u);
    io_wait();
    outb(PIC2_DATA, 2u);
    io_wait();

    outb(PIC1_DATA, PIC_ICW4_8086);
    io_wait();
    outb(PIC2_DATA, PIC_ICW4_8086);
    io_wait();

    outb(PIC1_DATA, mask_master);
    outb(PIC2_DATA, mask_slave);
}

static void pic_send_eoi(uint8_t vector) {
    if (vector >= IRQ_VECTOR_BASE_SLAVE && vector < (IRQ_VECTOR_BASE_SLAVE + 8u)) {
        outb(PIC2_COMMAND, PIC_EOI);
    }
    outb(PIC1_COMMAND, PIC_EOI);
}

static void idt_set_gate_dpl(uint8_t vector, void (*handler)(void), uint8_t dpl) {
    uint64_t addr = (uint64_t)handler;

    g_idt[vector].offset_low = (uint16_t)(addr & 0xFFFFu);
    g_idt[vector].selector = 0x18u;
    g_idt[vector].ist = 0;
    g_idt[vector].type_attr = 0x8Eu | (uint8_t)((dpl & 3u) << 5u);
    g_idt[vector].offset_mid = (uint16_t)((addr >> 16) & 0xFFFFu);
    g_idt[vector].offset_high = (uint32_t)((addr >> 32) & 0xFFFFFFFFu);
    g_idt[vector].zero = 0;
}

static void idt_set_gate(uint8_t vector, void (*handler)(void)) {
    idt_set_gate_dpl(vector, handler, 0);
}

static const char* const g_exception_names[32] = {
    "Divide-by-zero",
    "Debug",
    "Non-maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack-Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 Floating-Point Exception",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating-Point Exception",
    "Virtualization Exception",
    "Control Protection Exception",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved",
    "VMM Communication Exception",
    "Security Exception",
    "Reserved"
};

static void print_hex64(uint64_t val) {
    serial_write_string("0x");
    char hex_chars[] = "0123456789ABCDEF";
    for (int i = 60; i >= 0; i -= 4) {
        serial_write_char(hex_chars[(val >> i) & 0xF]);
    }
}

void idt_handle_exception(registers_t* regs) {
    __asm__ __volatile__("cli");

    const char* name = "Unknown Exception";
    if (regs->vector < 32) {
        name = g_exception_names[regs->vector];
    }

    serial_write_string("\n==================================================\n");
    serial_write_string("!!! WoOS KERNEL PANIC: CPU EXCEPTION: ");
    serial_printf("%d (%s) !!!\n", (int)regs->vector, name);
    serial_write_string("==================================================\n");
    
    serial_write_string("RIP:    "); print_hex64(regs->rip);
    serial_write_string("   CS:     "); print_hex64(regs->cs);
    serial_write_string("\n");
    
    serial_write_string("RFLAGS: "); print_hex64(regs->rflags);
    serial_write_string("   RSP:    "); print_hex64(regs->rsp);
    serial_write_string("\n");
    
    serial_write_string("SS:     "); print_hex64(regs->ss);
    serial_write_string("   ERRCOD: "); print_hex64(regs->error_code);
    serial_write_string("\n");

    if (regs->vector == 14) {
        uint64_t cr2;
        __asm__ __volatile__("mov %%cr2, %0" : "=r"(cr2));
        serial_write_string("Page Fault Address (CR2): ");
        print_hex64(cr2);
        serial_write_string("\n");
    }

    serial_write_string("\nGeneral Purpose Registers:\n");
    serial_write_string("RAX: "); print_hex64(regs->rax);
    serial_write_string("   RBX: "); print_hex64(regs->rbx);
    serial_write_string("   RCX: "); print_hex64(regs->rcx);
    serial_write_string("\n");
    
    serial_write_string("RDX: "); print_hex64(regs->rdx);
    serial_write_string("   RSI: "); print_hex64(regs->rsi);
    serial_write_string("   RDI: "); print_hex64(regs->rdi);
    serial_write_string("\n");
    
    serial_write_string("RBP: "); print_hex64(regs->rbp);
    serial_write_string("   R8:  "); print_hex64(regs->r8);
    serial_write_string("   R9:  "); print_hex64(regs->r9);
    serial_write_string("\n");
    
    serial_write_string("R10: "); print_hex64(regs->r10);
    serial_write_string("   R11: "); print_hex64(regs->r11);
    serial_write_string("   R12: "); print_hex64(regs->r12);
    serial_write_string("\n");
    
    serial_write_string("R13: "); print_hex64(regs->r13);
    serial_write_string("   R14: "); print_hex64(regs->r14);
    serial_write_string("   R15: "); print_hex64(regs->r15);
    serial_write_string("\n");
    
    serial_write_string("==================================================\n");
    serial_write_string("System halted. Please reboot the virtual machine.\n");
    serial_write_string("==================================================\n");

    while (1) {
        __asm__ __volatile__("hlt");
    }
}

void idt_init(void) {
    for (uint16_t i = 0; i < 256; i++) {
        idt_set_gate((uint8_t)i, idt_stub_ignore);
    }

    // Заполняем первые 32 вектора обработчиками исключений CPU
    for (uint8_t i = 0; i < 32; i++) {
        idt_set_gate(i, g_exception_stubs[i]);
    }

    for (uint8_t irq = 0; irq < 16u; irq++) {
        idt_set_gate((uint8_t)(IRQ_VECTOR_BASE_MASTER + irq), g_irq_stubs[irq]);
    }

    // Регистрируем системный вызов int 0x80 с DPL = 3
    idt_set_gate_dpl(0x80, idt_stub_syscall, 3);

    pic_remap();

    // Разве маскируем только то, что нам нужно:
    // PIT (IRQ0), Keyboard (IRQ1), Cascade (IRQ2) -> Master = 0xF8 (1111 1000)
    // Mouse (IRQ12) -> Slave = 0xEF (1110 1111)
    outb(PIC1_DATA, 0xF8u);
    outb(PIC2_DATA, 0xEFu);

    idtr_t idtr;
    idtr.limit = (uint16_t)(sizeof(g_idt) - 1u);
    idtr.base = (uint64_t)&g_idt[0];

    idt_load(&idtr);
    g_idt_ready = 1u;
}

void idt_enable_interrupts(void) {
    __asm__ __volatile__("sti");
}

extern void timer_handler(void);
extern void mouse_handler(void);

#include "input.h"

static const uint16_t kbd_us_lower[128] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
  '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
     0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
     0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',   0,
   '*',   0, ' ',   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
};

static const uint16_t kbd_us_upper[128] = {
    0,  27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
  '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
     0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
     0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?',   0,
   '*',   0, ' ',   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
};

static const uint16_t kbd_ru_lower[128] = {
    0,      27,     '1',    '2',    '3',    '4',    '5',    '6',
    '7',    '8',    '9',    '0',    '-',    '=',    '\b',   '\t',
    0x0439, 0x0446, 0x0443, 0x043A, 0x0435, 0x043D, 0x0433, 0x0448, // й ц у к е н г ш
    0x0449, 0x0437, 0x0445, 0x044A, '\n',   0,      0x0444, 0x044B, // щ з х ъ \n   ф ы
    0x0432, 0x0430, 0x043F, 0x0440, 0x043E, 0x043B, 0x0434, 0x0436, // в а п р о л д ж
    0x044D, 0x0451, 0,      '\\',   0x044F, 0x0447, 0x0441, 0x043C, // э ё   \  я ч с м
    0x0438, 0x0442, 0x044C, 0x0431, 0x044E, '.',    0,      '*',    // и т ь б ю .
    0,      ' ',    0,      0,      0,      0,      0,      0
};

static const uint16_t kbd_ru_upper[128] = {
    0,      27,     '!',    '"',    0x2116, ';',    '%',    ':',    // ! " № ; % :
    '?',    '*',    '(',    ')',    '_',    '+',    '\b',   '\t',
    0x0419, 0x0426, 0x0423, 0x041A, 0x0415, 0x041D, 0x0413, 0x0428, // Й Ц У К Е Н Г Ш
    0x0429, 0x0417, 0x0425, 0x042A, '\n',   0,      0x0424, 0x042B, // Щ З Х Ъ \n   Ф Ы
    0x0412, 0x0410, 0x041F, 0x0420, 0x041E, 0x041B, 0x0414, 0x0416, // В А П Р О Л Д Ж
    0x042D, 0x0401, 0,      '/',    0x042F, 0x0427, 0x0421, 0x041C, // Э Ё   /  Я Ч С М
    0x0418, 0x0422, 0x042C, 0x0411, 0x042E, ',',    0,      '*',    // И Т Ь Б Ю ,
    0,      ' ',    0,      0,      0,      0,      0,      0
};

static uint8_t g_kbd_shift = 0;
static uint8_t g_kbd_alt = 0;
static uint8_t g_kbd_ctrl = 0;
static uint8_t g_kbd_caps_lock = 0;
static uint8_t g_kbd_extended = 0;
static uint8_t g_kbd_layout = 0; // 0 = EN, 1 = RU

uint8_t idt_get_keyboard_layout(void) {
    return g_kbd_layout;
}

void idt_handle_irq(uint32_t vector) {
    if (vector == IRQ_VECTOR_BASE_MASTER + 0u) {
        // Обрабатывается планировщиком в idt_stub_irq0 напрямую
    } else if (vector == IRQ_KEYBOARD_VECTOR) {
        if (ps2_status() & 0x01u) {
            uint8_t sc = ps2_data();
            if (sc == 0xE0) {
                g_kbd_extended = 1;
            } else if (sc == 0x2A || sc == 0x36) {
                if (g_kbd_alt) {
                    // Alt+Shift combination -> Toggle Layout
                    g_kbd_layout ^= 1;
                }
                g_kbd_shift = 1;
                g_kbd_extended = 0;
                input_event_t ev;
                ev.type = INPUT_EVENT_KEY_DOWN;
                ev.x = 0;
                ev.y = (uint16_t)sc;
                ev.buttons = (uint8_t)(g_kbd_shift | (g_kbd_ctrl << 1) | (g_kbd_caps_lock << 2) | (g_kbd_alt << 3) | (g_kbd_layout << 4));
                input_push(&ev);
            } else if (sc == 0xAA || sc == 0xB6) {
                g_kbd_shift = 0;
                g_kbd_extended = 0;
            } else if (sc == 0x38) {
                if (g_kbd_shift) {
                    // Shift+Alt combination -> Toggle Layout
                    g_kbd_layout ^= 1;
                }
                g_kbd_alt = 1;
                g_kbd_extended = 0;
                input_event_t ev;
                ev.type = INPUT_EVENT_KEY_DOWN;
                ev.x = 0;
                ev.y = (uint16_t)sc;
                ev.buttons = (uint8_t)(g_kbd_shift | (g_kbd_ctrl << 1) | (g_kbd_caps_lock << 2) | (g_kbd_alt << 3) | (g_kbd_layout << 4));
                input_push(&ev);
            } else if (sc == 0xB8) {
                g_kbd_alt = 0;
                g_kbd_extended = 0;
            } else if (sc == 0x1D) {
                g_kbd_ctrl = 1;
                g_kbd_extended = 0;
            } else if (sc == 0x9D) {
                g_kbd_ctrl = 0;
                g_kbd_extended = 0;
            } else if (sc == 0x3A) {
                // Caps Lock toggle
                g_kbd_caps_lock ^= 1;
                g_kbd_extended = 0;
            } else if (!(sc & 0x80)) {
                uint16_t codepoint = 0;
                if (!g_kbd_extended && sc < 128) {
                    if (g_kbd_ctrl) {
                        uint16_t us_low = kbd_us_lower[sc];
                        if (us_low >= 'a' && us_low <= 'z') {
                            codepoint = (uint16_t)(us_low - 'a' + 1);
                        } else if (us_low >= 'A' && us_low <= 'Z') {
                            codepoint = (uint16_t)(us_low - 'A' + 1);
                        }
                    } else if (g_kbd_layout == 1) {
                        uint16_t lower = kbd_ru_lower[sc];
                        uint16_t upper = kbd_ru_upper[sc];
                        int is_letter = ((lower >= 0x0430 && lower <= 0x044F) || (lower == 0x0451));
                        int use_upper = is_letter ? (g_kbd_shift ^ g_kbd_caps_lock) : g_kbd_shift;
                        codepoint = use_upper ? upper : lower;
                    } else {
                        uint16_t lower = kbd_us_lower[sc];
                        uint16_t upper = kbd_us_upper[sc];
                        int is_letter = (lower >= 'a' && lower <= 'z');
                        int use_upper = is_letter ? (g_kbd_shift ^ g_kbd_caps_lock) : g_kbd_shift;
                        codepoint = use_upper ? upper : lower;
                    }
                }
                if (codepoint != 0 || sc == 0x48 || sc == 0x50 || sc == 0x4B || sc == 0x4D ||
                    sc == 0x47 || sc == 0x4F || sc == 0x49 || sc == 0x51 || sc == 0x53 || sc == 0x52) {
                    input_event_t ev;
                    ev.type = INPUT_EVENT_KEY_DOWN;
                    ev.x = codepoint;
                    ev.y = (uint16_t)sc;
                    ev.buttons = (uint8_t)(g_kbd_shift | (g_kbd_ctrl << 1) | (g_kbd_caps_lock << 2) | (g_kbd_alt << 3) | (g_kbd_layout << 4));
                    input_push(&ev);
                }
                g_kbd_extended = 0;
            } else {
                g_kbd_extended = 0;
            }
        }
        g_keyboard_irq_count++;
    } else if (vector == IRQ_MOUSE_VECTOR) {
        mouse_handler();
        g_mouse_irq_count++;
    }

    pic_send_eoi((uint8_t)vector);
}


uint8_t idt_is_ready(void) {
    return g_idt_ready;
}

uint32_t idt_keyboard_irq_count(void) {
    return g_keyboard_irq_count;
}

uint32_t idt_mouse_irq_count(void) {
    return g_mouse_irq_count;
}
