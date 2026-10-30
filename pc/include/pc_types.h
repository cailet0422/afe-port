#ifndef PC_TYPES_H
#define PC_TYPES_H

/**
 * pc_types.h - Common type definitions for the PC port layer.
 * Included automatically via pc_platform.h.
 */

typedef unsigned char      u8;
typedef unsigned short     u16;
#ifdef __LP64__ /* long is 64-bit; matches types.h */
typedef unsigned int       u32;
#else
typedef unsigned long      u32;
#endif
typedef unsigned long long u64;
typedef signed char        s8;
typedef signed short       s16;
#ifdef __LP64__
typedef signed int         s32;
#else
typedef signed long        s32;
#endif
typedef signed long long   s64;
typedef float              f32;
typedef double             f64;
typedef int                BOOL;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#endif /* PC_TYPES_H */
