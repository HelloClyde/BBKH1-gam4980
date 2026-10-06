/* Source compatibility only: the core uses types and byte copies, no 9588 services. */
#ifndef GAM_H1_COMPAT_H
#define GAM_H1_COMPAT_H
#include <stdint.h>
#include <string.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
#define bda_memset memset
#define bda_memcpy memcpy
#endif
