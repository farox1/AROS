/*
    Minimal stdarg.h for AROS cross-compiler compatibility.
    Defines va_list type and va_* macros via GCC built-ins.
*/

#ifndef _STDARG_H
#define _STDARG_H

#ifndef __GNUC_VA_LIST
#define __GNUC_VA_LIST
typedef __builtin_va_list __gnuc_va_list;
#endif

#ifndef _VA_LIST_DEFINED
typedef __builtin_va_list va_list;
#define _VA_LIST_DEFINED
#endif

#define va_start(v, l)      __builtin_va_start(v, l)
#define va_end(v)           __builtin_va_end(v)
#define va_arg(v, l)        __builtin_va_arg(v, l)
#define va_copy(d, s)       __builtin_va_copy(d, s)

#endif /* _STDARG_H */
