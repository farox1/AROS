/*
    Intel display mode setting.

    Programs the pipe (transcoder) timing and primary-plane geometry registers
    so that a selected mode is actually displayed, rather than relying on the
    BIOS-configured mode.

    Generation-aware:
      - Gen 9+ (Skylake/Kabylake/...) use the PLANE_CTL pixel-format encoding.
      - Gen 5..7 (Sandy/Ivy/Haswell/Broadwell) use the DSPCNTR encoding.
    Both share the same pipe-timing and plane-geometry register addresses.

    NOTE (limitation): the pixel clock (CDCLK/DPLL) is NOT yet reprogrammed.
    Modes whose pixel clock differs significantly from the BIOS value will
    produce an incorrect refresh rate until clock management is added.

    Copyright 2026, The AROS Development Team. All rights reserved.
*/

#include "intel_intern.h"

#include <libdrm/arosdrmmode.h>
#include <proto/exec.h>

#define DEBUG 0
#include <aros/debug.h>

/* Transcoder (pipe) A timing registers */
#define HTOTAL_A    0x60000
#define HBLANK_A    0x60004
#define HSYNC_A     0x60008
#define VTOTAL_A    0x6000c
#define VBLANK_A    0x60010
#define VSYNC_A     0x60014
#define PIPESRC_A   0x6001c

/* Transcoder EDP timing registers (laptop eDP). Linux's intel_set_pipe_timings
 * indexes HTOTAL/VTOTAL/... by cpu_transcoder (0x6f000 group for TRANSCODER_EDP),
 * while PIPESRC is indexed by pipe (0x6001c). The fixed eDP panel needs these
 * transcoder timings programmed to the panel's fixed mode. */
#define HTOTAL_EDP  0x6f000
#define HBLANK_EDP  0x6f004
#define HSYNC_EDP   0x6f008
#define VTOTAL_EDP  0x6f00c
#define VBLANK_EDP  0x6f010
#define VSYNC_EDP   0x6f014

/* Pipe A config */
#define PIPEACONF_A         0x70008
#define PIPEACONF_ENABLE    (1UL << 31)
/* PIPECONF for the eDP pipe (PIPE_EDP offset 0x7f000 -> PIPECONF 0x7f008). The
 * eDP laptop's active pipe is PIPE_EDP, NOT pipe A. */
#define PIPECONF_EDP        0x7f008

/* Pipe misc (bdw_set_pipemisc): bpc + dither for the eDP 6bpc panel */
#define PIPEMISC_A          0x70030
#define PIPEMISC_DITHER_6_BPC    (2UL << 5)
#define PIPEMISC_DITHER_ENABLE   (1UL << 4)

/* Primary plane A registers (Gen 5..9 share the addresses) */
#define PLANE_CTL_A         0x70180
#define PLANE_STRIDE_A      0x70188
#define PLANE_POS_A         0x7018c
#define PLANE_SIZE_A        0x70190
#define PLANE_SURF_A        0x7019c
#define PLANE_OFFSET_A      0x701a4

#define PLANE_CTL_ENABLE    (1UL << 31)
/* Gen 9+ PLANE_CTL pixel format (bits 27:24) */
#define PLANE_CTL_FORMAT_XRGB8888   (4UL << 24)
#define PLANE_CTL_FORMAT_RGB_565    (14UL << 24)
/* Gen 5..7 DSPCNTR pixel format (bits 29:26) */
#define DSPCNTR_FORMAT_XRGB8888     (6UL << 26)
#define DSPCNTR_FORMAT_RGB_565      (5UL << 26)

/* Skylake+ shared DPLL (pixel clock) registers */
#define DPLL_CTRL1              0x6C058
#define DPLL_CTRL2              0x6C05C
#define DPLL1_CFGCR1            0x6C040
#define DPLL1_CFGCR2            0x6C044

#define DPLL_CFGCR1_FREQ_ENABLE     (1UL << 31)
#define DPLL_CFGCR1_DCO_FRACTION(x) ((x) << 9)

#define DPLL_CFGCR2_QDIV_RATIO(x)   ((x) << 8)
#define DPLL_CFGCR2_QDIV_MODE(x)    ((x) << 7)
#define DPLL_CFGCR2_KDIV(x)         ((x) << 5)
#define DPLL_CFGCR2_PDIV(x)         ((x) << 2)

#define DPLL_CTRL1_OVERRIDE(id)     (1UL << ((id) * 6))
#define DPLL_CTRL1_HDMI_MODE(id)    (1UL << ((id) * 6 + 5))

#define DPLL_CTRL2_DDI_CLK_SEL(clk, port)   ((clk) << ((port) * 3 + 1))
#define DPLL_CTRL2_DDI_SEL_OVERRIDE(port)   (1UL << ((port) * 3))

/* DP/eDP M/N (pixel clock derived from the link clock) */
#define PIPE_DATA_M1    0x60030
#define PIPE_DATA_N1    0x60034
#define PIPE_LINK_M1    0x60040
#define PIPE_LINK_N1    0x60044
#define TU_SIZE(x)      (((x) - 1) << 25)

/* DP link / DDI state */
#define TRANS_DDI_FUNC_CTL_A    0x60400
#define TRANS_DDI_FUNC_CTL_EDP  0x6f400
#define TRANS_DDI_FUNC_ENABLE   (1UL << 31)
#define TRANS_DDI_PHSYNC        (1UL << 16)
#define TRANS_DDI_PVSYNC        (1UL << 17)
/* TRANS_DDI_FUNC_CTL mode/bpc/EDP-input selects (i915_reg.h, HSW+ DDI).
 * The EDP_INPUT *_ONOFF value is what the BIOS uses while the panel fitter
 * is scaling a smaller source up to the fixed panel; when the fitter is off
 * (native resolution) Linux switches the eDP input to *_A_ON so the pipe
 * drives the panel directly. See intel_ddi_enable_transcoder_func. */
#define TRANS_DDI_MODE_SELECT_MASK      (7UL << 24)
#define TRANS_DDI_MODE_SELECT_DP_SST    (2UL << 24)
#define TRANS_DDI_EDP_INPUT_MASK        (7UL << 12)
#define TRANS_DDI_EDP_INPUT_A_ON        (0UL << 12)
#define TRANS_DDI_EDP_INPUT_A_ONOFF     (4UL << 12)
#define DDI_BUF_CTL_A           0x64000
#define DDI_BUF_CTL_E           0x64400
#define DP_TP_CTL_E             0x64440
#define DP_TP_STATUS_E          0x64448
/* DP_TP_CTL_E link-training field (bits 10:8) over the enabled+enhanced base.
 * Bit 7 = DP_TP_CTL_SCRAMBLE_DISABLE is set during training (Linux does this:
 * the training patterns are never scrambled). */
#define DP_TP_CTL_PAT1          0x80040080
#define DP_TP_CTL_PAT2          0x80040180
#define DP_TP_CTL_NORMAL        0x80040300
#define TRANS_MSA_MISC_A        0x60410
#define TRANS_CLK_SEL_A         0x46140
#define PIPEDSL_A               0x70000
#define PIPEFRAME_A             0x70040
#define DPLL_STATUS             0x6C060
#define LCPLL1_CTL              0x46010
#define LCPLL2_CTL              0x46014

/* DP AUX channel. The DP is on DDI port E, but on Skylake DDI E shares its
 * PHY/IO with DDI A, so its AUX channel is AUX A (probed: chan 0 = DPCD 0x11). */
#define DDI_AUX_CTL_A           0x64010
#define DDI_AUX_DATA_A          0x64014
#define DDI_AUX_CTL_E           0x64410
#define DDI_AUX_DATA_E          0x64414

/* Per-DDI-port register bases (Gen 9+). Ports A..E are spaced 0x100 apart.
 * The driver targets the port detected from the BIOS state
 * (carddata->display_port) instead of hardcoding port E, so it works on any
 * DDI port assignment (not just the Skylake desktop used during development). */
#define DDI_BUF_CTL_PORT(p)     (0x64000 + (p) * 0x100)
#define DP_TP_CTL_PORT(p)       (0x64040 + (p) * 0x100)
#define DP_TP_STATUS_PORT(p)    (0x64048 + (p) * 0x100)
#define DDI_AUX_CTL_PORT(p)     (0x64010 + (p) * 0x100)
#define DDI_AUX_DATA_PORT(p)    (0x64014 + (p) * 0x100)

/* Fall back to the historical Skylake desktop config (port E, AUX A) when the
 * topology could not be detected. */
#define ACTIVE_DDI_PORT(cd)     (((cd)->display_port != 0xFFFFFFFF) ? (cd)->display_port : 4)
#define ACTIVE_AUX_CH(cd)       (((cd)->display_aux  != 0xFFFFFFFF) ? (cd)->display_aux  : 0)

/* Per-transcoder / per-pipe register bases (Gen 4+ DDI platforms). The active
 * transcoder/pipe come from carddata->display_trans/display_pipe; they default
 * to transcoder/pipe A (0), which is the historical behaviour. */
#define TRANS_TIMING(t)         (0x60000 + (t) * 0x1000)  /* HTOTAL..VSYNC   */
#define TRANS_DDI_FUNC(t)       (0x60400 + (t) * 0x1000)  /* TRANS_DDI_FUNC  */
#define TRANS_MSA_MISC(t)       (0x60410 + (t) * 0x1000)  /* TRANS_MSA_MISC  */
#define TRANS_CLK_SEL(t)        (0x46140 + (t) * 4)       /* TRANS_CLK_SEL   */
#define MN_BASE(t)              (0x60030 + (t) * 0x1000)  /* DATA_M1..LINK_N1 */
#define PIPESRC_REG(p)          (0x6001c + (p) * 0x1000)  /* PIPESRC (per-pipe) */
#define PIPE_CBASE(p)           (0x70000 + (p) * 0x1000)  /* PIPEDSL/FRAME/PCONF/MISC */
#define PLANE_CBASE(p)          (0x70180 + (p) * 0x1000)  /* PLANE_CTL..OFFSET */
#define PS_CTRL_REG(p)          (0x68180 + (p) * 0x800)   /* pipe scaler 0 */

/*
 * Gen 11+ (Ice Lake) / Gen 12+ (Tiger Lake) display differences (from Linux 5.4):
 *  - Combo PHY: PHY init is a block of ICL_PORT_COMP_DWxx / ICL_PORT_CL_DWxx
 *    writes (see drm/i915/display/intel_combo_phy.c). This driver currently
 *    relies on the BIOS to have set the active output's PHY up (as it does for
 *    Gen 9), so no combo-PHY programming is done yet.
 *  - Type-C (TC) ports: ICL/TGL add TC1..TC6 with their own DDI/AUX register
 *    space and clocking (DKL PLL); the DBUF/PHY power wells use
 *    TGL_PW_CTL_IDX_AUX_TCx / TGL_PW_CTL_IDX_DDI_TCx.
 *  - TRANS_DDI_FUNC_CTL DDI port field moved from [30:28] to [30:27] (+1 port
 *    value) on TGL+ - handled via carddata->is_tgl_plus below.
 *  - CDCLK/PLL selection differs (DKL vs the SKL shared DPLLs).
 * These need tester logs from Gen 10/11/12 hardware before being wired up; the
 * topology dump in the probe ("display topology: port=... icl+_display=...") is
 * the starting point.
 */
#define DP_AUX_CH_CTL_SEND_BUSY       (1UL << 31)
#define DP_AUX_CH_CTL_DONE            (1UL << 30)
#define DP_AUX_CH_CTL_TIME_OUT_ERROR  (1UL << 28)
#define DP_AUX_CH_CTL_RECEIVE_ERROR   (1UL << 25)
#define DP_AUX_NATIVE_WRITE     0x8
#define DP_AUX_NATIVE_READ      0x9
#define DP_AUX_I2C_WRITE        0x0
#define DP_AUX_I2C_READ         0x1
#define DP_AUX_I2C_MOT          0x4
#define DP_EDID_I2C_ADDR        0x50

/* DPCD registers (link training) */
#define DPCD_LINK_BW_SET        0x100
#define DPCD_LANE_COUNT_SET     0x101
#define DPCD_TRAINING_PATTERN   0x102
#define DPCD_TRAINING_LANE0     0x103
#define DPCD_LANE0_1_STATUS     0x202
#define DPCD_ADJUST_REQUEST_LANE0_1  0x206
#define DPCD_DOWNSPREAD_CTRL         0x107
#define DPCD_SET_POWER_STATE         0x600
#define DP_LINK_BW_2_7          0x0a
#define DP_LANE_COUNT_ENHANCED_FRAME_EN  (1 << 7)
#define DP_TRAINING_PATTERN_DISABLE      0
#define DP_TRAINING_PATTERN_1            1
#define DP_TRAINING_PATTERN_2            2
#define DP_LINK_SCRAMBLING_DISABLE       (1 << 5)

/* Panel fitter / pipe scaler (pipe A, scaler 0) */
#define PS_CTRL_A       0x68180
#define PS_WIN_SZ_A     0x68174
#define PS_WIN_POS_A    0x68170
#define PS_HPHASE_A     0x68194
#define PS_VPHASE_A     0x68188
#define PS_PWR_GATE_A   0x68160
#define PS_PWR_GATE_DIS_OVERRIDE    (1UL << 31)

#define PS_SCALER_EN            (1UL << 31)
#define SKL_PS_SCALER_MODE_HQ   (1UL << 28)
#define PS_PLANE_SEL(plane)     (((plane) + 1) << 25)
#define PS_Y_PHASE(x)           ((x) << 16)
#define PS_UV_RGB_PHASE(x)      ((x) << 0)
#define PS_FILTER_MEDIUM        (0UL << 23)
/* Matches the BIOS-programmed scaler control value (EN | HQ | bit23) */
#define PS_CTRL_ENABLE          0x90800000UL

/* HSW/Broadwell panel fitter (pre-SKL pipe scaler), pipe A base */
#define PF_CTL_A                0x68080
#define PF_WIN_SZ_A             0x68074
#define PF_WIN_POS_A            0x68070
#define PF_ENABLE               (1UL << 31)
#define PF_PIPE_SEL_IVB(pipe)   ((pipe) << 29)
#define PF_FILTER_MED_3x3       (1UL << 23)

/* SKL plane watermark/DDB registers (read-only here, for the STATE debug dump) */
#define PLANE_WM_1_A            0x70240
#define PLANE_WM_TRANS_1_A      0x70268
#define PLANE_BUF_CFG_1_A       0x7027c

static void intel_delay(struct CardData *carddata, ULONG count);
static BOOL intel_wrpll_calc_cfgcr(UQUAD pixel_clock_hz, ULONG *cfgcr1, ULONG *cfgcr2);
static void intel_hdmi_clone_update(struct CardData *carddata,
                                    ULONG hdisplay, ULONG vdisplay,
                                    ULONG htotal, ULONG hsync_start, ULONG hsync_end,
                                    ULONG vtotal, ULONG vsync_start, ULONG vsync_end,
                                    ULONG pitch, ULONG bpp, ULONG clock_khz,
                                    ULONG flags);

/*
 * Dump the complete eDP scaler/pipe/plane/watermark register set. Used to diff a
 * WORKING mode against a BROKEN mode register-by-register (and against the BIOS
 * boot state) to find exactly which register differs.
 */
static void intel_dump_scaler_state(struct CardData *carddata, const char *tag)
{
    bug("[Intel] STATE[%s] PS_CTRL=0x%08lx PS_WIN_SZ=0x%08lx PS_WIN_POS=0x%08lx PS_HPHASE=0x%08lx PS_VPHASE=0x%08lx PS_PWR_GATE=0x%08lx\n",
        tag,
        INTEL_READ(carddata, PS_CTRL_A),
        INTEL_READ(carddata, PS_WIN_SZ_A),
        INTEL_READ(carddata, PS_WIN_POS_A),
        INTEL_READ(carddata, PS_HPHASE_A),
        INTEL_READ(carddata, PS_VPHASE_A),
        INTEL_READ(carddata, PS_PWR_GATE_A));
    bug("[Intel] STATE[%s] PIPESRC=0x%08lx PIPEMISC=0x%08lx PIPECONF_EDP=0x%08lx HTOTAL_EDP=0x%08lx VTOTAL_EDP=0x%08lx\n",
        tag,
        INTEL_READ(carddata, PIPESRC_A),
        INTEL_READ(carddata, PIPEMISC_A),
        INTEL_READ(carddata, PIPECONF_EDP),
        INTEL_READ(carddata, HTOTAL_EDP),
        INTEL_READ(carddata, VTOTAL_EDP));
    bug("[Intel] STATE[%s] PLANE_CTL=0x%08lx PLANE_SIZE=0x%08lx PLANE_STRIDE=0x%08lx PLANE_SURF=0x%08lx PLANE_POS=0x%08lx\n",
        tag,
        INTEL_READ(carddata, PLANE_CTL_A),
        INTEL_READ(carddata, PLANE_SIZE_A),
        INTEL_READ(carddata, PLANE_STRIDE_A),
        INTEL_READ(carddata, PLANE_SURF_A),
        INTEL_READ(carddata, PLANE_POS_A));
    bug("[Intel] STATE[%s] PLANE_WM=0x%08lx PLANE_WM_TRANS=0x%08lx PLANE_BUF_CFG=0x%08lx PIPE_CONF_A=0x%08lx\n",
        tag,
        INTEL_READ(carddata, PLANE_WM_1_A),
        INTEL_READ(carddata, PLANE_WM_TRANS_1_A),
        INTEL_READ(carddata, PLANE_BUF_CFG_1_A),
        INTEL_READ(carddata, PIPEACONF_A));
}


static void intel_compute_m_n(UQUAD m, UQUAD n, ULONG *ret_m, ULONG *ret_n)
{
    UQUAD n2 = 1, m2;

    while (n2 < n)
        n2 <<= 1;
    if (n2 > 0x800000ULL)
        n2 = 0x800000ULL;

    m2 = m * n2 / n;

    while (m2 > 0xffffffULL || n2 > 0xffffffULL)
    {
        m2 >>= 1;
        n2 >>= 1;
    }

    *ret_m = (ULONG)m2;
    *ret_n = (ULONG)n2;
}

static ULONG intel_link_clock(struct CardData *carddata, UQUAD pixel_clock_hz)
{
    /*
     * The DP link PHY stays at whatever the BIOS trained it to: 270 MHz HBR
     * (confirmed by the BIOS's own M/N registers decoding to 148.5 MHz native
     * at ratio 0.55). We do NOT retrain the link to RBR (162 MHz) — Linux does
     * that for low modes, but doing so needs a full AUX link-training sequence
     * this driver does not yet implement. So M/N is always computed against
     * 270 MHz, and M/N alone derives the pixel clock.
     */
    return 270000;  /* HBR: 270 MHz, 2 lanes (BIOS-trained) */
}

static void intel_delay(struct CardData *carddata, ULONG count)
{
    ULONG i;
    for (i = 0; i < count; i++)
        INTEL_READ(carddata, 0);   /* ~1us MMIO round trip */
}

static void intel_program_m_n(struct CardData *carddata, UQUAD pixel_clock_hz,
                              ULONG bpp)
{
    ULONG link_clk = intel_link_clock(carddata, pixel_clock_hz);
    ULONG gmch_m, gmch_n, link_m, link_n;
    ULONG mn_base = MN_BASE((carddata->display_trans != 0xFFFFFFFF)
                            ? carddata->display_trans : 0);

    /* DP link, 2 lanes, output always 8bpc/24bpp. The DATA (gmch) M/N ratio is
     * bpp-weighted in Linux's intel_link_compute_m_n, BUT here bpp is the SINK
     * colour depth of the DP LINK, NOT the framebuffer depth: the transcoder
     * upconverts a 16bpp framebuffer to the 8bpc stream the monitor expects.
     * Using the framebuffer depth here produces a wrong TU size for the link.
     * Evidence (out_pc_4.log): 24bpp M/N works for 24-bit; a 16bpp-weighted
     * gmch (ratio == link ratio) makes the monitor report "No Signal" on the
     * same resolution. So the gmch ratio is ALWAYS computed at 24bpp. */
    intel_compute_m_n(pixel_clock_hz, link_clk, &link_m, &link_n);
    intel_compute_m_n(pixel_clock_hz * 24, (UQUAD)link_clk * 2 * 8, &gmch_m, &gmch_n);

    bug("[Intel] m_n: pix=%llu link=%lu lanes=2 fb_bpp=%lu out_bpp=24 gmch=%lu/%lu link=%lu/%lu\n",
        pixel_clock_hz, link_clk, bpp, gmch_m, gmch_n, link_m, link_n);

    INTEL_WRITE(carddata, mn_base + 0x00, TU_SIZE(64) | gmch_m);  /* DATA_M1 */
    INTEL_WRITE(carddata, mn_base + 0x04, gmch_n);                /* DATA_N1 */
    INTEL_WRITE(carddata, mn_base + 0x10, link_m);                /* LINK_M1 */
    INTEL_WRITE(carddata, mn_base + 0x14, link_n);                /* LINK_N1 */
}



static void intel_program_dpll(struct CardData *carddata, UQUAD pixel_clock_hz)
{
    ULONG cfgcr1, cfgcr2;

    if (!intel_wrpll_calc_cfgcr(pixel_clock_hz, &cfgcr1, &cfgcr2))
        return;

    INTEL_WRITE(carddata, DPLL1_CFGCR1, cfgcr1);
    INTEL_WRITE(carddata, DPLL1_CFGCR2, cfgcr2);
    /* Use shared DPLL id 1 (LCPLL2) for the HDMI/DDI pixel clock */
    INTEL_WRITE(carddata, DPLL_CTRL1, DPLL_CTRL1_OVERRIDE(1) | DPLL_CTRL1_HDMI_MODE(1));
    /* Select DPLL1 (clk sel = 1) for DDI port A */
    INTEL_WRITE(carddata, DPLL_CTRL2,
        DPLL_CTRL2_DDI_SEL_OVERRIDE(0) | DPLL_CTRL2_DDI_CLK_SEL(1, 0));
}

/*
 * SKL HDMI shared-DPLL (WRPLL) math, port of Linux skl_ddi_calculate_wrpll /
 * skl_wrpll_params_populate. Fills the DPLL_CFGCR1/CFGCR2 values for the given
 * pixel clock (the AFE clock = 5x pixel clock matches the DDI oscillator).
 */
static BOOL intel_wrpll_calc_cfgcr(UQUAD pixel_clock_hz, ULONG *cfgcr1, ULONG *cfgcr2)
{
    static const ULONG even_div[] = {
        4, 6, 8, 10, 12, 14, 16, 18, 20, 24, 28, 30, 32, 36, 40, 42, 44,
        48, 52, 54, 56, 60, 64, 66, 68, 70, 72, 76, 78, 80, 84, 88, 90, 92, 96, 98
    };
    static const ULONG odd_div[] = { 3, 5, 7, 9, 15, 21, 35 };
    UQUAD central[3] = { 8400000000ULL, 9000000000ULL, 9600000000ULL };
    UQUAD afe_clock = pixel_clock_hz * 5;
    UQUAD best_dev = ~0ULL, best_central = 0;
    ULONG best_p = 0, p0 = 0, p1 = 0, p2 = 0;
    ULONG pdiv, kdiv, qdiv_ratio, qdiv_mode, central_code;
    UQUAD dco_freq;
    ULONG dco_integer, dco_fraction;
    int d, i, c;

    for (d = 0; d < 2; d++)
    {
        const ULONG *div = d ? odd_div : even_div;
        ULONG n = d ? (sizeof(odd_div) / sizeof(odd_div[0]))
                    : (sizeof(even_div) / sizeof(even_div[0]));

        for (i = 0; i < (int)n; i++)
        {
            UQUAD df = (UQUAD)div[i] * afe_clock;

            for (c = 0; c < 3; c++)
            {
                UQUAD dev = (df >= central[c])
                    ? (df - central[c]) * 10000 / central[c]
                    : (central[c] - df) * 10000 / central[c];

                if (dev < best_dev)
                {
                    best_dev = dev;
                    best_central = central[c];
                    best_p = div[i];
                }
            }
        }
    }

    if (!best_p)
        return FALSE;

    /* Factor best_p = p0 * p1 * p2, with p0 in {2,3,7}, p2 in {1,2,3,5} */
    if (best_p % 2 == 0)
    {
        ULONG half = best_p / 2;

        p0 = 2; p2 = 2;
        if (half == 1 || half == 2 || half == 3 || half == 5)
        {
            p1 = 1; p2 = half;
        }
        else if (half % 2 == 0)
        {
            p1 = half / 2;
        }
        else if (half % 3 == 0)
        {
            p0 = 3; p1 = half / 3;
        }
        else if (half % 7 == 0)
        {
            p0 = 7; p1 = half / 7;
        }
    }
    else
    {
        p1 = 1;
        switch (best_p)
        {
        case 3: case 9: p0 = 3; p2 = best_p / 3; break;
        case 5: case 7: p0 = best_p; p2 = 1; break;
        case 15: p0 = 3; p2 = 5; break;
        case 21: p0 = 7; p2 = 3; break;
        case 35: p0 = 7; p2 = 5; break;
        default: return FALSE;
        }
    }

    switch (p0)
    {
    case 1: pdiv = 0; break;
    case 2: pdiv = 1; break;
    case 3: pdiv = 2; break;
    case 7: pdiv = 4; break;
    default: return FALSE;
    }

    switch (p2)
    {
    case 5: kdiv = 0; break;
    case 2: kdiv = 1; break;
    case 3: kdiv = 2; break;
    case 1: kdiv = 3; break;
    default: return FALSE;
    }

    qdiv_ratio = p1;
    qdiv_mode = (qdiv_ratio == 1) ? 0 : 1;

    switch (best_central)
    {
    case 9600000000ULL: central_code = 0; break;
    case 9000000000ULL: central_code = 1; break;
    case 8400000000ULL: central_code = 3; break;
    default: central_code = 0;
    }

    dco_freq = (UQUAD)best_p * afe_clock;
    dco_integer = (ULONG)(dco_freq / (24ULL * 1000000ULL));
    dco_fraction = (ULONG)(((dco_freq / 24ULL) -
        (UQUAD)dco_integer * 1000000ULL) * 0x8000ULL / 1000000ULL);

    D(bug("[Intel] wrpll: clk=%llu p=%lu p0/p1/p2=%lu/%lu/%lu dco=%lu.%lu central=%u\n",
        pixel_clock_hz, best_p, p0, p1, p2, dco_integer, dco_fraction, central_code));

    *cfgcr1 = DPLL_CFGCR1_FREQ_ENABLE | DPLL_CFGCR1_DCO_FRACTION(dco_fraction) | dco_integer;
    *cfgcr2 = DPLL_CFGCR2_QDIV_RATIO(qdiv_ratio) | DPLL_CFGCR2_QDIV_MODE(qdiv_mode) |
              DPLL_CFGCR2_KDIV(kdiv) | DPLL_CFGCR2_PDIV(pdiv) | central_code;
    return TRUE;
}

/*********************************************************************************************/

static int intel_dp_aux_transfer(struct CardData *carddata, ULONG aux_ctl,
                                 ULONG aux_data, int request, ULONG address,
                                 UBYTE *data, int size)
{
    UBYTE txbuf[20];
    ULONG status, v;
    int txsize, i, j, n, try;

    /* DP AUX request header: request | addr[19:16], addr[15:8], addr[7:0],
     * then for a payload message the data length (size-1).
     *
     * A BARE-ADDRESS packet (size 0) sends only the 3 header bytes, WITHOUT
     * the length byte (Linux BARE_ADDRESS_SIZE=3). Sending the length byte
     * with size-1=0xFF makes the sink behave as if it must return a large
     * payload, which misaligns the following reads (this broke EDID reading). */
    txbuf[0] = (request << 4) | ((address >> 16) & 0xf);
    txbuf[1] = (address >> 8) & 0xff;
    txbuf[2] = address & 0xff;

    if (size > 0)
    {
        /* Payload messages: 4-byte header + payload. The payload is carried by
         * every WRITE request (even request values: NATIVE_WRITE 0x8, I2C_WRITE
         * 0x0, I2C_WRITE|MOT 0x4, STATUS_UPDATE 0x2); reads have none. */
        txbuf[3] = size - 1;
        txsize = 4 + size;
        if ((request & 0x1) == 0)
        {
            for (i = 0; i < size; i++)
                txbuf[4 + i] = data[i];
        }
    }
    else
        txsize = 3;   /* bare address: no length byte, no payload */

    /* Load the send data into the 5 AUX data registers (big-endian 4 bytes) */
    for (i = 0; i < txsize; i += 4)
    {
        v = 0;
        n = txsize - i;
        if (n > 4)
            n = 4;
        for (j = 0; j < n; j++)
            v |= ((ULONG)txbuf[i + j]) << ((3 - j) * 8);
        INTEL_WRITE(carddata, aux_data + (i & ~3), v);
    }

    /* Send the command (SKL send-ctl base 0xFE0003FF + message size<<20) */
    INTEL_WRITE(carddata, aux_ctl, 0xFE0003FFUL | ((ULONG)txsize << 20));

    /* Wait for the busy bit to clear */
    for (try = 0; try < 200; try++)
    {
        status = INTEL_READ(carddata, aux_ctl);
        if (!(status & DP_AUX_CH_CTL_SEND_BUSY))
            break;
        intel_delay(carddata, 100);
    }

    if (status & DP_AUX_CH_CTL_TIME_OUT_ERROR)
    {
        bug("[Intel] aux: transfer timeout (ctl=0x%08lx)\n", status);
        return -1;
    }
    if (status & DP_AUX_CH_CTL_RECEIVE_ERROR)
    {
        bug("[Intel] aux: transfer receive error (ctl=0x%08lx)\n", status);
        return -1;
    }

    /* First received byte is the reply; its high nibble must be 0 (ACK) */
    v = INTEL_READ(carddata, aux_data);
    if (((v >> 28) & 0xf) != 0)
    {
        bug("[Intel] aux: non-ACK reply 0x%02lx (data reg=0x%08lx)\n",
            (v >> 24) & 0xff, v);
        return -1;
    }
    bug("[Intel] aux: xfer ok ctl=0x%08lx datareg0=0x%08lx req=%d\n", status, v, request);

    /* Copy the received payload for every READ request (odd values:
     * NATIVE_READ 0x9, I2C_READ 0x1, I2C_READ|MOT 0x5). The first byte is the
     * reply, then the payload (up to 16 bytes over the 5 data registers). */
    if ((request & 0x1) == 1)
    {
        for (i = 0; i < size; i++)
        {
            int byte = i + 1;           /* skip the reply byte */
            int reg = byte >> 2;
            int off = byte & 3;
            ULONG rv = INTEL_READ(carddata, aux_data + reg * 4);
            data[i] = (rv >> ((3 - off) * 8)) & 0xff;
        }
    }

    return 0;
}

static int intel_dp_dpcd_write(struct CardData *carddata, ULONG aux_ctl,
                               ULONG aux_data, ULONG address, UBYTE *data, int size)
{
    return intel_dp_aux_transfer(carddata, aux_ctl, aux_data, DP_AUX_NATIVE_WRITE,
                                 address, data, size);
}

static int intel_dp_dpcd_read(struct CardData *carddata, ULONG aux_ctl,
                              ULONG aux_data, ULONG address, UBYTE *data, int size)
{
    return intel_dp_aux_transfer(carddata, aux_ctl, aux_data, DP_AUX_NATIVE_READ,
                                 address, data, size);
}

/*
 * Read the 128-byte EDID base block over the DP AUX channel using the
 * I2C-over-AUX protocol (the DDC/EDID address is 0x50). Mirrors Linux
 * drm_dp_i2c_xfer()/drm_do_probe_ddc_edid() for the base block:
 *   - bare address packet (size 0) to open the write,
 *   - write the offset (0) with MOT set,
 *   - bare address packet (size 0) to switch to read mode,
 *   - read 128 bytes in 16-byte chunks, all with MOT set,
 *   - closing bare address packet with MOT cleared.
 * Note: every chunk keeps MOT; only the final close packet clears it.
 */
static BOOL intel_dp_edid_read(struct CardData *carddata, ULONG aux_ctl,
                               ULONG aux_data, UBYTE *edid)
{
    UBYTE offset = 0;
    int i;
    static const ULONG edid_addr = 0x50;

    if (intel_dp_aux_transfer(carddata, aux_ctl, aux_data,
                              DP_AUX_I2C_WRITE | DP_AUX_I2C_MOT,
                              edid_addr, NULL, 0) != 0)
    {
        bug("[Intel] aux: EDID write-bare failed\n");
        return FALSE;
    }
    if (intel_dp_aux_transfer(carddata, aux_ctl, aux_data,
                              DP_AUX_I2C_WRITE | DP_AUX_I2C_MOT,
                              edid_addr, &offset, 1) != 0)
    {
        bug("[Intel] aux: EDID offset write failed\n");
        return FALSE;
    }
    if (intel_dp_aux_transfer(carddata, aux_ctl, aux_data,
                              DP_AUX_I2C_READ | DP_AUX_I2C_MOT,
                              edid_addr, NULL, 0) != 0)
    {
        bug("[Intel] aux: EDID read-bare failed\n");
        return FALSE;
    }
    for (i = 0; i < 128; i += 16)
    {
        if (intel_dp_aux_transfer(carddata, aux_ctl, aux_data,
                                  DP_AUX_I2C_READ | DP_AUX_I2C_MOT,
                                  edid_addr, &edid[i], 16) != 0)
        {
            bug("[Intel] aux: EDID read at %d failed\n", i);
            return FALSE;
        }
    }
    /* close the I2C transaction */
    if (intel_dp_aux_transfer(carddata, aux_ctl, aux_data,
                              DP_AUX_I2C_READ, edid_addr, NULL, 0) != 0)
    {
        bug("[Intel] aux: EDID close failed\n");
        return FALSE;
    }

    return TRUE;
}

/*
 * Parse the first pixel-format Detailed Timing Descriptor (DTD) from the EDID
 * base block (offset 0x36, 18 bytes each) into an AROS drmModeModeInfo.
 * Mirrors Linux's drm_mode_detailed() byte decode.
 */
static BOOL intel_edid_parse_preferred(const UBYTE *edid, drmModeModeInfoPtr mode)
{
    int i;

    for (i = 0; i < 4; i++)
    {
        const UBYTE *dtd = edid + 0x36 + i * 18;
        ULONG pixel_clock = dtd[0] | (dtd[1] << 8);
        ULONG hactive, vactive, hblank, vblank;
        ULONG hsync_offset, hsync_pw, vsync_offset, vsync_pw;

        if (pixel_clock == 0)
            continue;   /* not a detailed pixel timing (monitor descriptor) */

        hactive = ((dtd[4] & 0xf0) << 4) | dtd[2];
        vactive = ((dtd[7] & 0xf0) << 4) | dtd[5];
        hblank  = ((dtd[4] & 0x0f) << 8) | dtd[3];
        vblank  = ((dtd[7] & 0x0f) << 8) | dtd[6];
        hsync_offset = ((dtd[11] & 0xc0) << 2) | dtd[8];
        hsync_pw     = ((dtd[11] & 0x30) << 4) | dtd[9];
        vsync_offset = ((dtd[11] & 0x0c) << 2) | (dtd[10] >> 4);
        vsync_pw     = ((dtd[11] & 0x03) << 4) | (dtd[10] & 0x0f);

        if (hactive < 64 || vactive < 64 || !hsync_pw || !vsync_pw)
            continue;

        mode->clock       = pixel_clock * 10;               /* kHz */
        mode->hdisplay    = hactive;
        mode->hsync_start = hactive + hsync_offset;
        mode->hsync_end   = mode->hsync_start + hsync_pw;
        mode->htotal      = hactive + hblank;
        mode->hskew       = 0;
        mode->vdisplay    = vactive;
        mode->vsync_start = vactive + vsync_offset;
        mode->vsync_end   = mode->vsync_start + vsync_pw;
        mode->vtotal      = vactive + vblank;
        mode->vscan       = 0;
        if (mode->hsync_end > mode->htotal)
            mode->htotal = mode->hsync_end + 1;
        if (mode->vsync_end > mode->vtotal)
            mode->vtotal = mode->vsync_end + 1;
        mode->vrefresh = (mode->clock * 1000) /
            ((ULONG)mode->htotal * mode->vtotal);
        /* Sync polarity: misc byte (dtd[17]) bits 1/2 = positive H/V sync */
        mode->flags = 0;
        if (dtd[17] & 0x02)
            mode->flags |= 1;    /* AROS PHSYNC (see intel_set_mode) */
        if (dtd[17] & 0x04)
            mode->flags |= 4;    /* AROS PVSYNC */
        mode->type = DRM_MODE_TYPE_DRIVER;
        mode->name[0] = '\0';
        return TRUE;
    }

    return FALSE;
}

/*
 * Minimal EDID base-block validation: header magic + 8-bit checksum. Avoids
 * pulling the kernel drm_edid.h into the HIDD compile context.
 */
static BOOL intel_edid_is_valid(const UBYTE *edid)
{
    static const UBYTE hdr[8] = { 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00 };
    ULONG sum = 0;
    int i;

    for (i = 0; i < 8; i++)
        if (edid[i] != hdr[i])
            return FALSE;
    for (i = 0; i < 128; i++)
        sum += edid[i];
    return (sum & 0xff) == 0;
}

/*
 * Read + validate the EDID and return the preferred (native) mode. Called from
 * the probe so the connector mode list can be seeded with the monitor's real
 * native mode. Safe to fail: the caller keeps the static DMT list.
 */
BOOL intel_read_edid_preferred(struct CardData *carddata, APTR mode_arg)
{
    UBYTE edid[128];
    drmModeModeInfoPtr mode = (drmModeModeInfoPtr)mode_arg;
    ULONG aux, aux_ctl, aux_data;

    if (!carddata || !carddata->mmio || !mode)
        return FALSE;

    aux = ACTIVE_AUX_CH(carddata);
    aux_ctl = DDI_AUX_CTL_PORT(aux);
    aux_data = DDI_AUX_DATA_PORT(aux);

    if (!intel_dp_edid_read(carddata, aux_ctl, aux_data, edid))
        return FALSE;

    if (!intel_edid_is_valid(edid))
    {
        bug("[Intel] aux: EDID invalid, first bytes: %02x %02x %02x %02x %02x %02x %02x %02x / %02x %02x %02x %02x %02x %02x %02x %02x\n",
            edid[0], edid[1], edid[2], edid[3], edid[4], edid[5], edid[6], edid[7],
            edid[8], edid[9], edid[10], edid[11], edid[12], edid[13], edid[14], edid[15]);
        return FALSE;
    }

    if (!intel_edid_parse_preferred(edid, mode))
    {
        bug("[Intel] aux: EDID has no usable DTD\n");
        return FALSE;
    }

    return TRUE;
}

static BOOL intel_dp_link_train(struct CardData *carddata)
{
    UBYTE buf[8];
    UBYTE status[6];
    ULONG port = ACTIVE_DDI_PORT(carddata);
    ULONG aux = ACTIVE_AUX_CH(carddata);
    ULONG aux_ctl = DDI_AUX_CTL_PORT(aux);
    ULONG aux_data = DDI_AUX_DATA_PORT(aux);
    ULONG dp_tp_ctl = DP_TP_CTL_PORT(port);
    int tries;

    /* Probe all 5 AUX channels to find which one the sink is on. On Skylake DDI
     * E shares its PHY/IO with DDI A, so the AUX channel != port; probing is the
     * only reliable way. Record the channel that answers with a real DPCD rev. */
    {
        static const ULONG chans[5] = { 0x64010, 0x64110, 0x64210, 0x64310, 0x64410 };
        int c, found = -1;
        for (c = 0; c < 5; c++)
        {
            UBYTE r = 0;
            if (intel_dp_dpcd_read(carddata, chans[c], chans[c] + 4, 0x000, &r, 1) == 0)
            {
                bug("[Intel] aux: chan %d (0x%lx) DPCD rev = 0x%02x\n", c, chans[c], r);
                if (r != 0 && r != 0xFF && found < 0)
                    found = c;
            }
            else
                bug("[Intel] aux: chan %d (0x%lx) read failed\n", c, chans[c]);
        }
        if (found >= 0)
        {
            aux = (ULONG)found;
            carddata->display_aux = aux;
            aux_ctl = DDI_AUX_CTL_PORT(aux);
            aux_data = DDI_AUX_DATA_PORT(aux);
            bug("[Intel] aux: using AUX channel %lu (ctl=0x%lx)\n", aux, aux_ctl);
        }
    }

    /* Read DPCD revision to confirm the AUX channel works at all */
    if (intel_dp_dpcd_read(carddata, aux_ctl, aux_data, 0x000, buf, 1) < 0)
    {
        bug("[Intel] aux: DPCD rev read failed\n");
        return FALSE;
    }
    bug("[Intel] aux: DPCD rev = 0x%02x\n", buf[0]);

    /* Link config: HBR (2.7 Gb/s) + 2 lanes (enhanced frame) */
    buf[0] = DP_LINK_BW_2_7;
    if (intel_dp_dpcd_write(carddata, aux_ctl, aux_data, DPCD_LINK_BW_SET, buf, 1) < 0)
    {
        bug("[Intel] aux: LINK_BW_SET write failed\n");
        return FALSE;
    }
    buf[0] = 2 | DP_LANE_COUNT_ENHANCED_FRAME_EN;
    if (intel_dp_dpcd_write(carddata, aux_ctl, aux_data, DPCD_LANE_COUNT_SET, buf, 1) < 0)
    {
        bug("[Intel] aux: LANE_COUNT_SET write failed\n");
        return FALSE;
    }

    /* Downspread off + ANSI 8b/10b (Linux intel_dp_link_training_clock_recovery) */
    buf[0] = 0;
    buf[1] = 0x01;  /* DP_SET_ANSI_8B10B */
    intel_dp_dpcd_write(carddata, aux_ctl, aux_data, DPCD_DOWNSPREAD_CTRL, buf, 2);

    /* Clock recovery: training pattern 1, default lane drive settings.
     * Set the LOCAL DP_TP_CTL pattern first (as Linux does), then write the
     * sink's training pattern + lane drive registers in one AUX burst. */
    INTEL_WRITE(carddata, dp_tp_ctl, DP_TP_CTL_PAT1);
    buf[0] = DP_TRAINING_PATTERN_1 | DP_LINK_SCRAMBLING_DISABLE;
    buf[1] = 0;  /* lane 0 voltage swing/pre-emphasis */
    buf[2] = 0;  /* lane 1 voltage swing/pre-emphasis */
    if (intel_dp_dpcd_write(carddata, aux_ctl, aux_data, DPCD_TRAINING_PATTERN, buf, 3) < 0)
    {
        bug("[Intel] aux: PAT1 write failed\n");
        return FALSE;
    }
    intel_delay(carddata, 100000);
    bug("[Intel] aux: DP_TP_CTL after PAT1 = 0x%08lx\n", INTEL_READ(carddata, dp_tp_ctl));

    for (tries = 0; tries < 10; tries++)
    {
        intel_delay(carddata, 20000);
        if (intel_dp_dpcd_read(carddata, aux_ctl, aux_data, DPCD_LANE0_1_STATUS, status, 6) < 0)
            continue;
        if ((status[0] & 0x11) == 0x11)     /* lane0 + lane1 CR done */
            break;
    }
    if (tries == 10)
    {
        bug("[Intel] aux: clock recovery failed (status 0x%02x)\n", status[0]);
        return FALSE;
    }

    /* Channel equalization: read the sink's ADJUST_REQUEST (voltage swing /
     * pre-emphasis) and apply it, then send training pattern 2 */
    buf[0] = 0; buf[1] = 0;
    if (intel_dp_dpcd_read(carddata, aux_ctl, aux_data,
                           DPCD_ADJUST_REQUEST_LANE0_1, buf, 1) == 0)
    {
        UBYTE adj = buf[0];
        UBYTE vswing0 = adj & 0x3;
        UBYTE preemp0 = (adj >> 2) & 0x3;
        UBYTE vswing1 = (adj >> 4) & 0x3;
        UBYTE preemp1 = (adj >> 6) & 0x3;
        buf[0] = vswing0 | (preemp0 << 2);
        buf[1] = vswing1 | (preemp1 << 2);
        intel_dp_dpcd_write(carddata, aux_ctl, aux_data, DPCD_TRAINING_LANE0, buf, 2);
        bug("[Intel] aux: adjust req 0x%02x -> lane0=0x%02x lane1=0x%02x\n",
            adj, buf[0], buf[1]);
    }

    /* Channel equalization: local PAT2 first, then the sink's pattern register */
    INTEL_WRITE(carddata, dp_tp_ctl, DP_TP_CTL_PAT2);
    buf[0] = DP_TRAINING_PATTERN_2 | DP_LINK_SCRAMBLING_DISABLE;
    if (intel_dp_dpcd_write(carddata, aux_ctl, aux_data, DPCD_TRAINING_PATTERN, buf, 1) < 0)
    {
        bug("[Intel] aux: PAT2 write failed\n");
        return FALSE;
    }
    intel_delay(carddata, 100000);

    for (tries = 0; tries < 10; tries++)
    {
        intel_delay(carddata, 20000);
        if (intel_dp_dpcd_read(carddata, aux_ctl, aux_data, DPCD_LANE0_1_STATUS, status, 6) < 0)
            continue;
        if ((status[0] & 0x77) == 0x77)     /* lane0 + lane1 EQ done + locked */
            break;
    }
    if (tries == 10)
    {
        bug("[Intel] aux: channel equalization failed (status 0x%02x)\n", status[0]);
        return FALSE;
    }

    /* Training complete: stop training (NORMAL + disable pattern + D0 power),
     * as Linux intel_dp_stop_link_train does at the end of pre_enable. */
    {
        UBYTE b = DP_TRAINING_PATTERN_DISABLE;
        INTEL_WRITE(carddata, dp_tp_ctl, DP_TP_CTL_NORMAL);  /* send video */
        intel_dp_dpcd_write(carddata, aux_ctl, aux_data, DPCD_TRAINING_PATTERN, &b, 1);
        b = 0x01;  /* D0 */
        intel_dp_dpcd_write(carddata, aux_ctl, aux_data, DPCD_SET_POWER_STATE, &b, 1);
    }

    bug("[Intel] aux: link training OK (CR + EQ done)\n");
    return TRUE;
}

BOOL intel_set_mode(struct CardData *carddata, APTR mode_arg, ULONG pitch, ULONG bpp)
{
    drmModeModeInfoPtr mode = (drmModeModeInfoPtr)mode_arg;
    ULONG hdisplay, hsync_start, hsync_end, htotal;
    ULONG vdisplay, vsync_start, vsync_end, vtotal;
    ULONG plane_ctl_format, plane_stride;
    ULONG dport, daux, ddi_buf_ctl, dp_tp_ctl, dp_tp_status, aux_ctl, aux_data;
    ULONG trans, pipe, tb, pc, pl, ps;

    if (!carddata || !carddata->mmio || !mode)
        return FALSE;

    /*
     * Select the primary-plane pixel format + stride to match the framebuffer
     * depth, generation aware:
     *   Gen 9+  : PLANE_CTL at 0x70180.., format bits [27:24] (XRGB8888 4<<24,
     *             RGB565 14<<24), PLANE_STRIDE = pitch/64.
     *   Pre-Gen9 (Gen 4..8): DSPCNTR at the SAME offsets, format bits [29:26]
     *             (BGRX888 6<<26, BGRX565 5<<26), DSPSTRIDE = pitch IN BYTES.
     * This MUST match the framebuffer bytes-per-pixel: a 16bpp buffer fed with
     * a 32bpp format makes the plane fetch half a line per display line
     * (-> doubled/mirrored image) with wrong colors.
     */
    if (carddata->Generation >= 90)
    {
        switch (bpp)
        {
        case 16: plane_ctl_format = PLANE_CTL_FORMAT_RGB_565;   break;
        default: plane_ctl_format = PLANE_CTL_FORMAT_XRGB8888;  break;
        }
        plane_stride = pitch / 64;
    }
    else
    {
        switch (bpp)
        {
        case 16: plane_ctl_format = DSPCNTR_FORMAT_RGB_565;     break;   /* 5<<26 */
        default: plane_ctl_format = DSPCNTR_FORMAT_XRGB8888;    break;   /* 6<<26 */
        }
        plane_stride = pitch;
    }

    hdisplay    = mode->hdisplay;
    hsync_start = mode->hsync_start;
    hsync_end   = mode->hsync_end;
    htotal      = mode->htotal;
    vdisplay    = mode->vdisplay;
    vsync_start = mode->vsync_start;
    vsync_end   = mode->vsync_end;
    vtotal      = mode->vtotal;

    D(bug("[Intel] set_mode: %ux%u@%u clk=%u h=%u-%u/%u v=%u-%u/%u pitch=%lu bpp=%lu\n",
        (unsigned)hdisplay, (unsigned)vdisplay, (unsigned)mode->vrefresh,
        (unsigned)mode->clock,
        (unsigned)hsync_start, (unsigned)hsync_end, (unsigned)htotal,
        (unsigned)vsync_start, (unsigned)vsync_end, (unsigned)vtotal, pitch, bpp));
    bug("[Intel] set_mode: %ux%u@%u clk=%u pitch=%lu bpp=%lu plane_fmt=0x%08lx\n",
        (unsigned)hdisplay, (unsigned)vdisplay, (unsigned)mode->vrefresh,
        (unsigned)mode->clock, pitch, bpp, (unsigned long)plane_ctl_format);

    if (carddata->mode_width == hdisplay && carddata->mode_height == vdisplay &&
        carddata->mode_bpp == bpp && carddata->gtt_fb_pitch == pitch)
        return TRUE;

    carddata->mode_width = hdisplay;
    carddata->mode_height = vdisplay;
    carddata->mode_bpp = bpp;
    carddata->gtt_fb_pitch = pitch;

    /*
     * LEGACY (pre-DDI: Sandy Bridge / Ivy Bridge): no DDI, no DP/HDMI
     * transcoder-func, no shared DPLL. External outputs route through FDI/PCH
     * and the panel/eDP through the CPU pipe, all established by the BIOS.
     * Re-show the BIOS-active output by reprogramming only DSPCNTR + pipe
     * timing + PIPECONF (never touching DDI/clock). The plane uses the
     * pre-SKL DSPCNTR encoding + stride-in-bytes (computed above).
     */
    if (carddata->output_type == INTEL_OUTPUT_LEGACY)
    {
        pipe = (carddata->display_pipe != 0xFFFFFFFF) ? carddata->display_pipe : 0;
        trans = pipe;                     /* SNB/IVB: transcoder == pipe */
        tb = TRANS_TIMING(pipe);          /* 0x60000 + pipe*0x1000 */
        pc = PIPE_CBASE(pipe);            /* 0x70000 + pipe*0x1000 */
        pl = PLANE_CBASE(pipe);           /* 0x70180 + pipe*0x1000 (DSPCNTR etc.) */

        bug("[Intel] legacy mode set: %lux%lu pipe=%lu pitch=%lu bpp=%lu\n",
            hdisplay, vdisplay, pipe, pitch, bpp);

        /* Disable: plane -> pipe */
        INTEL_WRITE(carddata, pl, 0);
        INTEL_WRITE(carddata, pc + 0x08, 0x00000000);     /* PIPECONF off */
        intel_delay(carddata, 50000);

        /* Pipe timing (transcoder == pipe on SNB/IVB) */
        INTEL_WRITE(carddata, tb + 0x00, (hdisplay - 1) | ((htotal - 1) << 16));
        INTEL_WRITE(carddata, tb + 0x04, (hdisplay - 1) | ((htotal - 1) << 16));
        INTEL_WRITE(carddata, tb + 0x08, (hsync_start - 1) | ((hsync_end - 1) << 16));
        INTEL_WRITE(carddata, tb + 0x0c, (vdisplay - 1) | ((vtotal - 1) << 16));
        INTEL_WRITE(carddata, tb + 0x10, (vdisplay - 1) | ((vtotal - 1) << 16));
        INTEL_WRITE(carddata, tb + 0x14, (vsync_start - 1) | ((vsync_end - 1) << 16));
        INTEL_WRITE(carddata, PIPESRC_REG(pipe), ((hdisplay - 1) << 16) | (vdisplay - 1));

        /* Plane geometry: DSPCNTR/DSPSTRIDE(bytes)/DSPPOS/DSPSIZE/DSPSURF */
        INTEL_WRITE(carddata, pl + 0x08, plane_stride);
        INTEL_WRITE(carddata, pl + 0x0c, 0);
        INTEL_WRITE(carddata, pl + 0x10, (hdisplay - 1) | ((vdisplay - 1) << 16));
        INTEL_WRITE(carddata, pl + 0x1c, 0);
        INTEL_WRITE(carddata, pl + 0x24, 0);

        INTEL_WRITE(carddata, pc + 0x08, 0x80000000UL);   /* PIPECONF on */
        intel_delay(carddata, 50000);
        INTEL_WRITE(carddata, pl, 0x80000000UL | plane_ctl_format);  /* DSPCNTR on */

        bug("[Intel] legacy readback: HTOTAL=0x%08lx PIPESRC=0x%08lx PIPECONF=0x%08lx DSPCNTR=0x%08lx\n",
            INTEL_READ(carddata, tb + 0x00),
            INTEL_READ(carddata, PIPESRC_REG(pipe)),
            INTEL_READ(carddata, pc + 0x08),
            INTEL_READ(carddata, pl));
        return TRUE;
    }

    if (carddata->is_edp)
    {
        /*
         * eDP fixed panel (laptop). The panel fitter (SKL pipe scaler) drives
         * the fixed-timing panel: the scaler SOURCE is PIPESRC (= the selected
         * user mode) and the DESTINATION is PS_WIN_SZ (= panel native), exactly
         * as the BIOS leaves it at boot.
         *
         * KEY: the scaler re-latches its source size only when the pipe is
         * disabled while the SCALER + PIPESRC are reprogrammed, then the pipe
         * is re-enabled last (Linux haswell_crtc_enable order: pipe OFF ->
         * set_pipe_src_size + skylake_pfit_enable -> intel_enable_pipe ->
         * intel_enable_plane). Writes made while the pipe is RUNNING are
         * silently ignored by the hardware, which is why the previous code
         * "changed PIPESRC but nothing rescales" and why smaller
         * planes-than-source modes rendered as garbage.
         *
         * The EDP transcoder timing is left EXACTLY as the BIOS left it (this
         * panel's BIOS never programs HTOTAL_EDP/VTOTAL_EDP, the fitter drives
         * the fixed timing) and the scaler is KEPT ARMED in every mode - the
         * native mode is a 1:1 fitter passthrough. Disarming the fitter needs a
         * standalone transcoder timing this panel does not have.
         */
        ULONG pw = carddata->panel_width;
        ULONG ph = carddata->panel_height;
        ULONG hscale, vscale, i, max_dsl, f0, f1;

        intel_dump_scaler_state(carddata, "edp-entry");

        if (pw < 8 || ph < 8)
        {
            pw = 1366;
            ph = 768;
        }

        hscale = (hdisplay << 16) / pw;
        vscale = (vdisplay << 16) / ph;

        bug("[Intel] edp scaler: %lux%lu -> %lux%lu hscale=0x%lx vscale=0x%lx\n",
            hdisplay, vdisplay, pw, ph, hscale, vscale);

        /* Disable: plane -> pipe -> scaler/fitter (haswell_crtc_disable order) */
        INTEL_WRITE(carddata, PLANE_CTL_A, 0);
        INTEL_WRITE(carddata, PIPECONF_EDP, 0x00000000);      /* pipe off */
        intel_delay(carddata, 20000);
        if (carddata->Generation >= 90)
        {
            INTEL_WRITE(carddata, PS_CTRL_A, 0);              /* SKL scaler off */
            INTEL_WRITE(carddata, PS_WIN_SZ_A, 0);
            INTEL_WRITE(carddata, PS_WIN_POS_A, 0);
        }
        else
        {
            INTEL_WRITE(carddata, PF_CTL_A, 0);               /* HSW/BDW fitter off */
            INTEL_WRITE(carddata, PF_WIN_SZ_A, 0);
            INTEL_WRITE(carddata, PF_WIN_POS_A, 0);
        }

        /* Scaler SOURCE = user mode (Linux intel_set_pipe_src_size: "pipesrc
         * controls the size that is scaled from, which should always be the
         * user's requested size"). */
        INTEL_WRITE(carddata, PIPESRC_A, ((hdisplay - 1) << 16) | (vdisplay - 1));
        INTEL_WRITE(carddata, PIPEMISC_A, PIPEMISC_DITHER_6_BPC | PIPEMISC_DITHER_ENABLE);

        /* Plane geometry = mode */
        INTEL_WRITE(carddata, PLANE_STRIDE_A, plane_stride);
        INTEL_WRITE(carddata, PLANE_POS_A, 0);
        INTEL_WRITE(carddata, PLANE_SIZE_A, (hdisplay - 1) | ((vdisplay - 1) << 16));
        INTEL_WRITE(carddata, PLANE_SURF_A, 0);
        INTEL_WRITE(carddata, PLANE_OFFSET_A, 0);

        /*
         * TRANS_DDI_FUNC_CTL_EDP - eDP DDI function. This is the register the
         * earlier Haswell modeset never touched (BIOS 0x82204002 = ENABLE |
         * DP_SST | BPC_6 | EDP_INPUT_A_ONOFF | 2 lanes). Linux
         * intel_ddi_enable_transcoder_func selects the EDP_INPUT field from the
         * panel fitter state:
         *   - fitter SCALING a smaller source to the fixed panel: EDP_INPUT_A_ONOFF
         *   - fitter OFF, native 1:1 pipe->panel:            EDP_INPUT_A_ON
         * The BIOS boots the fitted 1024x768 desktop, so it left A_ONOFF. Keeping
         * that while disarming the fitter at native is what blanked the Haswell
         * 1920x1080 native mode in every build (PF on AND off both blanked
         * because TRANS_DDI_FUNC_EDP never switched the input select).
         */
        if (carddata->Generation < 90 &&
            (hdisplay != pw || vdisplay != ph))   /* fitter active */
        {
            ULONG edp_func = INTEL_READ(carddata, TRANS_DDI_FUNC_CTL_EDP);
            edp_func |= TRANS_DDI_FUNC_ENABLE;
            edp_func |= TRANS_DDI_EDP_INPUT_A_ONOFF;
            INTEL_WRITE(carddata, TRANS_DDI_FUNC_CTL_EDP, edp_func);
        }
        else if (carddata->Generation < 90)       /* native, fitter off */
        {
            ULONG edp_func = INTEL_READ(carddata, TRANS_DDI_FUNC_CTL_EDP);
            edp_func |= TRANS_DDI_FUNC_ENABLE;
            edp_func &= ~TRANS_DDI_EDP_INPUT_MASK;
            edp_func |= TRANS_DDI_EDP_INPUT_A_ON;
            INTEL_WRITE(carddata, TRANS_DDI_FUNC_CTL_EDP, edp_func);
            bug("[Intel] edp ddi_func native: 0x%08lx (EDP_INPUT A_ON)\n",
                INTEL_READ(carddata, TRANS_DDI_FUNC_CTL_EDP));
        }

        /* Panel fitter/scaler DESTINATION = panel native. SKL uses the PS_* pipe
         * scaler (0x90800000 = EN|HQ|bit23, phase 0, PWR_GATE un-gated);
         * HSW/Broadwell use the PF_* panel fitter (EN + MED_3x3 + pipe select).
         *
         * The HSW/Broadwell fitter is ARMED only for scaled modes; at the native
         * resolution it must stay DISARMED (Linux leaves scaler_users=0 there)
         * because the eDP DDI (see TRANS_DDI_FUNC_CTL_EDP above) is then switched
         * to EDP_INPUT_A_ON so the pipe drives the panel 1:1 directly. The
         * previous "keep the fitter armed at native" change was NOT the fix: with
         * A_ONOFF still latched the display stayed blank (out_has_4). */
        if (carddata->Generation >= 90)
        {
            INTEL_WRITE(carddata, PS_WIN_SZ_A, (pw << 16) | ph);
            INTEL_WRITE(carddata, PS_WIN_POS_A, 0);
            INTEL_WRITE(carddata, PS_HPHASE_A, 0);
            INTEL_WRITE(carddata, PS_VPHASE_A, 0);
            INTEL_WRITE(carddata, PS_PWR_GATE_A, PS_PWR_GATE_DIS_OVERRIDE);
            INTEL_WRITE(carddata, PS_CTRL_A, 0x90800000);
        }
        else
        {
            if (hdisplay != pw || vdisplay != ph)
            {
                INTEL_WRITE(carddata, PF_WIN_SZ_A, (pw << 16) | ph);
                INTEL_WRITE(carddata, PF_WIN_POS_A, 0);
                INTEL_WRITE(carddata, PF_CTL_A,
                            PF_ENABLE | PF_FILTER_MED_3x3 | PF_PIPE_SEL_IVB(0));
                bug("[Intel] edp pfit: PF_CTL=0x%08lx PF_WIN_SZ=0x%08lx PF_WIN_POS=0x%08lx\n",
                    INTEL_READ(carddata, PF_CTL_A),
                    INTEL_READ(carddata, PF_WIN_SZ_A),
                    INTEL_READ(carddata, PF_WIN_POS_A));
            }
        }

        bug("[Intel] edp readback: PIPESRC=0x%08lx PS_CTRL=0x%08lx PS_WIN_SZ=0x%08lx PIPEMISC=0x%08lx\n",
            INTEL_READ(carddata, PIPESRC_A),
            INTEL_READ(carddata, PS_CTRL_A),
            INTEL_READ(carddata, PS_WIN_SZ_A),
            INTEL_READ(carddata, PIPEMISC_A));
        bug("[Intel] edp readback2: HPHASE=0x%08lx VPHASE=0x%08lx WIN_POS=0x%08lx PWR_GATE=0x%08lx PLANE_CTL=0x%08lx PLANE_SIZE=0x%08lx STRIDE=0x%08lx\n",
            INTEL_READ(carddata, PS_HPHASE_A),
            INTEL_READ(carddata, PS_VPHASE_A),
            INTEL_READ(carddata, PS_WIN_POS_A),
            INTEL_READ(carddata, PS_PWR_GATE_A),
            INTEL_READ(carddata, PLANE_CTL_A),
            INTEL_READ(carddata, PLANE_SIZE_A),
            INTEL_READ(carddata, PLANE_STRIDE_A));

        /* Re-enable the pipe (latches PIPESRC + scaler), verify the pipe is
         * actually generating frames, then re-enable the plane (Linux
         * intel_enable_pipe asserts planes disabled; planes come up after). */
        INTEL_WRITE(carddata, PIPECONF_EDP, 0xc0000000);      /* pipe on */
        intel_delay(carddata, 20000);

        max_dsl = 0;
        f0 = INTEL_READ(carddata, PIPEFRAME_A);
        for (i = 0; i < 60000; i++)
        {
            ULONG d = INTEL_READ(carddata, PIPEDSL_A) & 0x1fff;
            if (d > max_dsl)
                max_dsl = d;
            intel_delay(carddata, 1);
        }
        f1 = INTEL_READ(carddata, PIPEFRAME_A);
        bug("[Intel] edp pipe: dsl max 0x%08lx (vtotal ~= %lu), frames: %lu->%lu\n",
            max_dsl, max_dsl + 1, f0, f1);

        INTEL_WRITE(carddata, PLANE_CTL_A, 0x80000000UL | plane_ctl_format);   /* plane on */
        /* Probe the panel's actual framebuffer/content into the GTT is the plane's job;
         * here we want the eDP LINK state that could drop (blank) on a mode
         * change: transcoder function, M/N, and the pipe conf. */
        bug("[Intel] edp link: TRANS_DDI_FUNC_EDP=0x%08lx DATA_M1=0x%08lx LINK_M1=0x%08lx PIPECONF_EDP=0x%08lx\n",
            INTEL_READ(carddata, 0x6f400),
            INTEL_READ(carddata, 0x6f030),   /* eDP transcoder DATA_M1 */
            INTEL_READ(carddata, 0x6f040),   /* eDP transcoder LINK_M1 */
            INTEL_READ(carddata, PIPECONF_EDP));
        intel_dump_scaler_state(carddata, "edp-exit");

        /* Laptop HDMI clone: also drive an HDMI monitor on DDI B mirroring
         * this mode (full bring-up; the BIOS leaves that DDI unpowered). */
        if (carddata->hdmi_clone_enable)
            intel_hdmi_clone_update(carddata,
                                    hdisplay, vdisplay, htotal,
                                    hsync_start, hsync_end,
                                    vtotal, vsync_start, vsync_end,
                                    pitch, bpp, mode->clock, mode->flags);

        return TRUE;
    }

    /* HDMI / DVI over DDI.
     *
     * Unlike DP there is no M/N, no AUX link training and (crucially) no
     * re-trainable PHY link: the HDMI TMDS clock comes from a shared DPLL that
     * this driver does not yet recompute, and the DDI buffer must keep running
     * the BIOS-established signal. So we only toggle the plane/pipe, reprogram
     * the timing (latches on the transcoder re-enable) and re-issue the
     * transcoder function in HDMI mode - the DDI buffer and TRANS_CLK_SEL are
     * left exactly as the BIOS set them.
     *
     * Known limitation: modes whose pixel clock differs from the BIOS HDMI mode
     * will not sync until shared-DPLL management is added (same class of
     * limitation the DP path had before its link work).
     */
    if (carddata->output_type == INTEL_OUTPUT_HDMI ||
        carddata->output_type == INTEL_OUTPUT_DVI)
    {
        ULONG ddi_func;
        ULONG is_hdmi = (carddata->output_type == INTEL_OUTPUT_HDMI);

        dport = ACTIVE_DDI_PORT(carddata);
        trans = (carddata->display_trans != 0xFFFFFFFF) ? carddata->display_trans : 0;
        pipe  = (carddata->display_pipe  != 0xFFFFFFFF) ? carddata->display_pipe  : 0;
        tb = TRANS_TIMING(trans);
        pc = PIPE_CBASE(pipe);
        pl = PLANE_CBASE(pipe);

        bug("[Intel] HDMI/DVI mode set: %lux%lu port=%lu trans=%lu pipe=%lu pitch=%lu bpp=%lu\n",
            hdisplay, vdisplay, dport, trans, pipe, pitch, bpp);

        /* Disable: plane -> transcoder func -> pipe (DDI buf + clock stay on). */
        INTEL_WRITE(carddata, pl, 0);
        ddi_func = INTEL_READ(carddata, TRANS_DDI_FUNC(trans));
        INTEL_WRITE(carddata, TRANS_DDI_FUNC(trans), ddi_func & ~TRANS_DDI_FUNC_ENABLE);
        INTEL_WRITE(carddata, pc + 0x08, 0x00000000);            /* pipe off */
        intel_delay(carddata, 100000);

        /* Pipe timing (latches on the transcoder re-enable below). */
        INTEL_WRITE(carddata, tb + 0x00, (hdisplay - 1) | ((htotal - 1) << 16));
        INTEL_WRITE(carddata, tb + 0x04, (hdisplay - 1) | ((htotal - 1) << 16));
        INTEL_WRITE(carddata, tb + 0x08, (hsync_start - 1) | ((hsync_end - 1) << 16));
        INTEL_WRITE(carddata, tb + 0x0c, (vdisplay - 1) | ((vtotal - 1) << 16));
        INTEL_WRITE(carddata, tb + 0x10, (vdisplay - 1) | ((vtotal - 1) << 16));
        INTEL_WRITE(carddata, tb + 0x14, (vsync_start - 1) | ((vsync_end - 1) << 16));
        INTEL_WRITE(carddata, PIPESRC_REG(pipe), ((hdisplay - 1) << 16) | (vdisplay - 1));

        /* Plane geometry */
        INTEL_WRITE(carddata, pl + 0x08, plane_stride);   /* PLANE_STRIDE */
        INTEL_WRITE(carddata, pl + 0x0c, 0);               /* PLANE_POS */
        INTEL_WRITE(carddata, pl + 0x10, (hdisplay - 1) | ((vdisplay - 1) << 16)); /* PLANE_SIZE */
        INTEL_WRITE(carddata, pl + 0x1c, 0);               /* PLANE_SURF */
        INTEL_WRITE(carddata, pl + 0x24, 0);               /* PLANE_OFFSET */

        /* Transcoder function in HDMI mode:
         * ENABLE | port | (HDMI(0)/DVI(1) << 24) | BPC_8 | sync polarity */
        ddi_func = TRANS_DDI_FUNC_ENABLE;
        if (carddata->is_tgl_plus)
            ddi_func |= ((dport + 1) & 0xF) << 27;
        else
            ddi_func |= (dport & 0x7) << 28;
        ddi_func |= (is_hdmi ? 0x0UL : 0x1UL) << 24;      /* HDMI / DVI mode select */
        ddi_func |= 0x0UL << 20;                          /* TRANS_DDI_BPC_8 */
        if (mode->flags & 1)  ddi_func |= (1UL << 16);    /* PHSYNC */
        if (mode->flags & 4)  ddi_func |= (1UL << 17);    /* PVSYNC */

        bug("[Intel] hdmi ddi_func=0x%08lx (%s)\n", ddi_func, is_hdmi ? "HDMI" : "DVI");
        INTEL_WRITE(carddata, TRANS_DDI_FUNC(trans), ddi_func);

        INTEL_WRITE(carddata, pc + 0x08, 0x80000000);      /* pipe on */
        intel_delay(carddata, 100000);
        INTEL_WRITE(carddata, pl, 0x80000000UL | plane_ctl_format); /* plane on */

        bug("[Intel] hdmi readback: HTOTAL=0x%08lx PIPESRC=0x%08lx FUNC=0x%08lx PIPEACONF=0x%08lx\n",
            INTEL_READ(carddata, tb + 0x00),
            INTEL_READ(carddata, PIPESRC_REG(pipe)),
            INTEL_READ(carddata, TRANS_DDI_FUNC(trans)),
            INTEL_READ(carddata, pc + 0x08));
        return TRUE;
    }

    /*
     * DP mode set (Linux haswell_crtc_enable/disable order): the pipe TIMING
     * latches on pipe re-enable, the M/N latches when the DDI buffer is
     * toggled, and the PHY link must be re-trained over AUX after the DDI
     * buffer is re-enabled. So: disable plane/transcoder/pipe/DDI/clock, then
     * re-enable clock+DDI, retrain the link, write M/N+timing, and re-enable
     * transcoder/pipe/plane.
     *
     * The DDI port and AUX channel come from the topology detected at probe time
     * (carddata->display_port/display_aux), falling back to the historical
     * Skylake desktop (port E, AUX A). This makes the path work on any DDI
     * assignment instead of only that one board.
     */
    dport = ACTIVE_DDI_PORT(carddata);
    daux  = ACTIVE_AUX_CH(carddata);
    ddi_buf_ctl  = DDI_BUF_CTL_PORT(dport);
    dp_tp_ctl    = DP_TP_CTL_PORT(dport);
    dp_tp_status = DP_TP_STATUS_PORT(dport);
    aux_ctl      = DDI_AUX_CTL_PORT(daux);
    aux_data     = DDI_AUX_DATA_PORT(daux);

    /* Active transcoder + pipe (default A == 0). This is what makes the driver
     * work when the monitor is not on transcoder/pipe A. */
    trans = (carddata->display_trans != 0xFFFFFFFF) ? carddata->display_trans : 0;
    pipe  = (carddata->display_pipe  != 0xFFFFFFFF) ? carddata->display_pipe  : 0;
    tb = TRANS_TIMING(trans);
    pc = PIPE_CBASE(pipe);
    pl = PLANE_CBASE(pipe);
    ps = PS_CTRL_REG(pipe);

    bug("[Intel] DP port=%lu aux=%lu trans=%lu pipe=%lu (DDI_BUF=0x%lx DP_TP=0x%lx AUX=0x%lx)\n",
        dport, daux, trans, pipe, ddi_buf_ctl, dp_tp_ctl, aux_ctl);

    bug("[Intel] link before: DDI_BUF=0x%08lx DP_TP=0x%08lx DP_TP_STATUS=0x%08lx CLK_SEL=0x%08lx AUX_CTL=0x%08lx\n",
        INTEL_READ(carddata, ddi_buf_ctl),
        INTEL_READ(carddata, dp_tp_ctl),
        INTEL_READ(carddata, dp_tp_status),
        INTEL_READ(carddata, TRANS_CLK_SEL(trans)),
        INTEL_READ(carddata, aux_ctl));

    /* Disable: plane -> transcoder -> pipe -> DDI buffer -> DP transport -> clock */
    INTEL_WRITE(carddata, pl, 0);
    INTEL_WRITE(carddata, TRANS_DDI_FUNC(trans), 0x42030002);   /* transcoder off */
    INTEL_WRITE(carddata, pc + 0x08, 0x00000000);               /* pipe off */
    INTEL_WRITE(carddata, ddi_buf_ctl, 0x00000002);            /* DDI buffer off */
    INTEL_WRITE(carddata, dp_tp_ctl, 0x00000000);              /* DP transport off */
    INTEL_WRITE(carddata, TRANS_CLK_SEL(trans), 0x00000000);   /* clock off */
    intel_delay(carddata, 100000);

    /* Re-enable the clock, then write M/N BEFORE the DDI-buffer enable (so the
     * M/N latches on that enable) and the timing AFTER the link training but
     * before the transcoder enable (so the timing latches on that enable). */
    INTEL_WRITE(carddata, TRANS_CLK_SEL(trans), 0xa0000000);   /* clock = port E */

    /* Reprogram M/N (latches on the DDI-buffer enable below) */
    intel_program_m_n(carddata, (UQUAD)mode->clock, bpp);

    /* Enable the DDI buffer (latches M/N), then retrain the DP link over AUX */
    INTEL_WRITE(carddata, ddi_buf_ctl, 0x80000002);            /* DDI buffer on */
    intel_dp_link_train(carddata);

    /* Reprogram the pipe timing (latches on the transcoder enable below) */
    INTEL_WRITE(carddata, tb + 0x00, (hdisplay - 1) | ((htotal - 1) << 16));
    INTEL_WRITE(carddata, tb + 0x04, (hdisplay - 1) | ((htotal - 1) << 16));
    INTEL_WRITE(carddata, tb + 0x08, (hsync_start - 1) | ((hsync_end - 1) << 16));
    INTEL_WRITE(carddata, tb + 0x0c, (vdisplay - 1) | ((vtotal - 1) << 16));
    INTEL_WRITE(carddata, tb + 0x10, (vdisplay - 1) | ((vtotal - 1) << 16));
    INTEL_WRITE(carddata, tb + 0x14, (vsync_start - 1) | ((vsync_end - 1) << 16));
    INTEL_WRITE(carddata, PIPESRC_REG(pipe), ((hdisplay - 1) << 16) | (vdisplay - 1));
    INTEL_WRITE(carddata, TRANS_MSA_MISC(trans), 0x00000021);

    /* Plane geometry */
    INTEL_WRITE(carddata, pl + 0x08, plane_stride);   /* PLANE_STRIDE */
    INTEL_WRITE(carddata, pl + 0x0c, 0);               /* PLANE_POS */
    INTEL_WRITE(carddata, pl + 0x10, (hdisplay - 1) | ((vdisplay - 1) << 16)); /* PLANE_SIZE */
    INTEL_WRITE(carddata, pl + 0x1c, 0);               /* PLANE_SURF */
    INTEL_WRITE(carddata, pl + 0x24, 0);               /* PLANE_OFFSET */

    /* Panel fitter off (no GPU scaler for DP; the monitor scales itself) */
    if (carddata->Generation >= 90)
        INTEL_WRITE(carddata, ps, 0);

    bug("[Intel] dp readback: HTOTAL=0x%08lx VTOTAL=0x%08lx PIPESRC=0x%08lx MSA=0x%08lx\n",
        INTEL_READ(carddata, tb + 0x00),
        INTEL_READ(carddata, tb + 0x0c),
        INTEL_READ(carddata, PIPESRC_REG(pipe)),
        INTEL_READ(carddata, TRANS_MSA_MISC(trans)));

    /* Enable: transcoder -> pipe -> plane. Sync polarity is derived from the
     * mode flags; the AROS mode table leaves flags=0, which means negative
     * hsync/vsync (Linux's intel_ddi_enable_transcoder_func also defaults to
     * negative when PHSYNC/PVSYNC are absent).
     *
     * DDI port field: bits [30:28] = port on SKL/ICL, but Gen 12+ (Tiger Lake)
     * moved it to bits [30:27] with the port value offset by +1
     * (TGL_TRANS_DDI_PORT_SHIFT / TGL_TRANS_DDI_SELECT_PORT in Linux i915). */
    {
        ULONG ddi_func = 0x82000002;  /* ENABLE + DP SST + 8bpc + 2 lanes + bit1 */
        ULONG port_sel;
        if (carddata->is_tgl_plus)
            port_sel = ((dport + 1) & 0xF) << 27;
        else
            port_sel = (dport & 0x7) << 28;
        ddi_func |= port_sel;
        if (mode->flags & 1)  ddi_func |= (1UL << 16);  /* PHSYNC */
        if (mode->flags & 4)  ddi_func |= (1UL << 17);  /* PVSYNC */
        bug("[Intel] ddi_func=0x%08lx (flags=0x%x port=%lu)\n", ddi_func, mode->flags, dport);
        INTEL_WRITE(carddata, TRANS_DDI_FUNC(trans), ddi_func);
    }
    INTEL_WRITE(carddata, pc + 0x08, 0x80000000);            /* pipe on (no double-wide) */
    intel_delay(carddata, 100000);
    INTEL_WRITE(carddata, pl, 0x80000000UL | plane_ctl_format); /* plane on */

    /* Verify the link is still up (sink side) after the video is enabled */
    {
        UBYTE st[6] = {0,0,0,0,0,0};
        intel_dp_dpcd_read(carddata, aux_ctl, aux_data,
                           DPCD_LANE0_1_STATUS, st, 6);
        bug("[Intel] post link status: lane0_1=0x%02x align=0x%02x\n", st[0], st[2]);
    }

    /* Sample the scanline to find the actual vtotal (timing latch) */
    {
        ULONG max_dsl = 0, i, f0, f1;
        f0 = INTEL_READ(carddata, pc + 0x40);
        for (i = 0; i < 60000; i++)
        {
            ULONG d = INTEL_READ(carddata, pc) & 0x1fff;
            if (d > max_dsl)
                max_dsl = d;
            intel_delay(carddata, 1);
        }
        f1 = INTEL_READ(carddata, pc + 0x40);
        bug("[Intel] dsl max: 0x%08lx (vtotal ~= %lu), frames: %lu->%lu\n",
            max_dsl, max_dsl + 1, f0, f1);
    }

    bug("[Intel] post readback: DDI_BUF=0x%08lx DP_TP=0x%08lx DP_TP_STATUS=0x%08lx CLK_SEL=0x%08lx PIPEDSL=0x%08lx PIPEACONF=0x%08lx\n",
        INTEL_READ(carddata, ddi_buf_ctl),
        INTEL_READ(carddata, dp_tp_ctl),
        INTEL_READ(carddata, dp_tp_status),
        INTEL_READ(carddata, TRANS_CLK_SEL(trans)),
        INTEL_READ(carddata, pc),
        INTEL_READ(carddata, pc + 0x08));
    bug("[Intel] xcoder readback: TRANS_DDI_FUNC=0x%08lx DATA_M1=0x%08lx LINK_M1=0x%08lx PIPEMISC=0x%08lx PIPE_MULT=0x%08lx\n",
        INTEL_READ(carddata, TRANS_DDI_FUNC(trans)),
        INTEL_READ(carddata, MN_BASE(trans) + 0x00),
        INTEL_READ(carddata, MN_BASE(trans) + 0x10),
        INTEL_READ(carddata, pc + 0x28),
        INTEL_READ(carddata, tb + 0x2c));
    bug("[Intel] plane readback: CTL=0x%08lx STRIDE=0x%08lx SIZE=0x%08lx SURF=0x%08lx OFFSET=0x%08lx\n",
        INTEL_READ(carddata, pl + 0x00),
        INTEL_READ(carddata, pl + 0x08),
        INTEL_READ(carddata, pl + 0x10),
        INTEL_READ(carddata, pl + 0x1c),
        INTEL_READ(carddata, pl + 0x24));

    return TRUE;
}

/*
 * HDMI clone bring-up on DDI port B (port index 1). Used on laptops (eDP on
 * DDI A) to drive an HDMI monitor on DDI B as a SECOND output mirroring the
 * active mode. This is the "full HDMI bring-up" path: the BIOS leaves the HDMI
 * DDI unpowered, so we program the shared DPLL (HDMI WRPLL), the DDI buffer,
 * the transcoder clock and a second pipe/plane from scratch.
 *
 * Registers (Skylake, port B, transcoder/pipe B):
 *   DPLL1 via LCPLL2_CTL (0x46014), DPLL_STATUS bit8 lock.
 *   DPLL_CTRL1 0x6C058, DPLL_CTRL2 0x6C05C (DDI B sel/override bits 3,4).
 *   TRANS_CLK_SEL_B 0x46144 = TRANS_CLK_SEL_PORT(1) = (1+1)<<29.
 *   DDI_BUF_CTL_B 0x64100 = enable | DDI_PORT_WIDTH(4) = 0x80000006.
 *   Transcoder/pipe B: timing 0x61000.., PIPECONF 0x71008, plane 0x71180,
 *   PIPESRC 0x6101c, TRANS_DDI_FUNC_CTL_B 0x61400.
 *
 * The plane B scans the SAME GTT framebuffer, so both displays show the same
 * content (clone). Only correct for the laptop's DDI-B HDMI mapping.
 */
static void intel_hdmi_clone_update(struct CardData *carddata,
                                    ULONG hdisplay, ULONG vdisplay,
                                    ULONG htotal, ULONG hsync_start, ULONG hsync_end,
                                    ULONG vtotal, ULONG vsync_start, ULONG vsync_end,
                                    ULONG pitch, ULONG bpp, ULONG clock_khz,
                                    ULONG flags)
{
    static const ULONG trans_b = 1;     /* transcoder B */
    static const ULONG pipe_b = 1;      /* pipe B */
    ULONG tb, pc, pl;
    ULONG cfgcr1, cfgcr2;
    ULONG dpll_clock = (ULONG)clock_khz * 1000;
    int try;

    tb = TRANS_TIMING(trans_b);     /* 0x61000 */
    pc = PIPE_CBASE(pipe_b);        /* 0x71000 */
    pl = PLANE_CBASE(pipe_b);       /* 0x71180 */

    /* Only re-enable the DPLL if the clock changed. */
    if (!carddata->hdmi_clone_done || carddata->hdmi_clone_clock != dpll_clock)
    {
        if (!intel_wrpll_calc_cfgcr(dpll_clock, &cfgcr1, &cfgcr2))
        {
            bug("[Intel] hdmi clone: no WRPLL divider for %lu Hz\n", dpll_clock);
            return;
        }

        INTEL_WRITE(carddata, DPLL1_CFGCR1, cfgcr1);
        INTEL_WRITE(carddata, DPLL1_CFGCR2, cfgcr2);
        INTEL_WRITE(carddata, DPLL_CTRL1, DPLL_CTRL1_OVERRIDE(1) | DPLL_CTRL1_HDMI_MODE(1));
        /* DDI B <- DPLL1: DDI_SEL_OVERRIDE(1)=1<<3, DDI_CLK_SEL(1,1)=1<<4 */
        INTEL_WRITE(carddata, DPLL_CTRL2,
            (1UL << (1 * 3)) | (1UL << (1 * 3 + 1)));

        /* Enable DPLL1 (LCPLL2_CTL bit31) and wait for lock (bit8). */
        INTEL_WRITE(carddata, LCPLL2_CTL,
                    INTEL_READ(carddata, LCPLL2_CTL) | 0x80000000UL);
        for (try = 0; try < 50; try++)
        {
            if (INTEL_READ(carddata, DPLL_STATUS) & (1UL << 8))
                break;
            intel_delay(carddata, 1000);
        }
        bug("[Intel] hdmi clone: DPLL1 clk=%lu cfgcr1=0x%08lx cfgcr2=0x%08lx lock=%s\n",
            dpll_clock, cfgcr1, cfgcr2,
            (INTEL_READ(carddata, DPLL_STATUS) & (1UL << 8)) ? "YES" : "NO");

        /* Without a locked PLL there is no usable HDMI signal - do NOT enable
         * the DDI/pipe, to avoid a garbage output and interference with the
         * internal panel during mode switches. */
        if (!(INTEL_READ(carddata, DPLL_STATUS) & (1UL << 8)))
            return;

        carddata->hdmi_clone_clock = dpll_clock;
    }

    /* Disable transcoder func / pipe B first (DDI buf stays; no retrain). */
    INTEL_WRITE(carddata, pl, 0);                              /* plane B off */
    INTEL_WRITE(carddata, TRANS_DDI_FUNC(trans_b),
                INTEL_READ(carddata, TRANS_DDI_FUNC(trans_b)) & ~TRANS_DDI_FUNC_ENABLE);
    INTEL_WRITE(carddata, pc + 0x08, 0);                       /* pipe B off */
    intel_delay(carddata, 50000);

    /* Timing (latch on transcoder re-enable) */
    INTEL_WRITE(carddata, tb + 0x00, (hdisplay - 1) | ((htotal - 1) << 16));
    INTEL_WRITE(carddata, tb + 0x04, (hdisplay - 1) | ((htotal - 1) << 16));
    INTEL_WRITE(carddata, tb + 0x08, (hsync_start - 1) | ((hsync_end - 1) << 16));
    INTEL_WRITE(carddata, tb + 0x0c, (vdisplay - 1) | ((vtotal - 1) << 16));
    INTEL_WRITE(carddata, tb + 0x10, (vdisplay - 1) | ((vtotal - 1) << 16));
    INTEL_WRITE(carddata, tb + 0x14, (vsync_start - 1) | ((vsync_end - 1) << 16));
    INTEL_WRITE(carddata, PIPESRC_REG(pipe_b), ((hdisplay - 1) << 16) | (vdisplay - 1));

    /* Plane B geometry = mode */
    INTEL_WRITE(carddata, pl + 0x08, pitch / 64);
    INTEL_WRITE(carddata, pl + 0x0c, 0);
    INTEL_WRITE(carddata, pl + 0x10, (hdisplay - 1) | ((vdisplay - 1) << 16));
    INTEL_WRITE(carddata, pl + 0x1c, 0);
    INTEL_WRITE(carddata, pl + 0x24, 0);

    /* Transcoder B clock = port B */
    INTEL_WRITE(carddata, TRANS_CLK_SEL(trans_b), (1UL << 29));   /* TRANS_CLK_SEL_PORT(1) */

    /* DDI B buffer: enable + 4 lanes */
    INTEL_WRITE(carddata, 0x64100, 0x80000000UL | ((4 - 1) << 1));

    /* TRANS_DDI_FUNC_CTL_B: HDMI mode, port B, 8bpc, sync polarity */
    {
        ULONG ddi_func = 0x80000000UL | (1UL << 28);   /* ENABLE + port B */
        ddi_func |= 0x0UL << 24;                       /* HDMI mode select */
        ddi_func |= 0x0UL << 20;                       /* BPC_8 */
        if (flags & 1)  ddi_func |= (1UL << 16);       /* PHSYNC */
        if (flags & 4)  ddi_func |= (1UL << 17);       /* PVSYNC */
        INTEL_WRITE(carddata, TRANS_DDI_FUNC(trans_b), ddi_func);
    }

    INTEL_WRITE(carddata, pc + 0x08, 0x80000000UL);    /* pipe B on */
    intel_delay(carddata, 50000);
    INTEL_WRITE(carddata, pl, 0x80000000UL | (bpp == 16 ? (14UL << 24) : (4UL << 24))); /* plane B on */

    bug("[Intel] hdmi clone: %lux%lu DDI_BUF=0x%08lx FUNC=0x%08lx CLK_SEL=0x%08lx PIPEACONF=0x%08lx\n",
        hdisplay, vdisplay,
        INTEL_READ(carddata, 0x64100),
        INTEL_READ(carddata, TRANS_DDI_FUNC(trans_b)),
        INTEL_READ(carddata, TRANS_CLK_SEL(trans_b)),
        INTEL_READ(carddata, pc + 0x08));

    carddata->hdmi_clone_done = TRUE;
}

BOOL intel_set_mode_by_resolution(struct CardData *carddata, APTR connector_arg,
    ULONG width, ULONG height, ULONG pitch, ULONG bpp)
{
    drmModeConnectorPtr connector = (drmModeConnectorPtr)connector_arg;
    int i;

    if (!connector)
        return FALSE;

    for (i = 0; i < connector->count_modes; i++)
    {
        drmModeModeInfoPtr mode = &connector->modes[i];

        if (mode->hdisplay == width && mode->vdisplay == height)
            return intel_set_mode(carddata, mode, pitch, bpp);
    }

    return FALSE;
}

BOOL intel_mode_lookup_and_set(struct CardData *carddata, APTR connector_arg,
    ULONG modeid, ULONG bytesperpixel, ULONG *width, ULONG *height, ULONG *pitch)
{
    drmModeConnectorPtr connector = (drmModeConnectorPtr)connector_arg;
    ULONG syncidx = (modeid >> 8) & 0xFF;
    ULONG p;
    drmModeModeInfoPtr mode;

    bug("[Intel] mode_lookup_and_set: modeid=0x%lx sync=%lu bpp=%lu\n",
        modeid, syncidx, bytesperpixel * 8);

    if (!connector || syncidx >= (ULONG)connector->count_modes)
        return FALSE;

    mode = &connector->modes[syncidx];
    p = mode->hdisplay * bytesperpixel;
    p = (p + 63) & ~63;

    if (width)  *width  = mode->hdisplay;
    if (height) *height = mode->vdisplay;
    if (pitch)  *pitch  = p;

    return intel_set_mode(carddata, mode, p, bytesperpixel * 8);
}
