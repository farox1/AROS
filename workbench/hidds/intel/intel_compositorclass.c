/*
    Copyright (C) 2010-2026, The AROS Development Team. All rights reserved.
*/

/* 
    Compositor class for Intel driver.
    Based on the nouveau compositor with intel-specific calls.
*/

#include "intel_intern.h"
#include "intel_compositor.h"

#include <proto/exec.h>
#include <aros/debug.h>
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

#define MAX(a,b) a > b ? a : b
#define MIN(a,b) a < b ? a : b

static BOOL AndRectRect(struct _Rectangle *rect1, struct _Rectangle *rect2,
    struct _Rectangle *intersect)
{
    intersect->MinX = MAX(rect1->MinX, rect2->MinX);
    intersect->MinY = MAX(rect1->MinY, rect2->MinY);
    intersect->MaxX = MIN(rect1->MaxX, rect2->MaxX);
    intersect->MaxY = MIN(rect1->MaxY, rect2->MaxY);
    
    if ((intersect->MinX > intersect->MaxX) ||
        (intersect->MinY > intersect->MaxY))
        return FALSE;
    else
        return TRUE;
}

static struct StackBitMapNode *HIDDCompositorIsBitMapOnStack(struct HIDDCompositorData *compdata, OOP_Object *bm)
{
    struct StackBitMapNode *n = NULL;
    
    ForeachNode(&compdata->bitmapstack, n)
    {
        if (n->bm == bm)
            return n;
    }

    return NULL;
}

static VOID HIDDCompositorValidateBitMapPositionChange(struct HIDDCompositorData *compdata, OOP_Object *bm,
    LONG *newxoffset, LONG *newyoffset)
{
    struct StackBitMapNode *n = NULL;

    if ((n = HIDDCompositorIsBitMapOnStack(compdata, bm)) != NULL)
    {
        IPTR width, height;
        LONG limit;
        
        OOP_GetAttr(bm, aHidd_BitMap_Width, &width);
        OOP_GetAttr(bm, aHidd_BitMap_Height, &height);
        
        limit = n->displayedwidth - width;
        if (*(newxoffset) > 0)
            *(newxoffset) = 0;

        if (*(newxoffset) < limit)
            *(newxoffset) = limit;

        limit = n->displayedheight - height;
        if (*(newyoffset) > n->displayedheight - 15)
            *(newyoffset) = n->displayedheight - 15;

        if (*(newyoffset) < limit)
            *(newyoffset) = limit;
    }
}

static VOID HIDDCompositorRecalculateVisibleRects(struct HIDDCompositorData *compdata)
{
    ULONG lastscreenvisibleline = compdata->screenrect.MaxY;
    struct StackBitMapNode *n = NULL;
    
    ForeachNode(&compdata->bitmapstack, n)
    {
        IPTR topedge;
        struct _Rectangle tmprect;
        
        OOP_GetAttr(n->bm, aHidd_BitMap_TopEdge, &topedge);
        tmprect = compdata->screenrect;
        tmprect.MinY = topedge;
        tmprect.MaxY = lastscreenvisibleline;
        if (AndRectRect(&tmprect, &compdata->screenrect, &n->screenvisiblerect))
        {
            lastscreenvisibleline = n->screenvisiblerect.MinY - 1;
            n->isscreenvisible = TRUE;
        }
        else
            n->isscreenvisible = FALSE;
    }
}

static BOOL HIDDCompositorTopBitMapChanged(struct HIDDCompositorData *compdata, OOP_Object *bm)
{
    OOP_Object *sync = NULL;
    OOP_Object *pf = NULL;
    IPTR modeid, hdisp, vdisp, e, depth;

    OOP_GetAttr(bm, aHidd_BitMap_GfxHidd, &e);

    if (compdata->gfx != (OOP_Object *)e)
    {
        D(bug("[Compositor] GfxHidd different than one used by compositor\n"));
        return FALSE;
    }
    
    OOP_GetAttr(bm, aHidd_BitMap_ModeID, &modeid);
    if (modeid == vHidd_ModeID_Invalid)
    {
        D(bug("[Compositor] Invalid ModeID\n"));
        return FALSE;
    }
    
    if (compdata->screenbitmap == compdata->topbitmap)
        compdata->screenbitmap = NULL;

    compdata->topbitmap = bm;

    if (modeid == compdata->screenmodeid)
        return TRUE;

    {
        struct pHidd_Gfx_GetMode __getmodemsg =
        {
            modeID:     modeid,
            syncPtr:    &sync,
            pixFmtPtr:  &pf,
        }, *getmodemsg = &__getmodemsg;
        struct TagItem gctags[] =
        {
            { aHidd_GC_Foreground, (HIDDT_Pixel)0x99999999 },
            { TAG_DONE, TAG_DONE }
        };

        getmodemsg->mID = OOP_GetMethodID(IID_Hidd_Gfx, moHidd_Gfx_GetMode);
        OOP_DoMethod(compdata->gfx, (OOP_Msg)getmodemsg);

        OOP_GetAttr(sync, aHidd_Sync_HDisp, &hdisp);
        OOP_GetAttr(sync, aHidd_Sync_VDisp, &vdisp);
        OOP_GetAttr(pf, aHidd_PixFmt_Depth, &depth);

        compdata->screenmodeid      = modeid;
        compdata->screenrect.MinX   = 0;
        compdata->screenrect.MinY   = 0;
        compdata->screenrect.MaxX   = hdisp - 1;
        compdata->screenrect.MaxY   = vdisp - 1;
        compdata->modeschanged      = TRUE;
        
        if (depth < 24) gctags[0].ti_Data = (HIDDT_Pixel)0x9492;
        
        OOP_SetAttrs(compdata->gc, gctags);
    }

    return TRUE;
}

static BOOL HIDDCompositorCanCompositeWithScreenBitMap(struct HIDDCompositorData *compdata, OOP_Object *bm)
{
    OOP_Object *screenbm = compdata->topbitmap;
    IPTR screenbmwidth, screenbmheight, screenbmstdpixfmt;
    IPTR bmgfx, bmmodeid, bmwidth, bmheight, bmstdpixfmt;

    {
        IPTR pf;
        screenbmwidth   = compdata->screenrect.MaxX + 1;
        screenbmheight  = compdata->screenrect.MaxY + 1;
        OOP_GetAttr(screenbm, aHidd_BitMap_PixFmt, &pf);
        OOP_GetAttr((OOP_Object*)pf, aHidd_PixFmt_StdPixFmt, &screenbmstdpixfmt);
    }

    {
        IPTR pf;
        OOP_GetAttr(bm, aHidd_BitMap_GfxHidd, &bmgfx);
        OOP_GetAttr(bm, aHidd_BitMap_ModeID, &bmmodeid);
        OOP_GetAttr(bm, aHidd_BitMap_Width, &bmwidth);
        OOP_GetAttr(bm, aHidd_BitMap_Height, &bmheight);
        OOP_GetAttr(bm, aHidd_BitMap_PixFmt, &pf);
        OOP_GetAttr((OOP_Object*)pf, aHidd_PixFmt_StdPixFmt, &bmstdpixfmt);
    }

    if (compdata->gfx != (OOP_Object *)bmgfx)
        return FALSE;
    
    if (compdata->screenmodeid == bmmodeid)
        return TRUE;

    if (screenbmstdpixfmt != bmstdpixfmt)
        return FALSE;

    if ((screenbmwidth <= bmwidth) && (screenbmheight <= bmheight))
        return TRUE;

    return FALSE;
}

static VOID HIDDCompositorRedrawBitmap(struct HIDDCompositorData *compdata,
    OOP_Object *bm, WORD x, WORD y, WORD width, WORD height)
{
    struct StackBitMapNode *n = NULL;
    
    if (compdata->screenbitmap != compdata->compositedbitmap)
        return;
    
    if ((n = HIDDCompositorIsBitMapOnStack(compdata, bm)) == NULL)
        return;

    if (!n->isscreenvisible)
        return;

    if (compdata->compositedbitmap)
    {
        IPTR leftedge, topedge;
        struct _Rectangle srcrect;
        struct _Rectangle srcindstrect;
        struct _Rectangle dstandvisrect;

        OOP_GetAttr(bm, aHidd_BitMap_LeftEdge, &leftedge);
        OOP_GetAttr(bm, aHidd_BitMap_TopEdge, &topedge);
        
        srcrect.MinX = x;
        srcrect.MinY = y;
        srcrect.MaxX = x + width - 1;
        srcrect.MaxY = y + height - 1;
        
        srcindstrect.MinX = srcrect.MinX + leftedge;
        srcindstrect.MaxX = srcrect.MaxX + leftedge;
        srcindstrect.MinY = srcrect.MinY + topedge;
        srcindstrect.MaxY = srcrect.MaxY + topedge;
        
        if (AndRectRect(&srcindstrect, &n->screenvisiblerect, &dstandvisrect))
        {
            HIDD_Gfx_CopyBox(
                compdata->gfx,
                bm,
                dstandvisrect.MinX - leftedge, dstandvisrect.MinY - topedge,
                compdata->compositedbitmap,
                dstandvisrect.MinX, dstandvisrect.MinY,
                dstandvisrect.MaxX - dstandvisrect.MinX + 1,
                dstandvisrect.MaxY - dstandvisrect.MinY + 1,
                compdata->gc);
        }
    }
}

static VOID HIDDCompositorRedrawVisibleScreen(struct HIDDCompositorData *compdata)
{
    struct StackBitMapNode *n = NULL;
    ULONG lastscreenvisibleline = compdata->screenrect.MaxY;
    
    HIDDCompositorRecalculateVisibleRects(compdata);
    
    ForeachNode(&compdata->bitmapstack, n)
    {
        if (n->isscreenvisible)
        {
            IPTR width, height;
            OOP_GetAttr(n->bm, aHidd_BitMap_Width, &width);
            OOP_GetAttr(n->bm, aHidd_BitMap_Height, &height);

            HIDDCompositorRedrawBitmap(compdata, n->bm, 0, 0, width, height);
            if (lastscreenvisibleline > n->screenvisiblerect.MinY)
                lastscreenvisibleline = n->screenvisiblerect.MinY;
        }
    }

    if ((compdata->screenbitmap == compdata->compositedbitmap) && (lastscreenvisibleline > 1))
    {
        HIDD_BM_FillRect(compdata->compositedbitmap,
            compdata->gc, 0, 0, compdata->screenrect.MaxX, lastscreenvisibleline - 1);
    }
}

static VOID HIDDCompositorToggleCompositing(struct HIDDCompositorData *compdata)
{
    IPTR topedge;
    OOP_Object *oldscreenbitmap = compdata->screenbitmap;
    OOP_Object *oldcompositedbitmap = NULL;
    
    OOP_GetAttr(compdata->topbitmap, aHidd_BitMap_TopEdge, &topedge);

    if ((compdata->compositedbitmap) && (compdata->modeschanged))
    {
        oldcompositedbitmap = compdata->compositedbitmap;
        compdata->compositedbitmap = NULL;
    }

    if ((LONG)topedge > (LONG)0)
    {
        if (compdata->compositedbitmap == NULL)
        {
            struct TagItem bmtags[5];
            
            bmtags[0].ti_Tag = aHidd_BitMap_Width;       bmtags[0].ti_Data = compdata->screenrect.MaxX + 1;
            bmtags[1].ti_Tag = aHidd_BitMap_Height;      bmtags[1].ti_Data = compdata->screenrect.MaxY + 1;
            bmtags[2].ti_Tag = aHidd_BitMap_Displayable; bmtags[2].ti_Data = TRUE;
            bmtags[3].ti_Tag = aHidd_BitMap_ModeID;      bmtags[3].ti_Data = compdata->screenmodeid;
            bmtags[4].ti_Tag = TAG_DONE;                 bmtags[4].ti_Data = TAG_DONE;

            compdata->compositedbitmap = HIDD_Gfx_CreateObject(compdata->gfx,
                SD(OOP_OCLASS(compdata->gfx))->basebm, bmtags);
        }
        
        if (oldscreenbitmap != compdata->compositedbitmap)
        {
            compdata->screenbitmap = compdata->compositedbitmap;
            HIDDCompositorRedrawVisibleScreen(compdata);
        }
    }
    else
        compdata->screenbitmap = compdata->topbitmap;

    if (oldscreenbitmap != compdata->screenbitmap)
        HIDDIntelSwitchToVideoMode(compdata->screenbitmap);

    if (oldcompositedbitmap)
        OOP_DisposeObject(oldcompositedbitmap);

    compdata->modeschanged = FALSE;
}

static VOID HIDDCompositorPurgeBitMapStack(struct HIDDCompositorData *compdata)
{
    struct StackBitMapNode *curr, *next;

    ForeachNodeSafe(&compdata->bitmapstack, curr, next)
    {
        Remove((struct Node *)curr);
        FreeMem(curr, sizeof(struct StackBitMapNode));
    }
    
    NEWLIST(&compdata->bitmapstack);
}

/* PUBLIC METHODS */
OOP_Object *METHOD(Compositor, Root, New)
{
    o = (OOP_Object *)OOP_DoSuperMethod(cl, o, (OOP_Msg)msg);

    if (o)
    {
        struct HIDDCompositorData *compdata = OOP_INST_DATA(cl, o);
        
        struct OOP_ABDescr attrbases[] =
        {
            { IID_Hidd_PixFmt,          &compdata->pixFmtAttrBase },
            { IID_Hidd_Sync,            &compdata->syncAttrBase },
            { IID_Hidd_BitMap,          &compdata->bitMapAttrBase },
            { IID_Hidd_GC,              &compdata->gcAttrBase },
            { IID_Hidd_Compositor,      &compdata->compositorAttrBase },
            { NULL, NULL }
        };

        NEWLIST(&compdata->bitmapstack);
        compdata->compositedbitmap  = NULL;
        compdata->topbitmap         = NULL;
        compdata->screenbitmap      = NULL;
        compdata->screenmodeid      = vHidd_ModeID_Invalid;
        compdata->modeschanged      = FALSE;
        InitSemaphore(&compdata->semaphore);
        
        if (OOP_ObtainAttrBases(attrbases))
        {
            compdata->gfx = (OOP_Object *)GetTagData(aHidd_Compositor_GfxHidd, 0, msg->attrList);
            
            if (compdata->gfx != NULL)
            {
                compdata->gc = HIDD_Gfx_CreateObject(compdata->gfx,
                    SD(cl)->basegc, NULL);
            }
        }
        
        if ((compdata->gfx == NULL) || (compdata->gc == NULL))
        {
            OOP_MethodID disposemid;
            disposemid = OOP_GetMethodID(IID_Root, moRoot_Dispose);
            OOP_CoerceMethod(cl, o, (OOP_Msg)&disposemid);
            o = NULL;
        }
    }

    return o;
}

VOID METHOD(Compositor, Hidd_Compositor, BitMapStackChanged)
{
    struct HIDD_ViewPortData *vpdata;
    struct HIDDCompositorData *compdata = OOP_INST_DATA(cl, o);
    struct StackBitMapNode *n = NULL;

    D(bug("[Compositor] BitMapStackChanged, topbitmap: 0x%p\n",
        msg->data->Bitmap));

    LOCK_COMPOSITOR_WRITE
        
    HIDDCompositorPurgeBitMapStack(compdata);
    
    if (!msg->data)
    {
        UNLOCK_COMPOSITOR
        return;
    }
    
    if (!HIDDCompositorTopBitMapChanged(compdata, msg->data->Bitmap))
    {
        D(bug("[Compositor] Failed to change top bitmap\n"));
        UNLOCK_COMPOSITOR
        return;
    }
    
    for (vpdata = msg->data; vpdata; vpdata = vpdata->Next)
    {
        D(bug("[Compositor] Testing bitmap: %p\n", vpdata->Bitmap));
        if (HIDDCompositorCanCompositeWithScreenBitMap(compdata, vpdata->Bitmap))
        {
            n = AllocMem(sizeof(struct StackBitMapNode), MEMF_ANY | MEMF_CLEAR);

            n->bm               = vpdata->Bitmap;
            n->isscreenvisible  = FALSE;
            n->displayedwidth   = compdata->screenrect.MaxX + 1;
            n->displayedheight  = compdata->screenrect.MaxY + 1;
            AddTail(&compdata->bitmapstack, (struct Node *)n);
        }
    }
    
    ForeachNode(&compdata->bitmapstack, n)
    {
        LONG newxoffset, newyoffset;
        IPTR val;
        OOP_GetAttr(n->bm, aHidd_BitMap_TopEdge, &val); newyoffset = (LONG)val;
        OOP_GetAttr(n->bm, aHidd_BitMap_LeftEdge, &val); newxoffset = (LONG)val;
        
        HIDDCompositorValidateBitMapPositionChange(compdata, n->bm,
            &newxoffset, &newyoffset);

        HIDDIntelSetOffsets(n->bm, newxoffset, newyoffset);
    }

    HIDDCompositorToggleCompositing(compdata);

    HIDDCompositorRedrawVisibleScreen(compdata);

    UNLOCK_COMPOSITOR
}

VOID METHOD(Compositor, Hidd_Compositor, BitMapRectChanged)
{
    struct HIDDCompositorData *compdata = OOP_INST_DATA(cl, o);

    LOCK_COMPOSITOR_READ

    HIDDCompositorRedrawBitmap(compdata, msg->bm, msg->x, msg->y, msg->width, msg->height);
    
    UNLOCK_COMPOSITOR
}

VOID METHOD(Compositor, Hidd_Compositor, BitMapPositionChanged)
{
    struct HIDDCompositorData *compdata = OOP_INST_DATA(cl, o);

    LOCK_COMPOSITOR_WRITE

    if (HIDDCompositorIsBitMapOnStack(compdata, msg->bm) != NULL)
    {
        if (compdata->topbitmap == msg->bm)
            HIDDCompositorToggleCompositing(compdata);

        if ((compdata->screenbitmap == compdata->topbitmap)
            && (compdata->topbitmap == msg->bm))
        {
            HIDDIntelSwitchToVideoMode(compdata->screenbitmap);
        }
        else
            HIDDCompositorRedrawVisibleScreen(compdata);
    }
    
    UNLOCK_COMPOSITOR
}

VOID METHOD(Compositor, Hidd_Compositor, ValidateBitMapPositionChange)
{
    struct HIDDCompositorData *compdata = OOP_INST_DATA(cl, o);

    LOCK_COMPOSITOR_READ
    
    HIDDCompositorValidateBitMapPositionChange(compdata, msg->bm,
        msg->newxoffset, msg->newyoffset);
    
    UNLOCK_COMPOSITOR
}
