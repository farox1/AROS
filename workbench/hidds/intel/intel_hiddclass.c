/*
    Copyright (C) 2010-2026, The AROS Development Team. All rights reserved.
*/

#include "intel_intern.h"
#include "compositor.h"



#include <graphics/displayinfo.h>
#include <proto/utility.h>

#define DEBUG 0
#include <aros/debug.h>
#include <proto/oop.h>

#include <libdrm/arosdrmmode.h>
#include <uapi/drm/i915_drm.h>

#undef HiddAttrBase
#undef HiddPixFmtAttrBase
#undef HiddGfxAttrBase
#undef HiddGfxIntelAttrBase
#undef HiddSyncAttrBase
#undef HiddBitMapAttrBase
#undef HiddCompositorAttrBase
#undef HiddBitMapIntelAttrBase

#define HiddAttrBase          (SD(cl)->hiddAttrBase)
#define HiddPixFmtAttrBase          (SD(cl)->pixFmtAttrBase)
#define HiddGfxAttrBase             (SD(cl)->gfxAttrBase)
#define HiddGfxIntelAttrBase        (SD(cl)->gfxIntelAttrBase)
#define HiddSyncAttrBase            (SD(cl)->syncAttrBase)
#define HiddBitMapAttrBase          (SD(cl)->bitMapAttrBase)
#define HiddCompositorAttrBase      (SD(cl)->compositorAttrBase)
#define HiddBitMapIntelAttrBase     (SD(cl)->bitMapIntelAttrBase)

#define MAX_BITMAP_WIDTH    4096
#define MAX_BITMAP_HEIGHT   4096

/* HELPER FUNCTIONS */
VOID HIDDIntelShowCursor(OOP_Object *gfx, BOOL visible)
{
    OOP_Class *cl = OOP_OCLASS(gfx);
    struct HIDDIntelData *gfxdata = OOP_INST_DATA(cl, gfx);
    struct CardData *carddata = &(SD(cl)->carddata);

    LOCK_ENGINE

    if (visible && gfxdata->cursor)
    {
        drmModeSetCursor(carddata->fd, gfxdata->selectedcrtcid,
            gfxdata->cursor->handle, 64, 64);
    }
    else
    {
        drmModeSetCursor(carddata->fd, gfxdata->selectedcrtcid,
            0, 64, 64);
    }

    UNLOCK_ENGINE
}

static BOOL HIDDIntelSelectConnectorCrtc(LONG fd, drmModeConnectorPtr *selectedconnector,
    drmModeCrtcPtr *selectedcrtc)
{
    *selectedconnector = NULL;
    *selectedcrtc = NULL;
    drmModeResPtr drmmode = NULL;
    LONG i; ULONG crtc_id;

    bug("[Intel] SelectConnectorCrtc\n");

    drmmode = drmModeGetResources(fd);
    if (!drmmode)
    {
        bug("[Intel] Not able to get resources information\n");
        return FALSE;
    }
    
    for (i = 0; i < drmmode->count_connectors; i++)
    {
        drmModeConnectorPtr connector = drmModeGetConnector(fd, drmmode->connectors[i]);

        if (connector)
        {
            if (connector->connection == DRM_MODE_CONNECTED)
            {
                *selectedconnector = connector;
                break;
            }
            
            drmModeFreeConnector(connector);
        }
    }
    
    if (!(*selectedconnector))
    {
        bug("[Intel] No connected connector\n");
        drmModeFreeResources(drmmode);
        return FALSE;
    }

    if (drmmode->count_crtcs > 0)
        crtc_id = drmmode->crtcs[0];
    else
        crtc_id = 0;

    *selectedcrtc = drmModeGetCrtc(fd, crtc_id);
    if (!(*selectedcrtc))
    {
        bug("[Intel] Not able to get crtc information\n");
        drmModeFreeConnector(*selectedconnector);
        *selectedconnector = NULL;
        drmModeFreeResources(drmmode);
        return FALSE;
    }
    
    drmModeFreeResources(drmmode);
    return TRUE;
}

#include <stdio.h>

static struct TagItem *HIDDIntelCreateSyncTagsFromConnector(OOP_Class *cl, drmModeConnectorPtr connector)
{
    struct TagItem *syncs = NULL;
    ULONG modescount = connector->count_modes;
    ULONG i;
    
    if (modescount == 0)
        return NULL;
        
    syncs = HIDDIntelAlloc(sizeof(struct TagItem) * modescount);
    
    for (i = 0; i < modescount; i++)
    {
        struct TagItem *sync = HIDDIntelAlloc(sizeof(struct TagItem) * 15);
        LONG j = 0;
        
        drmModeModeInfoPtr mode = &connector->modes[i];

        sync[j].ti_Tag = aHidd_Sync_PixelClock;     sync[j++].ti_Data = mode->clock;
        sync[j].ti_Tag = aHidd_Sync_HDisp;          sync[j++].ti_Data = mode->hdisplay;
        sync[j].ti_Tag = aHidd_Sync_HSyncStart;     sync[j++].ti_Data = mode->hsync_start;
        sync[j].ti_Tag = aHidd_Sync_HSyncEnd;       sync[j++].ti_Data = mode->hsync_end;
        sync[j].ti_Tag = aHidd_Sync_HTotal;         sync[j++].ti_Data = mode->htotal;
        sync[j].ti_Tag = aHidd_Sync_HMin;           sync[j++].ti_Data = mode->hdisplay;
        sync[j].ti_Tag = aHidd_Sync_HMax;           sync[j++].ti_Data = MAX_BITMAP_WIDTH;

        sync[j].ti_Tag = aHidd_Sync_VDisp;          sync[j++].ti_Data = mode->vdisplay;
        sync[j].ti_Tag = aHidd_Sync_VSyncStart;     sync[j++].ti_Data = mode->vsync_start;
        sync[j].ti_Tag = aHidd_Sync_VSyncEnd;       sync[j++].ti_Data = mode->vsync_end;
        sync[j].ti_Tag = aHidd_Sync_VTotal;         sync[j++].ti_Data = mode->vtotal;
        sync[j].ti_Tag = aHidd_Sync_VMin;           sync[j++].ti_Data = mode->vdisplay;
        sync[j].ti_Tag = aHidd_Sync_VMax;           sync[j++].ti_Data = MAX_BITMAP_HEIGHT;
        
        STRPTR syncname = HIDDIntelAlloc(32);
        sprintf(syncname, "IG:%dx%d@%d", mode->hdisplay, mode->vdisplay, mode->vrefresh);
        
        sync[j].ti_Tag = aHidd_Sync_Description;    sync[j++].ti_Data = (IPTR)syncname;
        sync[j].ti_Tag = TAG_DONE;                  sync[j++].ti_Data = 0UL;
        
        syncs[i].ti_Tag = aHidd_Gfx_SyncTags;
        syncs[i].ti_Data = (IPTR)sync;
    }
    
    return syncs;
}

static BOOL HIDDIntelShowBitmapForSelectedMode(OOP_Object *bm)
{
    OOP_Class *cl = OOP_OCLASS(bm);
    struct HIDDIntelData *gfxdata = NULL;
    struct HIDDIntelBitMapData *bmdata = OOP_INST_DATA(cl, bm);
    struct CardData *carddata = &(SD(cl)->carddata);
    uint32_t output_ids[] = {0};
    uint32_t output_count = 1;
    IPTR e = (IPTR)NULL;
    OOP_Object *gfx = NULL;
    LONG ret;

    LOCK_ENGINE
    LOCK_BITMAP
    
    if (bmdata->fbid == 0)
    {
        UNLOCK_BITMAP
        UNLOCK_ENGINE
        return FALSE;
    }
    
    OOP_GetAttr(bm, aHidd_BitMap_GfxHidd, &e);
    gfx = (OOP_Object *)e;
    gfxdata = OOP_INST_DATA(OOP_OCLASS(gfx), gfx);
    output_ids[0] = ((drmModeConnectorPtr)gfxdata->selectedconnector)->connector_id;

    ret = drmModeSetCrtc(carddata->fd, gfxdata->selectedcrtcid,
            bmdata->fbid, -bmdata->xoffset, -bmdata->yoffset, output_ids,
            output_count, gfxdata->selectedmode);

    UNLOCK_BITMAP
    UNLOCK_ENGINE

    if (ret) return FALSE; else return TRUE;
}

BOOL HIDDIntelSwitchToVideoMode(OOP_Object *bm)
{
    OOP_Class *cl = OOP_OCLASS(bm);
    struct HIDDIntelBitMapData *bmdata = OOP_INST_DATA(cl, bm);
    OOP_Object *gfx = NULL;
    struct HIDDIntelData *gfxdata = NULL;
    struct CardData *carddata = &(SD(cl)->carddata);
    LONG i;
    drmModeConnectorPtr selectedconnector = NULL;
    HIDDT_ModeID modeid;
    OOP_Object *sync;
    OOP_Object *pf;
    IPTR pixel, e;
    IPTR hdisp, vdisp, hstart, hend, htotal, vstart, vend, vtotal;
    LONG ret;

    LOCK_ENGINE

    OOP_GetAttr(bm, aHidd_BitMap_GfxHidd, &e);
    gfx = (OOP_Object *)e;
    gfxdata = OOP_INST_DATA(OOP_OCLASS(gfx), gfx);
    selectedconnector = (drmModeConnectorPtr)gfxdata->selectedconnector;

    D(bug("[Intel] HIDDIntelSwitchToVideoMode, bm: 0x%p\n", bm));
    
    OOP_GetAttr(bm, aHidd_BitMap_ModeID, &modeid);

    if (modeid == vHidd_ModeID_Invalid)
    {
        D(bug("[Intel] Invalid ModeID\n"));
        UNLOCK_ENGINE
        return FALSE;
    }

    struct pHidd_Gfx_GetMode __getmodemsg =
    {
        modeID:     modeid,
        syncPtr:    &sync,
        pixFmtPtr:  &pf,
    }, *getmodemsg = &__getmodemsg;

    getmodemsg->mID = OOP_GetMethodID(IID_Hidd_Gfx, moHidd_Gfx_GetMode);
    OOP_DoMethod(gfx, (OOP_Msg)getmodemsg);

    OOP_GetAttr(sync, aHidd_Sync_PixelClock,    &pixel);
    OOP_GetAttr(sync, aHidd_Sync_HDisp,         &hdisp);
    OOP_GetAttr(sync, aHidd_Sync_VDisp,         &vdisp);
    OOP_GetAttr(sync, aHidd_Sync_HSyncStart,    &hstart);
    OOP_GetAttr(sync, aHidd_Sync_VSyncStart,    &vstart);
    OOP_GetAttr(sync, aHidd_Sync_HSyncEnd,      &hend);
    OOP_GetAttr(sync, aHidd_Sync_VSyncEnd,      &vend);
    OOP_GetAttr(sync, aHidd_Sync_HTotal,        &htotal);
    OOP_GetAttr(sync, aHidd_Sync_VTotal,        &vtotal);
    
    D(bug("[Intel] Sync: %ld, %ld, %ld, %ld, %ld, %ld, %ld, %ld, %ld\n",
        pixel, hdisp, hstart, hend, htotal, vdisp, vstart, vend, vtotal));

    D(bug("[Intel] Connector %d, CRTC %d\n",
        selectedconnector->connector_id, gfxdata->selectedcrtcid));

    gfxdata->selectedmode = NULL;
    for (i = 0; i < selectedconnector->count_modes; i++)
    {
        drmModeModeInfoPtr mode = &selectedconnector->modes[i];
        
        if ((mode->hdisplay == hdisp) && (mode->vdisplay == vdisp) &&
            (mode->hsync_start == hstart) && (mode->vsync_start == vstart) &&
            (mode->hsync_end == hend) && (mode->vsync_end == vend))
        {
            gfxdata->selectedmode = mode;
            break;
        }
    }
    
    if (!gfxdata->selectedmode)
    {
        D(bug("[Intel] Not able to select mode\n"));
        UNLOCK_ENGINE
        return FALSE;
    }

    if (bmdata->fbid == 0)
    {
        ret = drmModeAddFB(carddata->fd, bmdata->drawable.width, bmdata->drawable.height,
                    bmdata->drawable.depth, bmdata->bytesperpixel * 8,
                    bmdata->pitch, bmdata->bo->handle, &bmdata->fbid);
        if (ret)
        {
            D(bug("[Intel] Not able to add framebuffer\n"));
            UNLOCK_ENGINE
            return FALSE;
        }
    }

    if (!HIDDIntelShowBitmapForSelectedMode(bm))
    {
        D(bug("[Intel] Not able to set crtc\n"));
        UNLOCK_ENGINE
        return FALSE;        
    }

    HIDDIntelShowCursor(gfx, TRUE);

    UNLOCK_ENGINE
    return TRUE;
}

/* PUBLIC METHODS */
OOP_Object *METHOD(Intel, Root, New)
{
    drmModeCrtcPtr selectedcrtc = NULL;
    drmModeConnectorPtr selectedconnector = NULL;
    struct TagItem *syncs = NULL;
    struct CardData *carddata = &(SD(cl)->carddata);
    ULONG selectedcrtcid;

    bug("[Intel] Root::New\n");

    intel_init();

    {
        struct TagItem sync[] = {
            { aHidd_Sync_PixelClock,    65000 },
            { aHidd_Sync_HDisp,         1024 },
            { aHidd_Sync_HSyncStart,    1048 },
            { aHidd_Sync_HSyncEnd,      1184 },
            { aHidd_Sync_HTotal,        1344 },
            { aHidd_Sync_HMin,          1024 },
            { aHidd_Sync_HMax,          4096 },
            { aHidd_Sync_VDisp,         768 },
            { aHidd_Sync_VSyncStart,    771 },
            { aHidd_Sync_VSyncEnd,      777 },
            { aHidd_Sync_VTotal,        806 },
            { aHidd_Sync_VMin,          768 },
            { aHidd_Sync_VMax,          4096 },
            { aHidd_Sync_Description,   (IPTR)"IG:1024x768@60" },
            { TAG_DONE, 0UL }
        };

        struct TagItem pftags_24bpp[] = {
            { aHidd_PixFmt_RedShift,    8   },
            { aHidd_PixFmt_GreenShift,  16  },
            { aHidd_PixFmt_BlueShift,   24  },
            { aHidd_PixFmt_AlphaShift,  0   },
            { aHidd_PixFmt_RedMask,     0x00ff0000 },
            { aHidd_PixFmt_GreenMask,   0x0000ff00 },
            { aHidd_PixFmt_BlueMask,    0x000000ff },
            { aHidd_PixFmt_AlphaMask,   0x00000000 },
            { aHidd_PixFmt_ColorModel,  vHidd_ColorModel_TrueColor },
            { aHidd_PixFmt_Depth,       24  },
            { aHidd_PixFmt_BytesPerPixel, 4 },
            { aHidd_PixFmt_BitsPerPixel,24  },
            { aHidd_PixFmt_StdPixFmt,   vHidd_StdPixFmt_BGR032 },
            { aHidd_PixFmt_BitMapType,  vHidd_BitMapType_Chunky },
            { TAG_DONE, 0UL }
        };

        struct TagItem pftags_16bpp[] = {
            { aHidd_PixFmt_RedShift,    16  },
            { aHidd_PixFmt_GreenShift,  21  },
            { aHidd_PixFmt_BlueShift,   27  },
            { aHidd_PixFmt_AlphaShift,  0   },
            { aHidd_PixFmt_RedMask,     0x0000f800 },
            { aHidd_PixFmt_GreenMask,   0x000007e0 },
            { aHidd_PixFmt_BlueMask,    0x0000001f },
            { aHidd_PixFmt_AlphaMask,   0x00000000 },
            { aHidd_PixFmt_ColorModel,  vHidd_ColorModel_TrueColor },
            { aHidd_PixFmt_Depth,       16  },
            { aHidd_PixFmt_BytesPerPixel, 2 },
            { aHidd_PixFmt_BitsPerPixel,16  },
            { aHidd_PixFmt_StdPixFmt,   vHidd_StdPixFmt_RGB16_LE },
            { aHidd_PixFmt_BitMapType,  vHidd_BitMapType_Chunky },
            { TAG_DONE, 0UL }
        };

        struct TagItem modetags[] = {
            { aHidd_Gfx_PixFmtTags, (IPTR)pftags_24bpp },
            { aHidd_Gfx_PixFmtTags, (IPTR)pftags_16bpp },
            { aHidd_Gfx_SyncTags,   (IPTR)sync },
            { TAG_DONE, 0UL }
        };

        struct TagItem mytags[] = {
            { aHidd_Gfx_ModeTags, (IPTR)modetags },
            { aHidd_Name,         (IPTR)"Intel" },
            { aHidd_HardwareName, (IPTR)"Intel Gfx" },
            { aHidd_ProducerName, (IPTR)"Intel" },
            { TAG_MORE, (IPTR)msg->attrList }
        };

        struct pRoot_New mymsg;

        mymsg.mID = msg->mID;
        mymsg.attrList = mytags;

        msg = &mymsg;

        o = (OOP_Object *)OOP_DoSuperMethod(cl, o, (OOP_Msg)msg);

        if (o)
        {
            struct HIDDIntelData *gfxdata = OOP_INST_DATA(cl, o);
            gfxdata->selectedcrtcid = 1;
            gfxdata->selectedmode = NULL;
            gfxdata->selectedconnector = NULL;
            gfxdata->cursor = NULL;
            gfxdata->compositor = NULL;
        }

        return o;
    }
}

OOP_Object *METHOD(Intel, Hidd_Gfx, CreateObject)
{
    struct HIDDIntelData *gfxdata = OOP_INST_DATA(cl, o);
    OOP_Object *object = NULL;

    if (msg->cl == SD(cl)->basebm)
    {
        struct pHidd_Gfx_CreateObject mymsg;
        HIDDT_ModeID modeid;
        HIDDT_StdPixFmt stdpf;

        struct TagItem mytags[] =
        {
            { TAG_IGNORE, TAG_IGNORE },
            { TAG_IGNORE, TAG_IGNORE },
            { aHidd_BitMap_Intel_CompositorHidd, (IPTR)gfxdata->compositor },
            { TAG_MORE, (IPTR)msg->attrList }
        };

        modeid = (HIDDT_ModeID)GetTagData(aHidd_BitMap_ModeID, vHidd_ModeID_Invalid, msg->attrList);
        if (vHidd_ModeID_Invalid != modeid)
        {
            mytags[0].ti_Tag = aHidd_BitMap_ClassPtr;
            mytags[0].ti_Data = (IPTR)SD(cl)->bmclass;
        }

        stdpf = (HIDDT_StdPixFmt)GetTagData(aHidd_BitMap_StdPixFmt, vHidd_StdPixFmt_Unknown, msg->attrList);
        if (vHidd_StdPixFmt_Plane == stdpf)
        {
            mytags[1].ti_Tag = aHidd_BitMap_Align;
            mytags[1].ti_Data = 32;
        }
        
        mymsg.mID = msg->mID;
        mymsg.cl = msg->cl;
        mymsg.attrList = mytags;

        object = (OOP_Object *)OOP_DoSuperMethod(cl, o, (OOP_Msg)&mymsg);
    }
    else if (msg->cl == SD(cl)->basei2c)
    {
        /* Expose the i2c bus object */
    }
    else
        object = (OOP_Object *)OOP_DoSuperMethod(cl, o, (OOP_Msg)msg);

    return object;
}

VOID METHOD(Intel, Hidd_Gfx, CopyBox)
{
    OOP_Class *srcclass = OOP_OCLASS(msg->src);
    OOP_Class *destclass = OOP_OCLASS(msg->dest);

    if (IS_INTEL_BM_CLASS(srcclass) && IS_INTEL_BM_CLASS(destclass))
    {
        struct HIDDIntelBitMapData *srcdata = OOP_INST_DATA(srcclass, msg->src);
        struct HIDDIntelBitMapData *destdata = OOP_INST_DATA(destclass, msg->dest);
        struct CardData *carddata = &(SD(cl)->carddata);
        BOOL ret = FALSE;
        
        D(bug("[Intel] CopyBox %p -> %p\n", msg->src, msg->dest));

        LOCK_ENGINE

        LOCK_MULTI_BITMAP
        LOCK_BITMAP_BM(srcdata)
        LOCK_BITMAP_BM(destdata)
        UNLOCK_MULTI_BITMAP

        /* Try hardware accelerated copy */
        ret = HIDDIntelBatchCopySameFormat(carddata, srcdata, destdata,
                msg->srcX, msg->srcY, msg->destX, msg->destY,
                msg->width, msg->height, GC_DRMD(msg->gc));

        if (ret)
        {
            /* Flush and wait */
            HIDDIntelBatchSubmit(carddata);
            drm_intel_bo_wait_rendering(destdata->bo);
        }

        UNLOCK_BITMAP_BM(destdata);
        UNLOCK_BITMAP_BM(srcdata);

        UNLOCK_ENGINE

        if (ret)
            return;
    }

    OOP_DoSuperMethod(cl, o, (OOP_Msg)msg);
}

VOID METHOD(Intel, Root, Get)
{
    ULONG idx;

    if (IS_GFX_ATTR(msg->attrID, idx))
    {
        switch (idx)
        {
        case aoHidd_Gfx_NoFrameBuffer:
            *msg->storage = (IPTR)TRUE;
            return;
        case aoHidd_Gfx_SupportsHWCursor:
            *msg->storage = (IPTR)TRUE;
            return;
        case aoHidd_Gfx_HWSpriteTypes:
            *msg->storage = vHidd_SpriteType_DirectColor;
            return;
        case aoHidd_Gfx_DriverName:
            *msg->storage = (IPTR)"Intel";
            return;
        case aoHidd_Gfx_MemoryAttribs:
            {
                struct TagItem *matstate = (struct TagItem *)msg->storage;
                if (matstate)
                {
                    struct TagItem *matag;
                    while ((matag = NextTagItem(&matstate)))
                    {
                        switch(matag->ti_Tag)
                        {
                            case tHidd_Gfx_MemTotal:
                                {
                                    /* Use a reasonable default for Intel integrated graphics */
                                    matag->ti_Data = (IPTR)(512 * 1024 * 1024);
                                }
                                break;
                            case tHidd_Gfx_MemAddressableTotal:
                                {
                                    matag->ti_Data = (IPTR)(256 * 1024 * 1024);
                                }
                                break;
                        }
                    }
                }
            }
            return;
        }
    }

    OOP_DoSuperMethod(cl, o, (OOP_Msg)msg);
}

ULONG METHOD(Intel, Hidd_Gfx, ShowViewPorts)
{
    struct HIDDIntelData *gfxdata = OOP_INST_DATA(cl, o);
    struct pHidd_Compositor_BitMapStackChanged bscmsg =
    {
        mID : OOP_GetMethodID(IID_Hidd_Compositor, moHidd_Compositor_BitMapStackChanged),
        data : msg->Data
    };

    D(bug("[Intel] ShowViewPorts enter TopLevelBM %p\n", (msg->Data ? (msg->Data->Bitmap) : NULL)));

    OOP_DoMethod(gfxdata->compositor, (OOP_Msg)&bscmsg);

    return TRUE;
}

#if AROS_BIG_ENDIAN
#define Machine_ARGB32 vHidd_StdPixFmt_ARGB32
#else
#define Machine_ARGB32 vHidd_StdPixFmt_BGRA32
#endif

BOOL METHOD(Intel, Hidd_Gfx, SetCursorShape)
{
    struct HIDDIntelData *gfxdata = OOP_INST_DATA(cl, o);

    if (msg->shape == NULL)
    {
        HIDDIntelShowCursor(o, FALSE);
    }
    else
    {
        IPTR width, height;
        ULONG i;
        ULONG x, y;
        ULONG curimage[64 * 64];
        struct CardData *carddata = &(SD(cl)->carddata);
        
        OOP_GetAttr(msg->shape, aHidd_BitMap_Width, &width);
        OOP_GetAttr(msg->shape, aHidd_BitMap_Height, &height);

        if (width > 64) width = 64;
        if (height > 64) height = 64;

        LOCK_ENGINE

        /* Map the cursor buffer */
        drm_intel_gem_bo_map_gtt(gfxdata->cursor);

        for (i = 0; i < 64 * 64; i++)
            ((ULONG*)gfxdata->cursor->virtual)[i] = 0;

        HIDD_BM_GetImage(msg->shape, (UBYTE *)curimage, 64 * 4, 0, 0,
            width, height, Machine_ARGB32);

        for (y = 0; y < height; y++)
            for (x = 0; x < width; x++)
            {
                ULONG offset = y * 64 + x;
                writel(curimage[offset], ((ULONG *)gfxdata->cursor->virtual) + (offset));
            }

        drm_intel_bo_unmap(gfxdata->cursor);

        HIDDIntelShowCursor(o, TRUE);

        UNLOCK_ENGINE
    }

    return TRUE;
}

BOOL METHOD(Intel, Hidd_Gfx, SetCursorPos)
{
    struct HIDDIntelData *gfxdata = OOP_INST_DATA(cl, o);
    struct CardData *carddata = &(SD(cl)->carddata);

    LOCK_ENGINE
    drmModeMoveCursor(carddata->fd, gfxdata->selectedcrtcid, msg->x, msg->y);
    UNLOCK_ENGINE

    return TRUE;
}

VOID METHOD(Intel, Hidd_Gfx, SetCursorVisible)
{
    HIDDIntelShowCursor(o, msg->visible);
}

static struct HIDD_ModeProperties modeprops =
{
    DIPF_IS_SPRITES,
    1,
    COMPF_ABOVE
};

ULONG METHOD(Intel, Hidd_Gfx, ModeProperties)
{
    ULONG len = msg->propsLen;

    if (len > sizeof(modeprops))
        len = sizeof(modeprops);
    CopyMem(&modeprops, msg->props, len);

    return len;
}

VOID METHOD(Intel, Hidd_Gfx, NominalDimensions)
{
    if (msg->width)
        *(msg->width) = 1024;
    if (msg->height)
        *(msg->height) = 768;
    if (msg->depth)
        *(msg->depth) = 24;
}
