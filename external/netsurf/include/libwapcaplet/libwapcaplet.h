#ifndef LIBWAPCAPLET_LIBWAPCAPLET_H
#define LIBWAPCAPLET_LIBWAPCAPLET_H

#include "../stdint.h"
#include "../stddef.h"
#include "../stdbool.h"

typedef struct lwc_string_s lwc_string;

typedef enum lwc_error_e {
    LWC_ERROR_OK = 0,
    LWC_ERROR_OOM = 1
} lwc_error;

#endif
