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

void serial_write_char(char c) {
    while (is_transmit_empty() == 0);
    outb(PORT_COM1, (uint8_t)c);
}

void serial_write_string(const char* str) {
    if (!str) return;
    while (*str) {
        if (*str == '\n') {
            serial_write_char('\r');
        }
        serial_write_char(*str);
        str++;
    }
}

static void write_uint(uint64_t val, int base) {
    char buf[32];
    int i = 0;
    if (val == 0) {
        serial_write_char('0');
        return;
    }
    while (val > 0) {
        uint64_t rem = val % (uint64_t)base;
        buf[i++] = (rem < 10) ? (char)('0' + rem) : (char)('a' + rem - 10);
        val /= (uint64_t)base;
    }
    for (int j = i - 1; j >= 0; j--) {
        serial_write_char(buf[j]);
    }
}

static void write_int(int64_t val) {
    if (val < 0) {
        serial_write_char('-');
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
                    serial_write_char(c);
                    break;
                }
                case 's': {
                    const char* s = va_arg(args, const char*);
                    if (s == NULL) s = "(null)";
                    serial_write_string(s);
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
                    serial_write_string("0x");
                    write_uint(p, 16);
                    break;
                }
                case '%': {
                    serial_write_char('%');
                    break;
                }
                default: {
                    serial_write_char('%');
                    serial_write_char(*format);
                    break;
                }
            }
        } else {
            if (*format == '\n') {
                serial_write_char('\r');
            }
            serial_write_char(*format);
        }
        format++;
    }

    va_end(args);
}
