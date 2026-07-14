/*
    Copyright 2010-2026, The AROS Development Team. All rights reserved.

    AROS DRM user-space interface.
    Implements basic DRM ioctls for modesetting and GEM buffer management.
*/

#include <aros/debug.h>
#include <proto/exec.h>

#include <libdrm/arosdrm.h>
#include <libdrm/arosdrmmode.h>
#include <uapi/drm/i915_drm.h>
#include <uapi/drm/drm_mode.h>

#define DEBUG 0

/* Simple GEM buffer tracking */
#define MAX_GEM_HANDLES 256

typedef struct
{
    uint32_t handle;
    ULONG    size;
    APTR     data;
    BOOL     used;
} gem_bo_entry;

static gem_bo_entry gem_handles[MAX_GEM_HANDLES];
static uint32_t next_handle = 1;

static gem_bo_entry *find_bo(uint32_t handle)
{
    int i;
    for (i = 0; i < MAX_GEM_HANDLES; i++)
        if (gem_handles[i].used && gem_handles[i].handle == handle)
            return &gem_handles[i];
    return NULL;
}

static uint32_t alloc_bo(ULONG size)
{
    int i;
    gem_bo_entry *entry = NULL;

    for (i = 0; i < MAX_GEM_HANDLES; i++)
    {
        if (!gem_handles[i].used)
        {
            entry = &gem_handles[i];
            break;
        }
    }

    if (!entry)
        return 0;

    entry->data = AllocVec(size, MEMF_CLEAR);
    if (!entry->data)
        return 0;

    entry->handle = next_handle++;
    entry->size = size;
    entry->used = TRUE;

    D(bug("[AROS DRM] alloc_bo: handle=%u, size=%lu\n", entry->handle, size));
    return entry->handle;
}

static void free_bo(uint32_t handle)
{
    gem_bo_entry *entry = find_bo(handle);
    if (entry)
    {
        if (entry->data)
            FreeVec(entry->data);
        entry->used = FALSE;
        entry->data = NULL;
        entry->size = 0;
    }
}

int drmOpen(const char *name, const char *busid)
{
    D(bug("[AROS DRM] drmOpen(%s)\n", name));
    return 0;
}

int drmClose(int fd)
{
    return 0;
}

int drmIoctl(int fd, unsigned long request, void *arg)
{
    int ret = -1;

    D(bug("[AROS DRM] drmIoctl: request=0x%lx\n", request));

    switch (request)
    {
    /* GEM buffer management */
    case DRM_IOCTL_I915_GEM_CREATE:
    {
        struct drm_i915_gem_create *create = arg;
        create->handle = alloc_bo(create->size);
        ret = create->handle ? 0 : -1;
        break;
    }

    case DRM_IOCTL_GEM_CLOSE:
    {
        struct drm_gem_close *close = arg;
        free_bo(close->handle);
        ret = 0;
        break;
    }

    case DRM_IOCTL_GEM_FLINK:
    {
        /* FIXME: implement proper flink */
        struct drm_gem_flink *flink = arg;
        flink->name = flink->handle;
        ret = 0;
        break;
    }

    case DRM_IOCTL_I915_GEM_PWRITE:
    {
        struct drm_i915_gem_pwrite *pwrite = arg;
        gem_bo_entry *entry = find_bo(pwrite->handle);
        if (entry && pwrite->data_ptr)
        {
            ULONG copy_size = pwrite->size;
            if (pwrite->offset + copy_size > entry->size)
                copy_size = entry->size - pwrite->offset;
            CopyMem((APTR)(unsigned long)pwrite->data_ptr,
                    (APTR)((IPTR)entry->data + pwrite->offset),
                    copy_size);
            ret = 0;
        }
        break;
    }

    case DRM_IOCTL_I915_GEM_PREAD:
    {
        struct drm_i915_gem_pread *pread = arg;
        gem_bo_entry *entry = find_bo(pread->handle);
        if (entry && pread->data_ptr)
        {
            ULONG copy_size = pread->size;
            if (pread->offset + copy_size > entry->size)
                copy_size = entry->size - pread->offset;
            CopyMem((APTR)((IPTR)entry->data + pread->offset),
                    (APTR)(unsigned long)pread->data_ptr,
                    copy_size);
            ret = 0;
        }
        break;
    }

    case DRM_IOCTL_I915_GEM_SET_DOMAIN:
        ret = 0;
        break;

    case DRM_IOCTL_I915_GEM_SW_FINISH:
        ret = 0;
        break;

    case DRM_IOCTL_I915_GEM_BUSY:
    {
        struct drm_i915_gem_busy *busy = arg;
        busy->busy = 0; /* Always not busy in this simple implementation */
        ret = 0;
        break;
    }

    case DRM_IOCTL_I915_GEM_WAIT:
        ret = 0; /* Always ready */
        break;

    case DRM_IOCTL_I915_GEM_MMAP_GTT:
    {
        struct drm_i915_gem_mmap_gtt *mmap_gtt = arg;
        gem_bo_entry *entry = find_bo(mmap_gtt->handle);
        if (entry)
        {
            /* Return the offset = address for GTT mapping.
               The caller will use this to access the buffer. */
            mmap_gtt->offset = (uint64_t)(unsigned long)entry->data;
            ret = 0;
        }
        break;
    }

    case DRM_IOCTL_I915_GEM_SET_TILING:
    {
        struct drm_i915_gem_set_tiling *set = arg;
        set->tiling_mode = set->tiling_mode; /* Accept any tiling */
        ret = 0;
        break;
    }

    case DRM_IOCTL_I915_GEM_GET_TILING:
    {
        struct drm_i915_gem_get_tiling *get = arg;
        get->tiling_mode = I915_TILING_NONE;
        get->swizzle_mode = 0;
        ret = 0;
        break;
    }

    case DRM_IOCTL_I915_GEM_CONTEXT_CREATE:
    {
        struct drm_i915_gem_context_create *ctx = arg;
        ctx->ctx_id = 1; /* Dummy context */
        ret = 0;
        break;
    }

    case DRM_IOCTL_I915_GEM_CONTEXT_DESTROY:
        ret = 0;
        break;

    case DRM_IOCTL_I915_GEM_EXECBUFFER2:
    {
        /* FIXME: Execute batch buffer. For now, just accept it. */
        ret = 0;
        break;
    }

    case DRM_IOCTL_I915_GETPARAM:
    {
        struct drm_i915_getparam *gp = arg;
        switch (gp->param)
        {
        case I915_PARAM_CHIPSET_ID:
            *(int *)(unsigned long)gp->value = 0x2a02; /* G45 chipset */
            ret = 0;
            break;
        case I915_PARAM_HAS_BLT:
            *(int *)(unsigned long)gp->value = 1;
            ret = 0;
            break;
        case I915_PARAM_HAS_RELAXED_FENCING:
            *(int *)(unsigned long)gp->value = 1;
            ret = 0;
            break;
        case I915_PARAM_HAS_EXECBUF2:
            *(int *)(unsigned long)gp->value = 1;
            ret = 0;
            break;
        default:
            *(int *)(unsigned long)gp->value = 0;
            ret = 0;
            break;
        }
        break;
    }

    /* Modesetting ioctls */
    case DRM_IOCTL_MODE_GETRESOURCES:
    {
        struct drm_mode_card_res *res = arg;
        /* Always return counts first.
           The caller does 2 calls: 1st with NULL pointers to get counts,
           2nd with allocated buffers pointed by the pointers. */
        static uint32_t crtc_ids[] = { 1 };
        static uint32_t conn_ids[] = { 1 };
        static uint32_t enc_ids[] = { 1 };

        res->min_width = 640;
        res->max_width = 4096;
        res->min_height = 480;
        res->max_height = 4096;

        res->count_fbs = 0;
        res->count_crtcs = 1;
        res->count_connectors = 1;
        res->count_encoders = 1;

        if (res->crtc_id_ptr)
            CopyMem(crtc_ids, (APTR)(unsigned long)res->crtc_id_ptr, sizeof(crtc_ids));

        if (res->connector_id_ptr)
            CopyMem(conn_ids, (APTR)(unsigned long)res->connector_id_ptr, sizeof(conn_ids));

        if (res->encoder_id_ptr)
            CopyMem(enc_ids, (APTR)(unsigned long)res->encoder_id_ptr, sizeof(enc_ids));

        ret = 0;
        break;
    }

    case DRM_IOCTL_MODE_GETCONNECTOR:
    {
        struct drm_mode_get_connector *conn = arg;
        static struct drm_mode_modeinfo modes[] =
        {
            {
                .clock = 65000,
                .hdisplay = 1024, .hsync_start = 1048, .hsync_end = 1184, .htotal = 1344,
                .vdisplay = 768, .vsync_start = 771, .vsync_end = 777, .vtotal = 806,
                .vrefresh = 60,
                .name = "1024x768",
                .type = DRM_MODE_TYPE_PREFERRED,
            },
            {
                .clock = 83500,
                .hdisplay = 1280, .hsync_start = 1296, .hsync_end = 1440, .htotal = 1688,
                .vdisplay = 1024, .vsync_start = 1027, .vsync_end = 1032, .vtotal = 1066,
                .vrefresh = 60,
                .name = "1280x1024",
                .type = DRM_MODE_TYPE_DRIVER,
            },
        };
        int num_modes = sizeof(modes) / sizeof(modes[0]);

        conn->connector_id = 1;
        conn->encoder_id = 0;
        conn->connection = DRM_MODE_CONNECTED;
        conn->mm_width = 300;
        conn->mm_height = 200;
        conn->subpixel = 0;
        conn->connector_type = DRM_MODE_CONNECTOR_VGA;
        conn->connector_type_id = 1;

        conn->count_modes = num_modes;
        conn->count_props = 0;
        conn->count_encoders = 0;

        if (conn->modes_ptr)
            CopyMem(modes, (APTR)(unsigned long)conn->modes_ptr, num_modes * sizeof(modes[0]));

        ret = 0;
        break;
    }

    case DRM_IOCTL_MODE_GETENCODER:
    {
        struct drm_mode_get_encoder *enc = arg;
        enc->encoder_id = 1;
        enc->encoder_type = DRM_MODE_ENCODER_DAC;
        enc->possible_crtcs = 1;
        enc->possible_clones = 0;
        enc->crtc_id = 1;
        ret = 0;
        break;
    }

    case DRM_IOCTL_MODE_GETCRTC:
    {
        struct drm_mode_crtc *crtc = arg;
        crtc->crtc_id = 1;
        crtc->x = 0;
        crtc->y = 0;
        crtc->fb_id = 0;
        crtc->mode_valid = 0;
        crtc->gamma_size = 0;
        ret = 0;
        break;
    }

    case DRM_IOCTL_MODE_ADDFB:
    {
        struct drm_mode_fb_cmd *fb = arg;
        static uint32_t next_fb_id = 1;
        fb->fb_id = next_fb_id++;
        D(bug("[AROS DRM] AddFB: id=%u, %ux%u, pitch=%u\n",
            fb->fb_id, fb->width, fb->height, fb->pitch));
        ret = 0;
        break;
    }

    case DRM_IOCTL_MODE_ADDFB2:
    {
        struct drm_mode_fb_cmd2 *fb2 = arg;
        static uint32_t next_fb_id2 = 100;
        fb2->fb_id = next_fb_id2++;
        D(bug("[AROS DRM] AddFB2: id=%u, %ux%u\n",
            fb2->fb_id, fb2->width, fb2->height));
        ret = 0;
        break;
    }

    case DRM_IOCTL_MODE_RMFB:
        ret = 0;
        break;

    case DRM_IOCTL_MODE_SETCRTC:
    {
        struct drm_mode_crtc *crtc = arg;
        D(bug("[AROS DRM] SetCrtc: crtc=%u, fb=%u, %ux%u\n",
            crtc->crtc_id, crtc->fb_id, crtc->mode.hdisplay, crtc->mode.vdisplay));
        ret = 0;
        break;
    }

    case DRM_IOCTL_MODE_CURSOR:
        ret = 0;
        break;

    case DRM_IOCTL_MODE_DIRTYFB:
        ret = 0;
        break;

    case DRM_IOCTL_MODE_PAGE_FLIP:
        ret = 0;
        break;

    default:
        bug("[AROS DRM] Unsupported ioctl 0x%lx\n", request);
        ret = -1;
        break;
    }

    return ret;
}

void *drmMMap(int fd, uint32_t handle, VOID (*unmapped)(APTR), APTR data)
{
    gem_bo_entry *entry = find_bo(handle);
    if (entry)
        return entry->data;
    return NULL;
}

void drmMUnmap(int fd, uint32_t handle)
{
}

drmVersionPtr drmGetVersion(int fd)
{
    return NULL;
}

void drmFreeVersion(drmVersionPtr ptr) {}

int drmCreateContext(int fd, drm_context_t *handle) { return 0; }
int drmDestroyContext(int fd, drm_context_t handle) { return 0; }
void *drmMalloc(int size) { return AllocVec(size, MEMF_CLEAR); }
void drmFree(void *pt) { if (pt) FreeVec(pt); }
int drmCommandNone(int fd, unsigned long cmd) { return 0; }
int drmCommandRead(int fd, unsigned long cmd, void *data, unsigned long size) { return 0; }
int drmCommandWrite(int fd, unsigned long cmd, void *data, unsigned long size) { return 0; }
int drmCommandWriteRead(int fd, unsigned long cmd, void *data, unsigned long size) { return 0; }
