/*
    Copyright (C) 2020-2026, The AROS Development Team. All rights reserved.

    Intel GPU accelerated batch buffer operations.
    Ported from xf86-video-intel UXA batch buffer pattern.
*/

#include "intel_intern.h"
#include "intel_xf86.h"

#include <aros/debug.h>
#include <proto/exec.h>

#define DEBUG 0

#define BATCH_RESERVED          64
#define BATCH_SIZE              (4 * 4096)

/*
 * ROP translation table for Intel BLT engine.
 * Maps the 16 X11 raster ops to Intel BLT ROP values.
 */
static const UBYTE IntelBLTROP[16] =
{
    0x00,  /* GXclear        -> ROP_0      */
    0x55,  /* GXand          -> ROP_DSa    */
    0x33,  /* GXandReverse   -> ROP_SDna   */
    0xCC,  /* GXcopy         -> ROP_S      */
    0x0F,  /* GXandInverted  -> ROP_DSna   */
    0xAA,  /* GXnoop         -> ROP_D      */
    0x5A,  /* GXxor          -> ROP_DSx    */
    0xEE,  /* GXor           -> ROP_DSo    */
    0x44,  /* GXnor          -> ROP_DSon   */
    0x75,  /* GXequiv        -> ROP_DSxn   */
    0x05,  /* GXinvert       -> ROP_Dn     */
    0x22,  /* GXorReverse    -> ROP_SDno   */
    0xBB,  /* GXcopyInverted -> ROP_Sn     */
    0x11,  /* GXorInverted   -> ROP_DSno   */
    0x88,  /* GXnand         -> ROP_DSan   */
    0xFF   /* GXset          -> ROP_1      */
};

/* Batch buffer management */
VOID HIDDIntelBatchInit(struct CardData *carddata)
{
    struct IntelBatchState *batch = &carddata->batch;

    batch->batch_used = 0;
    batch->batch_emit_start = 0;
    batch->batch_emitting = 0;
    batch->in_batch_atomic = FALSE;
    batch->current_batch = 0;
    batch->force_fallback = FALSE;
    batch->currentRop = -1;
    batch->chipset_gen = carddata->Generation;

    /* Allocate batch buffer objects */
    batch->last_batch_bo[0] = NULL;
    batch->last_batch_bo[1] = NULL;

    batch->batch_bo = drm_intel_bo_alloc(carddata->bufmgr, "batch",
        BATCH_SIZE, 4096);
    if (!batch->batch_bo)
    {
        bug("[Intel] Failed to allocate batch buffer\n");
    }

    D(bug("[Intel] Batch init complete\n"));
}

VOID HIDDIntelBatchTeardown(struct CardData *carddata)
{
    struct IntelBatchState *batch = &carddata->batch;
    int i;

    for (i = 0; i < 2; i++)
    {
        if (batch->last_batch_bo[i])
        {
            drm_intel_bo_unreference(batch->last_batch_bo[i]);
            batch->last_batch_bo[i] = NULL;
        }
    }

    if (batch->batch_bo)
    {
        drm_intel_bo_unreference(batch->batch_bo);
        batch->batch_bo = NULL;
    }
}

static int HIDDIntelBatchSpace(struct CardData *carddata)
{
    struct IntelBatchState *batch = &carddata->batch;
    return (BATCH_SIZE / 4 - BATCH_RESERVED) - batch->batch_used;
}

static void HIDDIntelNextBatch(struct CardData *carddata)
{
    struct IntelBatchState *batch = &carddata->batch;
    drm_intel_bo *tmp;

    drm_intel_gem_bo_clear_relocs(batch->batch_bo, 0);

    tmp = batch->last_batch_bo[batch->current_batch == I915_EXEC_BLT ? 1 : 0];
    batch->last_batch_bo[batch->current_batch == I915_EXEC_BLT ? 1 : 0] = batch->batch_bo;
    batch->batch_bo = tmp;
    batch->batch_used = 0;
}

VOID HIDDIntelBatchEmitFlush(struct CardData *carddata)
{
    struct IntelBatchState *batch = &carddata->batch;

    if (carddata->Generation >= 060)
    {
        /* Gen6+ BLT flush */
        batch->batch_ptr[batch->batch_used++] = MI_FLUSH_DW | 2;
        batch->batch_ptr[batch->batch_used++] = 0;
        batch->batch_ptr[batch->batch_used++] = 0;
        batch->batch_ptr[batch->batch_used++] = 0;
    }
    else if (carddata->Generation >= 040)
    {
        /* Gen4 flush */
        batch->batch_ptr[batch->batch_used++] = MI_FLUSH;
    }
    else
    {
        /* Gen2/3 flush */
        batch->batch_ptr[batch->batch_used++] = MI_FLUSH | MI_WRITE_DIRTY_STATE | MI_INVALIDATE_MAP_CACHE;
    }
}

VOID HIDDIntelBatchSubmit(struct CardData *carddata)
{
    struct IntelBatchState *batch = &carddata->batch;
    int ret;

    if (batch->batch_used == 0)
        return;

    /* Mark end of batch */
    batch->batch_ptr[batch->batch_used++] = MI_BATCH_BUFFER_END;
    if (batch->batch_used & 1)
        batch->batch_ptr[batch->batch_used++] = MI_NOOP;

    /* Upload batch buffer */
    ret = drm_intel_bo_subdata(batch->batch_bo, 0,
        batch->batch_used * 4, batch->batch_ptr);
    if (ret == 0)
    {
        /* Execute */
        ret = drm_intel_bo_mrb_exec(batch->batch_bo,
            batch->batch_used * 4,
            NULL, 0, 0xffffffff,
            HAS_BLT(carddata) ?
                (batch->current_batch == I915_EXEC_BLT ?
                 I915_EXEC_BLT : I915_EXEC_RENDER) :
                I915_EXEC_DEFAULT);
    }

    if (ret != 0)
    {
        batch->force_fallback = TRUE;
        bug("[Intel] Batch submit failed: %d\n", ret);
    }

    HIDDIntelNextBatch(carddata);
    batch->current_batch = 0;
}

static void HIDDIntelBatchEmitReloc(struct CardData *carddata,
    drm_intel_bo *target_bo, uint32_t read_domains,
    uint32_t write_domains, uint32_t delta)
{
    struct IntelBatchState *batch = &carddata->batch;
    uint64_t offset;

    drm_intel_bo_emit_reloc(batch->batch_bo,
        batch->batch_used * 4,
        target_bo, delta,
        read_domains, write_domains);

    offset = target_bo->offset64 + delta;

    batch->batch_ptr[batch->batch_used++] = (uint32_t)(offset & 0xFFFFFFFF);
    if (carddata->Generation >= 0100)
        batch->batch_ptr[batch->batch_used++] = (uint32_t)(offset >> 32);
}

/*
 * XY_COLOR_BLT - Solid color fill using BLT engine.
 * Available on all generations from Gen2 onwards.
 */
BOOL HIDDIntelBatchFillSolidRect(struct CardData *carddata,
    struct HIDDIntelBitMapData *bmdata, LONG minX, LONG minY, LONG maxX,
    LONG maxY, ULONG drawmode, ULONG color)
{
    struct IntelBatchState *batch = &carddata->batch;
    int w = maxX - minX + 1;
    int h = maxY - minY + 1;
    int pitch = bmdata->pitch;
    int bpp = bmdata->bytesperpixel;
    BOOL tiled = FALSE; /* FIXME: detect tiling */
    ULONG cmd;
    int need = 8; /* space needed in batch */
    int br00, br13;

    if (batch->force_fallback)
        return FALSE;

    if (w <= 0 || h <= 0)
        return TRUE;

    /* Ensure we have enough batch space */
    if (HIDDIntelBatchSpace(carddata) < need)
        HIDDIntelBatchSubmit(carddata);

    if (batch->current_batch != I915_EXEC_BLT)
    {
        if (batch->current_batch)
            HIDDIntelBatchSubmit(carddata);
    }
    batch->current_batch = I915_EXEC_BLT;

    /* Build XY_COLOR_BLT command */
    cmd = XY_COLOR_BLT_CMD;
    if (bpp == 4)
    {
        cmd |= XY_COLOR_BLT_WRITE_ALPHA | XY_COLOR_BLT_WRITE_RGB;
        br13 = 0; /* 32bpp */
    }
    else if (bpp == 2)
    {
        cmd |= XY_COLOR_BLT_WRITE_RGB;
        br13 = 1; /* 16bpp */
    }
    else
    {
        br13 = 3; /* 8bpp */
    }

    br00 = (3 << 24) | /* BLT */
           (0xCC << 16) | /* ROP_S (GXcopy) */
           w;

    batch->batch_ptr[batch->batch_used++] = cmd;
    batch->batch_ptr[batch->batch_used++] = br13;
    batch->batch_ptr[batch->batch_used++] = (minY << 16) | minX;
    batch->batch_ptr[batch->batch_used++] = ((minY + h - 1) << 16) | (minX + w - 1);
    HIDDIntelBatchEmitReloc(carddata, bmdata->bo,
        I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER, 0);
    batch->batch_ptr[batch->batch_used++] = (pitch << 16) | pitch;
    batch->batch_ptr[batch->batch_used++] = color;

    return TRUE;
}

/*
 * XY_SRC_COPY_BLT - Copy between bitmaps using BLT engine.
 * Available on all generations from Gen2 onwards.
 */
BOOL HIDDIntelBatchCopySameFormat(struct CardData *carddata,
    struct HIDDIntelBitMapData *srcdata, struct HIDDIntelBitMapData *destdata,
    LONG srcX, LONG srcY, LONG destX, LONG destY, LONG width, LONG height,
    ULONG drawmode)
{
    struct IntelBatchState *batch = &carddata->batch;
    int dst_pitch = destdata->pitch;
    int src_pitch = srcdata->pitch;
    int bpp = destdata->bytesperpixel;
    ULONG cmd;
    int br13;
    int need = 12;

    if (batch->force_fallback)
        return FALSE;

    if (width <= 0 || height <= 0)
        return TRUE;

    if (HIDDIntelBatchSpace(carddata) < need)
        HIDDIntelBatchSubmit(carddata);

    if (batch->current_batch != I915_EXEC_BLT)
    {
        if (batch->current_batch)
            HIDDIntelBatchSubmit(carddata);
    }
    batch->current_batch = I915_EXEC_BLT;

    /* Build XY_SRC_COPY_BLT command */
    cmd = XY_SRC_COPY_BLT_CMD;
    if (bpp == 4)
    {
        cmd |= XY_SRC_COPY_BLT_WRITE_ALPHA | XY_SRC_COPY_BLT_WRITE_RGB;
        br13 = 0; /* 32bpp */
    }
    else if (bpp == 2)
    {
        cmd |= XY_SRC_COPY_BLT_WRITE_RGB;
        br13 = 1; /* 16bpp */
    }
    else
    {
        br13 = 3; /* 8bpp */
    }

    batch->batch_ptr[batch->batch_used++] = cmd;
    batch->batch_ptr[batch->batch_used++] = br13;
    batch->batch_ptr[batch->batch_used++] = (srcY << 16) | srcX;
    batch->batch_ptr[batch->batch_used++] = (destY << 16) | destX;
    batch->batch_ptr[batch->batch_used++] = ((destY + height - 1) << 16) | (destX + width - 1);

    /* Source offset */
    HIDDIntelBatchEmitReloc(carddata, srcdata->bo,
        I915_GEM_DOMAIN_RENDER, 0,
        srcY * src_pitch + srcX * bpp);

    /* Destination offset */
    HIDDIntelBatchEmitReloc(carddata, destdata->bo,
        I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER,
        destY * dst_pitch + destX * bpp);

    batch->batch_ptr[batch->batch_used++] = (dst_pitch << 16) | src_pitch;

    return TRUE;
}
