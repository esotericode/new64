/*
 * new64 -- compatibility shim for <PR/ultratypes.h>
 *
 * The N64 SDK header of this name defines the fixed-width scalar types that
 * every SM64-family source file expects (s8/u8/.../f32/f64).  This file is an
 * independent re-declaration of those *type names* so that decomp sources can
 * be compiled against the new64 engine on a modern host.  It contains no code
 * from any SDK or decompilation project.
 */
#ifndef NEW64_PR_ULTRATYPES_H
#define NEW64_PR_ULTRATYPES_H

#include <stdint.h>

typedef int8_t   s8;
typedef uint8_t  u8;
typedef int16_t  s16;
typedef uint16_t u16;
typedef int32_t  s32;
typedef uint32_t u32;
typedef int64_t  s64;
typedef uint64_t u64;

typedef float  f32;
typedef double f64;

#ifndef NULL
#define NULL ((void *) 0)
#endif

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#endif /* NEW64_PR_ULTRATYPES_H */
