#ifndef __INTEL_XF86_H__
#define __INTEL_XF86_H__
/*
    Copyright (C) 2010-2026, The AROS Development Team. All rights reserved.
*/

#include <string.h>
#include <stdio.h>
#include <aros/debug.h>

#include "intel_intern.h"

/* X type replacements for AROS */
#define Bool                        BOOL
#define ScrnInfoPtr                 struct CardData *
#define PixmapPtr                   struct HIDDIntelBitMapData *
#define DrawablePtr                 struct HIDDIntelBitMapData *
#define ScreenPtr                   APTR
#define PicturePtr                  struct Picture *
#define PictFormatPtr               APTR
#define PictTransformPtr            APTR
#define BoxPtr                      APTR
#define RegionPtr                   APTR
#define CARD32                      LONG
#define Pixel                       ULONG

/* X11 function stubs */
#define xf86DrvMsg(a, b, fmt, ...)  bug(fmt, ##__VA_ARGS__)
#define ErrorF(msg, ...)            bug(msg, ##__VA_ARGS__)
#define xf86ScreenToScrn(x)         globalcarddataptr
#define xf86ScrnToScreen(x)         ((APTR)0)
#define xf86Screens                 ((ScrnInfoPtr *)NULL)

/* X11 type replacements */
#define GCPtr                       OOP_Object *
#define ClientPtr                   APTR
#define FontPtr                     APTR
#define CursorPtr                   APTR
#define CreateScreenResourcesProcPtr APTR

/* UXA specific replacements */
#define UxaDriverPtr                APTR
#define uxa_set_force_fallback(s, f) ((void)0)

/* EXA replacements */
#define exaGetPixmapPitch(x)        (x->pitch)
#define exaGetPixmapOffset(x)       (0)

/* Pixmap drawable replacements */
#define pixmap_drawable_bitsPerPixel(x)  (x->drawable.bitsPerPixel)
#define pixmap_drawable_depth(x)         (x->drawable.depth)
#define pixmap_drawable_width(x)         (x->drawable.width)
#define pixmap_drawable_height(x)        (x->drawable.height)

/* Pixmap accessors */
#define get_drawable_pixmap(d)      ((PixmapPtr)(d))

/* Raster op defines */
#define GXcopy                  0x03
#define GXclear                 0x00
#define GXand                   0x01
#define GXandReverse            0x02
#define GXandInverted           0x04
#define GXnoop                  0x05
#define GXxor                   0x06
#define GXor                    0x07
#define GXnor                   0x08
#define GXequiv                 0x09
#define GXinvert                0x0A
#define GXorReverse             0x0B
#define GXcopyInverted          0x0C
#define GXorInverted            0x0D
#define GXnand                  0x0E
#define GXset                   0x0F

/* UXA PM macros */
#define UXA_PM_IS_SOLID(d, pm)   (1)

#define MAX(a,b) (((a)>(b))?(a):(b))
#define MIN(a,b) (((a)<(b))?(a):(b))

/* Align */
#ifndef ALIGN
#define ALIGN(i,m)          (((i) + (m) - 1) & ~((m) - 1))
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x)       (sizeof(x) / sizeof(x[0]))
#endif

#ifndef KB
#define KB(x)               ((x) * 1024)
#endif
#ifndef MB
#define MB(x)               ((x) * KB(1024))
#endif

/* Pipe control defines */
#define BRW_PIPE_CONTROL                  (0x7a000002)
#define BRW_PIPE_CONTROL_CS_STALL         (1 << 20)
#define BRW_PIPE_CONTROL_STALL_AT_SCOREBOARD (1 << 1)
#define BRW_PIPE_CONTROL_WRITE_QWORD      (1 << 14)
#define BRW_PIPE_CONTROL_WC_FLUSH         (1 << 5)
#define BRW_PIPE_CONTROL_TC_FLUSH         (1 << 4)
#define BRW_PIPE_CONTROL_NOWRITE          (1 << 11)

/* MI commands used in batchbuffer */
#define MI_FLUSH                          (0x04 << 23)
#define MI_FLUSH_DW                       (0x26 << 23)
#define MI_BATCH_BUFFER_END               (0xA << 23)
#define MI_NOOP                           0x00
#define MI_LOAD_REGISTER_IMM              (0x22 << 23 | (3-2))
#define MI_WRITE_DIRTY_STATE              (1 << 4)
#define MI_INVALIDATE_MAP_CACHE           (1 << 0)
#define MI_INHIBIT_RENDER_CACHE_FLUSH     (1 << 2)
#define MI_STATE_INSTRUCTION_CACHE_FLUSH  (1 << 1)

#define BCS_SWCTRL                        0x22200
#define BCS_SWCTRL_SRC_Y                  (1 << 0)
#define BCS_SWCTRL_DST_Y                  (1 << 1)

/* BLT commands */
#define XY_COLOR_BLT_CMD                  ((2 << 29) | (0x50 << 22))
#define XY_COLOR_BLT_WRITE_ALPHA          (1 << 21)
#define XY_COLOR_BLT_WRITE_RGB            (1 << 20)
#define XY_COLOR_BLT_TILED                (1 << 11)

#define XY_SRC_COPY_BLT_CMD               ((2 << 29) | (0x53 << 22))
#define XY_SRC_COPY_BLT_WRITE_ALPHA       (1 << 21)
#define XY_SRC_COPY_BLT_WRITE_RGB         (1 << 20)
#define XY_SRC_COPY_BLT_SRC_TILED         (1 << 15)
#define XY_SRC_COPY_BLT_DST_TILED         (1 << 11)

#define XY_SETUP_CLIP_BLT_CMD             ((2 << 29) | (3 << 22))
#define XY_MONO_PAT_BLT_CMD               ((0x2 << 29) | (0x52 << 22))

/* Tiling defines */
#define I915_TILING_NONE    0
#define I915_TILING_X       1
#define I915_TILING_Y       2

/* I915_GEM_DOMAIN_* */
#define I915_GEM_DOMAIN_CPU         0x00000001
#define I915_GEM_DOMAIN_RENDER      0x00000002
#define I915_GEM_DOMAIN_SAMPLER     0x00000004
#define I915_GEM_DOMAIN_COMMAND     0x00000008
#define I915_GEM_DOMAIN_INSTRUCTION 0x00000008
#define I915_GEM_DOMAIN_VERTEX      0x00000010
#define I915_GEM_DOMAIN_GTT         0x00000020

/* I915_EXEC flags */
#define I915_EXEC_DEFAULT     0
#define I915_EXEC_RENDER      (1 << 0)
#define I915_EXEC_BLT         (3 << 0)

/* ROP defines */
#define ROP_0       0x00
#define ROP_DSa     0x55
#define ROP_SDna    0x33
#define ROP_S       0xCC
#define ROP_DSna    0x0F
#define ROP_D       0xAA
#define ROP_DSx     0x5A
#define ROP_DSo     0xEE
#define ROP_DSon    0x44
#define ROP_DSxn    0x75
#define ROP_Dn      0x05
#define ROP_SDno    0x22
#define ROP_Sn      0xBB
#define ROP_DSno    0x11
#define ROP_DSan    0x88
#define ROP_1       0xFF

/* Global card data pointer (like nouveau) */
extern struct CardData * globalcarddataptr;

/* Picture structure for composite operations (matches nv_include.h pattern) */
struct Picture
{
    LONG format;
    BOOL componentAlpha;
    LONG filter;
    BOOL repeat;
    LONG repeatType;

    struct
    {
        ULONG width;
        ULONG height;
    } *pDrawable, drawableAREA;
};

#define PictFilterNearest   1
#define PictFilterBilinear  2

#define RepeatNone          0
#define RepeatNormal        1
#define RepeatReflect       2
#define RepeatPad           3

#define PICT_UNKNOWN        0
#define PICT_a8r8g8b8       1
#define PICT_x8r8g8b8       2
#define PICT_a8b8g8r8       3
#define PICT_x8b8g8r8       4
#define PICT_b8g8r8a8       5
#define PICT_b8g8r8x8       6
#define PICT_r5g6b5         7
#define PICT_a8             8

static inline BOOL PICT_FORMAT_A(int format)
{
    switch(format)
    {
    case PICT_a8r8g8b8:
    case PICT_a8b8g8r8:
    case PICT_b8g8r8a8:
    case PICT_a8:
        return TRUE;
    }
    return FALSE;
}

static inline BOOL PICT_FORMAT_RGB(int format)
{
    switch(format)
    {
    case PICT_a8r8g8b8:
    case PICT_x8r8g8b8:
    case PICT_x8b8g8r8:
    case PICT_a8b8g8r8:
    case PICT_b8g8r8a8:
    case PICT_b8g8r8x8:
    case PICT_r5g6b5:
        return TRUE;
    }
    return FALSE;
}

static inline VOID HIDDIntelFillPictureFromBitMapData(struct Picture * pPict,
    struct HIDDIntelBitMapData * bmdata)
{
    if (bmdata->drawable.depth == 32)
        pPict->format = PICT_a8r8g8b8;
    else if (bmdata->drawable.depth == 24)
        pPict->format = PICT_x8r8g8b8;
    else if (bmdata->drawable.depth == 16)
        pPict->format = PICT_r5g6b5;
    else
        pPict->format = PICT_UNKNOWN;

    pPict->componentAlpha = FALSE;
    pPict->filter = PictFilterNearest;
    pPict->repeat = FALSE;
    pPict->repeatType = RepeatNone;
    pPict->pDrawable = &pPict->drawableAREA;
    pPict->pDrawable->width = bmdata->drawable.width;
    pPict->pDrawable->height = bmdata->drawable.height;
}

#endif /* __INTEL_XF86_H__ */
