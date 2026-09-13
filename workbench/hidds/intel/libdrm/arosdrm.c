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
#define NUM_MODES       32

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

    return entry->handle;
}

static void free_bo(uint32_t handle)
{
    gem_bo_entry *entry = find_bo(handle);
    if (entry)
    {
        FreeVec(entry->data);
        entry->data = NULL;
        entry->size = 0;
        entry->used = FALSE;
    }
}

/*
 * Panel/connector topology, set once at probe time by the HIDD. Used to filter
 * the mode list: an eDP fixed panel (laptop) only offers modes <= its native
 * resolution (Linux offers just the panel's fixed mode), while a DP/HDMI monitor
 * (desktop) gets the full list. This keeps the driver universal: the mode list
 * adapts to whatever panel/monitor is attached.
 */
static BOOL drm_is_edp = FALSE;
static ULONG drm_panel_width = 0;
static ULONG drm_panel_height = 0;

void arosdrm_set_panel_native(BOOL is_edp, ULONG width, ULONG height)
{
    drm_is_edp = is_edp;
    drm_panel_width = width;
    drm_panel_height = height;
    D(bug("[AROS DRM] panel native: is_edp=%d %lux%lu\n", is_edp, width, height));
}

/*
 * Monitor/panel native mode read from EDID by the HIDD. When set, it is offered
 * as the preferred mode (first entry) of the connector, ahead of the static DMT
 * fallback list. The DTD is fully populated, so it can be selected directly.
 */
static BOOL drm_edid_mode_valid = FALSE;
static struct drm_mode_modeinfo drm_edid_mode;

void arosdrm_set_edid_mode(const struct drm_mode_modeinfo *mode)
{
    if (mode && mode->hdisplay && mode->vdisplay)
    {
        drm_edid_mode = *mode;
        drm_edid_mode_valid = TRUE;
        D(bug("[AROS DRM] EDID preferred mode: %ux%u@%u clk=%u\n",
              mode->hdisplay, mode->vdisplay, mode->vrefresh, mode->clock));
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
            if (pwrite->offset > entry->size)
                copy_size = 0;
            else if (copy_size > entry->size - pwrite->offset)
                copy_size = entry->size - pwrite->offset;
            if (copy_size)
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
            if (pread->offset > entry->size)
                copy_size = 0;
            else if (copy_size > entry->size - pread->offset)
                copy_size = entry->size - pread->offset;
            if (copy_size)
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
            /* Standard VESA DMT modes (external monitor) */
            { .clock = 65000,
              .hdisplay = 1024, .hsync_start = 1048, .hsync_end = 1184, .htotal = 1344,
              .vdisplay = 768, .vsync_start = 771, .vsync_end = 777, .vtotal = 806,
              .vrefresh = 60, .name = "1024x768", .type = DRM_MODE_TYPE_PREFERRED },
            { .clock = 148500,
              .hdisplay = 1920, .hsync_start = 2008, .hsync_end = 2052, .htotal = 2200,
              .vdisplay = 1080, .vsync_start = 1084, .vsync_end = 1089, .vtotal = 1125,
              .vrefresh = 60, .name = "1920x1080", .type = DRM_MODE_TYPE_DRIVER },
            { .clock = 146250,
              .hdisplay = 1680, .hsync_start = 1784, .hsync_end = 1960, .htotal = 2240,
              .vdisplay = 1050, .vsync_start = 1053, .vsync_end = 1059, .vtotal = 1089,
              .vrefresh = 60, .name = "1680x1050", .type = DRM_MODE_TYPE_DRIVER },
            { .clock = 162000,
              .hdisplay = 1600, .hsync_start = 1664, .hsync_end = 1856, .htotal = 2160,
              .vdisplay = 1200, .vsync_start = 1201, .vsync_end = 1204, .vtotal = 1250,
              .vrefresh = 60, .name = "1600x1200", .type = DRM_MODE_TYPE_DRIVER },
            { .clock = 108000,
              .hdisplay = 1600, .hsync_start = 1624, .hsync_end = 1704, .htotal = 1800,
              .vdisplay = 900, .vsync_start = 901, .vsync_end = 904, .vtotal = 1000,
              .vrefresh = 60, .name = "1600x900", .type = DRM_MODE_TYPE_DRIVER },
            { .clock = 106500,
              .hdisplay = 1440, .hsync_start = 1520, .hsync_end = 1672, .htotal = 1904,
              .vdisplay = 900, .vsync_start = 903, .vsync_end = 909, .vtotal = 934,
              .vrefresh = 60, .name = "1440x900", .type = DRM_MODE_TYPE_DRIVER },
            { .clock = 121750,
              .hdisplay = 1400, .hsync_start = 1488, .hsync_end = 1632, .htotal = 1864,
              .vdisplay = 1050, .vsync_start = 1053, .vsync_end = 1057, .vtotal = 1089,
              .vrefresh = 60, .name = "1400x1050", .type = DRM_MODE_TYPE_DRIVER },
            { .clock = 85500,
              .hdisplay = 1366, .hsync_start = 1436, .hsync_end = 1579, .htotal = 1792,
              .vdisplay = 768, .vsync_start = 771, .vsync_end = 774, .vtotal = 798,
              .vrefresh = 60, .name = "1366x768", .type = DRM_MODE_TYPE_DRIVER },
            { .clock = 108000,
              .hdisplay = 1280, .hsync_start = 1328, .hsync_end = 1440, .htotal = 1688,
              .vdisplay = 1024, .vsync_start = 1025, .vsync_end = 1028, .vtotal = 1066,
              .vrefresh = 60, .name = "1280x1024", .type = DRM_MODE_TYPE_DRIVER },
            { .clock = 108000,
              .hdisplay = 1280, .hsync_start = 1376, .hsync_end = 1488, .htotal = 1800,
              .vdisplay = 960, .vsync_start = 961, .vsync_end = 964, .vtotal = 1000,
              .vrefresh = 60, .name = "1280x960", .type = DRM_MODE_TYPE_DRIVER },
            { .clock = 83500,
              .hdisplay = 1280, .hsync_start = 1352, .hsync_end = 1480, .htotal = 1680,
              .vdisplay = 800, .vsync_start = 803, .vsync_end = 809, .vtotal = 831,
              .vrefresh = 60, .name = "1280x800", .type = DRM_MODE_TYPE_DRIVER },
            { .clock = 74250,
              .hdisplay = 1280, .hsync_start = 1390, .hsync_end = 1430, .htotal = 1650,
              .vdisplay = 720, .vsync_start = 725, .vsync_end = 730, .vtotal = 750,
              .vrefresh = 60, .name = "1280x720", .type = DRM_MODE_TYPE_DRIVER },
            { .clock = 108000,
              .hdisplay = 1152, .hsync_start = 1216, .hsync_end = 1344, .htotal = 1600,
              .vdisplay = 864, .vsync_start = 865, .vsync_end = 868, .vtotal = 900,
              .vrefresh = 75, .name = "1152x864", .type = DRM_MODE_TYPE_DRIVER },
            { .clock = 40000,
              .hdisplay = 800, .hsync_start = 840, .hsync_end = 968, .htotal = 1056,
              .vdisplay = 600, .vsync_start = 601, .vsync_end = 605, .vtotal = 628,
              .vrefresh = 60, .name = "800x600", .type = DRM_MODE_TYPE_DRIVER },
            { .clock = 25175,
              .hdisplay = 640, .hsync_start = 656, .hsync_end = 752, .htotal = 800,
              .vdisplay = 480, .vsync_start = 490, .vsync_end = 492, .vtotal = 525,
              .vrefresh = 60, .name = "640x480", .type = DRM_MODE_TYPE_DRIVER },
        };
        int num_modes = sizeof(modes) / sizeof(modes[0]);
        int vis, out = 0;
        static struct drm_mode_modeinfo filtered[NUM_MODES];

        /* The monitor/panel EDID native mode (read by the HIDD at probe time)
         * goes first as the preferred mode. */
        if (drm_edid_mode_valid)
            filtered[out++] = drm_edid_mode;

        /* For an eDP fixed panel, drop any mode taller/wider than the panel
         * native size (Linux only offers the panel's fixed mode there). A static
         * entry duplicating the EDID native mode is dropped too. */
        for (vis = 0; vis < num_modes; vis++)
        {
            if (drm_edid_mode_valid &&
                modes[vis].hdisplay == drm_edid_mode.hdisplay &&
                modes[vis].vdisplay == drm_edid_mode.vdisplay)
                continue;
            if (drm_is_edp && drm_panel_width >= 8 && drm_panel_height >= 8 &&
                (modes[vis].hdisplay > drm_panel_width ||
                 modes[vis].vdisplay > drm_panel_height))
                continue;
            filtered[out++] = modes[vis];
        }

        /* Keep the first surviving mode marked PREFERRED */
        {
            int i;
            for (i = 0; i < out; i++)
                filtered[i].type = (i == 0) ? (DRM_MODE_TYPE_PREFERRED | DRM_MODE_TYPE_DRIVER)
                                            : DRM_MODE_TYPE_DRIVER;
        }

        conn->connector_id = 1;
        conn->encoder_id = 0;
        conn->connection = DRM_MODE_CONNECTED;
        conn->mm_width = 300;
        conn->mm_height = 200;
        conn->subpixel = 0;
        conn->connector_type = DRM_MODE_CONNECTOR_VGA;
        conn->connector_type_id = 1;

        conn->count_modes = out;
        conn->count_props = 0;
        conn->count_encoders = 0;

        if (conn->modes_ptr)
            CopyMem(filtered, (APTR)(unsigned long)conn->modes_ptr, out * sizeof(filtered[0]));

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
