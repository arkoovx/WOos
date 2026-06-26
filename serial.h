#ifndef WOOS_SERIAL_H
#define WOOS_SERIAL_H

#include <stddef.h>
#include <stdint.h>

void serial_init(void);
void serial_write_char(char c);
void serial_write_string(const char* str);
void serial_printf(const char* format, ...);

#endif // WOOS_SERIAL_H
