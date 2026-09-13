/*
    Copyright (C) 2010-2026, The AROS Development Team. All rights reserved.
*/

#include "intel_intern.h"

#include <proto/oop.h>
#include <proto/exec.h>
#include <aros/debug.h>
#include <aros/symbolsets.h>

#include <libdrm/arosdrm.h>
#include <intel_bufmgr.h>

/* GLOBALS */
APTR IntelMemPool;
struct SignalSemaphore globalLock;
struct CardData *globalcarddataptr;
/* GLOBALS END */

static ULONG Intel_Init(LIBBASETYPEPTR LIBBASE)
{
    struct OOP_ABDescr attrbases[] = 
    {
    { IID_Hidd,                 &LIBBASE->sd.hiddAttrBase },
    { IID_Hidd_BitMap,          &LIBBASE->sd.bitMapAttrBase },
    { IID_Hidd_PixFmt,          &LIBBASE->sd.pixFmtAttrBase },
    { IID_Hidd_Sync,            &LIBBASE->sd.syncAttrBase },
    { IID_Hidd_Gfx,             &LIBBASE->sd.gfxAttrBase },
    { IID_Hidd_Gfx_Intel,       &LIBBASE->sd.gfxIntelAttrBase },
    { IID_Hidd_PlanarBM,        &LIBBASE->sd.planarAttrBase },
    { IID_Hidd_GC,              &LIBBASE->sd.gcAttrBase },
    { IID_Hidd_Compositor,      &LIBBASE->sd.compositorAttrBase },
    { IID_Hidd_BitMap_Intel,    &LIBBASE->sd.bitMapIntelAttrBase },
    { NULL, NULL }
    };

    InitSemaphore(&globalLock);

    if (!OOP_ObtainAttrBases(attrbases))
        return FALSE;

    LIBBASE->sd.basegc = OOP_FindClass(CLID_Hidd_GC);
    LIBBASE->sd.basebm = OOP_FindClass(CLID_Hidd_BitMap);

    LIBBASE->sd.mid_CopyMemBox16    = OOP_GetMethodID(IID_Hidd_BitMap, moHidd_BitMap_CopyMemBox16);
    LIBBASE->sd.mid_CopyMemBox32    = OOP_GetMethodID(IID_Hidd_BitMap, moHidd_BitMap_CopyMemBox32);
    LIBBASE->sd.mid_PutMem32Image16 = OOP_GetMethodID(IID_Hidd_BitMap, moHidd_BitMap_PutMem32Image16);
    LIBBASE->sd.mid_GetMem32Image16 = OOP_GetMethodID(IID_Hidd_BitMap, moHidd_BitMap_GetMem32Image16);
    LIBBASE->sd.mid_PutMemTemplate16 = OOP_GetMethodID(IID_Hidd_BitMap, moHidd_BitMap_PutMemTemplate16);
    LIBBASE->sd.mid_PutMemTemplate32 = OOP_GetMethodID(IID_Hidd_BitMap, moHidd_BitMap_PutMemTemplate32);
    LIBBASE->sd.mid_PutMemPattern16  = OOP_GetMethodID(IID_Hidd_BitMap, moHidd_BitMap_PutMemPattern16);
    LIBBASE->sd.mid_PutMemPattern32  = OOP_GetMethodID(IID_Hidd_BitMap, moHidd_BitMap_PutMemPattern32);
    LIBBASE->sd.mid_ConvertPixels   = OOP_GetMethodID(IID_Hidd_BitMap, moHidd_BitMap_ConvertPixels);
    LIBBASE->sd.mid_GetPixFmt       = OOP_GetMethodID(IID_Hidd_Gfx, moHidd_Gfx_GetPixFmt);

    LIBBASE->sd.mid_BitMapPositionChanged =
        OOP_GetMethodID(IID_Hidd_Compositor, moHidd_Compositor_BitMapPositionChanged);
    LIBBASE->sd.mid_BitMapRectChanged =
        OOP_GetMethodID(IID_Hidd_Compositor, moHidd_Compositor_BitMapRectChanged);
    LIBBASE->sd.mid_ValidateBitMapPositionChange =
        OOP_GetMethodID(IID_Hidd_Compositor, moHidd_Compositor_ValidateBitMapPositionChange);
    LIBBASE->sd.mid_BitMapStackChanged =
        OOP_GetMethodID(IID_Hidd_Compositor, moHidd_Compositor_BitMapStackChanged);

    InitSemaphore(&LIBBASE->sd.multibitmapsemaphore);

    IntelMemPool = CreatePool(MEMF_PUBLIC | MEMF_CLEAR | MEMF_SEM_PROTECTED, 32 * 1024, 16 * 1024);
    if (!IntelMemPool)
        return FALSE;

    globalcarddataptr = &LIBBASE->sd.carddata;
    return TRUE;
}

static VOID Intel_Exit(LIBBASETYPEPTR LIBBASE)
{
    if (IntelMemPool)
    {
        DeletePool(IntelMemPool);
        IntelMemPool = NULL;
    }
}

APTR HIDDIntelAlloc(ULONG size)
{
    return AllocVecPooled(IntelMemPool, size);
}

VOID HIDDIntelFree(APTR memory)
{
    FreeVecPooled(IntelMemPool, memory);
}

IPTR HIDDIntelAllocSize(CONST_APTR memory)
{
    (void)memory;
    return 0;
}

int intel_init(void)
{
    struct CardData *carddata = globalcarddataptr;

    carddata->fd = 0;
    carddata->bufmgr = drm_intel_bufmgr_gem_init(0, 4096 * 4);
    if (!carddata->bufmgr)
        return -1;

    /* Initialize PCI and detect the GPU */
    if (intel_drm_pci_init())
    {
        bug("[Intel] PCI/GPU probe failed\n");
        /* No Intel GPU found - do not register this driver, otherwise
           it would hijack default_monitor on machines using another
           display adapter (e.g. NVIDIA) */
        return -1;
    }

    return 0;
}

ADD2INITLIB(Intel_Init, 0);
ADD2EXPUNGELIB(Intel_Exit, 0);
