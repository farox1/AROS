#ifndef _INTEL_INTERN_H
#define _INTEL_INTERN_H
/*
    Copyright (C) 2010-2026, The AROS Development Team. All rights reserved.
*/

#include <exec/semaphores.h>
#include <hidd/gfx.h>
#include <hidd/i2c.h>
#include <stdarg.h>

#include <intel_bufmgr.h>

#include LC_LIBDEFS_FILE

#include "compositor.h"

#define CLID_Hidd_Gfx_Intel           "hidd.gfx.intel"
#define IID_Hidd_Gfx_Intel            "hidd.gfx.intel"

#define HiddGfxIntelAttrBase          __IHidd_Gfx_Intel

#ifndef __OOP_NOATTRBASES__
extern OOP_AttrBase HiddGfxIntelAttrBase;
#endif

extern struct SignalSemaphore globalLock;

struct HIDDIntelData
{
    drm_intel_bo        *cursor;
    ULONG               selectedcrtcid;
    APTR                selectedmode;
    APTR                selectedconnector;
    OOP_Object          *compositor;
};

#define CLID_Hidd_BitMap_Intel        "hidd.bitmap.intel"
#define IID_Hidd_BitMap_Intel         "hidd.bitmap.intel"

#define HiddBitMapIntelAttrBase __IHidd_BitMap_Intel

#ifndef __OOP_NOATTRBASES__
extern OOP_AttrBase HiddBitMapIntelAttrBase;
#endif

enum
{
    aoHidd_BitMap_Intel_CompositorHidd,       /* [I..] The compositor object that will be used by bitmap */
    
    num_Hidd_BitMap_Intel_Attrs
};

#define aHidd_BitMap_Intel_CompositorHidd    (HiddBitMapIntelAttrBase + aoHidd_BitMap_Intel_CompositorHidd)

#define IS_BITMAPINTEL_ATTR(attr, idx) \
    (((idx) = (attr) - HiddBitMapIntelAttrBase) < num_Hidd_BitMap_Intel_Attrs)

struct HIDDIntelBitMapData
{
    struct SignalSemaphore  semaphore;
    drm_intel_bo            *bo; /* Buffer object behind bitmap */

    ULONG   pitch;          /* Width of single data row in bytes */
    UBYTE   bytesperpixel;  /* In bytes, how many bytes to store a pixel */
    struct
    {
        ULONG height;           /* Height of bitmap in pixels */
        ULONG width;            /* Width of bitmap in pixels */
        UBYTE bitsPerPixel;     /* In bits, how many bits used to represent the color */
        UBYTE depth;            /* In bits, how many bits used to represent the color */
    } drawable;

    BOOL    displayable;    /* Can bitmap be displayed on screen */
    
    /* Information connected with display */
    OOP_Object  *compositor;   /* Compositor object used by bitmap */
    LONG        xoffset;        /* Offset to bitmap point that is displayed as (0,0) on screen */
    LONG        yoffset;        /* Offset to bitmap point that is displayed as (0,0) on screen */
    ULONG       fbid;           /* Contains ID under which bitmap 
                                          is registered as framebuffer or 
                                          0 otherwise */
};

#define CLID_Hidd_I2C_Intel           "hidd.i2c.intel"
#define IID_Hidd_I2C_Intel            "hidd.i2c.intel"

#define HiddI2CIntelAttrBase __IHidd_I2C_Intel

#ifndef __OOP_NOATTRBASES__
extern OOP_AttrBase HiddI2CIntelAttrBase;
#endif

/* Forward declaration */
struct IntelBatchState;

enum
{
    aoHidd_I2C_Intel_Adapter,     /* [I..] The i2c_adapter object */
    
    num_Hidd_I2C_Intel_Attrs
};

#define aHidd_I2C_Intel_Adapter          (HiddI2CIntelAttrBase + aoHidd_I2C_Intel_Adapter)

#define IS_I2CINTEL_ATTR(attr, idx) \
    (((idx) = (attr) - HiddI2CIntelAttrBase) < num_Hidd_I2C_Intel_Attrs)

struct HIDDIntelI2CData
{
    IPTR i2c_adapter;
};

struct IntelBatchState
{
    ULONG   batch_ptr[4096];
    ULONG   batch_used;
    ULONG   batch_emit_start;
    ULONG   batch_emitting;
    drm_intel_bo *batch_bo;
    drm_intel_bo *last_batch_bo[2];
    BOOL    in_batch_atomic;
    int     batch_atomic_limit;
    int     current_batch;
    int     chipset_gen;
    BOOL    force_fallback;
    LONG    currentRop;
};

struct CardData
{
    /* Card controlling objects */
    ULONG                   Generation;
    BOOL                    IsPCIE;

    LONG                    currentRop;

    int                     fd;                     /* DRM file descriptor */
    drm_intel_bufmgr        *bufmgr;                 /* Intel buffer manager */

    struct IntelBatchState  batch;                   /* Batch buffer state */
};

struct staticdata
{
    OOP_Class       *basegc;            /* baseclass for CreateObject */
    OOP_Class       *basebm;            /* baseclass for CreateObject */
    OOP_Class       *basei2c;

    OOP_Class       *gfxclass;
    OOP_Class       *bmclass;
    OOP_Class       *i2cclass;
    OOP_Class       *compositorclass;

    OOP_AttrBase    hiddAttrBase;    
    OOP_AttrBase    pixFmtAttrBase;
    OOP_AttrBase    gfxAttrBase;
    OOP_AttrBase    gfxIntelAttrBase;
    OOP_AttrBase    syncAttrBase;
    OOP_AttrBase    bitMapAttrBase;
    OOP_AttrBase    planarAttrBase;
    OOP_AttrBase    i2cIntelAttrBase;
    OOP_AttrBase    gcAttrBase;
    OOP_AttrBase    compositorAttrBase;
    OOP_AttrBase    bitMapIntelAttrBase;
    
    OOP_MethodID    mid_CopyMemBox16;
    OOP_MethodID    mid_CopyMemBox32;
    OOP_MethodID    mid_PutMem32Image16;
    OOP_MethodID    mid_GetMem32Image16;
    OOP_MethodID    mid_PutMemTemplate16;
    OOP_MethodID    mid_PutMemTemplate32;
    OOP_MethodID    mid_PutMemPattern16;
    OOP_MethodID    mid_PutMemPattern32;
    OOP_MethodID    mid_ConvertPixels;
    OOP_MethodID    mid_GetPixFmt;
    
    OOP_MethodID    mid_BitMapPositionChanged;
    OOP_MethodID    mid_BitMapRectChanged;
    OOP_MethodID    mid_ValidateBitMapPositionChange;

    struct CardData carddata;
    
    struct SignalSemaphore multibitmapsemaphore;
};

LIBBASETYPE 
{
    struct Library      base;
    struct staticdata   sd;
};

#define METHOD(base, id, name) \
  base ## __ ## id ## __ ## name (OOP_Class *cl, OOP_Object *o, struct p ## id ## _ ## name *msg)

#define BASE(lib)                   ((LIBBASETYPEPTR)(lib))

#define SD(cl)                      (&BASE(cl->UserData)->sd)

#define LOCK_ENGINE                 { ObtainSemaphore(&globalLock); }
#define UNLOCK_ENGINE               { ReleaseSemaphore(&globalLock); }

#define LOCK_BITMAP                 { ObtainSemaphore(&bmdata->semaphore); }
#define UNLOCK_BITMAP               { ReleaseSemaphore(&bmdata->semaphore); }

#define LOCK_BITMAP_BM(bmdata)      { ObtainSemaphore(&(bmdata)->semaphore); }
#define UNLOCK_BITMAP_BM(bmdata)    { ReleaseSemaphore(&(bmdata)->semaphore); }

#define LOCK_MULTI_BITMAP           { ObtainSemaphore(&(SD(cl))->multibitmapsemaphore); }
#define UNLOCK_MULTI_BITMAP         { ReleaseSemaphore(&(SD(cl))->multibitmapsemaphore); }

#define MAP_BUFFER                  { if (!bmdata->bo->virtual) drm_intel_gem_bo_map_gtt(bmdata->bo); }

#define IS_INTEL_BM_CLASS(x)        ((x) == SD(cl)->bmclass)

#define writel(val, addr)           (*(volatile ULONG*)(addr) = (val))
#define readl(addr)                 (*(volatile ULONG*)(addr))
#define writew(val, addr)           (*(volatile UWORD*)(addr) = (val))
#define readw(addr)                 (*(volatile UWORD*)(addr))

/* Intel Graphics Generations */
#define INTEL_GEN2      2
#define INTEL_GEN3      3
#define INTEL_GEN4      4
#define INTEL_GEN5      5
#define INTEL_GEN6      6
#define INTEL_GEN7      7
#define INTEL_GEN8      8
#define INTEL_GEN9      9
#define INTEL_GEN10     10
#define INTEL_GEN11     11
#define INTEL_GEN12     12

/* Generation check macros */
#define IS_GENx(intel, X) ((intel)->Generation >= 8*(X) && (intel)->Generation < 8*((X)+1))
#define IS_GEN2(intel)    IS_GENx(intel, 2)
#define IS_GEN3(intel)    IS_GENx(intel, 3)
#define IS_GEN4(intel)    IS_GENx(intel, 4)
#define IS_GEN5(intel)    IS_GENx(intel, 5)
#define IS_GEN6(intel)    IS_GENx(intel, 6)
#define IS_GEN7(intel)    IS_GENx(intel, 7)

/* Has BLT engine */
#define HAS_BLT(intel)    ((intel)->Generation >= 060)

APTR HIDDIntelAlloc(ULONG size);
VOID HIDDIntelFree(APTR memory);

/* intel_hiddclass.c functions */
VOID HIDDIntelShowCursor(OOP_Object *gfx, BOOL visible);
BOOL HIDDIntelSwitchToVideoMode(OOP_Object *bm);
VOID HIDDIntelSetOffsets(OOP_Object *bm, LONG newxoffset, LONG newyoffset);

/* intel_accel.c */
BOOL HiddIntelWriteFromRAM(
    APTR src, ULONG srcPitch, HIDDT_StdPixFmt srcPixFmt,
    APTR dst, ULONG dstPitch,
    ULONG width, ULONG height,
    OOP_Class *cl, OOP_Object *o);
BOOL HiddIntelReadIntoRAM(
    APTR src, ULONG srcPitch, 
    APTR dst, ULONG dstPitch, HIDDT_StdPixFmt dstPixFmt,
    ULONG width, ULONG height,
    OOP_Class *cl, OOP_Object *o);

/* Software rendering functions */
VOID HIDDIntelBitMapPutAlphaImage32(struct HIDDIntelBitMapData *bmdata,
    APTR srcbuff, ULONG srcpitch, LONG destX, LONG destY, LONG width, LONG height);
VOID HIDDIntelBitMapPutAlphaImage16(struct HIDDIntelBitMapData *bmdata,
    APTR srcbuff, ULONG srcpitch, LONG destX, LONG destY, LONG width, LONG height);
VOID HIDDIntelBitMapPutAlphaTemplate32(struct HIDDIntelBitMapData *bmdata,
    OOP_Object *gc, OOP_Object *bm, BOOL invertalpha,
    UBYTE *srcalpha, ULONG srcpitch, LONG destX, LONG destY, LONG width, LONG height);
VOID HIDDIntelBitMapPutAlphaTemplate16(struct HIDDIntelBitMapData *bmdata,
    OOP_Object *gc, OOP_Object *bm, BOOL invertalpha,
    UBYTE *srcalpha, ULONG srcpitch, LONG destX, LONG destY, LONG width, LONG height);
VOID HIDDIntelBitMapDrawSolidLine(struct HIDDIntelBitMapData *bmdata,
    OOP_Object *gc, LONG destX1, LONG destY1, LONG destX2, LONG destY2);

/* Accelerated batch buffer operations (ported from xf86-video-intel) */
/* intel_batch.c */
VOID HIDDIntelBatchInit(struct CardData *carddata);
VOID HIDDIntelBatchTeardown(struct CardData *carddata);
VOID HIDDIntelBatchEmitFlush(struct CardData *carddata);
VOID HIDDIntelBatchSubmit(struct CardData *carddata);
BOOL HIDDIntelBatchFillSolidRect(struct CardData *carddata,
    struct HIDDIntelBitMapData *bmdata, LONG minX, LONG minY, LONG maxX,
    LONG maxY, ULONG drawmode, ULONG color);
BOOL HIDDIntelBatchCopySameFormat(struct CardData *carddata,
    struct HIDDIntelBitMapData *srcdata, struct HIDDIntelBitMapData *destdata,
    LONG srcX, LONG srcY, LONG destX, LONG destY, LONG width, LONG height,
    ULONG drawmode);

/* Declaration of intel initialization function */
extern int intel_init(void);

/* Global carddata pointer for xf86-video-intel compatibility */
extern struct CardData * globalcarddataptr;

#endif /* _INTEL_INTERN_H */
