/*
    Copyright (C) 2010-2026, The AROS Development Team. All rights reserved.
*/

#include "intel_intern.h"

#define DEBUG 1
#include <aros/debug.h>
#include <proto/oop.h>
#include <proto/utility.h>

#include <libdrm/arosdrmmode.h>

#undef HiddBitMapAttrBase
#undef HiddPixFmtAttrBase
#undef HiddBitMapIntelAttrBase
#undef HiddSyncAttrBase
#undef HiddGfxAttrBase

#define HiddBitMapAttrBase          (SD(cl)->bitMapAttrBase)
#define HiddPixFmtAttrBase          (SD(cl)->pixFmtAttrBase)
#define HiddBitMapIntelAttrBase     (SD(cl)->bitMapIntelAttrBase)
#define HiddSyncAttrBase            (SD(cl)->syncAttrBase)
#define HiddGfxAttrBase             (SD(cl)->gfxAttrBase)

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
    IPTR width = 0, height = 0, depth = 0, displayable = 0, bytesperpixel = 0;
    OOP_Object *pf = NULL;
    struct HIDDIntelBitMapData *bmdata = NULL;
    HIDDT_StdPixFmt stdfmt = vHidd_StdPixFmt_Unknown;
    struct CardData *carddata = &(SD(cl)->carddata);
    struct TagItem *tag, *tstate;

    /* Read attributes directly from the taglist chain */
    tstate = msg->attrList;
    while ((tag = NextTagItem(&tstate)))
    {
        ULONG tagval = tag->ti_Tag;

        if (tagval == aHidd_BitMap_Width)
            width = tag->ti_Data;
        else if (tagval == aHidd_BitMap_Height)
            height = tag->ti_Data;
        else if (tagval == aHidd_BitMap_PixFmt)
            pf = (OOP_Object *)tag->ti_Data;
        else if (tagval == aHidd_BitMap_Displayable)
            displayable = tag->ti_Data;
    }

    /* Get pixfmt info if pf is available */
    if (pf)
    {
        OOP_GetAttr(pf, aHidd_PixFmt_StdPixFmt, &stdfmt);
        OOP_GetAttr(pf, aHidd_PixFmt_BytesPerPixel, &bytesperpixel);
        OOP_GetAttr(pf, aHidd_PixFmt_Depth, &depth);
    }

    /* If width/height not in tags, try to get from ModeID's sync */
    if (width == 0 || height == 0)
    {
        tstate = msg->attrList;
        HIDDT_ModeID modeid = vHidd_ModeID_Invalid;
        OOP_Object *gfx = NULL;

        while ((tag = NextTagItem(&tstate)))
        {
            if (tag->ti_Tag == aHidd_BitMap_ModeID)
                modeid = (HIDDT_ModeID)tag->ti_Data;
            else if (tag->ti_Tag == aHidd_BitMap_GfxHidd)
                gfx = (OOP_Object *)tag->ti_Data;
        }

        if (modeid != vHidd_ModeID_Invalid && gfx)
        {
            OOP_Object *sync = NULL, *pf_temp = NULL;
            struct pHidd_Gfx_GetMode gm;
            gm.mID = OOP_GetMethodID(IID_Hidd_Gfx, moHidd_Gfx_GetMode);
            gm.modeID = modeid;
            gm.syncPtr = &sync;
            gm.pixFmtPtr = &pf_temp;
            OOP_DoMethod(gfx, (OOP_Msg)&gm);
            if (sync)
            {
                OOP_GetAttr(sync, aHidd_Sync_HDisp, &width);
                OOP_GetAttr(sync, aHidd_Sync_VDisp, &height);
            }
        }
    }

    o = (OOP_Object *)OOP_DoSuperMethod(cl, o, (OOP_Msg)msg);

    if (!o)
        goto exit_fail;

    bmdata = OOP_INST_DATA(cl, o);

    bmdata->fbid = 0;
    bmdata->xoffset = 0;
    bmdata->yoffset = 0;
    bmdata->bo = NULL;
    
    D(bug("[Intel] BitMap New: %ld x %ld x %ld\n", width, height, depth));

    if (width > 4096 || height > 4096)
        goto exit_fail;

    if (stdfmt != vHidd_StdPixFmt_Unknown &&
        stdfmt != vHidd_StdPixFmt_BGR032 &&
        stdfmt != vHidd_StdPixFmt_BGRA32 &&
        stdfmt != vHidd_StdPixFmt_ARGB32 &&
        stdfmt != vHidd_StdPixFmt_RGBA32 &&
        stdfmt != vHidd_StdPixFmt_ABGR32 &&
        stdfmt != vHidd_StdPixFmt_RGB16 &&
        stdfmt != vHidd_StdPixFmt_RGB16_LE &&
        stdfmt != vHidd_StdPixFmt_BGR16 &&
        stdfmt != vHidd_StdPixFmt_BGR16_LE)
        goto exit_fail;

    bmdata->drawable.width = width;
    bmdata->drawable.height = height;
    bmdata->drawable.depth = bmdata->drawable.bitsPerPixel = depth;
    bmdata->bytesperpixel = bytesperpixel ? bytesperpixel : 4;

    bmdata->displayable = (displayable) ? TRUE : FALSE;
    bmdata->is_framebuffer = GetTagData(aHidd_BitMap_FrameBuffer, 0, msg->attrList) ? TRUE : FALSE;
    InitSemaphore(&bmdata->semaphore);

    LOCK_ENGINE

    if (width == 0 || height == 0)
    {
        bmdata->pitch = 64;
        bmdata->bo = NULL;
        bmdata->gtt_ptr = NULL;
    }
    else if (bmdata->is_framebuffer)
    {
        /* Framebuffer uses GTT aperture directly, no GEM buffer */
        bmdata->pitch = bmdata->drawable.width * bmdata->bytesperpixel;
        bmdata->pitch = (bmdata->pitch + 63) & ~63;
        bmdata->bo = NULL;
        bmdata->gtt_ptr = carddata->gtt_fb;

        bug("[Intel] framebuffer created: %lux%lu pitch=%lu gtt_fb_pitch=%lu\n",
            bmdata->drawable.width, bmdata->drawable.height, bmdata->pitch,
            carddata->gtt_fb_pitch);

        intel_set_mode_by_resolution(carddata, carddata->selected_connector,
            bmdata->drawable.width, bmdata->drawable.height, bmdata->pitch,
            bmdata->drawable.bitsPerPixel);
    }
    else
    {
        /* Allocate buffer object using libdrm_intel */
        bmdata->pitch = bmdata->drawable.width * bmdata->bytesperpixel;
        /* Align pitch to 64 bytes as required by Intel hardware */
        bmdata->pitch = (bmdata->pitch + 63) & ~63;

        bmdata->bo = drm_intel_bo_alloc(carddata->bufmgr, "bitmap",
            bmdata->pitch * bmdata->drawable.height, 4096);
        bmdata->gtt_ptr = NULL;
    }

    UNLOCK_ENGINE

    if (width > 0 && height > 0 &&
        !bmdata->is_framebuffer && bmdata->bo == NULL)
        goto exit_fail;

    bmdata->compositor = (OOP_Object *)GetTagData(aHidd_BitMap_Intel_CompositorHidd, 0, msg->attrList);
    if (bmdata->compositor == NULL && displayable)
    {
        D(bug("[Intel] Displayable bitmap without compositor - using generic path\n"));
    }

    return o;

exit_fail:

    D(bug("[Intel]: Failed to create bitmap %ldx%ld %ld %d\n", width, height, depth, stdfmt));

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
    LOCK_BITMAP

    if (!bmdata->is_framebuffer && bmdata->fbid != 0)
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

    UNLOCK_BITMAP
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
        case aoHidd_BitMap_Width:
            *msg->storage = bmdata->drawable.width;
            return;
        case aoHidd_BitMap_Height:
            *msg->storage = bmdata->drawable.height;
            return;
        case aoHidd_BitMap_Depth:
            *msg->storage = bmdata->drawable.depth;
            return;
        case aoHidd_BitMap_BytesPerRow:
            *msg->storage = bmdata->pitch;
            return;
        case aoHidd_BitMap_Displayable:
            *msg->storage = bmdata->displayable;
            return;
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
    HIDDT_ModeID newmodeid = vHidd_ModeID_Invalid;

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
            case aoHidd_BitMap_ModeID:
                newmodeid = tag->ti_Data;
                break;
            }
        }
    }

    if (bmdata->is_framebuffer && newmodeid != vHidd_ModeID_Invalid)
    {
        struct CardData *carddata = &(SD(cl)->carddata);
        ULONG w, h, p;

        if (intel_mode_lookup_and_set(carddata, carddata->selected_connector,
                newmodeid, bmdata->bytesperpixel, &w, &h, &p))
        {
            bmdata->drawable.width = w;
            bmdata->drawable.height = h;
            bmdata->pitch = p;
        }
    }

    if ((newxoffset != bmdata->xoffset) || (newyoffset != bmdata->yoffset))
    {
        D(bug("[Intel] BitMap Set offset %ld,%ld -> %ld,%ld (displayable=%d)\n",
              bmdata->xoffset, bmdata->yoffset, newxoffset, newyoffset, bmdata->displayable));

        if (bmdata->compositor)
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
        else
        {
            HIDDIntelSetOffsets(o, newxoffset, newyoffset);
        }
    }

    OOP_DoSuperMethod(cl, o, (OOP_Msg)msg);
}

VOID METHOD(IntelBitMap, Hidd_BitMap, PutPixel)
{
    struct HIDDIntelBitMapData *bmdata = OOP_INST_DATA(cl, o);
    IPTR addr = (msg->x * bmdata->bytesperpixel) + (bmdata->pitch * msg->y);

    IPTR map = (IPTR)(BM_VIRTUAL(bmdata));
    
    if (map == (IPTR)NULL)
    {
        LOCK_BITMAP
        MAP_BUFFER
        addr += (IPTR)(BM_VIRTUAL(bmdata));
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

    IPTR map = (IPTR)(BM_VIRTUAL(bmdata));

    if (map == (IPTR)NULL)
    {
        LOCK_BITMAP
        MAP_BUFFER
        addr += (IPTR)(BM_VIRTUAL(bmdata));
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
    BOOL ret = FALSE;

    LOCK_ENGINE
    LOCK_BITMAP

    /* Try GPU accelerated upload for same-bpp source */
    if (!carddata->batch.force_fallback)
    {
        LONG srcbpp = 0;

        switch (msg->pixFmt)
        {
        case vHidd_StdPixFmt_BGR032:
        case vHidd_StdPixFmt_RGB032:
        case vHidd_StdPixFmt_ARGB32:
        case vHidd_StdPixFmt_BGRA32:
        case vHidd_StdPixFmt_RGBA32:
        case vHidd_StdPixFmt_ABGR32:
        case vHidd_StdPixFmt_0RGB32:
        case vHidd_StdPixFmt_0BGR32:
            srcbpp = 4;
            break;
        case vHidd_StdPixFmt_RGB16:
        case vHidd_StdPixFmt_RGB16_LE:
        case vHidd_StdPixFmt_BGR16:
        case vHidd_StdPixFmt_BGR16_LE:
        case vHidd_StdPixFmt_RGB15:
        case vHidd_StdPixFmt_RGB15_LE:
        case vHidd_StdPixFmt_BGR15:
        case vHidd_StdPixFmt_BGR15_LE:
            srcbpp = 2;
            break;
        }

        if (srcbpp == bmdata->bytesperpixel)
        {
            ULONG src_size = msg->height * msg->modulo;
            drm_intel_bo *tmp_bo = drm_intel_bo_alloc(carddata->bufmgr,
                "putimage", src_size, 4096);
            if (tmp_bo)
            {
                drm_intel_gem_bo_map_gtt(tmp_bo);
                CopyMem(msg->pixels, tmp_bo->virtual, src_size);
                drm_intel_bo_unmap(tmp_bo);

                {
                    struct HIDDIntelBitMapData tmp_bmdata;
                    tmp_bmdata.bo = tmp_bo;
                    tmp_bmdata.pitch = msg->modulo;
                    tmp_bmdata.bytesperpixel = (UBYTE)srcbpp;

                    ret = HIDDIntelBatchCopySameFormat(carddata,
                        &tmp_bmdata, bmdata,
                        0, 0, msg->x, msg->y, msg->width, msg->height,
                        vHidd_GC_DrawMode_Copy);
                }

                drm_intel_bo_unreference(tmp_bo);

                if (ret)
                {
                    HIDDIntelBatchSubmit(carddata);
                    if (carddata->batch.force_fallback)
                    {
                        /* Batch execution failed; fall back to CPU upload */
                        bug("[Intel] PutImage batch failed, using CPU fallback\n");
                        ret = FALSE;
                    }
                    else
                    {
                        drm_intel_bo_wait_rendering(bmdata->bo);
                    }
                }
            }
        }
    }

    if (!ret)
    {
        MAP_BUFFER

        APTR dstBuff = (APTR)((IPTR)(BM_VIRTUAL(bmdata)) + (msg->y * bmdata->pitch)
            + (msg->x * bmdata->bytesperpixel));

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
    struct CardData *carddata = &(SD(cl)->carddata);
    BOOL ret = FALSE;

    LOCK_ENGINE
    LOCK_BITMAP

    /* Try GPU accelerated readback for same-bpp destination */
    if (!carddata->batch.force_fallback)
    {
        LONG dstbpp = 0;

        switch (msg->pixFmt)
        {
        case vHidd_StdPixFmt_BGR032:
        case vHidd_StdPixFmt_RGB032:
        case vHidd_StdPixFmt_ARGB32:
        case vHidd_StdPixFmt_BGRA32:
        case vHidd_StdPixFmt_RGBA32:
        case vHidd_StdPixFmt_ABGR32:
        case vHidd_StdPixFmt_0RGB32:
        case vHidd_StdPixFmt_0BGR32:
            dstbpp = 4;
            break;
        case vHidd_StdPixFmt_RGB16:
        case vHidd_StdPixFmt_RGB16_LE:
        case vHidd_StdPixFmt_BGR16:
        case vHidd_StdPixFmt_BGR16_LE:
        case vHidd_StdPixFmt_RGB15:
        case vHidd_StdPixFmt_RGB15_LE:
        case vHidd_StdPixFmt_BGR15:
        case vHidd_StdPixFmt_BGR15_LE:
            dstbpp = 2;
            break;
        }

        if (dstbpp == bmdata->bytesperpixel)
        {
            ULONG dst_size = msg->height * msg->modulo;
            drm_intel_bo *tmp_bo = drm_intel_bo_alloc(carddata->bufmgr,
                "getimage", dst_size, 4096);
            if (tmp_bo)
            {
                {
                    struct HIDDIntelBitMapData tmp_bmdata;
                    tmp_bmdata.bo = tmp_bo;
                    tmp_bmdata.pitch = msg->modulo;
                    tmp_bmdata.bytesperpixel = (UBYTE)dstbpp;

                    ret = HIDDIntelBatchCopySameFormat(carddata,
                        bmdata, &tmp_bmdata,
                        msg->x, msg->y, 0, 0, msg->width, msg->height,
                        vHidd_GC_DrawMode_Copy);
                }

                if (ret)
                {
                    HIDDIntelBatchSubmit(carddata);
                    if (carddata->batch.force_fallback)
                    {
                        /* Batch execution failed; fall back to CPU readback */
                        bug("[Intel] GetImage batch failed, using CPU fallback\n");
                        ret = FALSE;
                    }
                    else
                    {
                        drm_intel_bo_wait_rendering(tmp_bo);

                        drm_intel_gem_bo_map_gtt(tmp_bo);
                        CopyMem(tmp_bo->virtual, msg->pixels, dst_size);
                        drm_intel_bo_unmap(tmp_bo);
                    }
                }

                drm_intel_bo_unreference(tmp_bo);
            }
        }
    }

    if (!ret)
    {
        MAP_BUFFER

        APTR srcBuff = (APTR)((IPTR)(BM_VIRTUAL(bmdata)) + (msg->y * bmdata->pitch)
            + (msg->x * bmdata->bytesperpixel));

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

    *msg->addressReturn = (UBYTE*)(BM_VIRTUAL(bmdata));
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

    if (bmdata->compositor)
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

    /*
     * Mirror the updated region to the GTT framebuffer. The generic software
     * compositor (created by graphics.library) mirrors screen bitmaps into its
     * display bitmap and calls UpdateRect on it; in passthrough mode it calls
     * UpdateRect on the screen bitmap itself. Either way the rect must reach
     * the GTT aperture that the CRTC scans out.
     */
    if (bmdata->displayable && !bmdata->is_framebuffer)
    {
        struct CardData *carddata = &(SD(cl)->carddata);
        APTR srcbuf;
                IPTR dst = (IPTR)carddata->gtt_fb;
        ULONG y;

        /*
         * Take the same locks as Dispose() so a bitmap being disposed cannot
         * free bo->virtual out from under this copy (use-after-free).
         */
        LOCK_ENGINE
        LOCK_BITMAP

        if (!bmdata->bo)
        {
            UNLOCK_BITMAP
            UNLOCK_ENGINE
            return;
        }

        if (!carddata->gtt_fb)
        {
            UNLOCK_BITMAP
            UNLOCK_ENGINE
            return;
        }

        if (!bmdata->bo->virtual)
            drm_intel_gem_bo_map_gtt(bmdata->bo);
        srcbuf = BM_VIRTUAL(bmdata);

        /*
         * The screen bitmap may be dragged to a non-zero position on the
         * display. graphics.library still renders into it using local
         * coordinates, so the GTT destination must be offset by the bitmap's
         * LeftEdge/TopEdge to place the content at its real screen position.
         */
        if (srcbuf)
        {
            ULONG fbpitch = carddata->gtt_fb_pitch;
            ULONG dstpitch = fbpitch ? fbpitch : bmdata->pitch;

            for (y = 0; y < msg->height; y++)
            {
                ULONG sy = msg->y + y;
                ULONG dy = sy + bmdata->yoffset;
                ULONG sx = msg->x + bmdata->xoffset;
                ULONG sw = msg->width;

                if (sy >= bmdata->drawable.height)
                    break;
                CopyMem(
                    (APTR)((IPTR)srcbuf + sy * bmdata->pitch + msg->x * bmdata->bytesperpixel),
                    (APTR)(dst + dy * dstpitch + sx * bmdata->bytesperpixel),
                    sw * bmdata->bytesperpixel);
            }
        }

        UNLOCK_BITMAP
        UNLOCK_ENGINE
    }
}

VOID METHOD(IntelBitMap, Hidd_BitMap, Clear)
{
    struct HIDDIntelBitMapData *bmdata = OOP_INST_DATA(cl, o);
    struct CardData *carddata = &(SD(cl)->carddata);
    BOOL ret = FALSE;

    LOCK_ENGINE
    LOCK_BITMAP

    if (!carddata->batch.force_fallback)
    {
        ret = HIDDIntelBatchFillSolidRect(carddata, bmdata,
                0, 0, bmdata->drawable.width - 1, bmdata->drawable.height - 1,
                vHidd_GC_DrawMode_Copy, GC_BG(msg->gc));
        if (ret)
        {
            HIDDIntelBatchSubmit(carddata);
            if (carddata->batch.force_fallback)
            {
                bug("[Intel] Clear batch failed, using CPU fallback\n");
                ret = FALSE;
            }
            else
            {
                drm_intel_bo_wait_rendering(bmdata->bo);
            }
        }
    }

    if (!ret)
    {
        MAP_BUFFER

        {
            LONG x, y;
            ULONG bg = GC_BG(msg->gc);

            for (y = 0; y < (LONG)bmdata->drawable.height; y++)
            {
                if (bmdata->bytesperpixel == 2)
                {
                    UWORD *dest = (UWORD *)((IPTR)(BM_VIRTUAL(bmdata)) + y * bmdata->pitch);
                    for (x = 0; x < (LONG)bmdata->drawable.width; x++)
                        dest[x] = (UWORD)bg;
                }
                else
                {
                    ULONG *dest = (ULONG *)((IPTR)(BM_VIRTUAL(bmdata)) + y * bmdata->pitch);
                    for (x = 0; x < (LONG)bmdata->drawable.width; x++)
                        dest[x] = bg;
                }
            }
        }
    }

    UNLOCK_BITMAP
    UNLOCK_ENGINE
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
            if (carddata->batch.force_fallback)
            {
                bug("[Intel] FillRect batch failed, using CPU fallback\n");
                ret = FALSE;
            }
            else
            {
                drm_intel_bo_wait_rendering(bmdata->bo);
            }
        }
    }

    if (!ret)
    {
        /* Software fallback */
        MAP_BUFFER

        {
            LONG x, y;
            ULONG fg = GC_FG(msg->gc);
            ULONG dm = GC_DRMD(msg->gc);

            for (y = msg->minY; y <= msg->maxY; y++)
            {
                if (bmdata->bytesperpixel == 2)
                {
                    UWORD *dest = (UWORD *)((IPTR)(BM_VIRTUAL(bmdata)) + y * bmdata->pitch);
                    for (x = msg->minX; x <= msg->maxX; x++)
                    {
                        switch (dm)
                        {
                            case vHidd_GC_DrawMode_Xor:
                                dest[x] ^= (UWORD)fg;
                                break;
                            case vHidd_GC_DrawMode_Invert:
                                dest[x] = ~dest[x];
                                break;
                            case vHidd_GC_DrawMode_Or:
                                dest[x] |= (UWORD)fg;
                                break;
                            case vHidd_GC_DrawMode_And:
                                dest[x] &= (UWORD)fg;
                                break;
                            default:
                                dest[x] = (UWORD)fg;
                                break;
                        }
                    }
                }
                else
                {
                    ULONG *dest = (ULONG *)((IPTR)(BM_VIRTUAL(bmdata)) + y * bmdata->pitch);
                    for (x = msg->minX; x <= msg->maxX; x++)
                    {
                        switch (dm)
                        {
                            case vHidd_GC_DrawMode_Xor:
                                dest[x] ^= fg;
                                break;
                            case vHidd_GC_DrawMode_Invert:
                                dest[x] = ~dest[x];
                                break;
                            case vHidd_GC_DrawMode_Or:
                                dest[x] |= fg;
                                break;
                            case vHidd_GC_DrawMode_And:
                                dest[x] &= fg;
                                break;
                            default:
                                dest[x] = fg;
                                break;
                        }
                    }
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

    UNLOCK_BITMAP

    OOP_DoSuperMethod(cl, o, (OOP_Msg)msg);
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
                msg->srcx, (BM_VIRTUAL(bmdata)), bmdata->pitch, msg->x, msg->y,
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
                msg->srcx, (BM_VIRTUAL(bmdata)), bmdata->pitch, msg->x, msg->y,
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
                (BM_VIRTUAL(bmdata)), bmdata->pitch, msg->x, msg->y, msg->width, msg->height
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
                (BM_VIRTUAL(bmdata)), bmdata->pitch, msg->x, msg->y, msg->width, msg->height
            }, *m = &__m;
            OOP_DoMethod(o, (OOP_Msg)m);
        }
        break;
    }

    UNLOCK_BITMAP
}
