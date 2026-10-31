#include "serial.h"
#include <stdarg.h>

#define PORT_COM1 0x3F8

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ __volatile__("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

void serial_init(void) {
    outb(PORT_COM1 + 1, 0x00);    // Disable all interrupts
    outb(PORT_COM1 + 3, 0x80);    // Enable DLAB (set baud rate divisor)
    outb(PORT_COM1 + 0, 0x03);    // Set divisor to 3 (lo byte) 38400 baud
    outb(PORT_COM1 + 1, 0x00);    //                  (hi byte)
    outb(PORT_COM1 + 3, 0x03);    // 8 bits, no parity, one stop bit
    outb(PORT_COM1 + 2, 0xC7);    // Enable FIFO, clear them, with 14-byte threshold
    outb(PORT_COM1 + 4, 0x0B);    // IRQs enabled, RTS/DSR set
}

static int is_transmit_empty(void) {
    return (int)(inb(PORT_COM1 + 5) & 0x20u);
}

int serial_has_char(void) {
    return (int)(inb(PORT_COM1 + 5) & 0x01u);
}

char serial_read_char(void) {
    while (!serial_has_char());
    return (char)inb(PORT_COM1);
}

static inline void serial_putc_raw(char c) {
    while (is_transmit_empty() == 0);
    outb(PORT_COM1, (uint8_t)c);
}

void serial_write_char(char c) {
    serial_putc_raw(c);
}

static uint8_t cmos_read(uint8_t reg) {
    outb(0x70, (uint8_t)(reg & 0x7Fu));
    return inb(0x71);
}

static uint8_t bcd_to_bin(uint8_t val) {
    return (uint8_t)((val & 0x0Fu) + ((val >> 4) * 10u));
}

static void rtc_get_time(uint8_t *h, uint8_t *m, uint8_t *s) {
    static uint8_t last_h = 0, last_m = 0, last_s = 0;

    int timeout = 2000;
    while ((cmos_read(0x0Au) & 0x80u) && --timeout > 0);

    uint8_t sec = cmos_read(0x00u);
    uint8_t min = cmos_read(0x02u);
    uint8_t hour = cmos_read(0x04u);
    uint8_t reg_b = cmos_read(0x0Bu);

    // BCD to binary
    if (!(reg_b & 0x04u)) {
        sec = bcd_to_bin(sec);
        min = bcd_to_bin(min);
        hour = (uint8_t)(bcd_to_bin(hour & 0x7Fu) | (hour & 0x80u));
    }

    // 12-hour to 24-hour if PM bit set
    if (!(reg_b & 0x02u) && (hour & 0x80u)) {
        hour = (uint8_t)(((hour & 0x7Fu) + 12u) % 24u);
    }

    if (sec < 60u && min < 60u && hour < 24u) {
        last_h = hour;
        last_m = min;
        last_s = sec;
    }

    *h = last_h;
    *m = last_m;
    *s = last_s;
}

static int s_at_line_start = 1;

static void serial_print_prefix_time(void) {
    uint8_t h = 0, m = 0, s = 0;
    rtc_get_time(&h, &m, &s);
    serial_putc_raw('[');
    serial_putc_raw((char)('0' + (h / 10u) % 10u));
    serial_putc_raw((char)('0' + (h % 10u)));
    serial_putc_raw(':');
    serial_putc_raw((char)('0' + (m / 10u) % 10u));
    serial_putc_raw((char)('0' + (m % 10u)));
    serial_putc_raw(':');
    serial_putc_raw((char)('0' + (s / 10u) % 10u));
    serial_putc_raw((char)('0' + (s % 10u)));
    serial_putc_raw(']');
    serial_putc_raw(' ');
}

static inline void check_line_start(void) {
    if (s_at_line_start) {
        s_at_line_start = 0;
        serial_print_prefix_time();
    }
}

static void serial_write_string_logged(const char* str) {
    if (!str) return;
    while (*str) {
        if (*str == '\n') {
            serial_putc_raw('\r');
            serial_putc_raw('\n');
            s_at_line_start = 1;
        } else {
            check_line_start();
            serial_putc_raw(*str);
        }
        str++;
    }
}

void serial_write_string(const char* str) {
    serial_write_string_logged(str);
}

static void write_uint(uint64_t val, int base) {
    check_line_start();
    char buf[32];
    int i = 0;
    if (val == 0) {
        serial_putc_raw('0');
        return;
    }
    while (val > 0) {
        uint64_t rem = val % (uint64_t)base;
        buf[i++] = (rem < 10) ? (char)('0' + rem) : (char)('a' + rem - 10);
        val /= (uint64_t)base;
    }
    for (int j = i - 1; j >= 0; j--) {
        serial_putc_raw(buf[j]);
    }
}

static void write_int(int64_t val) {
    check_line_start();
    if (val < 0) {
        serial_putc_raw('-');
        val = -val;
    }
    write_uint((uint64_t)val, 10);
}

void serial_printf(const char* format, ...) {
    if (!format) return;
    va_list args;
    va_start(args, format);

    while (*format) {
        if (*format == '%' && *(format + 1) != '\0') {
            format++;
            switch (*format) {
                case 'c': {
                    char c = (char)va_arg(args, int);
                    if (c == '\n') {
                        serial_putc_raw('\r');
                        serial_putc_raw('\n');
                        s_at_line_start = 1;
                    } else {
                        check_line_start();
                        serial_putc_raw(c);
                    }
                    break;
                }
                case 's': {
                    const char* s = va_arg(args, const char*);
                    if (s == NULL) s = "(null)";
                    serial_write_string_logged(s);
                    break;
                }
                case 'd': {
                    int64_t d = va_arg(args, int);
                    write_int(d);
                    break;
                }
                case 'u': {
                    uint64_t u = va_arg(args, unsigned int);
                    write_uint(u, 10);
                    break;
                }
                case 'x': {
                    uint64_t x = va_arg(args, unsigned int);
                    write_uint(x, 16);
                    break;
                }
                case 'p': {
                    uint64_t p = (uint64_t)va_arg(args, void*);
                    check_line_start();
                    serial_putc_raw('0');
                    serial_putc_raw('x');
                    write_uint(p, 16);
                    break;
                }
                case '%': {
                    check_line_start();
                    serial_putc_raw('%');
                    break;
                }
                default: {
                    check_line_start();
                    serial_putc_raw('%');
                    serial_putc_raw(*format);
                    break;
                }
            }
        } else {
            if (*format == '\n') {
                serial_putc_raw('\r');
                serial_putc_raw('\n');
                s_at_line_start = 1;
            } else {
                check_line_start();
                serial_putc_raw(*format);
            }
        }
        format++;
    }

    va_end(args);
}
