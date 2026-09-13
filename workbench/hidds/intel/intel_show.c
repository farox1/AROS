/*
    Intel Hidd_Gfx Show method - copies bitmap to GTT framebuffer
    Copyright 2026, The AROS Development Team.
*/
#include "intel_intern.h"
#include <proto/oop.h>
#include <proto/exec.h>
#include <exec/tasks.h>
#include <libdrm/arosdrmmode.h>

#define DEBUG 0
#include <aros/debug.h>

OOP_Object *METHOD(Intel, Hidd_Gfx, Show)
{
    struct HIDDIntelData *gfxdata = OOP_INST_DATA(cl, o);
    struct HiddGfxData *gfxbas_data = OOP_INST_DATA(OOP_OCLASS(o), o);
    OOP_Object *bm = msg->bitMap;

    if (!bm)
        return NULL;

    {
        OOP_Class *bmcl = OOP_OCLASS(bm);
        if (IS_INTEL_BM_CLASS(bmcl))
        {
            struct HIDDIntelBitMapData *bmdata = OOP_INST_DATA(bmcl, bm);
            struct CardData *carddata = &(SD(cl)->carddata);

            D(bug("[Intel] Show bm=0x%p fb=%d %lux%lu off=%ld,%ld\n",
                bm, bmdata->is_framebuffer,
                bmdata->drawable.width, bmdata->drawable.height,
                bmdata->xoffset, bmdata->yoffset));

            bug("[Intel] Show: bm=%lux%lu fb=%d mode=%lux%lu bpp=%d\n",
                bmdata->drawable.width, bmdata->drawable.height,
                bmdata->is_framebuffer,
                carddata->mode_width, carddata->mode_height,
                bmdata->drawable.bitsPerPixel);

            /*
             * When the shown bitmap's dimensions OR bit depth differ from the
             * current scanout mode, reprogram the pipe + plane timings. This is
             * the mode-change trigger for a NoFrameBuffer driver: the
             * compositor creates a display bitmap at the new configuration and
             * shows it here. The depth comparison is required so that changing
             * only the color depth at the same resolution (e.g. 1920x1080@24
             * -> 1920x1080@16) still reprograms PLANE_CTL + stride; without it
             * a 16-bit bitmap would be copied into a plane still configured for
             * 24-bit -> wrong colors/partial image.
             */
            if (bmdata->drawable.width != carddata->mode_width ||
                bmdata->drawable.height != carddata->mode_height ||
                bmdata->drawable.bitsPerPixel != carddata->mode_bpp)
            {
                intel_set_mode_by_resolution(carddata, carddata->selected_connector,
                    bmdata->drawable.width, bmdata->drawable.height,
                    bmdata->pitch, bmdata->drawable.bitsPerPixel);
            }

            /*
             * The framebuffer itself is already displayed: it lives in the
             * GTT aperture that the CRTC scans out directly. Nothing to copy.
             */
            if (bmdata->is_framebuffer)
                return bm;

            IPTR map;

            LOCK_ENGINE
            LOCK_BITMAP_BM(bmdata)

            drm_intel_gem_bo_map_gtt(bmdata->bo);
            map = (IPTR)bmdata->bo->virtual;

            if (map)
            {
                /* Copy bitmap contents to GTT framebuffer */
                ULONG copy_lines = bmdata->drawable.height;
                ULONG copy_bytes = bmdata->pitch;
                IPTR src = map;
                IPTR dst = (IPTR)(SD(cl)->carddata.gtt_fb);

                if (dst)
                {
                    ULONG y;
                    for (y = 0; y < copy_lines; y++)
                        CopyMem((APTR)(src + y * copy_bytes),
                                (APTR)(dst + y * copy_bytes),
                                copy_bytes);

                    /* Probe the copied framebuffer content */
                    bug("[Intel] fb content: p0=0x%08lx p_mid=0x%08lx p_last=0x%08lx stride=%lu\n",
                        ((ULONG*)dst)[0],
                        ((ULONG*)dst)[(copy_lines/2) * (copy_bytes/4) + (copy_bytes/4)/2],
                        ((ULONG*)dst)[(copy_lines-1) * (copy_bytes/4) + (copy_bytes/4) - 1],
                        copy_bytes);
                }
            }

            UNLOCK_BITMAP_BM(bmdata)
            UNLOCK_ENGINE
        }
    }

    /* Return the bitmap that was shown */
    return bm;
}
