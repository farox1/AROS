/*
    Copyright (C) 2010-2026, The AROS Development Team. All rights reserved.
*/

#include "intel_intern.h"

#define DEBUG 0
#include <aros/debug.h>
#include <proto/oop.h>
#include <proto/utility.h>

#include <libdrm/arosdrmmode.h>

#undef HiddBitMapAttrBase
#undef HiddPixFmtAttrBase
#undef HiddBitMapIntelAttrBase

#define HiddBitMapAttrBase          (SD(cl)->bitMapAttrBase)
#define HiddPixFmtAttrBase          (SD(cl)->pixFmtAttrBase)
#define HiddBitMapIntelAttrBase     (SD(cl)->bitMapIntelAttrBase)

VOID HIDDIntelSetOffsets(OOP_Object *bm, LONG newxoffset, LONG newyoffset)
{
    OOP_Class *cl = OOP_OCLASS(bm);
    struct HIDDIntelBitMapData *bmdata = OOP_INST_DATA(cl, bm);
    bmdata->xoffset = newxoffset;
    bmdata->yoffset = newyoffset;
}

/* PUBLIC METHODS */
OOP_Object *METHOD(IntelBitMap, Root, New)
{
    IPTR width, height, depth, displayable, bytesperpixel;
    OOP_Object *pf;
    struct HIDDIntelBitMapData *bmdata = NULL;
    HIDDT_StdPixFmt stdfmt = vHidd_StdPixFmt_Unknown;
    struct CardData *carddata = &(SD(cl)->carddata);

    o = (OOP_Object *)OOP_DoSuperMethod(cl, o, (OOP_Msg)msg);

    if (!o)
        goto exit_fail;

    bmdata = OOP_INST_DATA(cl, o);

    bmdata->fbid = 0;
    bmdata->xoffset = 0;
    bmdata->yoffset = 0;
    bmdata->bo = NULL;
    
    OOP_GetAttr(o, aHidd_BitMap_Width,  &width);
    OOP_GetAttr(o, aHidd_BitMap_Height, &height);
    OOP_GetAttr(o, aHidd_BitMap_PixFmt, (APTR)&pf);
    OOP_GetAttr(o, aHidd_BitMap_Displayable, &displayable);
    OOP_GetAttr(pf, aHidd_PixFmt_StdPixFmt, &stdfmt);
    OOP_GetAttr(pf, aHidd_PixFmt_BytesPerPixel, &bytesperpixel);
    OOP_GetAttr(pf, aHidd_PixFmt_Depth, &depth);
    
    D(bug("[Intel] BitMap New: %ld x %ld x %ld\n", width, height, depth));

    if ((stdfmt != vHidd_StdPixFmt_BGR032) && (stdfmt != vHidd_StdPixFmt_RGB16_LE))
        goto exit_fail;

    if (depth < 16)
        goto exit_fail;
    
    if ((bytesperpixel != 2) && (bytesperpixel != 4))
        goto exit_fail;

    bmdata->drawable.width = width;
    bmdata->drawable.height = height;
    bmdata->drawable.depth = bmdata->drawable.bitsPerPixel = depth;
    bmdata->bytesperpixel = bytesperpixel;

    if (displayable) bmdata->displayable = TRUE; else bmdata->displayable = FALSE;
    InitSemaphore(&bmdata->semaphore);

    LOCK_ENGINE

    /* Allocate buffer object using libdrm_intel */
    bmdata->pitch = bmdata->drawable.width * bmdata->bytesperpixel;
    /* Align pitch to 64 bytes as required by Intel hardware */
    bmdata->pitch = (bmdata->pitch + 63) & ~63;

    bmdata->bo = drm_intel_bo_alloc(carddata->bufmgr, "bitmap",
        bmdata->pitch * bmdata->drawable.height, 4096);

    UNLOCK_ENGINE

    if (bmdata->bo == NULL)
        goto exit_fail;

    bmdata->compositor = (OOP_Object *)GetTagData(aHidd_BitMap_Intel_CompositorHidd, 0, msg->attrList);
    if (bmdata->compositor == NULL)
        goto exit_fail;

    return o;

exit_fail:

    bug("[Intel]: Failed to create bitmap %ldx%ld %ld %d\n", width, height, depth, stdfmt);

    if (o)
    {
        OOP_MethodID disp_mid = OOP_GetMethodID(IID_Root, moRoot_Dispose);
        OOP_CoerceMethod(cl, o, (OOP_Msg) &disp_mid);
    }

    return NULL;
}

VOID IntelBitMap__Root__Dispose(OOP_Class *cl, OOP_Object *o, OOP_Msg msg)
{
    struct HIDDIntelBitMapData *bmdata = OOP_INST_DATA(cl, o);

    D(bug("[Intel] Dispose %p\n", o));

    LOCK_ENGINE
    if (bmdata->fbid != 0)
    {
        struct CardData *carddata = &(SD(cl)->carddata);
        drmModeRmFB(carddata->fd, bmdata->fbid);
        bmdata->fbid = 0;
    }

    if (bmdata->bo)
    {
        drm_intel_bo_unreference(bmdata->bo);
        bmdata->bo = NULL;
    }
    UNLOCK_ENGINE

    OOP_DoSuperMethod(cl, o, msg);
}

VOID METHOD(IntelBitMap, Root, Get)
{
    struct HIDDIntelBitMapData *bmdata = OOP_INST_DATA(cl, o);
    ULONG idx;

    if (IS_BITMAP_ATTR(msg->attrID, idx))
    {
        switch (idx)
        {
        case aoHidd_BitMap_LeftEdge:
            *msg->storage = bmdata->xoffset;
            return;
        case aoHidd_BitMap_TopEdge:
            *msg->storage = bmdata->yoffset;
            return;
        }
    }

    OOP_DoSuperMethod(cl, o, (OOP_Msg)msg);
}

VOID METHOD(IntelBitMap, Root, Set)
{
    struct TagItem  *tag, *tstate;
    struct HIDDIntelBitMapData *bmdata = OOP_INST_DATA(cl, o);
    ULONG idx;
    LONG newxoffset = bmdata->xoffset;
    LONG newyoffset = bmdata->yoffset;

    tstate = msg->attrList;
    while((tag = NextTagItem(&tstate)))
    {
        if(IS_BITMAP_ATTR(tag->ti_Tag, idx))
        {
            switch(idx)
            {
            case aoHidd_BitMap_LeftEdge:
                newxoffset = tag->ti_Data;
                break;
            case aoHidd_BitMap_TopEdge:
                newyoffset = tag->ti_Data;
                break;
            }
        }
    }

    if ((newxoffset != bmdata->xoffset) || (newyoffset != bmdata->yoffset))
    {
        struct pHidd_Compositor_ValidateBitMapPositionChange vbpcmsg =
        {
            mID : SD(cl)->mid_ValidateBitMapPositionChange,
            bm : o,
            newxoffset : &newxoffset,
            newyoffset : &newyoffset
        };
        
        OOP_DoMethod(bmdata->compositor, (OOP_Msg)&vbpcmsg);
        
        if ((newxoffset != bmdata->xoffset) || (newyoffset != bmdata->yoffset))
        {
            struct pHidd_Compositor_BitMapPositionChanged bpcmsg =
            {
                mID : SD(cl)->mid_BitMapPositionChanged,
                bm : o
            };

            HIDDIntelSetOffsets(o, newxoffset, newyoffset);
        
            OOP_DoMethod(bmdata->compositor, (OOP_Msg)&bpcmsg);
        }
    }

    OOP_DoSuperMethod(cl, o, (OOP_Msg)msg);
}

VOID METHOD(IntelBitMap, Hidd_BitMap, PutPixel)
{
    struct HIDDIntelBitMapData *bmdata = OOP_INST_DATA(cl, o);
    IPTR addr = (msg->x * bmdata->bytesperpixel) + (bmdata->pitch * msg->y);

    IPTR map = (IPTR)bmdata->bo->virtual;
    
    if (map == (IPTR)NULL)
    {
        LOCK_BITMAP
        MAP_BUFFER
        addr += (IPTR)bmdata->bo->virtual;
    }
    else
        addr += map;
    
    switch(bmdata->bytesperpixel)
    {
    case(1):
        break;
    case(2):
        writew(msg->pixel, (APTR)addr);
        break;
    case(4):
        writel(msg->pixel, (APTR)addr);
        break;
    }

    if (map == (IPTR)NULL)
        UNLOCK_BITMAP
}

HIDDT_Pixel METHOD(IntelBitMap, Hidd_BitMap, GetPixel)
{
    struct HIDDIntelBitMapData *bmdata = OOP_INST_DATA(cl, o);
    IPTR addr = (msg->x * bmdata->bytesperpixel) + (bmdata->pitch * msg->y);
    HIDDT_Pixel pixel = 0;

    IPTR map = (IPTR)bmdata->bo->virtual;

    if (map == (IPTR)NULL)
    {
        LOCK_BITMAP
        MAP_BUFFER
        addr += (IPTR)bmdata->bo->virtual;
    }
    else
        addr += map;
    
    switch(bmdata->bytesperpixel)
    {
    case(1):
        break;
    case(2):
        pixel = readw((APTR)addr);
        break;
    case(4):
        pixel = readl((APTR)addr);
        break;
    }
    
    if (map == (IPTR)NULL)
        UNLOCK_BITMAP
    
    return pixel;
}

VOID METHOD(IntelBitMap, Hidd_BitMap, PutImage)
{
    struct HIDDIntelBitMapData *bmdata = OOP_INST_DATA(cl, o);
    struct CardData *carddata = &(SD(cl)->carddata);

    LOCK_ENGINE
    LOCK_BITMAP

    /* Fallback: RAM->CPU->VRAM */
    {
        APTR dstBuff = NULL;
        
        MAP_BUFFER

        dstBuff = (APTR)((IPTR)bmdata->bo->virtual + (msg->y * bmdata->pitch) + (msg->x * bmdata->bytesperpixel));
        
        HiddIntelWriteFromRAM(
            msg->pixels, msg->modulo, msg->pixFmt,
            dstBuff, bmdata->pitch,
            msg->width, msg->height,
            cl, o);
    }

    UNLOCK_BITMAP
    UNLOCK_ENGINE
}

VOID METHOD(IntelBitMap, Hidd_BitMap, GetImage)
{
    struct HIDDIntelBitMapData *bmdata = OOP_INST_DATA(cl, o);

    LOCK_ENGINE
    LOCK_BITMAP

    /* Fallback: VRAM->CPU->RAM */
    {
        APTR srcBuff = NULL;
        
        MAP_BUFFER

        srcBuff = (APTR)((IPTR)bmdata->bo->virtual + (msg->y * bmdata->pitch) + (msg->x * bmdata->bytesperpixel));
        
        HiddIntelReadIntoRAM(
            srcBuff, bmdata->pitch,
            msg->pixels, msg->modulo, msg->pixFmt,
            msg->width, msg->height,
            cl, o);
    }

    UNLOCK_BITMAP
    UNLOCK_ENGINE
}

ULONG METHOD(IntelBitMap, Hidd_BitMap, BytesPerLine)
{
    struct HIDDIntelBitMapData *bmdata = OOP_INST_DATA(cl, o);
    
    return (bmdata->pitch);
}

BOOL METHOD(IntelBitMap, Hidd_BitMap, ObtainDirectAccess)
{
    struct HIDDIntelBitMapData *bmdata = OOP_INST_DATA(cl, o);
    struct CardData *carddata = &(SD(cl)->carddata);
    
    LOCK_ENGINE
    LOCK_BITMAP
    MAP_BUFFER

    *msg->addressReturn = (UBYTE*)bmdata->bo->virtual;
    *msg->widthReturn = bmdata->pitch / bmdata->bytesperpixel;
    *msg->heightReturn = bmdata->drawable.height;
    *msg->bankSizeReturn = *msg->memSizeReturn = bmdata->pitch * bmdata->drawable.height;

    return TRUE;
}

VOID METHOD(IntelBitMap, Hidd_BitMap, ReleaseDirectAccess)
{
    struct HIDDIntelBitMapData *bmdata = OOP_INST_DATA(cl, o);

    UNLOCK_BITMAP
    UNLOCK_ENGINE
}

VOID METHOD(IntelBitMap, Hidd_BitMap, UpdateRect)
{
    struct HIDDIntelBitMapData *bmdata = OOP_INST_DATA(cl, o);
    
    if (bmdata->displayable)
    {
        struct pHidd_Compositor_BitMapRectChanged brcmsg =
        {
            mID : SD(cl)->mid_BitMapRectChanged,
            bm : o,
            x : msg->x,
            y : msg->y,
            width : msg->width,
            height : msg->height
        };
        
        OOP_DoMethod(bmdata->compositor, (OOP_Msg)&brcmsg);
    }
}

VOID METHOD(IntelBitMap, Hidd_BitMap, Clear)
{
    /* Fallback to default method */
    OOP_DoSuperMethod(cl, o, (OOP_Msg)msg);
}

VOID METHOD(IntelBitMap, Hidd_BitMap, FillRect)
{
    struct HIDDIntelBitMapData *bmdata = OOP_INST_DATA(cl, o);
    struct CardData *carddata = &(SD(cl)->carddata);
    BOOL ret = FALSE;

    LOCK_ENGINE
    LOCK_BITMAP

    /* Try hardware accelerated fill */
    if (!carddata->batch.force_fallback)
    {
        ULONG color = GC_FG(msg->gc);
        ret = HIDDIntelBatchFillSolidRect(carddata, bmdata,
                msg->minX, msg->minY, msg->maxX, msg->maxY,
                GC_DRMD(msg->gc), color);
        if (ret)
        {
            HIDDIntelBatchSubmit(carddata);
            drm_intel_bo_wait_rendering(bmdata->bo);
        }
    }

    if (!ret)
    {
        /* Software fallback */
        MAP_BUFFER

        {
            LONG x, y;
            ULONG fg = GC_FG(msg->gc);
            ULONG *dest;
            
            for (y = msg->minY; y <= msg->maxY; y++)
            {
                dest = (ULONG *)((IPTR)bmdata->bo->virtual + y * bmdata->pitch + msg->minX * bmdata->bytesperpixel);
                if (bmdata->bytesperpixel == 2)
                {
                    for (x = msg->minX; x <= msg->maxX; x++)
                        *((UWORD*)dest + x - msg->minX) = (UWORD)fg;
                }
                else
                {
                    for (x = msg->minX; x <= msg->maxX; x++)
                        dest[x - msg->minX] = fg;
                }
            }
        }
    }

    UNLOCK_BITMAP
    UNLOCK_ENGINE
}

VOID METHOD(IntelBitMap, Hidd_BitMap, DrawLine)
{
    struct HIDDIntelBitMapData *bmdata = OOP_INST_DATA(cl, o);

    LOCK_BITMAP

    if ((GC_DRMD(msg->gc) == vHidd_GC_DrawMode_Copy) && (GC_COLMASK(msg->gc) == ~0))
    {
        MAP_BUFFER

        HIDDIntelBitMapDrawSolidLine(bmdata, msg->gc, msg->x1, msg->y1, msg->x2, msg->y2);

        UNLOCK_BITMAP

        return;
    }

    OOP_DoSuperMethod(cl, o, (OOP_Msg)msg);
    
    UNLOCK_BITMAP
}

VOID METHOD(IntelBitMap, Hidd_BitMap, PutAlphaImage)
{
    struct HIDDIntelBitMapData *bmdata = OOP_INST_DATA(cl, o);

    LOCK_ENGINE
    LOCK_BITMAP

    switch(bmdata->bytesperpixel)
    {
    case 1:
        break;

    case 2:
        {
            MAP_BUFFER
            HIDDIntelBitMapPutAlphaImage16(bmdata, msg->pixels, msg->modulo, msg->x,
                msg->y, msg->width, msg->height);
        }
        break;

    case 4:
        {
            MAP_BUFFER
            HIDDIntelBitMapPutAlphaImage32(bmdata, msg->pixels, msg->modulo, msg->x,
                msg->y, msg->width, msg->height);
        }
        break;
    }

    UNLOCK_BITMAP
    UNLOCK_ENGINE
}

VOID METHOD(IntelBitMap, Hidd_BitMap, PutAlphaTemplate)
{
    struct HIDDIntelBitMapData *bmdata = OOP_INST_DATA(cl, o);

    LOCK_BITMAP
    MAP_BUFFER

    switch(bmdata->bytesperpixel)
    {
    case 1:
        break;

    case 2:
        {
            HIDDIntelBitMapPutAlphaTemplate16(bmdata, msg->gc, o, msg->invertalpha,
                msg->alpha, msg->modulo, msg->x, msg->y, msg->width, msg->height);
        }
        break;

    case 4:
        {
            HIDDIntelBitMapPutAlphaTemplate32(bmdata, msg->gc, o, msg->invertalpha,
                msg->alpha, msg->modulo, msg->x, msg->y, msg->width, msg->height);
        }
        break;
    }

    UNLOCK_BITMAP
}

VOID METHOD(IntelBitMap, Hidd_BitMap, PutTemplate)
{
    struct HIDDIntelBitMapData *bmdata = OOP_INST_DATA(cl, o);

    LOCK_BITMAP
    MAP_BUFFER

    switch(bmdata->bytesperpixel)
    {
    case 1:
        break;

    case 2:
        {
            struct pHidd_BitMap_PutMemTemplate16 __m =
            {
                SD(cl)->mid_PutMemTemplate16, msg->gc, msg->masktemplate, msg->modulo,
                msg->srcx, bmdata->bo->virtual, bmdata->pitch, msg->x, msg->y,
                msg->width, msg->height, msg->inverttemplate
            }, *m = &__m;
            OOP_DoMethod(o, (OOP_Msg)m);
        }
        break;

    case 4:
        {
            struct pHidd_BitMap_PutMemTemplate32 __m =
            {
                SD(cl)->mid_PutMemTemplate32, msg->gc, msg->masktemplate, msg->modulo,
                msg->srcx, bmdata->bo->virtual, bmdata->pitch, msg->x, msg->y,
                msg->width, msg->height, msg->inverttemplate
            }, *m = &__m;
            OOP_DoMethod(o, (OOP_Msg)m);
        }
        break;
    }

    UNLOCK_BITMAP
}

VOID METHOD(IntelBitMap, Hidd_BitMap, PutPattern)
{
    struct HIDDIntelBitMapData *bmdata = OOP_INST_DATA(cl, o);

    LOCK_BITMAP
    MAP_BUFFER

    switch(bmdata->bytesperpixel)
    {
    case 1:
        break;

    case 2:
        {
            struct pHidd_BitMap_PutMemPattern16 __m =
            {
                SD(cl)->mid_PutMemPattern16, msg->gc, msg->pattern, msg->patternsrcx,
                msg->patternsrcy, msg->patternheight, msg->patterndepth, msg->patternlut,
                msg->invertpattern, msg->mask, msg->maskmodulo, msg->masksrcx,
                bmdata->bo->virtual, bmdata->pitch, msg->x, msg->y, msg->width, msg->height
            }, *m = &__m;
            OOP_DoMethod(o, (OOP_Msg)m);
        }
        break;

    case 4:
        {
            struct pHidd_BitMap_PutMemPattern32 __m =
            {
                SD(cl)->mid_PutMemPattern32, msg->gc, msg->pattern, msg->patternsrcx,
                msg->patternsrcy, msg->patternheight, msg->patterndepth, msg->patternlut,
                msg->invertpattern, msg->mask, msg->maskmodulo, msg->masksrcx,
                bmdata->bo->virtual, bmdata->pitch, msg->x, msg->y, msg->width, msg->height
            }, *m = &__m;
            OOP_DoMethod(o, (OOP_Msg)m);
        }
        break;
    }

    UNLOCK_BITMAP
}
