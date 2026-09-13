/*
    Copyright (C) 2010-2026, The AROS Development Team. All rights reserved.
*/

/*
    Compositor class for Intel DRM driver.
    Software CPU compositing — replaces generic compositor for Intel GPUs.
*/

#include "intel_intern.h"
#include "intel_compositor.h"

#define DEBUG 1
#include <aros/debug.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/oop.h>
#include <proto/utility.h>

#undef HiddPixFmtAttrBase
#undef HiddSyncAttrBase
#undef HiddBitMapAttrBase
#undef HiddGCAttrBase
#undef HiddCompositorAttrBase

#define HiddPixFmtAttrBase      (compdata->pixFmtAttrBase)
#define HiddSyncAttrBase        (compdata->syncAttrBase)
#define HiddBitMapAttrBase      (compdata->bitMapAttrBase)
#define HiddGCAttrBase          (compdata->gcAttrBase)
#define HiddCompositorAttrBase  (compdata->compositorAttrBase)

#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define MIN(a,b) ((a) < (b) ? (a) : (b))

/*********************************************************************************************/

static BOOL AndRectRect(struct Rectangle *rect1, struct Rectangle *rect2,
    struct Rectangle *intersect)
{
    intersect->MinX = MAX(rect1->MinX, rect2->MinX);
    intersect->MinY = MAX(rect1->MinY, rect2->MinY);
    intersect->MaxX = MIN(rect1->MaxX, rect2->MaxX);
    intersect->MaxY = MIN(rect1->MaxY, rect2->MaxY);

    if ((intersect->MinX > intersect->MaxX) ||
        (intersect->MinY > intersect->MaxY))
        return FALSE;

    return TRUE;
}

/*********************************************************************************************/

static struct StackBitMapNode *HIDDCompositorIsBitMapOnStack(
    struct HIDDCompositorData *compdata, OOP_Object *bm)
{
    struct StackBitMapNode *n = NULL;
    ForeachNode(&compdata->bitmapstack, n)
        if (n->bm == bm)
            return n;
    return NULL;
}

/*********************************************************************************************/

static VOID HIDDCompositorValidateBitMapPositionChange(
    struct HIDDCompositorData *compdata, OOP_Object *bm,
    LONG *newxoffset, LONG *newyoffset)
{
    struct StackBitMapNode *n;

    n = HIDDCompositorIsBitMapOnStack(compdata, bm);
    if (n)
    {
        IPTR width, height;
        LONG limit;

        OOP_GetAttr(bm, aHidd_BitMap_Width, &width);
        OOP_GetAttr(bm, aHidd_BitMap_Height, &height);

        limit = n->displayedwidth - width;
        if (*newxoffset > 0)
            *newxoffset = 0;
        if (*newxoffset < limit)
            *newxoffset = limit;

        limit = n->displayedheight - height;
        if (*newyoffset > n->displayedheight - 15)
            *newyoffset = n->displayedheight - 15;
        if (*newyoffset < limit)
            *newyoffset = limit;
    }
}

/*********************************************************************************************/

static VOID HIDDCompositorRecalculateVisibility(
    struct HIDDCompositorData *compdata)
{
    struct StackBitMapNode *n = NULL;
    struct Region *visregion;
    struct Rectangle screenrect;

    screenrect.MinX = compdata->screenrect.MinX;
    screenrect.MinY = compdata->screenrect.MinY;
    screenrect.MaxX = compdata->screenrect.MaxX;
    screenrect.MaxY = compdata->screenrect.MaxY;

    visregion = NewRegion();
    if (!visregion)
        return;

    OrRectRegion(visregion, &screenrect);

    ForeachNode(&compdata->bitmapstack, n)
    {
        struct Rectangle bmrect;

        n->sbmflags &= ~STACKNODEF_VISIBLE;

        bmrect.MinX = n->leftedge;
        bmrect.MinY = n->topedge;
        bmrect.MaxX = n->leftedge + OOP_GET(n->bm, aHidd_BitMap_Width) - 1;
        bmrect.MaxY = n->topedge + OOP_GET(n->bm, aHidd_BitMap_Height) - 1;

        if (!n->screenregion)
            n->screenregion = NewRegion();
        else
            ClearRegion(n->screenregion);

        if (n->screenregion)
        {
            OrRectRegion(n->screenregion, &bmrect);
            AndRegionRegion(visregion, n->screenregion);

            if (n->screenregion->RegionRectangle)
            {
                n->sbmflags |= STACKNODEF_VISIBLE;
                ClearRectRegion(visregion, &bmrect);
            }
        }
    }

    DisposeRegion(visregion);
}

/*********************************************************************************************/

static VOID HIDDCompositorRedrawRegion(struct HIDDCompositorData *compdata,
    LONG minX, LONG minY, LONG maxX, LONG maxY)
{
    OOP_Class *fbcl;
    struct HIDDIntelBitMapData *fbdata;
    APTR fbptr;
    ULONG bg_color;
    LONG x, y;
    ULONG screenw, screenh;
    UBYTE bpp;
    ULONG pitch;
    struct StackBitMapNode *n = NULL;

    if (!compdata->compositedbitmap)
        return;

    fbcl = OOP_OCLASS(compdata->compositedbitmap);
    fbdata = OOP_INST_DATA(fbcl, compdata->compositedbitmap);
    bpp = fbdata->bytesperpixel;
    pitch = fbdata->pitch;
    fbptr = BM_VIRTUAL(fbdata);
    screenw = compdata->screenrect.MaxX + 1;
    screenh = compdata->screenrect.MaxY + 1;
    bg_color = GC_BG(compdata->gc);

    if (minX < 0) minX = 0;
    if (minY < 0) minY = 0;
    if (maxX >= (LONG)screenw) maxX = screenw - 1;
    if (maxY >= (LONG)screenh) maxY = screenh - 1;
    if (minX > maxX || minY > maxY)
        return;

    bug("[IntelCompositor] RedrawRegion %ld,%ld-%ld,%ld\n",
        minX, minY, maxX, maxY);

    HIDDCompositorRecalculateVisibility(compdata);

    ObtainSemaphore(&globalLock);

    if (bpp == 2)
    {
        UWORD bg16 = (UWORD)bg_color;
        for (y = minY; y <= maxY; y++)
        {
            UWORD *row = (UWORD *)((IPTR)fbptr + y * pitch);
            for (x = minX; x <= maxX; x++)
                row[x] = bg16;
        }
    }
    else
    {
        for (y = minY; y <= maxY; y++)
        {
            ULONG *row = (ULONG *)((IPTR)fbptr + y * pitch);
            for (x = minX; x <= maxX; x++)
                row[x] = bg_color;
        }
    }

    ForeachNode(&compdata->bitmapstack, n)
    {
        if (n->sbmflags & STACKNODEF_VISIBLE)
        {
            LONG destX = n->leftedge;
            LONG destY = n->topedge;
            IPTR srcW, srcH;
            LONG clipW, clipH;
            LONG srcX, srcY;

            OOP_GetAttr(n->bm, aHidd_BitMap_Width, &srcW);
            OOP_GetAttr(n->bm, aHidd_BitMap_Height, &srcH);

            /*
             * Intersect the bitmap's on-screen rectangle with the requested region.
             * For the full-screen case this degenerates to drawing the whole bitmap.
             */
            if (destX < (LONG)minX)
            {
                srcX = minX - destX;
                clipW = destX + (LONG)srcW - minX;
                destX = minX;
            }
            else
            {
                srcX = 0;
                clipW = (LONG)srcW;
            }

            if (destY < (LONG)minY)
            {
                srcY = minY - destY;
                clipH = destY + (LONG)srcH - minY;
                destY = minY;
            }
            else
            {
                srcY = 0;
                clipH = (LONG)srcH;
            }

            if (destX + clipW > maxX + 1)
                clipW = maxX + 1 - destX;
            if (destY + clipH > maxY + 1)
                clipH = maxY + 1 - destY;

            if (clipW > 0 && clipH > 0 && srcX >= 0 && srcY >= 0)
            {
                OOP_Class *srccl = OOP_OCLASS(n->bm);
                struct HIDDIntelBitMapData *srcdata;
                APTR srcbuf;
                ULONG srcpitch;
                LONG cy;
                UBYTE srcbpp;

                srcdata = OOP_INST_DATA(srccl, n->bm);
                srcbpp = srcdata->bytesperpixel;
                srcpitch = srcdata->pitch;

                if (!srcdata->is_framebuffer)
                {
                    if (!srcdata->bo)
                        continue;
                    if (!srcdata->bo->virtual)
                        drm_intel_gem_bo_map_gtt(srcdata->bo);
                }

                srcbuf = BM_VIRTUAL(srcdata);

                if (srcbuf && fbptr && srcbpp == bpp)
                {
                    for (cy = 0; cy < clipH; cy++)
                    {
                        CopyMem(
                            (APTR)((IPTR)srcbuf + (srcY + cy) * srcpitch + srcX * bpp),
                            (APTR)((IPTR)fbptr + (destY + cy) * pitch + destX * bpp),
                            clipW * bpp);
                    }
                }
            }
        }
    }

    ReleaseSemaphore(&globalLock);
}

static VOID HIDDCompositorRedrawVisibleScreen(
    struct HIDDCompositorData *compdata)
{
    bug("[IntelCompositor] RedrawVisibleScreen\n");

    HIDDCompositorRedrawRegion(compdata, 0, 0,
        compdata->screenrect.MaxX, compdata->screenrect.MaxY);
}

 /*********************************************************************************************/

static BOOL HIDDCompositorTopBitMapChanged(
    struct HIDDCompositorData *compdata, OOP_Object *bm)
{
    OOP_Object *sync = NULL, *pf = NULL;
    IPTR modeid, hdisp, vdisp, e, depth;

    OOP_GetAttr(bm, aHidd_BitMap_GfxHidd, &e);
    if (compdata->gfx != (OOP_Object *)e)
    {
        bug("[IntelCompositor] GfxHidd mismatch\n");
        return FALSE;
    }

    OOP_GetAttr(bm, aHidd_BitMap_ModeID, &modeid);
    if (modeid == vHidd_ModeID_Invalid)
    {
        bug("[IntelCompositor] Invalid ModeID\n");
        return FALSE;
    }

    if (compdata->screenbitmap == compdata->topbitmap)
        compdata->screenbitmap = NULL;

    compdata->topbitmap = bm;

    if (modeid == compdata->screenmodeid)
        return TRUE;

    {
        struct pHidd_Gfx_GetMode gm = {
            modeID: modeid, syncPtr: &sync, pixFmtPtr: &pf,
        };
        struct TagItem gctags[] = {
            { aHidd_GC_Foreground, (HIDDT_Pixel)0x99999999 },
            { aHidd_GC_Background, (HIDDT_Pixel)0x99999999 },
            { TAG_DONE, TAG_DONE }
        };

        gm.mID = OOP_GetMethodID(IID_Hidd_Gfx, moHidd_Gfx_GetMode);
        OOP_DoMethod(compdata->gfx, (OOP_Msg)&gm);

        OOP_GetAttr(sync, aHidd_Sync_HDisp, &hdisp);
        OOP_GetAttr(sync, aHidd_Sync_VDisp, &vdisp);
        OOP_GetAttr(pf, aHidd_PixFmt_Depth, &depth);

        compdata->screenmodeid      = modeid;
        compdata->screenrect.MinX   = 0;
        compdata->screenrect.MinY   = 0;
        compdata->screenrect.MaxX   = hdisp - 1;
        compdata->screenrect.MaxY   = vdisp - 1;
        compdata->modeschanged      = TRUE;

        if (depth < 24)
        {
            gctags[0].ti_Data = (HIDDT_Pixel)0x9492;
            gctags[1].ti_Data = (HIDDT_Pixel)0x9492;
        }
        OOP_SetAttrs(compdata->gc, gctags);
    }

    return TRUE;
}

static BOOL HIDDCompositorCanCompositeWithScreenBitMap(
    struct HIDDCompositorData *compdata, OOP_Object *bm)
{
    IPTR bmgfx;
    OOP_GetAttr(bm, aHidd_BitMap_GfxHidd, &bmgfx);
    return compdata->gfx == (OOP_Object *)bmgfx;
}

static VOID HIDDCompositorSetCursorVisible(
    struct HIDDCompositorData *compdata, BOOL visible)
{
    /*
     * The mouse pointer is drawn into the GTT framebuffer by fakegfx.hidd
     * (software cursor). Scrolling or recompositing the framebuffer would
     * smear the cursor pixels, so the drag path hides the cursor around its
     * fb manipulation. We reach fakegfx through the composited framebuffer:
     * when fakegfx wraps our driver, the framebuffer bitmap's GfxHidd is the
     * fakegfx object itself.
     */
    OOP_Object *fb = compdata->compositedbitmap;

    if (fb)
    {
        IPTR fbglx = 0;

        OOP_GetAttr(fb, aHidd_BitMap_GfxHidd, &fbglx);
        if (fbglx && (OOP_Object *)fbglx != compdata->gfx)
            HIDD_Gfx_SetCursorVisible((OOP_Object *)fbglx, visible);
    }
}

static VOID HIDDCompositorToggleCompositing(
    struct HIDDCompositorData *compdata)
{
    OOP_Object *oldscreenbitmap = compdata->screenbitmap;
    OOP_Object *oldcompositedbitmap = NULL;

    bug("[IntelCompositor] ToggleCompositing modeschanged=%d\n",
        compdata->modeschanged);

    if (compdata->compositedbitmap && compdata->modeschanged)
    {
        oldcompositedbitmap = compdata->compositedbitmap;
        compdata->compositedbitmap = NULL;
    }

    if (compdata->compositedbitmap == NULL)
    {
        OOP_Object *gfx = compdata->gfx;
        struct HIDDIntelData *gfxdata = OOP_INST_DATA(OOP_OCLASS(gfx), gfx);

        compdata->compositing_active = TRUE;

        if (gfxdata->framebuffer)
        {
            compdata->compositedbitmap = gfxdata->framebuffer;
        }
        else if (compdata->screenmodeid != vHidd_ModeID_Invalid)
        {
            struct TagItem bmtags[5] = {
                { aHidd_BitMap_Width, compdata->screenrect.MaxX + 1 },
                { aHidd_BitMap_Height, compdata->screenrect.MaxY + 1 },
                { aHidd_BitMap_Displayable, TRUE },
                { aHidd_BitMap_ModeID, compdata->screenmodeid },
                { TAG_DONE, TAG_DONE }
            };

            compdata->compositedbitmap = HIDD_Gfx_CreateObject(gfx,
                SD(OOP_OCLASS(gfx))->basebm, bmtags);
        }
    }

    compdata->screenbitmap = compdata->compositedbitmap;

    if (oldscreenbitmap != compdata->screenbitmap)
        HIDDIntelSwitchToVideoMode(compdata->screenbitmap);

    if (oldcompositedbitmap)
        OOP_DisposeObject(oldcompositedbitmap);

    compdata->modeschanged = FALSE;
}

static VOID HIDDCompositorPurgeBitMapStack(
    struct HIDDCompositorData *compdata)
{
    struct StackBitMapNode *curr, *next;
    ForeachNodeSafe(&compdata->bitmapstack, curr, next)
    {
        if (curr->screenregion)
            DisposeRegion(curr->screenregion);
        Remove((struct Node *)curr);
        FreeMem(curr, sizeof(struct StackBitMapNode));
    }
    NEWLIST(&compdata->bitmapstack);
}

static VOID HIDDCompositorEnsureInit(
    struct HIDDCompositorData *compdata)
{
    OOP_Object *gfx = compdata->gfx;
    struct HIDDIntelData *gfxdata;
    IPTR w, h;

    if (compdata->compositing_active)
        return;

    gfxdata = OOP_INST_DATA(OOP_OCLASS(gfx), gfx);

    if (compdata->screenrect.MaxX == 0 && gfxdata->framebuffer)
    {
        OOP_GetAttr(gfxdata->framebuffer, aHidd_BitMap_Width, &w);
        OOP_GetAttr(gfxdata->framebuffer, aHidd_BitMap_Height, &h);
        compdata->screenrect.MinX = 0;
        compdata->screenrect.MinY = 0;
        compdata->screenrect.MaxX = w - 1;
        compdata->screenrect.MaxY = h - 1;
    }

    if (gfxdata->framebuffer)
        compdata->compositedbitmap = gfxdata->framebuffer;
    else if (compdata->screenmodeid != vHidd_ModeID_Invalid)
    {
        struct TagItem bmtags[5] = {
            { aHidd_BitMap_Width, compdata->screenrect.MaxX + 1 },
            { aHidd_BitMap_Height, compdata->screenrect.MaxY + 1 },
            { aHidd_BitMap_Displayable, TRUE },
            { aHidd_BitMap_ModeID, compdata->screenmodeid },
            { TAG_DONE, TAG_DONE }
        };
        compdata->compositedbitmap = HIDD_Gfx_CreateObject(gfx,
            SD(OOP_OCLASS(gfx))->basebm, bmtags);
    }

    if (compdata->compositedbitmap)
    {
        compdata->screenbitmap = compdata->compositedbitmap;
        compdata->compositing_active = TRUE;
        bug("[IntelCompositor] EnsureInit done bm=0x%p\n",
            compdata->compositedbitmap);
    }
}

/*********************************************************************************************/
/* PUBLIC METHODS */

OOP_Object *METHOD(Compositor, Root, New)
{
    bug("[IntelCompositor] Root::New ENTER\n");
    o = (OOP_Object *)OOP_DoSuperMethod(cl, o, (OOP_Msg)msg);

    if (o)
    {
        struct HIDDCompositorData *compdata = OOP_INST_DATA(cl, o);
        struct OOP_ABDescr attrbases[] = {
            { IID_Hidd_PixFmt,      &compdata->pixFmtAttrBase },
            { IID_Hidd_Sync,        &compdata->syncAttrBase },
            { IID_Hidd_BitMap,      &compdata->bitMapAttrBase },
            { IID_Hidd_GC,          &compdata->gcAttrBase },
            { IID_Hidd_Compositor,  &compdata->compositorAttrBase },
            { NULL, NULL }
        };

        NEWLIST(&compdata->bitmapstack);
        compdata->compositedbitmap  = NULL;
        compdata->topbitmap         = NULL;
        compdata->screenbitmap      = NULL;
        compdata->fb                = NULL;
        compdata->compositing_active = FALSE;
        compdata->screenmodeid      = vHidd_ModeID_Invalid;
        compdata->screenrect.MaxX   = 0;
        compdata->screenrect.MaxY   = 0;
        compdata->modeschanged      = FALSE;
        compdata->dirtyregion       = NULL;
        compdata->backgroundregion  = NULL;
        InitSemaphore(&compdata->semaphore);

        if (OOP_ObtainAttrBases(attrbases))
        {
            compdata->gfx = (OOP_Object *)GetTagData(
                aHidd_Compositor_GfxHidd, 0, msg->attrList);

            if (compdata->gfx)
                compdata->gc = HIDD_Gfx_CreateObject(compdata->gfx,
                    SD(cl)->basegc, NULL);
        }

        if (!compdata->gfx || !compdata->gc)
        {
            OOP_MethodID m = OOP_GetMethodID(IID_Root, moRoot_Dispose);
            OOP_CoerceMethod(cl, o, (OOP_Msg)&m);
            o = NULL;
        }
    }

    return o;
}

/*********************************************************************************************/

OOP_Object *METHOD(Compositor, Hidd_Compositor, BitMapStackChanged)
{
#if defined(INTEL_NO_COMPOSITOR)
    return NULL;
#else
    struct HIDD_ViewPortData *vpdata;
    struct HIDDCompositorData *compdata = OOP_INST_DATA(cl, o);
    struct StackBitMapNode *n = NULL;

    bug("[IntelCompositor] BitMapStackChanged top=0x%p\n",
        msg->data ? msg->data->Bitmap : NULL);

    bug("[IntelCompositor]   compdata->gfx=0x%p\n", compdata->gfx);

    {
        struct HIDD_ViewPortData *vp = msg->data;
        IPTR gfxid = 0;
        LONG idx = 0;
        while (vp)
        {
            OOP_GetAttr(vp->Bitmap, aHidd_BitMap_GfxHidd, &gfxid);
            bug("[IntelCompositor]   chain[%ld] bm=0x%p gfx=0x%p canComposite=%d\n",
                idx, vp->Bitmap, (void *)gfxid,
                HIDDCompositorCanCompositeWithScreenBitMap(compdata, vp->Bitmap));
            vp = vp->Next;
            idx++;
        }
    }

    LOCK_COMPOSITOR_WRITE

    HIDDCompositorPurgeBitMapStack(compdata);

    if (!msg->data)
    {
        UNLOCK_COMPOSITOR
        return NULL;
    }

    if (!HIDDCompositorTopBitMapChanged(compdata, msg->data->Bitmap))
    {
        UNLOCK_COMPOSITOR
        return NULL;
    }

    for (vpdata = msg->data; vpdata; vpdata = vpdata->Next)
    {
        if (HIDDCompositorCanCompositeWithScreenBitMap(compdata, vpdata->Bitmap))
        {
            n = AllocMem(sizeof(struct StackBitMapNode), MEMF_ANY | MEMF_CLEAR);
            if (!n) continue;

            n->bm               = vpdata->Bitmap;
            n->isscreenvisible  = FALSE;
            n->screenregion     = NULL;
            n->displayedwidth   = compdata->screenrect.MaxX + 1;
            n->displayedheight  = compdata->screenrect.MaxY + 1;
            n->sbmflags         = 0;
            AddTail(&compdata->bitmapstack, (struct Node *)n);
        }
    }

    ForeachNode(&compdata->bitmapstack, n)
    {
        LONG newxoffset, newyoffset;
        IPTR val;

        OOP_GetAttr(n->bm, aHidd_BitMap_TopEdge, &val);
        newyoffset = (LONG)val;
        OOP_GetAttr(n->bm, aHidd_BitMap_LeftEdge, &val);
        newxoffset = (LONG)val;

        HIDDCompositorValidateBitMapPositionChange(compdata, n->bm,
            &newxoffset, &newyoffset);

        HIDDIntelSetOffsets(n->bm, newxoffset, newyoffset);
        n->leftedge = newxoffset;
        n->topedge = newyoffset;
    }

    /*
     * Only record the stack here. The frontmost screen is displayed by
     * graphics.library's HIDD_Gfx_Show() path (ShowViewPorts returns FALSE),
     * so we must not redraw/toggle mode from here or we'd wipe the software
     * cursor drawn by fakegfx. Per-bitmap events (position changes, rect
     * updates) trigger the actual compositing.
     */

    bug("[IntelCompositor] BitMapStackChanged done, %d bitmaps\n",
        compdata->bitmapstack.lh_Head->ln_Succ ? 1 : 0);

    UNLOCK_COMPOSITOR
    return NULL;
#endif
}

/*********************************************************************************************/

VOID METHOD(Compositor, Hidd_Compositor, BitMapRectChanged)
{
#if defined(INTEL_NO_COMPOSITOR)
    return;
#else
    struct HIDDCompositorData *compdata = OOP_INST_DATA(cl, o);
    struct StackBitMapNode *n;
    LONG destX, destY, clipW, clipH, srcX, srcY;
    LONG screenw, screenh;

    bug("[IntelCompositor] BitMapRectChanged bm=0x%p [%ld,%ld %ldx%ld]\n",
        msg->bm, msg->x, msg->y, msg->width, msg->height);

    LOCK_COMPOSITOR_WRITE

    if (!compdata->compositing_active || !compdata->compositedbitmap)
    {
        UNLOCK_COMPOSITOR
        return;
    }

    n = HIDDCompositorIsBitMapOnStack(compdata, msg->bm);

    if (n && (n->sbmflags & STACKNODEF_VISIBLE))
    {
        screenw = compdata->screenrect.MaxX + 1;
        screenh = compdata->screenrect.MaxY + 1;

        destX = n->leftedge + msg->x;
        destY = n->topedge + msg->y;
        srcX = msg->x;
        srcY = msg->y;
        clipW = msg->width;
        clipH = msg->height;

        if (destX < 0) { srcX -= destX; clipW += destX; destX = 0; }
        if (destY < 0) { srcY -= destY; clipH += destY; destY = 0; }
        if (destX + clipW > screenw) clipW = screenw - destX;
        if (destY + clipH > screenh) clipH = screenh - destY;

        if (clipW > 0 && clipH > 0 && srcX >= 0 && srcY >= 0)
        {
            OOP_Class *srccl = OOP_OCLASS(msg->bm);
            struct HIDDIntelBitMapData *srcdata;
            OOP_Class *fbcl;
            struct HIDDIntelBitMapData *fbdata;
            APTR srcbuf, destbuf;
            LONG cy;

            srcdata = OOP_INST_DATA(srccl, msg->bm);

            if (!srcdata->is_framebuffer)
            {
                if (!srcdata->bo)
                {
                    UNLOCK_COMPOSITOR
                    return;
                }
                if (!srcdata->bo->virtual)
                    drm_intel_gem_bo_map_gtt(srcdata->bo);
            }

            srcbuf = BM_VIRTUAL(srcdata);

            fbcl = OOP_OCLASS(compdata->compositedbitmap);
            fbdata = OOP_INST_DATA(fbcl, compdata->compositedbitmap);
            destbuf = BM_VIRTUAL(fbdata);

            if (srcbuf && destbuf)
            {
                ObtainSemaphore(&globalLock);
                for (cy = 0; cy < clipH; cy++)
                    CopyMem(
                        (APTR)((IPTR)srcbuf + (srcY + cy) * srcdata->pitch + srcX * srcdata->bytesperpixel),
                        (APTR)((IPTR)destbuf + (destY + cy) * fbdata->pitch + destX * fbdata->bytesperpixel),
                        clipW * fbdata->bytesperpixel);
                ReleaseSemaphore(&globalLock);
            }
        }
    }

    UNLOCK_COMPOSITOR
#endif
}

/*********************************************************************************************/

VOID METHOD(Compositor, Hidd_Compositor, BitMapPositionChanged)
{
#if defined(INTEL_NO_COMPOSITOR)
    return;
#else
    struct HIDDCompositorData *compdata = OOP_INST_DATA(cl, o);
    struct StackBitMapNode *n;

    bug("[IntelCompositor] BitMapPositionChanged bm=0x%p\n", msg->bm);

    LOCK_COMPOSITOR_WRITE

    HIDDCompositorEnsureInit(compdata);

    if (!compdata->compositing_active || !compdata->compositedbitmap)
    {
        UNLOCK_COMPOSITOR
        return;
    }

    n = HIDDCompositorIsBitMapOnStack(compdata, msg->bm);

    if (n)
    {
        IPTR new_leftedge, new_topedge;
        LONG oldleftedge, oldtopedge;

        OOP_GetAttr(msg->bm, aHidd_BitMap_LeftEdge, &new_leftedge);
        OOP_GetAttr(msg->bm, aHidd_BitMap_TopEdge, &new_topedge);

        oldleftedge = n->leftedge;
        oldtopedge = n->topedge;

        if (oldleftedge != (LONG)new_leftedge ||
            oldtopedge != (LONG)new_topedge)
        {
            LONG screenw = compdata->screenrect.MaxX + 1;
            LONG screenh = compdata->screenrect.MaxY + 1;

            n->leftedge = (LONG)new_leftedge;
            n->topedge = (LONG)new_topedge;

            /*
             * Screen drag. The GTT framebuffer already contains the full
             * composite (front screen + its windows, which are blitted into
             * the fb directly by the render path). We scroll the existing fb
             * content by the delta so the windows travel together with the
             * dragged screen, then repaint only the strip revealed at the
             * top/bottom with the backdrop color and the screen behind.
             *
             * The mouse pointer is a fakegfx software cursor drawn into the
             * fb. Scrolling the fb would smear its pixels, so we hide the
             * cursor around the fb manipulation and restore it afterwards.
             */
            if (oldleftedge == n->leftedge &&
                n->topedge >= 0 && n->topedge < screenh)
            {
                LONG dy = n->topedge - oldtopedge;
                LONG top = (oldtopedge < 0) ? 0 : oldtopedge;
                LONG clipTop, clipBot;
                OOP_Class *fbcl;
                struct HIDDIntelBitMapData *fbdata;
                APTR fbptr;
                ULONG pitch, bpp;
                LONG y;

                HIDDCompositorSetCursorVisible(compdata, FALSE);

                fbcl = OOP_OCLASS(compdata->compositedbitmap);
                fbdata = OOP_INST_DATA(fbcl, compdata->compositedbitmap);
                fbptr = BM_VIRTUAL(fbdata);
                bpp = fbdata->bytesperpixel;
                pitch = fbdata->pitch;

                if (dy != 0 && fbptr)
                {
                    if (dy > 0)
                    {
                        for (y = (LONG)screenh - 1; y >= top + dy; y--)
                            CopyMem(
                                (APTR)((IPTR)fbptr + (y - dy) * pitch),
                                (APTR)((IPTR)fbptr + y * pitch),
                                screenw * bpp);
                    }
                    else
                    {
                        for (y = top; y < (LONG)screenh + dy; y++)
                            CopyMem(
                                (APTR)((IPTR)fbptr + (y - dy) * pitch),
                                (APTR)((IPTR)fbptr + y * pitch),
                                screenw * bpp);
                    }

                    /*
                     * Repaint the revealed strip. During a drag-down the strip
                     * above the dragged screen shows the screen that lies
                     * behind it; here that is the same Workbench bitmap, so we
                     * reveal its own top rows (the Workbench screen title bar)
                     * instead of a flat backdrop color.
                     */
                    clipTop = (dy > 0) ? top : screenh + dy;
                    clipBot = (dy > 0) ? top + dy - 1 : screenh - 1;
                    if (clipTop < 0) clipTop = 0;
                    if (clipBot >= screenh) clipBot = screenh - 1;

                    if (clipTop <= clipBot)
                    {
                        LONG stripH = clipBot - clipTop + 1;
                        OOP_Class *srccl;
                        struct HIDDIntelBitMapData *srcdata;
                        APTR srcbuf;

                        /*
                         * Reveal the moved screen's own top rows (its title
                         * bar) in the exposed strip, so dragging down looks
                         * like the Workbench screen is revealed behind the
                         * dragged screen, exactly as the generic compositor
                         * shows the screen underneath.
                         */
                        srccl = OOP_OCLASS(n->bm);
                        srcdata = OOP_INST_DATA(srccl, n->bm);

                        if (!srcdata->is_framebuffer)
                        {
                            if (srcdata->bo && !srcdata->bo->virtual)
                                drm_intel_gem_bo_map_gtt(srcdata->bo);
                        }

                        srcbuf = BM_VIRTUAL(srcdata);

                        if (srcbuf && (dy > 0))
                        {
                            for (y = 0; y < stripH && y < (LONG)screenh; y++)
                            {
                                LONG srcy = y;
                                if (srcy >= (LONG)srcdata->drawable.height)
                                    srcy = srcdata->drawable.height - 1;
                                CopyMem(
                                    (APTR)((IPTR)srcbuf + srcy * srcdata->pitch),
                                    (APTR)((IPTR)fbptr + (clipTop + y) * pitch),
                                    screenw * bpp);
                            }
                        }
                        else
                        {
                            HIDDCompositorRedrawRegion(compdata,
                                0, clipTop, compdata->screenrect.MaxX, clipBot);
                        }
                    }
                }

                HIDDCompositorSetCursorVisible(compdata, TRUE);
            }
            else
            {
                /* Horizontal move or off-screen: fall back to full redraw */
                HIDDCompositorSetCursorVisible(compdata, FALSE);
                HIDDCompositorRedrawVisibleScreen(compdata);
                HIDDCompositorSetCursorVisible(compdata, TRUE);
            }
        }
    }
    else
    {
        n = AllocMem(sizeof(struct StackBitMapNode), MEMF_ANY | MEMF_CLEAR);
        if (n)
        {
            IPTR val;

            n->bm = msg->bm;
            n->isscreenvisible = FALSE;
            n->screenregion = NULL;
            n->displayedwidth = compdata->screenrect.MaxX + 1;
            n->displayedheight = compdata->screenrect.MaxY + 1;
            n->sbmflags = 0;
            OOP_GetAttr(msg->bm, aHidd_BitMap_LeftEdge, &val);
            n->leftedge = (LONG)val;
            OOP_GetAttr(msg->bm, aHidd_BitMap_TopEdge, &val);
            n->topedge = (LONG)val;
            AddTail(&compdata->bitmapstack, (struct Node *)n);
        }
    }

    UNLOCK_COMPOSITOR
#endif
}

/*********************************************************************************************/

VOID METHOD(Compositor, Hidd_Compositor, ValidateBitMapPositionChange)
{
#if defined(INTEL_NO_COMPOSITOR)
    return;
#else
    struct HIDDCompositorData *compdata = OOP_INST_DATA(cl, o);

    LOCK_COMPOSITOR_WRITE
    HIDDCompositorValidateBitMapPositionChange(compdata, msg->bm,
        msg->newxoffset, msg->newyoffset);
    UNLOCK_COMPOSITOR
#endif
}
