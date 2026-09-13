/*
    Copyright 2009-2026, The AROS Development Team. All rights reserved.
*/

#ifndef _DRM_COMPAT_MEM_
#define _DRM_COMPAT_MEM_

#ifndef _VA_LIST_DEFINED
typedef __builtin_va_list __gnuc_va_list;
typedef __gnuc_va_list va_list;
#define _VA_LIST_DEFINED
#endif

#include "drm_compat_types.h"

APTR HIDDIntelAlloc(ULONG size);
VOID HIDDIntelFree(APTR memory);
IPTR HIDDIntelAllocSize(CONST_APTR memory);

#define gfp_t           ULONG
#define GFP_KERNEL      (1UL << 0)
#define __GFP_ZERO      (1UL << 1)
#define GFP_DMA32       (1UL << 2)
#define GFP_HIGHUSER    (1UL << 3)
#define GFP_USER        (1UL << 4)
#define GFP_NOWAIT      (1UL << 5)
#define GFP_ATOMIC      (1UL << 6)


#define kcalloc(count, size, flags)     HIDDIntelAlloc((count) * (size))
#define kzalloc(size, flags)            HIDDIntelAlloc(size)
#define kmalloc_array(n, size, flags)   kmalloc((n) * (size), flags)
#define kfree(objp)                     HIDDIntelFree((APTR)objp)
#define ksize(objp)                     HIDDIntelAllocSize(objp)

#define vmalloc_user(size)              HIDDIntelAlloc(size)
#define vmalloc(size)                   HIDDIntelAlloc(size)
#define vzalloc(size)                   vmalloc(size)
#define vfree(objp)                     HIDDIntelFree(objp)

#define kvmalloc(size, flags)           HIDDIntelAlloc(size)
#define kvcalloc(count, size, flags)    HIDDIntelAlloc((count) * (size))
#define kvmalloc_array(n, size, flags)  kvmalloc((n) * (size), flags)
#define kvzalloc(size, flags)           kvmalloc(size, flags | __GFP_ZERO)
#define kvfree(objp)                    HIDDIntelFree(objp)

void *kmalloc(size_t size, gfp_t flags);
void *krealloc(const void *ptr, size_t size, gfp_t flags);

char *kvasprintf(gfp_t, const char *, va_list);
char *kasprintf(gfp_t, const char *, ...);

#endif /* _DRM_COMPAT_MEM_ */
