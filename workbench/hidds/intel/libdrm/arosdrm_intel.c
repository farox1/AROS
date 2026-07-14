/*
    Copyright 2026, The AROS Development Team. All rights reserved.

    AROS-native implementation of libdrm_intel buffer management.
    Uses drmIoctl() to communicate with the ported i915 kernel DRM driver.
*/

#include <stdarg.h>
#include <string.h>
#include <aros/debug.h>
#include <proto/exec.h>

#include <libdrm/arosdrm.h>
#include <intel_bufmgr.h>
#include <uapi/drm/i915_drm.h>

#define DEBUG 0

/* Buffer manager context */
typedef struct _drm_intel_bufmgr_gem
{
    int fd;
    int debug;
} drm_intel_bufmgr_gem;

/* Internal BO structure extending the public one */
typedef struct _drm_intel_bo_gem
{
    drm_intel_bo base;
    uint32_t     tiling_mode;
    uint32_t     swizzle_mode;
    int          mapped;
    int          map_count;
} drm_intel_bo_gem;

/* Allocate a new buffer manager */
drm_intel_bufmgr *drm_intel_bufmgr_gem_init(int fd, int batch_size)
{
    drm_intel_bufmgr_gem *bufmgr;

    D(bug("[AROS libdrm] drm_intel_bufmgr_gem_init(fd=%d, batch_size=%d)\n",
        fd, batch_size));

    bufmgr = (drm_intel_bufmgr_gem *)AllocVec(sizeof(drm_intel_bufmgr_gem), MEMF_CLEAR);
    if (!bufmgr)
        return NULL;

    bufmgr->fd = fd;
    bufmgr->debug = 0;

    return (drm_intel_bufmgr *)bufmgr;
}

void drm_intel_bufmgr_destroy(drm_intel_bufmgr *bufmgr)
{
    if (bufmgr)
        FreeVec(bufmgr);
}

void drm_intel_bufmgr_set_debug(drm_intel_bufmgr *bufmgr, int enable_debug)
{
    drm_intel_bufmgr_gem *bg = (drm_intel_bufmgr_gem *)bufmgr;
    if (bg)
        bg->debug = enable_debug;
}

/* Allocate a buffer object */
drm_intel_bo *drm_intel_bo_alloc(drm_intel_bufmgr *bufmgr,
    const char *name, unsigned long size, unsigned int alignment)
{
    drm_intel_bufmgr_gem *bg = (drm_intel_bufmgr_gem *)bufmgr;
    drm_intel_bo_gem *bo_gem;
    struct drm_i915_gem_create create;
    int ret;

    if (!bufmgr)
        return NULL;

    D(bug("[AROS libdrm] bo_alloc: size=%lu, align=%u\n", size, alignment));

    bo_gem = (drm_intel_bo_gem *)AllocVec(sizeof(drm_intel_bo_gem), MEMF_CLEAR);
    if (!bo_gem)
        return NULL;

    /* Request GEM buffer from DRM */
    memset(&create, 0, sizeof(create));
    create.size = size;
    ret = drmIoctl(bg->fd, DRM_IOCTL_I915_GEM_CREATE, &create);
    if (ret)
    {
        D(bug("[AROS libdrm] GEM_CREATE failed: %d\n", ret));
        FreeVec(bo_gem);
        return NULL;
    }

    bo_gem->base.bufmgr = bufmgr;
    bo_gem->base.handle = create.handle;
    bo_gem->base.size = size;
    bo_gem->base.align = alignment ? alignment : 4096;
    bo_gem->base.virtual = NULL;
    bo_gem->base.offset = 0;
    bo_gem->base.offset64 = 0;
    bo_gem->tiling_mode = I915_TILING_NONE;

    D(bug("[AROS libdrm] bo_alloc: handle=%u\n", bo_gem->base.handle));

    return &bo_gem->base;
}

void drm_intel_bo_reference(drm_intel_bo *bo)
{
    /* Reference counting not implemented */
}

void drm_intel_bo_unreference(drm_intel_bo *bo)
{
    drm_intel_bo_gem *bo_gem = (drm_intel_bo_gem *)bo;

    if (!bo)
        return;

    /* Close the GEM handle */
    if (bo->handle)
    {
        struct drm_gem_close close;
        memset(&close, 0, sizeof(close));
        close.handle = bo->handle;
        drmIoctl(((drm_intel_bufmgr_gem *)bo->bufmgr)->fd,
                 DRM_IOCTL_GEM_CLOSE, &close);
    }

    if (bo->virtual)
    {
        /* Unmap if mapped */
        drm_intel_bo_unmap(bo);
    }

    FreeVec(bo_gem);
}

int drm_intel_bo_map(drm_intel_bo *bo, int write_enable)
{
    /* Map via GTT */
    return drm_intel_gem_bo_map_gtt(bo);
}

int drm_intel_bo_unmap(drm_intel_bo *bo)
{
    drm_intel_bo_gem *bo_gem = (drm_intel_bo_gem *)bo;

    if (!bo || !bo->virtual)
        return 0;

    /* Unmap - AROS doesn't need to munmap GTT mappings */
    bo->virtual = NULL;
    bo_gem->mapped = 0;

    return 0;
}

int drm_intel_bo_subdata(drm_intel_bo *bo, unsigned long offset,
    unsigned long size, const void *data)
{
    struct drm_i915_gem_pwrite pwrite;

    if (!bo)
        return -1;

    D(bug("[AROS libdrm] bo_subdata: handle=%u, offset=%lu, size=%lu\n",
        bo->handle, offset, size));

    memset(&pwrite, 0, sizeof(pwrite));
    pwrite.handle = bo->handle;
    pwrite.offset = offset;
    pwrite.size = size;
    pwrite.data_ptr = (uint64_t)(unsigned long)data;

    return drmIoctl(((drm_intel_bufmgr_gem *)bo->bufmgr)->fd,
                    DRM_IOCTL_I915_GEM_PWRITE, &pwrite);
}

int drm_intel_bo_get_subdata(drm_intel_bo *bo, unsigned long offset,
    unsigned long size, void *data)
{
    struct drm_i915_gem_pread pread;

    memset(&pread, 0, sizeof(pread));
    pread.handle = bo->handle;
    pread.offset = offset;
    pread.size = size;
    pread.data_ptr = (uint64_t)(unsigned long)data;

    return drmIoctl(((drm_intel_bufmgr_gem *)bo->bufmgr)->fd,
                    DRM_IOCTL_I915_GEM_PREAD, &pread);
}

void drm_intel_bo_wait_rendering(drm_intel_bo *bo)
{
    struct drm_i915_gem_wait wait;

    memset(&wait, 0, sizeof(wait));
    wait.bo_handle = bo->handle;
    wait.timeout_ns = -1; /* Infinite wait */

    drmIoctl(((drm_intel_bufmgr_gem *)bo->bufmgr)->fd,
             DRM_IOCTL_I915_GEM_WAIT, &wait);
}

int drm_intel_bo_mrb_exec(drm_intel_bo *bo, int used,
    struct drm_clip_rect *cliprects, int num_cliprects, int DR4,
    unsigned int flags)
{
    struct drm_i915_gem_execbuffer2 execbuf;
    struct drm_i915_gem_exec_object2 exec_objects[1];

    D(bug("[AROS libdrm] bo_mrb_exec: handle=%u, used=%d, flags=%u\n",
        bo->handle, used, flags));

    memset(&exec_objects, 0, sizeof(exec_objects));
    exec_objects[0].handle = bo->handle;
    exec_objects[0].relocation_count = 0;
    exec_objects[0].relocs_ptr = 0;
    exec_objects[0].alignment = 0;
    exec_objects[0].offset = 0;
    exec_objects[0].flags = 0;
    exec_objects[0].rsvd1 = 0;
    exec_objects[0].rsvd2 = 0;

    memset(&execbuf, 0, sizeof(execbuf));
    execbuf.buffers_ptr = (uint64_t)(unsigned long)exec_objects;
    execbuf.buffer_count = 1;
    execbuf.batch_start_offset = 0;
    execbuf.batch_len = used;
    execbuf.cliprects_ptr = (uint64_t)(unsigned long)cliprects;
    execbuf.num_cliprects = num_cliprects;
    execbuf.DR1 = 0;
    execbuf.DR4 = DR4;
    execbuf.flags = flags;
    execbuf.rsvd1 = 0;
    execbuf.rsvd2 = 0;

    return drmIoctl(((drm_intel_bufmgr_gem *)bo->bufmgr)->fd,
                    DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf);
}

int drm_intel_bo_exec(drm_intel_bo *bo, int used,
    struct drm_clip_rect *cliprects, int num_cliprects, int DR4)
{
    return drm_intel_bo_mrb_exec(bo, used, cliprects, num_cliprects, DR4,
                                  I915_EXEC_DEFAULT);
}

int drm_intel_bufmgr_check_aperture_space(drm_intel_bo **bo_array, int count)
{
    /* FIXME: implement proper aperture checking */
    return 0;
}

int drm_intel_bo_emit_reloc(drm_intel_bo *bo, uint32_t offset,
    drm_intel_bo *target_bo, uint32_t target_offset,
    uint32_t read_domains, uint32_t write_domain)
{
    /* For the batch buffer approach, relocations are handled
       internally by writing the target offset directly.
       The execbuffer ioctl will also handle relocations if needed. */
    return 0;
}

int drm_intel_bo_emit_reloc_fence(drm_intel_bo *bo, uint32_t offset,
    drm_intel_bo *target_bo, uint32_t target_offset,
    uint32_t read_domains, uint32_t write_domain)
{
    return drm_intel_bo_emit_reloc(bo, offset, target_bo, target_offset,
                                   read_domains, write_domain);
}

int drm_intel_gem_bo_map_gtt(drm_intel_bo *bo)
{
    drm_intel_bo_gem *bo_gem = (drm_intel_bo_gem *)bo;
    struct drm_i915_gem_mmap_gtt mmap_gtt;
    int ret;

    D(bug("[AROS libdrm] map_gtt: handle=%u\n", bo->handle));

    if (bo_gem->mapped && bo->virtual)
        return 0;

    memset(&mmap_gtt, 0, sizeof(mmap_gtt));
    mmap_gtt.handle = bo->handle;

    ret = drmIoctl(((drm_intel_bufmgr_gem *)bo->bufmgr)->fd,
                   DRM_IOCTL_I915_GEM_MMAP_GTT, &mmap_gtt);
    if (ret)
    {
        D(bug("[AROS libdrm] MMAP_GTT failed: %d\n", ret));
        return ret;
    }

    /* FIXME: Actually mmap the offset. This requires the DRM layer
       to provide a mmap implementation. For now, allocate CPU-side. */
    if (!bo->virtual)
    {
        bo->virtual = AllocVec(bo->size, MEMF_CLEAR);
        if (!bo->virtual)
            return -1;
    }

    bo_gem->mapped = 1;
    return 0;
}

int drm_intel_gem_bo_unmap_gtt(drm_intel_bo *bo)
{
    return drm_intel_bo_unmap(bo);
}

void drm_intel_gem_bo_clear_relocs(drm_intel_bo *bo, int start)
{
    /* Relocations not tracked in this simple implementation */
}

int drm_intel_bo_busy(drm_intel_bo *bo)
{
    struct drm_i915_gem_busy busy;

    memset(&busy, 0, sizeof(busy));
    busy.handle = bo->handle;

    if (drmIoctl(((drm_intel_bufmgr_gem *)bo->bufmgr)->fd,
                 DRM_IOCTL_I915_GEM_BUSY, &busy))
        return 0;

    return busy.busy;
}

int drm_intel_bo_madvise(drm_intel_bo *bo, int madv)
{
    return 0;
}

int drm_intel_bo_set_tiling(drm_intel_bo *bo, uint32_t *tiling_mode,
    uint32_t stride)
{
    drm_intel_bo_gem *bo_gem = (drm_intel_bo_gem *)bo;
    struct drm_i915_gem_set_tiling set_tiling;
    int ret;

    memset(&set_tiling, 0, sizeof(set_tiling));
    set_tiling.handle = bo->handle;
    set_tiling.tiling_mode = *tiling_mode;
    set_tiling.stride = stride;

    ret = drmIoctl(((drm_intel_bufmgr_gem *)bo->bufmgr)->fd,
                   DRM_IOCTL_I915_GEM_SET_TILING, &set_tiling);

    *tiling_mode = set_tiling.tiling_mode;
    if (!ret)
        bo_gem->tiling_mode = set_tiling.tiling_mode;

    return ret;
}

int drm_intel_bo_get_tiling(drm_intel_bo *bo, uint32_t *tiling_mode,
    uint32_t *swizzle_mode)
{
    drm_intel_bo_gem *bo_gem = (drm_intel_bo_gem *)bo;
    struct drm_i915_gem_get_tiling get_tiling;

    memset(&get_tiling, 0, sizeof(get_tiling));
    get_tiling.handle = bo->handle;

    int ret = drmIoctl(((drm_intel_bufmgr_gem *)bo->bufmgr)->fd,
                       DRM_IOCTL_I915_GEM_GET_TILING, &get_tiling);
    if (ret)
        return ret;

    *tiling_mode = get_tiling.tiling_mode;
    *swizzle_mode = get_tiling.swizzle_mode;

    return 0;
}

int drm_intel_bo_flink(drm_intel_bo *bo, uint32_t *name)
{
    struct drm_gem_flink flink;

    memset(&flink, 0, sizeof(flink));
    flink.handle = bo->handle;

    int ret = drmIoctl(((drm_intel_bufmgr_gem *)bo->bufmgr)->fd,
                       DRM_IOCTL_GEM_FLINK, &flink);
    if (!ret)
        *name = flink.name;

    return ret;
}

int drm_intel_bo_pin(drm_intel_bo *bo, uint32_t alignment)
{
    return 0;
}

int drm_intel_bo_unpin(drm_intel_bo *bo)
{
    return 0;
}

int drm_intel_bo_disable_reuse(drm_intel_bo *bo)
{
    return 0;
}

int drm_intel_bo_is_reusable(drm_intel_bo *bo)
{
    return 0;
}

int drm_intel_bo_references(drm_intel_bo *bo, drm_intel_bo *target_bo)
{
    return 0;
}

int drm_intel_gem_bo_get_reloc_count(drm_intel_bo *bo)
{
    return 0;
}

void drm_intel_gem_bo_start_gtt_access(drm_intel_bo *bo, int write_enable)
{
}

drm_intel_bo *drm_intel_bo_alloc_for_render(drm_intel_bufmgr *bufmgr,
    const char *name, unsigned long size, unsigned int alignment)
{
    return drm_intel_bo_alloc(bufmgr, name, size, alignment);
}

drm_intel_bo *drm_intel_bo_alloc_tiled(drm_intel_bufmgr *bufmgr,
    const char *name, int x, int y, int cpp,
    uint32_t *tiling_mode, unsigned long *pitch,
    unsigned long flags)
{
    drm_intel_bo *bo;

    *pitch = ((x * cpp) + 63) & ~63;
    bo = drm_intel_bo_alloc(bufmgr, name, *pitch * y, 4096);

    if (bo && *tiling_mode != I915_TILING_NONE)
        drm_intel_bo_set_tiling(bo, tiling_mode, *pitch);

    return bo;
}

drm_intel_bo *drm_intel_bo_gem_create_from_name(drm_intel_bufmgr *bufmgr,
    const char *name, unsigned int handle)
{
    return NULL; /* FIXME: implement import from name */
}

int drm_intel_bo_use_48b_address_range(drm_intel_bo *bo, uint32_t enable)
{
    return 0;
}

int drm_intel_bo_set_softpin_offset(drm_intel_bo *bo, uint64_t offset)
{
    return 0;
}

int drm_intel_bufmgr_gem_can_disable_implicit_sync(drm_intel_bufmgr *bufmgr)
{
    return 0;
}

void drm_intel_gem_bo_disable_implicit_sync(drm_intel_bo *bo)
{
}

void drm_intel_gem_bo_enable_implicit_sync(drm_intel_bo *bo)
{
}

void drm_intel_bufmgr_gem_enable_reuse(drm_intel_bufmgr *bufmgr)
{
}

void drm_intel_bufmgr_gem_enable_fenced_relocs(drm_intel_bufmgr *bufmgr)
{
}

void drm_intel_bufmgr_gem_set_vma_cache_size(drm_intel_bufmgr *bufmgr, int limit)
{
}
