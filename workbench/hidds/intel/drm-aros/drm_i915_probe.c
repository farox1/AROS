/*
    Copyright 2026, The AROS Development Team. All rights reserved.

    Minimal Intel GPU probe for AROS.
    Detects the GPU chipset from the PCI device ID and maps MMIO.
*/

#include <aros/debug.h>

#include <drm-compat/drm_compat_types.h>
#include <drm-compat/drm_compat_pci.h>
#include <drm-compat/drm_compat_funcs.h>
#include <drm-aros/drm_aros_pci.h>
#include <libdrm/arosdrm.h>
#include <uapi/drm/drm_mode.h>

#include "../intel_intern.h"

struct gpu_info {
    UWORD device_id;
    ULONG generation;
    const char *name;
};

static const struct gpu_info intel_gpu_table[] = {
    /* Gen 2/3 (i8xx/i9xx) */
    { 0x3577, 20, "i830M" },
    { 0x2562, 20, "i845G" },
    { 0x3582, 30, "i852GM/i855GM" },
    { 0x2572, 30, "i865G" },
    { 0x2582, 30, "i915G" },
    { 0x2592, 30, "i915GM" },
    { 0x2772, 30, "i945G" },
    { 0x27A2, 30, "i945GM" },
    { 0x27AE, 30, "i945GME" },
    { 0x2972, 30, "i946GZ" },
    { 0x2982, 30, "G35" },
    { 0x2992, 30, "Q965" },
    { 0x29A2, 30, "G965" },
    { 0x29B2, 30, "Q35" },
    { 0x29C2, 30, "G33" },
    { 0x29D2, 30, "Q33" },
    { 0x2A02, 30, "GM965" },
    { 0x2A12, 30, "GME965" },
    { 0x2A42, 30, "GM45" },
    { 0x2E02, 30, "G41" },
    { 0x2E12, 30, "B43" },
    { 0x2E22, 30, "G45" },
    { 0x2E32, 30, "Q45" },
    { 0x2E42, 30, "G43" },
    { 0x2E92, 30, "B43" },
    /* Gen 4 (Ironlake) */
    { 0x0042, 40, "Ironlake Desktop" },
    { 0x0046, 40, "Ironlake Mobile" },
    /* Gen 5 (Sandy Bridge) */
    { 0x0102, 50, "Sandy Bridge GT1" },
    { 0x0112, 50, "Sandy Bridge GT2" },
    { 0x0122, 50, "Sandy Bridge GT2+" },
    { 0x0106, 50, "Sandy Bridge GT1 Mobile" },
    { 0x0116, 50, "Sandy Bridge GT2 Mobile" },
    { 0x0126, 50, "Sandy Bridge GT2+ Mobile" },
    { 0x010A, 50, "Sandy Bridge Server" },
    /* Gen 6 (Ivy Bridge) */
    { 0x0152, 60, "Ivy Bridge GT1" },
    { 0x0162, 60, "Ivy Bridge GT2" },
    { 0x0156, 60, "Ivy Bridge GT1 Mobile" },
    { 0x0166, 60, "Ivy Bridge GT2 Mobile" },
    { 0x015A, 60, "Ivy Bridge Server" },
    /* Gen 7 (Haswell) */
    { 0x0402, 70, "Haswell GT1" },
    { 0x0412, 70, "Haswell GT2" },
    { 0x0422, 70, "Haswell GT3" },
    { 0x0406, 70, "Haswell GT1 Mobile" },
    { 0x0416, 70, "Haswell GT2 Mobile" },
    { 0x0426, 70, "Haswell GT3 Mobile" },
    { 0x040A, 70, "Haswell Server" },
    { 0x041A, 70, "Haswell Server" },
    { 0x042A, 70, "Haswell Server" },
    { 0x040B, 70, "Haswell" },
    { 0x041B, 70, "Haswell" },
    { 0x042B, 70, "Haswell" },
    { 0x040E, 70, "Haswell ULT" },
    { 0x041E, 70, "Haswell ULT" },
    { 0x042E, 70, "Haswell ULT" },
    { 0x0C02, 70, "Haswell SDV" },
    { 0x0C12, 70, "Haswell SDV" },
    { 0x0C22, 70, "Haswell SDV" },
    { 0x0C06, 70, "Haswell SDV Mobile" },
    { 0x0C16, 70, "Haswell SDV Mobile" },
    { 0x0C26, 70, "Haswell SDV Mobile" },
    { 0x0A02, 70, "Haswell ULT SDV" },
    { 0x0A12, 70, "Haswell ULT SDV" },
    { 0x0A22, 70, "Haswell ULT SDV" },
    { 0x0A06, 70, "Haswell ULT SDV Mobile" },
    { 0x0A16, 70, "Haswell ULT SDV Mobile" },
    { 0x0A26, 70, "Haswell ULT SDV Mobile" },
    { 0x0D02, 70, "Haswell CRW" },
    { 0x0D12, 70, "Haswell CRW" },
    { 0x0D22, 70, "Haswell CRW" },
    { 0x0D06, 70, "Haswell CRW Mobile" },
    { 0x0D16, 70, "Haswell CRW Mobile" },
    { 0x0D26, 70, "Haswell CRW Mobile" },
    /* Gen 7.5 (Broadwell) */
    { 0x1602, 75, "Broadwell GT1" },
    { 0x1606, 75, "Broadwell GT1 ULT" },
    { 0x160B, 75, "Broadwell GT1 Iris" },
    { 0x160E, 75, "Broadwell GT1 ULX" },
    { 0x1612, 75, "Broadwell GT2" },
    { 0x1616, 75, "Broadwell GT2 ULT" },
    { 0x161B, 75, "Broadwell GT2 Iris" },
    { 0x161E, 75, "Broadwell GT2 ULX" },
    { 0x1622, 75, "Broadwell GT3" },
    { 0x1626, 75, "Broadwell GT3 ULT" },
    { 0x162B, 75, "Broadwell GT3 Iris 6100" },
    { 0x162E, 75, "Broadwell GT3 ULX" },
    { 0x1632, 75, "Broadwell GT3" },
    { 0x1636, 75, "Broadwell GT3 ULT" },
    { 0x163B, 75, "Broadwell GT3 Iris Pro P6300" },
    { 0x163E, 75, "Broadwell GT3 ULX" },
    /* Gen 8 (Broadwell DT) */
    { 0x22B0, 80, "Cherryview" },
    { 0x22B1, 80, "Cherryview" },
    { 0x22B2, 80, "Cherryview" },
    { 0x22B3, 80, "Cherryview" },
    /* Gen 9 (Skylake) */
    { 0x1902, 90, "Skylake GT1" },
    { 0x1906, 90, "Skylake GT1 ULT" },
    { 0x190B, 90, "Skylake GT1" },
    { 0x190E, 90, "Skylake GT1 ULX" },
    { 0x1912, 90, "Skylake GT2" },
    { 0x1916, 90, "Skylake GT2 ULT" },
    { 0x191B, 90, "Skylake GT2f" },
    { 0x191E, 90, "Skylake GT2 ULX" },
    { 0x1922, 90, "Skylake GT3" },
    { 0x1926, 90, "Skylake GT3 ULT" },
    { 0x192B, 90, "Skylake GT3e" },
    { 0x192E, 90, "Skylake GT3 ULX" },
    { 0x1932, 90, "Skylake GT4" },
    { 0x1936, 90, "Skylake GT4 ULT" },
    { 0x193B, 90, "Skylake GT4e" },
    { 0x193E, 90, "Skylake GT4 ULX" },
    { 0x190D, 90, "Skylake" },
    { 0x191D, 90, "Skylake" },
    { 0x192D, 90, "Skylake" },
    { 0x193D, 90, "Skylake" },
    /* Gen 9 (Broxton) */
    { 0x0A84, 90, "Broxton" },
    { 0x1A84, 90, "Broxton" },
    { 0x1A85, 90, "Broxton" },
    { 0x5A84, 90, "Broxton" },
    { 0x5A85, 90, "Broxton" },
    /* Gen 9 (Kabylake) */
    { 0x5902, 90, "Kabylake GT1" },
    { 0x5906, 90, "Kabylake GT1 ULT" },
    { 0x590B, 90, "Kabylake GT1" },
    { 0x590E, 90, "Kabylake GT1 ULX" },
    { 0x5912, 90, "Kabylake GT2" },
    { 0x5916, 90, "Kabylake GT2 ULT" },
    { 0x591B, 90, "Kabylake GT2" },
    { 0x591E, 90, "Kabylake GT2 ULX" },
    { 0x5922, 90, "Kabylake GT3" },
    { 0x5926, 90, "Kabylake GT3 ULT" },
    { 0x592B, 90, "Kabylake GT3" },
    { 0x592E, 90, "Kabylake GT3 ULX" },
    { 0x591D, 90, "Kabylake" },
    { 0x593D, 90, "Kabylake" },
    /* Gen 9.5 (Coffeelake) */
    { 0x3E90, 95, "Coffeelake GT1" },
    { 0x3E93, 95, "Coffeelake GT1" },
    { 0x3E91, 95, "Coffeelake GT2" },
    { 0x3E92, 95, "Coffeelake GT2" },
    { 0x3E96, 95, "Coffeelake GT2" },
    { 0x3E94, 95, "Coffeelake GT3" },
    { 0x3E99, 95, "Coffeelake" },
    { 0x3E9C, 95, "Coffeelake" },
    { 0x3EA0, 95, "Coffeelake" },
    { 0x3EA4, 95, "Coffeelake" },
    { 0x3EA1, 95, "Coffeelake" },
    { 0x3EA8, 95, "Coffeelake" },
    { 0x3E9A, 95, "Coffeelake" },
    /* Gen 10 (Cannonlake) */
    { 0x5A52, 100, "Cannonlake GT2" },
    { 0x5A51, 100, "Cannonlake" },
    { 0x5A59, 100, "Cannonlake" },
    { 0x5A5C, 100, "Cannonlake" },
    { 0x5A50, 100, "Cannonlake" },
    /* Gen 11 (Icelake) */
    { 0x8A56, 110, "Icelake GT1" },
    { 0x8A52, 110, "Icelake GT2" },
    { 0x8A5A, 110, "Icelake GT3" },
    { 0x8A54, 110, "Icelake" },
    { 0x8A58, 110, "Icelake" },
    { 0x8A5C, 110, "Icelake" },
    { 0x8A50, 110, "Icelake" },
    /* Gen 12 (Tigerlake) */
    { 0x9A60, 120, "Tigerlake" },
    { 0x9A68, 120, "Tigerlake" },
    { 0x9A70, 120, "Tigerlake" },
    { 0x9A40, 120, "Tigerlake" },
    { 0x9A49, 120, "Tigerlake" },
    { 0x9A59, 120, "Tigerlake" },
    { 0x9A78, 120, "Tigerlake" },
    /* Sentinel */
    { 0x0000,   0, NULL }
};

static const struct gpu_info *find_gpu(UWORD device_id)
{
    const struct gpu_info *gpu = intel_gpu_table;
    while (gpu->device_id != 0)
    {
        if (gpu->device_id == device_id)
            return gpu;
        gpu++;
    }
    return NULL;
}

/*
 * Whether the AROS IntelHD display path currently supports this GPU.
 *
 * The display engine code (this probe + intel_modeset.c) is implemented for the
 * GEN 9+ display engine (Skylake and its family: Skylake, Kaby Lake, Coffee
 * Lake, Cannon Lake, Ice Lake, Tiger Lake). Older generations (Gen 2..8:
 * i8xx/i9xx/G4x/Ironlake/Sandy Bridge/Ivy Bridge/Haswell/Broadwell) use a
 * totally different display engine (DSPCNTR planes, FDI/PCH ports, legacy
 * PLLs) and MUST NOT be driven by the Gen 9 code - doing so leaves the display
 * in a broken state (e.g. the Ivy Bridge tester got "BIOS plane size: 1x1").
 *
 * Until per-generation support is implemented (see HANDOFF section 25), such
 * GPUs are disarmed so the driver does not register and the machine falls back
 * to another monitor driver (e.g. VESA).
 */
BOOL i915_display_supported(UWORD device_id)
{
    const struct gpu_info *gpu = find_gpu(device_id);
    if (!gpu)
        return FALSE;

    /* Gen 9+ use PLANE_CTL + the SKL scaler (fully supported). Haswell(70) and
     * Broadwell(75) use DDI + DSPCNTR (Phase C). Sandy(50)/Ivy(60) use DSPCNTR
     * + FDI/PCH (Phase D, legacy re-show path). i8xx/9xx/G4x/Ironlake (20..49)
     * still use the legacy engine (Phase E) - stay disarmed (VESA fallback). */
    return gpu->generation >= 90 ||
           gpu->generation == 70 ||
           gpu->generation == 75 ||
           gpu->generation == 50 ||
           gpu->generation == 60;
}

int i915_aros_probe(struct CardData *carddata)
{
    struct pci_dev *pdev;
    const struct gpu_info *gpu;

    pdev = drm_aros_pci_find_supported_video_card();
    if (!pdev)
    {
        bug("[i915] No Intel GPU found\n");
        return -1;
    }

    gpu = find_gpu(pdev->device);
    if (!gpu)
    {
        bug("[i915] Unknown Intel GPU: 0x%04x\n", pdev->device);
        return -1;
    }

    bug("[i915] Detected %s (Gen %ld, device 0x%04x)\n",
        gpu->name, gpu->generation, pdev->device);

    /* BUILD DATE: proves the freshly-built intel.hidd is the one being loaded
     * (distinguish from a stale ISO/disk copy). */
    bug("[i915] BUILD DATE %s %s\n", __DATE__, __TIME__);

    carddata->Generation = gpu->generation;
    carddata->IsPCIE = pdev->isPCIE;
    carddata->pdev = pdev;

    /* Map MMIO BAR */
    {
        resource_size_t mmio_base;
        unsigned long mmio_size;

        /* Gen3+: BAR0 is the MMIO register space.
         * Gen3-4: 512KB, Gen5+: 2MB */
        mmio_base = pci_resource_start(pdev, 0);
        mmio_size = pci_resource_len(pdev, 0);

        if (!mmio_base || !mmio_size)
        {
            /* Try BAR1 for older GPUs (Gen2) */
            mmio_base = pci_resource_start(pdev, 1);
            mmio_size = pci_resource_len(pdev, 1);
        }

        if (mmio_base && mmio_size)
        {
            carddata->mmio = ioremap(mmio_base, mmio_size);
            carddata->mmio_size = mmio_size;
        }

        if (!carddata->mmio)
        {
            bug("[i915] Failed to map MMIO (base=0x%lx, size=%lu)\n",
                (unsigned long)mmio_base, mmio_size);
            return -1;
        }

        bug("[i915] MMIO mapped: base=0x%lx -> %p, size=%lu\n",
            (unsigned long)mmio_base, carddata->mmio, carddata->mmio_size);
    }

    /* Verify GPU is alive by reading known registers */
    {
        #define GEN7_GT_MODE 0x7008
        ULONG gt_mode = INTEL_READ(carddata, GEN7_GT_MODE);
        bug("[i915] GEN7_GT_MODE = 0x%08lx\n", gt_mode);

        #define PIPEACONF 0x70008
        ULONG pipe_a_conf = INTEL_READ(carddata, PIPEACONF);
        bug("[i915] PIPE_A_CONF = 0x%08lx\n", pipe_a_conf);

        /* Display detection */
        #define DIGITAL_PORT_HOTPLUG_CNTRL 0x44030
        ULONG hotplug = INTEL_READ(carddata, DIGITAL_PORT_HOTPLUG_CNTRL);
        bug("[i915] DIGITAL_PORT_HOTPLUG = 0x%08lx\n", hotplug);

        #define DDI_BUF_CTL_A 0x64000
        ULONG ddi_buf_a = INTEL_READ(carddata, DDI_BUF_CTL_A);
        bug("[i915] DDI_BUF_CTL_A = 0x%08lx\n", ddi_buf_a);

        #define SFUSE_STRAP 0xc2014
        ULONG sfuse = INTEL_READ(carddata, SFUSE_STRAP);
        bug("[i915] SFUSE_STRAP = 0x%08lx\n", sfuse);

        #define PCH_PORT_HOTPLUG 0xc4030
        ULONG pch_hotplug = INTEL_READ(carddata, PCH_PORT_HOTPLUG);
        bug("[i915] PCH_PORT_HOTPLUG = 0x%08lx\n", pch_hotplug);

        /* Power wells and display state */
        #define HSW_PWR_WELL_CTL1 0x45400
        ULONG pw1 = INTEL_READ(carddata, HSW_PWR_WELL_CTL1);
        bug("[i915] PWR_WELL_CTL1 = 0x%08lx\n", pw1);

        #define PLANE_CTL_A 0x70180
        ULONG plane_ctl = INTEL_READ(carddata, PLANE_CTL_A);
        bug("[i915] PLANE_CTL_A = 0x%08lx\n", plane_ctl);

        #define PLANE_SURF_A 0x7019c
        ULONG plane_surf = INTEL_READ(carddata, PLANE_SURF_A);
        bug("[i915] PLANE_SURF_A = 0x%08lx\n", plane_surf);

        #define PLANE_STRIDE_A 0x70188
        ULONG plane_stride = INTEL_READ(carddata, PLANE_STRIDE_A);
        bug("[i915] PLANE_STRIDE_A = 0x%08lx\n", plane_stride);

        #define TRANS_DDI_FUNC_CTL_A 0x60400
        ULONG trans_a = INTEL_READ(carddata, TRANS_DDI_FUNC_CTL_A);
        bug("[i915] TRANS_DDI_FUNC_CTL_A = 0x%08lx\n", trans_a);

        #define TRANS_DDI_FUNC_CTL_EDP 0x6F400
        ULONG trans_edp = INTEL_READ(carddata, TRANS_DDI_FUNC_CTL_EDP);
        bug("[i915] TRANS_DDI_FUNC_CTL_EDP = 0x%08lx\n", trans_edp);
        carddata->is_edp = (trans_edp & (1UL << 31)) != 0;

        /* Gen 11+ (Ice Lake/Tiger Lake) display engine: combo PHY + Type-C (TC)
         * ports + DKL PLL. Same DDI/plane register model as Gen 9 for combo
         * ports, but the PHY/clock handling differs (see intel_modeset.c). */
        carddata->is_icl_plus = (gpu->generation >= 110);
        /* Gen 12+ (Tiger Lake) moved the TRANS_DDI_FUNC_CTL DDI-port field from
         * bits [30:28] to [30:27] and offsets the port value by +1
         * (TGL_TRANS_DDI_PORT_SHIFT / TGL_TRANS_DDI_SELECT_PORT in Linux). */
        carddata->is_tgl_plus = (gpu->generation >= 120);

        /*
         * Detect the ACTIVE display topology from the BIOS-programmed state.
         * The DP path must target the real port/transcoder/pipe; hardcoding
         * port E + transcoder A + pipe A (Skylake desktop DP) only works on
         * that one board. This is the foundation for the universal driver.
         */
        {
            #define DDI_BUF_CTL(p)   (0x64000 + (p) * 0x100)
            #define TRANS_DDI_CTL(t) (0x60400 + (t) * 0x1000)
            #define PLANE_CTL(p)     (0x70180 + (p) * 0x1000)
            int i;

            carddata->display_port  = 0xFFFFFFFF;
            carddata->display_trans = 0xFFFFFFFF;
            carddata->display_pipe  = 0xFFFFFFFF;
            carddata->display_aux   = 0xFFFFFFFF;

            for (i = 0; i < 5; i++)
            {
                if (INTEL_READ(carddata, DDI_BUF_CTL(i)) & (1UL << 31))
                {
                    carddata->display_port = i;
                    break;
                }
            }
            for (i = 0; i < 4; i++)
            {
                if (INTEL_READ(carddata, TRANS_DDI_CTL(i)) & (1UL << 31))
                {
                    carddata->display_trans = i;
                    break;
                }
            }
            for (i = 0; i < 3; i++)
            {
                if (INTEL_READ(carddata, PLANE_CTL(i)) & (1UL << 31))
                {
                    carddata->display_pipe = i;
                    break;
                }
            }
            /* Skylake DDI E shares its PHY/IO (and AUX) with DDI A; the real
             * AUX channel is refined by probing in intel_dp_link_train(). */
            if (carddata->display_port != 0xFFFFFFFF)
                carddata->display_aux = (carddata->display_port == 4) ? 0 : carddata->display_port;

            /* Decode the active output type from the TRANS_DDI_FUNC_CTL mode
             * select on the active transcoder (bits [26:24]: 0=HDMI, 1=DVI,
             * 2=DP_SST, 3=DP_MST). eDP is detected separately above and must
             * win: the EDP transcoder (0x6f400) is not in the A..D scan, and
             * decoding an inactive transcoder-A register can falsely report
             * HDMI/DVI. */
            carddata->output_type = INTEL_OUTPUT_UNKNOWN;
            if (gpu->generation < 70)
            {
                /* Pre-DDI (Sandy/Ivy): no TRANS_DDI_FUNC_CTL; external outputs
                 * go through FDI/PCH. We can't classify DP/HDMI/VGA here, so
                 * mark LEGACY = re-show the BIOS-active output only. */
                carddata->output_type = INTEL_OUTPUT_LEGACY;
            }
            else if (carddata->is_edp)
                carddata->output_type = INTEL_OUTPUT_EDP;
            else if (carddata->display_trans != 0xFFFFFFFF)
            {
                ULONG fctl = INTEL_READ(carddata, TRANS_DDI_CTL(carddata->display_trans));
                switch ((fctl >> 24) & 0x7)
                {
                case 0: carddata->output_type = INTEL_OUTPUT_HDMI; break;
                case 1: carddata->output_type = INTEL_OUTPUT_DVI;  break;
                default: carddata->output_type = INTEL_OUTPUT_DP;  break;
                }
            }

            bug("[i915] display topology: port=%ld trans=%ld pipe=%ld aux=%ld out=%d icl+_display=%d tgl+_display=%d\n",
                (LONG)carddata->display_port, (LONG)carddata->display_trans,
                (LONG)carddata->display_pipe, (LONG)carddata->display_aux,
                carddata->output_type, carddata->is_icl_plus, carddata->is_tgl_plus);
        }

        /*
         * All-port survey. Shows every DDI port's buffer state and the hotplug
         * status, independent of which one is the active scanout. This is what
         * reveals e.g. a laptop HDMI DDI with a monitor attached (DDI_BUF_CTL
         * bit31 = enabled, bit0-6 = link/idle status, PCH hotplug = cable
         * detect) and is the basis for future multi-output support.
         */
        {
            #define DDI_BUF_CTL_ALL(p) (0x64000 + (p) * 0x100)
            #define SKL_HOTPLUG_CTRL   0x44030
            int p;

            for (p = 0; p < 5; p++)
                bug("[i915] port %d: DDI_BUF_CTL=0x%08lx\n",
                    p, INTEL_READ(carddata, DDI_BUF_CTL_ALL(p)));

            /* PCH hotplug: which PCH connector port has a cable (status bits
             * latch on HPD pulse and stay until cleared). Enables+status:
             * A bit28/24-25, D bit20/16-17, C bit12/8-9, B bit4/0-1
             * (PCH_PORT_HOTPLUG 0xc4030); E bit4/0-1 (PCH_PORT_HOTPLUG2 0xc403c). */
            {
                #define PCH_HOTPLUG   0xc4030
                #define PCH_HOTPLUG2  0xc403c
                ULONG hp = INTEL_READ(carddata, PCH_HOTPLUG);
                ULONG hp2 = INTEL_READ(carddata, PCH_HOTPLUG2);

                bug("[i915] hpd: A en=%d st=%d | B en=%d st=%d | C en=%d st=%d | D en=%d st=%d | E en=%d st=%d\n",
                    (int)((hp >> 28) & 1), (int)((hp >> 24) & 3),
                    (int)((hp >> 4) & 1),  (int)(hp & 3),
                    (int)((hp >> 12) & 1), (int)((hp >> 8) & 3),
                    (int)((hp >> 20) & 1), (int)((hp >> 16) & 3),
                    (int)((hp2 >> 4) & 1), (int)(hp2 & 3));

                /* On laptops (eDP active) with a PCH DDI-B or DDI-C HPD enabled,
                 * that port is the HDMI output and is BIOS-unpowered.
                 *
                 * NOTE: the DDI-B HDMI CLONE is intentionally NOT auto-enabled.
                 * Experimenting with it on this laptop exposed an AROS front-end
                 * freeze in the mode-switch path (outlaptop_9..13) and a flaky
                 * USB/cdceth boot wedge - both outside the IntelHD hardware
                 * code. Until those are resolved, keep the eDP path 100% stable
                 * and leave HDMI for machines where it is the ACTIVE output
                 * (out=HDMI -> the HDMI/DVI branch in intel_set_mode). */
                carddata->hdmi_clone_enable = FALSE;
            }
            bug("[i915] hotplug: CPU=0x%08lx PCH=0x%08lx\n",
                INTEL_READ(carddata, SKL_HOTPLUG_CTRL),
                INTEL_READ(carddata, 0xc4030));
        }

        /* Read current BIOS DPLL / clock state (Skylake shared DPLLs) */
        #define DPLL_CTRL1   0x6C058
        #define DPLL_CTRL2   0x6C05C
        #define DPLL1_CFGCR1 0x6C040
        #define DPLL1_CFGCR2 0x6C044
        #define DPLL2_CFGCR1 0x6C048
        #define DPLL2_CFGCR2 0x6C04C
        #define DPLL3_CFGCR1 0x6C050
        #define DPLL3_CFGCR2 0x6C054
        #define LCPLL_CTL    0x130040
        #define CDCLK_CTL    0x46000
        #define CDCLK_FREQ   0x46200
        #define LCPLL1_CTL   0x46010
        #define LCPLL2_CTL   0x46014
        #define PS_CTRL_A    0x68180
        #define PS_WIN_SZ_A  0x68174
        #define PS_WIN_POS_A 0x68170
        #define PS_HPHASE_A  0x68194
        #define PS_VPHASE_A  0x68188

        bug("[i915] DPLL_CTRL1 = 0x%08lx  DPLL_CTRL2 = 0x%08lx  LCPLL_CTL = 0x%08lx\n",
            INTEL_READ(carddata, DPLL_CTRL1), INTEL_READ(carddata, DPLL_CTRL2),
            INTEL_READ(carddata, LCPLL_CTL));
        bug("[i915] CDCLK_CTL = 0x%08lx  CDCLK_FREQ = 0x%08lx\n",
            INTEL_READ(carddata, CDCLK_CTL), INTEL_READ(carddata, CDCLK_FREQ));
        bug("[i915] LCPLL1_CTL = 0x%08lx  LCPLL2_CTL = 0x%08lx\n",
            INTEL_READ(carddata, LCPLL1_CTL), INTEL_READ(carddata, LCPLL2_CTL));
        bug("[i915] PSR(scaler0) CTL=0x%08lx WIN_SZ=0x%08lx WIN_POS=0x%08lx HPHASE=0x%08lx VPHASE=0x%08lx\n",
            INTEL_READ(carddata, PS_CTRL_A), INTEL_READ(carddata, PS_WIN_SZ_A),
            INTEL_READ(carddata, PS_WIN_POS_A), INTEL_READ(carddata, PS_HPHASE_A),
            INTEL_READ(carddata, PS_VPHASE_A));

        /* BIOS M/N (transcoder A): encodes link rate * bpp * lanes */
        #define PIPE_DATA_M1_A 0x60030
        #define PIPE_DATA_N1_A 0x60034
        #define PIPE_LINK_M1_A 0x60040
        #define PIPE_LINK_N1_A 0x60044
        bug("[i915] BIOS M/N: DATA_M1=0x%08lx DATA_N1=0x%08lx LINK_M1=0x%08lx LINK_N1=0x%08lx\n",
            INTEL_READ(carddata, PIPE_DATA_M1_A), INTEL_READ(carddata, PIPE_DATA_N1_A),
            INTEL_READ(carddata, PIPE_LINK_M1_A), INTEL_READ(carddata, PIPE_LINK_N1_A));

        {
            ULONG psz = INTEL_READ(carddata, PS_WIN_SZ_A);
            carddata->panel_width  = (psz >> 16) & 0xFFFF;
            carddata->panel_height = psz & 0xFFFF;
        }
        /* Panel native size from PS_WIN_SZ is applied to DRM ioctls AFTER the EDID
         * read below (so a pre-SKL panel, where PS_WIN_SZ reads 0, can use the
         * EDID native size instead). */
        bug("[i915] DPLL1 CFGCR1=0x%08lx CFGCR2=0x%08lx\n",
            INTEL_READ(carddata, DPLL1_CFGCR1), INTEL_READ(carddata, DPLL1_CFGCR2));
        bug("[i915] DPLL2 CFGCR1=0x%08lx CFGCR2=0x%08lx\n",
            INTEL_READ(carddata, DPLL2_CFGCR1), INTEL_READ(carddata, DPLL2_CFGCR2));
        bug("[i915] DPLL3 CFGCR1=0x%08lx CFGCR2=0x%08lx\n",
            INTEL_READ(carddata, DPLL3_CFGCR1), INTEL_READ(carddata, DPLL3_CFGCR2));

        /* Read current BIOS pipe timings */
        #define HTOTAL_A 0x60000
        #define HBLANK_A 0x60004
        #define HSYNC_A 0x60008
        #define VTOTAL_A 0x6000c
        #define VBLANK_A 0x60010
        #define VSYNC_A 0x60014
        #define PIPESRC_A 0x6001c

        ULONG htotal  = INTEL_READ(carddata, HTOTAL_A);
        ULONG hblank  = INTEL_READ(carddata, HBLANK_A);
        ULONG hsync   = INTEL_READ(carddata, HSYNC_A);
        ULONG vtotal  = INTEL_READ(carddata, VTOTAL_A);
        ULONG vblank  = INTEL_READ(carddata, VBLANK_A);
        ULONG vsync   = INTEL_READ(carddata, VSYNC_A);
        ULONG pipesrc = INTEL_READ(carddata, PIPESRC_A);

        UWORD hactive = pipesrc & 0xffff;
        UWORD vactive = (pipesrc >> 16) & 0xffff;
        UWORD hstart = hsync >> 16;
        UWORD hend   = hsync & 0xffff;
        UWORD vstart = vsync >> 16;
        UWORD vend   = vsync & 0xffff;
        UWORD htotal_v = htotal & 0xffff;
        UWORD vtotal_v = vtotal & 0xffff;

        bug("[i915] BIOS mode: %ux%u htotal=%u vtotal=%u\n",
            hactive, vactive, htotal_v, vtotal_v);
        bug("[i915] BIOS sync: hstart=%u hend=%u vstart=%u vend=%u\n",
            hstart, hend, vstart, vend);
        bug("[i915] BIOS blank: HBLANK=0x%08lx (start=%u end=%u) VBLANK=0x%08lx (start=%u end=%u)\n",
            hblank, (unsigned)(hblank & 0xffff), (unsigned)((hblank >> 16) & 0xffff),
            vblank, (unsigned)(vblank & 0xffff), (unsigned)((vblank >> 16) & 0xffff));

        /* Remove corrupting test: we now know writes work.
         * Allocate a test framebuffer and program the plane. */

        /* Read the monitor/panel EDID over AUX and seed the connector's
         * preferred mode. Failure is non-fatal (static DMT list remains). */
        {
            struct drm_mode_modeinfo edidmode;
            if (intel_read_edid_preferred(carddata, &edidmode))
            {
                bug("[i915] EDID preferred mode: %ux%u@%u clk=%u flags=0x%x\n",
                    edidmode.hdisplay, edidmode.vdisplay, edidmode.vrefresh,
                    edidmode.clock, edidmode.flags);
                arosdrm_set_edid_mode(&edidmode);

                /* Pre-SKL eDP panels report PS_WIN_SZ as 0 (that register is
                 * SKL-only), so panel_width/height fell back to 1366x768. Use
                 * the EDID native size instead - it is the true fixed-panel
                 * resolution for the panel fitter and the mode-list filter. */
                if (carddata->is_edp &&
                    (carddata->panel_width < 8 || carddata->panel_height < 8))
                {
                    carddata->panel_width = edidmode.hdisplay;
                    carddata->panel_height = edidmode.vdisplay;
                    bug("[i915] eDP panel native from EDID: %lux%lu\n",
                        (unsigned long)carddata->panel_width,
                        (unsigned long)carddata->panel_height);
                }
            }
            else
                bug("[i915] EDID read failed - using static mode list\n");

            /* Tell the DRM ioctl layer about the panel/connector topology so it
             * can filter the mode list (eDP fixed panel -> only modes <= native). */
            arosdrm_set_panel_native(carddata->is_edp,
                                     carddata->panel_width, carddata->panel_height);
        }

    bug("[i915] GPU probe complete. Setting up framebuffer test...\n");
    }

    return 0;
}

int i915_fb_test(struct CardData *carddata)
{
    #define PLANE_CTL_A 0x70180
    #define PLANE_SURF_A 0x7019c
    #define PLANE_SIZE_A 0x70190
    #define PLANE_STRIDE_A 0x70188
    #define PLANE_OFFSET_A 0x701a4
    #define PLANE_CTL_ENABLE (1 << 31)
    #define PLANE_CTL_FORMAT_XRGB8888 (4 << 24)

    ULONG bdsm, pipesize, width, height, pitch, surf_addr;
    resource_size_t bar2_base;
    unsigned long bar2_size;
    APTR gtt;

    /* Read stolen memory base from PCI config offset 0x5C (BDSM) */
    pci_read_config_dword(carddata->pdev, 0x5C, &bdsm);
    bdsm &= 0xFFF00000;
    bug("[i915] BDSM: 0x%08lx\n", bdsm);

    if (!bdsm)
    {
        bug("[i915] No stolen memory, skip FB test\n");
        return -1;
    }

    /* Read BIOS plane size and use those dimensions */
    pipesize = INTEL_READ(carddata, PLANE_SIZE_A);
    width  = (pipesize & 0xFFFF) + 1;
    height = ((pipesize >> 16) & 0xFFFF) + 1;
    bug("[i915] BIOS plane size: %lux%lu (reg=0x%08lx)\n", width, height, pipesize);

    /* Some pre-SKL BIOSes (e.g. Ivy Bridge) leave LSPSIZE at 0 -> 1x1. Fall back
     * to the pipe source / a sane default so the GTT framebuffer isn't 1x1. */
    if (width < 64 || height < 64)
    {
        ULONG src = INTEL_READ(carddata, 0x6001c);   /* PIPESRC A */
        ULONG sw = (src >> 16) & 0xffff, sh = src & 0xffff;
        if (sw >= 64 && sh >= 64)
        {
            width = sw; height = sh;
        }
        else
        {
            width = 1024; height = 768;
        }
        bug("[i915] BIOS plane size invalid - using %lux%lu\n", width, height);
    }

    pitch = width * 4;
    pitch = (pitch + 63) & ~63;

    /* Map GTT aperture via BAR2 */
    bar2_base = pci_resource_start(carddata->pdev, 2);
    bar2_size = pci_resource_len(carddata->pdev, 2);
    bug("[i915] GTT BAR2: base=0x%08lx size=%lu\n",
        (unsigned long)bar2_base, bar2_size);

    if (!bar2_base || !bar2_size)
    {
        bug("[i915] No GTT BAR, skip FB test\n");
        return -1;
    }

    gtt = ioremap(bar2_base, bar2_size);
    if (!gtt)
    {
        bug("[i915] Failed to map GTT\n");
        return -1;
    }

    bug("[i915] GTT mapped at %p\n", gtt);

    /*
     * Store the GTT framebuffer. We deliberately do NOT fill it with the
     * 3 MB test pattern here — that fill was a candidate corruption source.
     * The real rendering (Show/UpdateRect) writes the framebuffer contents.
     */
    carddata->gtt_fb = gtt;
    carddata->gtt_fb_pitch = pitch;
    carddata->mode_width = width;
    carddata->mode_height = height;
    carddata->mode_bpp = 24;   /* BIOS boots the plane in XRGB8888 (4 bpp) */

    bug("[i915] GTT FB ready (unfilled): %lux%lu stride=%lu\n", width, height, pitch);

    return 0;
}
