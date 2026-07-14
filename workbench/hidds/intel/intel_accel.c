/*
 * Copyright (C) 2010-2026, The AROS Development Team. All rights reserved.
 */

#include "intel_intern.h"
#include <proto/oop.h>
#include <proto/exec.h>
#include <stdlib.h>

#undef HiddBitMapAttrBase
#define HiddBitMapAttrBase  (SD(cl)->bitMapAttrBase)

static inline int do_alpha(int a, int v)
{
    int tmp = a * v;
    return ((tmp << 8) + tmp + 32768) >> 16;
}

VOID HIDDIntelBitMapPutAlphaImage32(struct HIDDIntelBitMapData *bmdata,
    APTR srcbuff, ULONG srcpitch, LONG destX, LONG destY, LONG width, LONG height)
{
    LONG x, y;
    
    for (y = 0; y < height; y++)
    {
        IPTR srcaddr = (srcpitch * y) + (IPTR)srcbuff;
        IPTR destaddr = (destX * 4) + (bmdata->pitch * (destY + y)) + (IPTR)bmdata->bo->virtual;
        
        for (x = 0; x < width; x++)
        {
            ULONG destpix;
            ULONG srcpix;
            LONG src_red, src_green, src_blue, src_alpha;
            LONG dst_red, dst_green, dst_blue;

            srcpix = *(ULONG *)srcaddr;
#if AROS_BIG_ENDIAN
            src_red   = (srcpix & 0x00FF0000) >> 16;
            src_green = (srcpix & 0x0000FF00) >> 8;
            src_blue  = (srcpix & 0x000000FF);
            src_alpha = (srcpix & 0xFF000000) >> 24;
#else
            src_red   = (srcpix & 0x0000FF00) >> 8;
            src_green = (srcpix & 0x00FF0000) >> 16;
            src_blue  = (srcpix & 0xFF000000) >> 24;
            src_alpha = (srcpix & 0x000000FF);
#endif

            if (src_alpha != 0)
            {
                if (src_alpha == 0xff)
                {
                    dst_red = src_red;
                    dst_green = src_green;
                    dst_blue = src_blue;
                }
                else
                {
                    destpix = readl(destaddr);

                    dst_red   = (destpix & 0x00FF0000) >> 16;
                    dst_green = (destpix & 0x0000FF00) >> 8;
                    dst_blue  = (destpix & 0x000000FF);

                    dst_red   += do_alpha(src_alpha, src_red - dst_red);
                    dst_green += do_alpha(src_alpha, src_green - dst_green);
                    dst_blue  += do_alpha(src_alpha, src_blue - dst_blue);
                }

                destpix = (dst_red << 16) + (dst_green << 8) + (dst_blue);
                writel(destpix, destaddr);
            }

            srcaddr += 4;
            destaddr += 4;
        }
    }
}

VOID HIDDIntelBitMapPutAlphaImage16(struct HIDDIntelBitMapData *bmdata,
    APTR srcbuff, ULONG srcpitch, LONG destX, LONG destY, LONG width, LONG height)
{
    LONG x, y;
    
    for (y = 0; y < height; y++)
    {
        IPTR srcaddr = (srcpitch * y) + (IPTR)srcbuff;
        IPTR destaddr = (destX * 2) + (bmdata->pitch * (destY + y)) + (IPTR)bmdata->bo->virtual;
        
        for (x = 0; x < width; x++)
        {
            UWORD destpix;
            ULONG srcpix;
            LONG src_red, src_green, src_blue, src_alpha;
            LONG dst_red, dst_green, dst_blue;

            srcpix = *(ULONG *)srcaddr;
#if AROS_BIG_ENDIAN
            src_red   = (srcpix & 0x00FF0000) >> 16;
            src_green = (srcpix & 0x0000FF00) >> 8;
            src_blue  = (srcpix & 0x000000FF);
            src_alpha = (srcpix & 0xFF000000) >> 24;
#else
            src_red   = (srcpix & 0x0000FF00) >> 8;
            src_green = (srcpix & 0x00FF0000) >> 16;
            src_blue  = (srcpix & 0xFF000000) >> 24;
            src_alpha = (srcpix & 0x000000FF);
#endif

            if (src_alpha != 0)
            {
                if (src_alpha == 0xff)
                {
                    dst_red = src_red;
                    dst_green = src_green;
                    dst_blue = src_blue;
                }
                else
                {
                    destpix = readw(destaddr);

                    dst_red   = (destpix & 0x0000F800) >> 8;
                    dst_green = (destpix & 0x000007e0) >> 3;
                    dst_blue  = (destpix & 0x0000001f) << 3;

                    dst_red   += do_alpha(src_alpha, src_red - dst_red);
                    dst_green += do_alpha(src_alpha, src_green - dst_green);
                    dst_blue  += do_alpha(src_alpha, src_blue - dst_blue);
                }

                destpix = (((dst_red << 8) & 0xf800) | ((dst_green << 3) & 0x07e0) | ((dst_blue >> 3) & 0x001f));
                writew(destpix, destaddr);
            }

            srcaddr += 4;
            destaddr += 2;
        }
    }
}

VOID HIDDIntelBitMapPutAlphaTemplate32(struct HIDDIntelBitMapData *bmdata,
    OOP_Object *gc, OOP_Object *bm, BOOL invertalpha,
    UBYTE *srcalpha, ULONG srcpitch, LONG destX, LONG destY, LONG width, LONG height)
{
    WORD x, y;
    UBYTE *pixarray = srcalpha;
    HIDDT_Color color;
    LONG fg_red, fg_green, fg_blue;
    LONG bg_red = 0, bg_green = 0, bg_blue = 0;
    WORD type = 0;

    if (width <= 0 || height <= 0)
        return;

    HIDD_BM_UnmapPixel(bm, GC_FG(gc), &color);

    fg_red   = color.red >> 8;
    fg_green = color.green >> 8;
    fg_blue  = color.blue >> 8;

    if (GC_COLEXP(gc) == vHidd_GC_ColExp_Transparent)
        type = 0;
    else if (GC_DRMD(gc) == vHidd_GC_DrawMode_Invert)
        type = 2;
    else
    {
        type = 4;
        HIDD_BM_UnmapPixel(bm, GC_BG(gc), &color);
        bg_red   = color.red >> 8;
        bg_green = color.green >> 8;
        bg_blue  = color.blue >> 8;
    }

    if (invertalpha) type++;

    for (y = 0; y < height; y++)
    {
        IPTR destaddr = (destX * 4) + ((destY + y) * bmdata->pitch) + (IPTR)bmdata->bo->virtual;

        switch(type)
        {
            case 0:
            for (x = 0; x < width; x++)
            {
                ULONG destpix;
                LONG dst_red, dst_green, dst_blue, alpha;

                alpha = *pixarray++;

                if (alpha != 0)
                {
                    if (alpha == 0xff)
                    {
                        dst_red = fg_red;
                        dst_green = fg_green;
                        dst_blue = fg_blue;
                    }
                    else
                    {
                        destpix = readl(destaddr);

                        dst_red   = (destpix & 0x00FF0000) >> 16;
                        dst_green = (destpix & 0x0000FF00) >> 8;
                        dst_blue  = (destpix & 0x000000FF);

                        dst_red   += do_alpha(alpha, fg_red - dst_red);
                        dst_green += do_alpha(alpha, fg_green - dst_green);
                        dst_blue  += do_alpha(alpha, fg_blue - dst_blue);
                    }

                    destpix = (dst_red << 16) + (dst_green << 8) + (dst_blue);
                    writel(destpix, destaddr);
                }

                destaddr += 4;
            }
            break;

            case 1:
            for (x = 0; x < width; x++)
            {
                ULONG destpix;
                LONG dst_red, dst_green, dst_blue, alpha;

                alpha = (*pixarray++) ^ 255;

                if (alpha != 0)
                {
                    if (alpha == 0xff)
                    {
                        dst_red = fg_red;
                        dst_green = fg_green;
                        dst_blue = fg_blue;
                    }
                    else
                    {
                        destpix = readl(destaddr);

                        dst_red   = (destpix & 0x00FF0000) >> 16;
                        dst_green = (destpix & 0x0000FF00) >> 8;
                        dst_blue  = (destpix & 0x000000FF);

                        dst_red   += do_alpha(alpha, fg_red - dst_red);
                        dst_green += do_alpha(alpha, fg_green - dst_green);
                        dst_blue  += do_alpha(alpha, fg_blue - dst_blue);
                    }

                    destpix = (dst_red << 16) + (dst_green << 8) + (dst_blue);
                    writel(destpix, destaddr);
                }

                destaddr += 4;
            }
            break;

            case 2:
            for (x = 0; x < width; x++)
            {
                ULONG destpix;
                UBYTE alpha;

                alpha = *pixarray++;

                if (alpha >= 0x80)
                {
                    destpix = readl(destaddr);
                    destpix = ~destpix;
                    writel(destpix, destaddr);
                }

                destaddr += 4;
            }
            break;

            case 3:
            for (x = 0; x < width; x++)
            {
                ULONG destpix;
                UBYTE alpha;

                alpha = *pixarray++;

                if (alpha < 0x80)
                {
                    destpix = readl(destaddr);
                    destpix = ~destpix;
                    writel(destpix, destaddr);
                }

                destaddr += 4;
            }
            break;

            case 4:
            for (x = 0; x < width; x++)
            {
                ULONG destpix;
                LONG dst_red, dst_green, dst_blue, alpha;

                alpha = *pixarray++;

                dst_red   = bg_red   + ((fg_red   - bg_red)   * alpha) / 256;
                dst_green = bg_green + ((fg_green - bg_green) * alpha) / 256;
                dst_blue  = bg_blue  + ((fg_blue  - bg_blue)  * alpha) / 256;

                destpix = (dst_red << 16) + (dst_green << 8) + (dst_blue);
                writel(destpix, destaddr);
                destaddr += 4;
            }
            break;

            case 5:
            for (x = 0; x < width; x++)
            {
                ULONG destpix;
                LONG dst_red, dst_green, dst_blue, alpha;

                alpha = (*pixarray++) ^ 255;

                dst_red   = bg_red   + ((fg_red   - bg_red)   * alpha) / 256;
                dst_green = bg_green + ((fg_green - bg_green) * alpha) / 256;
                dst_blue  = bg_blue  + ((fg_blue  - bg_blue)  * alpha) / 256;

                destpix = (dst_red << 16) + (dst_green << 8) + (dst_blue);
                writel(destpix, destaddr);
                destaddr += 4;
            }
            break;
        }

        pixarray += srcpitch - width;
    }
}

VOID HIDDIntelBitMapPutAlphaTemplate16(struct HIDDIntelBitMapData *bmdata,
    OOP_Object *gc, OOP_Object *bm, BOOL invertalpha,
    UBYTE *srcalpha, ULONG srcpitch, LONG destX, LONG destY, LONG width, LONG height)
{
    WORD x, y;
    UBYTE *pixarray = srcalpha;
    HIDDT_Color color;
    LONG fg_red, fg_green, fg_blue;
    LONG bg_red = 0, bg_green = 0, bg_blue = 0;
    WORD type = 0;

    if (width <= 0 || height <= 0)
        return;

    HIDD_BM_UnmapPixel(bm, GC_FG(gc), &color);

    fg_red   = color.red >> 8;
    fg_green = color.green >> 8;
    fg_blue  = color.blue >> 8;

    if (GC_COLEXP(gc) == vHidd_GC_ColExp_Transparent)
        type = 0;
    else if (GC_DRMD(gc) == vHidd_GC_DrawMode_Invert)
        type = 2;
    else
    {
        type = 4;
        HIDD_BM_UnmapPixel(bm, GC_BG(gc), &color);
        bg_red   = color.red >> 8;
        bg_green = color.green >> 8;
        bg_blue  = color.blue >> 8;
    }

    if (invertalpha) type++;

    for (y = 0; y < height; y++)
    {
        IPTR destaddr = (destX * 2) + ((destY + y) * bmdata->pitch) + (IPTR)bmdata->bo->virtual;

        switch(type)
        {
            case 0:
            for (x = 0; x < width; x++)
            {
                UWORD destpix;
                LONG dst_red, dst_green, dst_blue, alpha;

                alpha = *pixarray++;

                if (alpha != 0)
                {
                    if (alpha == 0xff)
                    {
                        dst_red = fg_red;
                        dst_green = fg_green;
                        dst_blue = fg_blue;
                    }
                    else
                    {
                        destpix = readw(destaddr);

                        dst_red   = (destpix & 0x0000F800) >> 8;
                        dst_green = (destpix & 0x000007e0) >> 3;
                        dst_blue  = (destpix & 0x0000001f) << 3;

                        dst_red   += do_alpha(alpha, fg_red - dst_red);
                        dst_green += do_alpha(alpha, fg_green - dst_green);
                        dst_blue  += do_alpha(alpha, fg_blue - dst_blue);
                    }

                    destpix = (((dst_red << 8) & 0xf800) | ((dst_green << 3) & 0x07e0) | ((dst_blue >> 3) & 0x001f));
                    writew(destpix, destaddr);
                }

                destaddr += 2;
            }
            break;

            case 1:
            for (x = 0; x < width; x++)
            {
                UWORD destpix;
                LONG dst_red, dst_green, dst_blue, alpha;

                alpha = (*pixarray++) ^ 255;

                if (alpha != 0)
                {
                    if (alpha == 0xff)
                    {
                        dst_red = fg_red;
                        dst_green = fg_green;
                        dst_blue = fg_blue;
                    }
                    else
                    {
                        destpix = readw(destaddr);

                        dst_red   = (destpix & 0x0000F800) >> 8;
                        dst_green = (destpix & 0x000007e0) >> 3;
                        dst_blue  = (destpix & 0x0000001f) << 3;

                        dst_red   += do_alpha(alpha, fg_red - dst_red);
                        dst_green += do_alpha(alpha, fg_green - dst_green);
                        dst_blue  += do_alpha(alpha, fg_blue - dst_blue);
                    }

                    destpix = (((dst_red << 8) & 0xf800) | ((dst_green << 3) & 0x07e0) | ((dst_blue >> 3) & 0x001f));
                    writew(destpix, destaddr);
                }

                destaddr += 2;
            }
            break;

            case 2:
            for (x = 0; x < width; x++)
            {
                UWORD destpix;
                UBYTE alpha;

                alpha = *pixarray++;

                if (alpha >= 0x80)
                {
                    destpix = readw(destaddr);
                    destpix = ~destpix;
                    writew(destpix, destaddr);
                }

                destaddr += 2;
            }
            break;

            case 3:
            for (x = 0; x < width; x++)
            {
                UWORD destpix;
                UBYTE alpha;

                alpha = *pixarray++;

                if (alpha < 0x80)
                {
                    destpix = readw(destaddr);
                    destpix = ~destpix;
                    writew(destpix, destaddr);
                }

                destaddr += 2;
            }
            break;

            case 4:
            for (x = 0; x < width; x++)
            {
                UWORD destpix;
                LONG dst_red, dst_green, dst_blue, alpha;

                alpha = *pixarray++;

                dst_red   = bg_red   + ((fg_red   - bg_red)   * alpha) / 256;
                dst_green = bg_green + ((fg_green - bg_green) * alpha) / 256;
                dst_blue  = bg_blue  + ((fg_blue  - bg_blue)  * alpha) / 256;

                destpix = (((dst_red << 8) & 0xf800) | ((dst_green << 3) & 0x07e0) | ((dst_blue >> 3) & 0x001f));
                writew(destpix, destaddr);
                destaddr += 2;
            }
            break;

            case 5:
            for (x = 0; x < width; x++)
            {
                UWORD destpix;
                LONG dst_red, dst_green, dst_blue, alpha;

                alpha = (*pixarray++) ^ 255;

                dst_red   = bg_red   + ((fg_red   - bg_red)   * alpha) / 256;
                dst_green = bg_green + ((fg_green - bg_green) * alpha) / 256;
                dst_blue  = bg_blue  + ((fg_blue  - bg_blue)  * alpha) / 256;

                destpix = (((dst_red << 8) & 0xf800) | ((dst_green << 3) & 0x07e0) | ((dst_blue >> 3) & 0x001f));
                writew(destpix, destaddr);
                destaddr += 2;
            }
            break;
        }

        pixarray += srcpitch - width;
    }
}

BOOL HiddIntelWriteFromRAM(
    APTR src, ULONG srcPitch, HIDDT_StdPixFmt srcPixFmt,
    APTR dst, ULONG dstPitch,
    ULONG width, ULONG height,
    OOP_Class *cl, OOP_Object *o)
{
    struct HIDDIntelBitMapData *bmdata = OOP_INST_DATA(cl, o);
    UBYTE dstBpp = bmdata->bytesperpixel;

    switch(srcPixFmt)
    {
    case vHidd_StdPixFmt_Native:
        switch(dstBpp)
        {
        case 1:
            break;
        case 2:
            {
                struct pHidd_BitMap_CopyMemBox16 __m =
                {
                    SD(cl)->mid_CopyMemBox16, src, 0, 0, dst,
                    0, 0, width, height, srcPitch, dstPitch
                }, *m = &__m;
                OOP_DoMethod(o, (OOP_Msg)m);
            }
            break;
        case 4:
            {
                struct pHidd_BitMap_CopyMemBox32 __m =
                {
                    SD(cl)->mid_CopyMemBox32, src, 0, 0, dst,
                    0, 0, width, height, srcPitch, dstPitch
                }, *m = &__m;
                OOP_DoMethod(o, (OOP_Msg)m);
            }
            break;
        }
        break;

    case vHidd_StdPixFmt_Native32:
        switch(dstBpp)
        {
        case 1:
            break;
        case 2:
            {
                struct pHidd_BitMap_PutMem32Image16 __m =
                {
                    SD(cl)->mid_PutMem32Image16, src, dst,
                    0, 0, width, height, srcPitch, dstPitch
                }, *m = &__m;
                OOP_DoMethod(o, (OOP_Msg)m);
            }
            break;
        case 4:
            {
                struct pHidd_BitMap_CopyMemBox32 __m =
                {
                    SD(cl)->mid_CopyMemBox32, src, 0, 0, dst,
                    0, 0, width, height, srcPitch, dstPitch
                }, *m = &__m;
                OOP_DoMethod(o, (OOP_Msg)m);
            }
            break;
        }
        break;
    default:
        {
            APTR csrc = src;
            APTR *psrc = &csrc;
            APTR cdst = dst;
            APTR *pdst = &cdst;
            OOP_Object *dstPF = NULL;
            OOP_Object *srcPF = NULL;
            OOP_Object *gfxHidd = NULL;
            struct pHidd_Gfx_GetPixFmt __gpf =
            {
                SD(cl)->mid_GetPixFmt, srcPixFmt
            }, *gpf = &__gpf;
            
            OOP_GetAttr(o, aHidd_BitMap_PixFmt, (APTR)&dstPF);
            OOP_GetAttr(o, aHidd_BitMap_GfxHidd, (APTR)&gfxHidd);
            srcPF = (OOP_Object *)OOP_DoMethod(gfxHidd, (OOP_Msg)gpf);

            {
                struct pHidd_BitMap_ConvertPixels __m =
                {
                    SD(cl)->mid_ConvertPixels,
                    psrc, (HIDDT_PixelFormat *)srcPF, srcPitch,
                    pdst, (HIDDT_PixelFormat *)dstPF, dstPitch,
                    width, height, NULL
                }, *m = &__m;
                OOP_DoMethod(o, (OOP_Msg)m);
            }
        }
        break;
    }

    return TRUE;
}

BOOL HiddIntelReadIntoRAM(
    APTR src, ULONG srcPitch,
    APTR dst, ULONG dstPitch, HIDDT_StdPixFmt dstPixFmt,
    ULONG width, ULONG height,
    OOP_Class *cl, OOP_Object *o)
{
    struct HIDDIntelBitMapData *bmdata = OOP_INST_DATA(cl, o);
    UBYTE srcBpp = bmdata->bytesperpixel;

    switch(dstPixFmt)
    {
    case vHidd_StdPixFmt_Native:
        switch(srcBpp)
        {
        case 1:
            break;
        case 2:
            {
                struct pHidd_BitMap_CopyMemBox16 __m =
                {
                    SD(cl)->mid_CopyMemBox16, src, 0, 0, dst,
                    0, 0, width, height, srcPitch, dstPitch
                }, *m = &__m;
                OOP_DoMethod(o, (OOP_Msg)m);
            }
            break;
        case 4:
            {
                struct pHidd_BitMap_CopyMemBox32 __m =
                {
                    SD(cl)->mid_CopyMemBox32, src, 0, 0, dst,
                    0, 0, width, height, srcPitch, dstPitch
                }, *m = &__m;
                OOP_DoMethod(o, (OOP_Msg)m);
            }
            break;
        }
        break;

    case vHidd_StdPixFmt_Native32:
        switch(srcBpp)
        {
        case 1:
            break;
        case 2:
            {
                struct pHidd_BitMap_GetMem32Image16 __m =
                {
                    SD(cl)->mid_GetMem32Image16, src, 0, 0, dst,
                    width, height, srcPitch, dstPitch
                }, *m = &__m;
                OOP_DoMethod(o, (OOP_Msg)m);
            }
            break;
        case 4:
            {
                struct pHidd_BitMap_CopyMemBox32 __m =
                {
                    SD(cl)->mid_CopyMemBox32, src, 0, 0, dst,
                    0, 0, width, height, srcPitch, dstPitch
                }, *m = &__m;
                OOP_DoMethod(o, (OOP_Msg)m);
            }
            break;
        }
        break;
    default:
        {
            APTR csrc = src;
            APTR *psrc = &csrc;
            APTR cdst = dst;
            APTR *pdst = &cdst;
            OOP_Object *dstPF = NULL;
            OOP_Object *srcPF = NULL;
            OOP_Object *gfxHidd = NULL;
            struct pHidd_Gfx_GetPixFmt __gpf =
            {
                SD(cl)->mid_GetPixFmt, dstPixFmt
            }, *gpf = &__gpf;
            
            OOP_GetAttr(o, aHidd_BitMap_PixFmt, (APTR)&srcPF);
            OOP_GetAttr(o, aHidd_BitMap_GfxHidd, (APTR)&gfxHidd);
            dstPF = (OOP_Object *)OOP_DoMethod(gfxHidd, (OOP_Msg)gpf);

            {
                struct pHidd_BitMap_ConvertPixels __m =
                {
                    SD(cl)->mid_ConvertPixels,
                    psrc, (HIDDT_PixelFormat *)srcPF, srcPitch,
                    pdst, (HIDDT_PixelFormat *)dstPF, dstPitch,
                    width, height, NULL
                }, *m = &__m;
                OOP_DoMethod(o, (OOP_Msg)m);
            }
        }
        break;
    }

    return TRUE;
}

#define POINT_OUTSIDE_CLIP(gc, x, y) \
    (  (x) < GC_CLIPX1(gc)       \
    || (x) > GC_CLIPX2(gc)       \
    || (y) < GC_CLIPY1(gc)       \
    || (y) > GC_CLIPY2(gc) )

VOID HIDDIntelBitMapDrawSolidLine(struct HIDDIntelBitMapData *bmdata,
    OOP_Object *gc, LONG destX1, LONG destY1, LONG destX2, LONG destY2)
{
    WORD i;
    LONG x1, y1, x2, y2;
    ULONG fg;
    APTR doclip;

    IPTR map = (IPTR)bmdata->bo->virtual;

    doclip = GC_DOCLIP(gc);
    fg = GC_FG(gc);

    if (destX1 > destX2)
    {
        x1 = destX2; x2 = destX1;
    }
    else
    {
        x1 = destX1; x2 = destX2;
    }

    if (destY1 > destY2)
    {
        y1 = destY2; y2 = destY1;
    }
    else
    {
        y1 = destY1; y2 = destY2;
    }

    if (doclip)
    {
        if (x1 > GC_CLIPX2(gc) || x2 < GC_CLIPX1(gc)
            || y1 > GC_CLIPY2(gc) || y2 < GC_CLIPY1(gc))
        {
            return;
        }
    }

    if (y1 == y2)
    {
        for (i = x1; i != x2; i++)
        {
            if ((!doclip) || (!POINT_OUTSIDE_CLIP(gc, i, y1)))
            {
                IPTR addr = map + (bmdata->pitch * y1) + (i * bmdata->bytesperpixel);
                if (bmdata->bytesperpixel == 2)
                    writew(fg, (APTR)addr);
                else
                    writel(fg, (APTR)addr);
            }
        }
    }
    else if (x1 == x2)
    {
        for (i = y1; i != y2; i++)
        {
            if (!doclip || !POINT_OUTSIDE_CLIP(gc, x1, i))
            {
                IPTR addr = map + (bmdata->pitch * i) + (x1 * bmdata->bytesperpixel);
                if (bmdata->bytesperpixel == 2)
                    writew(fg, (APTR)addr);
                else
                    writel(fg, (APTR)addr);
            }
        }
    }
    else
    {
        WORD dx, dy, x, y, incrE, incrNE, d, s1, s2, t;
        
        x1 = destX1;
        y1 = destY1;
        x2 = destX2;
        y2 = destY2;

        dx = abs(x2 - x1);
        dy = abs(y2 - y1);
    
        if ((x2 - x1) > 0) s1 = 1; else s1 = -1;
        if ((y2 - y1) > 0) s2 = 1; else s2 = -1;
    
        if (dx < dy)
        {
            d = dx;
            dx = dy;
            dy = d;
            t = 0;
        }
        else
            t = 1;
    
        d  = 2 * dy - dx;
        incrE  = 2 * dy;
        incrNE = 2 * (dy - dx);
    
        x = x1; y = y1;
        
        for (i = 0; i <= dx; i++)
        {
            if (!doclip || !POINT_OUTSIDE_CLIP(gc, x, y))
            {
                IPTR addr = map + (x * bmdata->bytesperpixel) + (bmdata->pitch * y);
                if (bmdata->bytesperpixel == 2)
                    writew(fg, (APTR)addr);
                else
                    writel(fg, (APTR)addr);
            }
    
            if (d <= 0)
            {
                if (t == 1)
                    x = x + s1;
                else
                    y = y + s2;
                d = d + incrE;
            }
            else
            {
                x = x + s1;
                y = y + s2;
                d = d + incrNE;
            }
        }
    }
}
