/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Sensor plug-in for the front "Skype_HD" camera module of the Nokia Lumia
 * 930 (and 1520), for the 2014 MSM8974 mm-camera2 daemon of OnePlus One
 * (bacon). Written against camera-sensors/bacon_sensor_abi.h, same structure
 * as the rear karma plug-in.
 *
 * Module: SMIA module 0x6A30 (manufacturer 0x0A, revision 3 on the test
 * phone), die 0x6A30 revision 2 (die manufacturer 0x09), CCI master 0 at
 * 7-bit 0x10, CSIPHY2/CSID2, one CSI-2 lane (0x0114 stays 0 when 1 is
 * written), RAW10, pixel order GRBG (0x0006 = 0), data pedestal 64.
 * Analog gain = code / 32 (0x0084-0x0093: codes 32-512, m0 1, c1 32).
 * Fixed focus, no flash. Power: L17 2.8 V, GPIO 18 high, LVS2, MCLK2 9.6 MHz.
 *
 * Module settings come from skyphd_module_regs.h: the idle register block
 * (analog setup and fine_integration_time 1104), the limit overrides
 * (min_line_length_pck 1602, no fine integration control, D-PHY timing from
 * the requested link rate). fine_integration_time follows the line length
 * for this module (0x0200 = line_length_pck - 498, the idle value at 1602).
 *
 * Clock tree, from the SMIA limit registers read on the phone (0x1100-0x117f)
 * and the 250 MHz maximum CSI clock of the board (500 Mbit/s per lane):
 *   EXTCLK 9.6 MHz / pre_pll_div 2 = 4.8 MHz (3-6) x 104 = 499.2 MHz PLL
 *   (494-1000); vt_sys_div 1, vt_pix_div 10: vt_pix_clk 49.92 MHz (8-125);
 *   op_sys_div 1: 499.2 Mbit/s on the lane, op_pix_div 10 (RAW10): op_pix_clk
 *   49.92 MHz, the same pixel rate as the readout.
 * line_length_pck 1602 (module minimum), frame_length_lines 1040:
 * 29.96 frames/s in all modes.
 *
 * The sensor stops producing frames (frame counter 0x0005 still, no frame
 * end on CSI-2) as soon as the output height 0x034e is above 796 rows, so
 * the modes stay within the 1412x796 default window (rows 308-1103):
 *   0  1408x792 16:9, stills and preview
 *   1  1056x792 4:3
 *   2  1280x720 video
 *
 * Copyright (c) 2026, Lorenzo Giannoccaro.
 */
#include "../bacon_sensor_abi.h"
#include "skyphd_module_regs.h"

#define SENSOR_NAME "skyphd"

/* ---- SMIA limits read on the phone (scan6.txt), used by the checks ---- */
#define LIM_PLL_IP_MIN_KHZ      3000
#define LIM_PLL_IP_MAX_KHZ      6000
#define LIM_PRE_DIV_MIN         1
#define LIM_PRE_DIV_MAX         8
#define LIM_PLL_MUL_MIN         83
#define LIM_PLL_MUL_MAX         333
#define LIM_PLL_OP_MIN_KHZ      494000
#define LIM_PLL_OP_MAX_KHZ      1000000
#define LIM_SYS_MIN_KHZ         256000    /* vt and op */
#define LIM_SYS_MAX_KHZ         1000000
#define LIM_SYS_DIV_MAX         12
#define LIM_PIX_MIN_KHZ         8000      /* vt and op */
#define LIM_PIX_MAX_KHZ         125000
#define LIM_PIX_DIV_MIN         8
#define LIM_PIX_DIV_MAX         10
#define LIM_MIN_FRAME_LINES     214
#define LIM_MIN_FRAME_BLANK     22
#define LIM_MIN_LINE_BLANK      190
#define LIM_X_ADDR_MAX          1411
#define LIM_Y_ADDR_MAX          1411
#define LIM_MARGIN              6         /* coarse_integration_time_max_margin */
#define LIM_COARSE_MIN          2
#define BOARD_MAX_LANE_KBPS     500000    /* FFC MaxCsiFreqMhz 250, DDR */

/* ---- clock tree ---- */
#define SKYPHD_EXTCLK_HZ    9600000
#define SKYPHD_EXTCLK_8_8   0x099a    /* 9.6 MHz in 8.8 */
#define SKYPHD_PRE_DIV      2
#define SKYPHD_PLL_MUL      104
#define SKYPHD_VT_SYS_DIV   1
#define SKYPHD_VT_PIX_DIV   10
#define SKYPHD_OP_SYS_DIV   1
#define SKYPHD_OP_PIX_DIV   10        /* RAW10 */
#define PLL_IP_KHZ   (SKYPHD_EXTCLK_HZ / 1000 / SKYPHD_PRE_DIV)
#define PLL_OP_KHZ   (PLL_IP_KHZ * SKYPHD_PLL_MUL)
#define VT_SYS_KHZ   (PLL_OP_KHZ / SKYPHD_VT_SYS_DIV)
#define VT_PIX_KHZ   (VT_SYS_KHZ / SKYPHD_VT_PIX_DIV)
#define OP_SYS_KHZ   (PLL_OP_KHZ / SKYPHD_OP_SYS_DIV)
#define OP_PIX_KHZ   (OP_SYS_KHZ / SKYPHD_OP_PIX_DIV)
#define SKYPHD_VT_CLK  (VT_PIX_KHZ * 1000)
/* Clock the daemon sets the VFE to: the real output pixel clock (49.92 MHz)
 * leaves the VFE no headroom, so ask for the clock the rear module uses
 * (tested this way). */
#ifndef SKYPHD_OP_CLK
#define SKYPHD_OP_CLK  240000000
#endif
/* requested_link_bit_rate_mbps (0x0820, 16.16): op_sys_clk in whole MHz, as
 * the SMIA driver writes it when the sensor has no automatic D-PHY control. */
#define SKYPHD_LINK_MBPS (OP_SYS_KHZ / 1000)

/* ---- CSI-2 ---- */
#ifndef SKYPHD_LANES
#define SKYPHD_LANES 1      /* power-on 0x0114; 2 only if 0x1602 allows it */
#endif
#if defined(SKYPHD_LANE_MASK)
/* given on the command line */
#elif SKYPHD_LANES == 1
#define SKYPHD_LANE_MASK 0x3   /* data lane 0 + clock lane */
#elif SKYPHD_LANES == 2
#define SKYPHD_LANE_MASK 0x7   /* as the bacon OV5648 front on CSIPHY2 */
#else
#error "SKYPHD_LANES must be 1 or 2"
#endif
#ifndef SKYPHD_LANE_ASSIGN
#define SKYPHD_LANE_ASSIGN 0x4320 /* CSID lane i <- CSIPHY lane nibble i */
#endif
#ifndef SKYPHD_SETTLE
#define SKYPHD_SETTLE 20    /* 100 ns at the 200 MHz CSIPHY timer */
#endif

/* ---- timing ---- */
#ifndef SKYPHD_LINE_LEN
#define SKYPHD_LINE_LEN   SKYPHD_MODULE_LIMIT_MIN_LINE_LENGTH_PCK   /* 1602 */
#endif
#define SKYPHD_FINE_INT   (SKYPHD_LINE_LEN - 498)
#ifndef SKYPHD_FRAME_LEN
#define SKYPHD_FRAME_LEN  1040
#endif
#define SKYPHD_FPS ((float)SKYPHD_VT_CLK / ((float)SKYPHD_LINE_LEN * (float)SKYPHD_FRAME_LEN))

/* ---- geometry ----
 * The digital crop (0x0408-0x040f) powers on at 1412x796 and the sensor
 * stops producing frames when the output is taller than it, so every mode
 * sets it to the window. Windows centred on the default window (rows
 * 308-1103, centre row 705). */
#ifndef SKYPHD_Y_BASE
#define SKYPHD_Y_BASE 226
#endif
#define FULL_W  1408
#define FULL_H  960
#define ARRAY_W (LIM_X_ADDR_MAX + 1)         /* 1412 */
#define M0_W SKYPHD_MODULE_MODE0_OUT_W       /* 1408x960, stills */
#define M0_H SKYPHD_MODULE_MODE0_OUT_H
#define M1_W SKYPHD_MODULE_MODE2_OUT_W       /* 1280x960, 4:3 preview */
#define M1_H SKYPHD_MODULE_MODE2_OUT_H
#define M2_W SKYPHD_MODULE_MODE1_OUT_W       /* 1408x792, 16:9 video */
#define M2_H SKYPHD_MODULE_MODE1_OUT_H
#define X0(w) (((ARRAY_W - (w)) / 2) & ~1)
#define Y0(h) ((SKYPHD_Y_BASE + (FULL_H - (h)) / 2) & ~1)

/* ---- orientation and Bayer order ---- */
#ifndef SKYPHD_ORIENTATION
#define SKYPHD_ORIENTATION 0  /* 0x0101: no flip or mirror for the front module */
#endif
#ifndef SKYPHD_PIXEL_ORDER
#define SKYPHD_PIXEL_ORDER 0  /* 0x0006 placeholder: 0 GRBG, 1 RGGB, 2 BGGR, 3 GBRG */
#endif
#ifndef SKYPHD_FOURCC
#if SKYPHD_ORIENTATION != 0
#error "set SKYPHD_FOURCC by hand when the image is flipped"
#elif SKYPHD_PIXEL_ORDER == 0
#define SKYPHD_FOURCC 0x30314142 /* 'BA10' = V4L2_PIX_FMT_SGRBG10 */
#elif SKYPHD_PIXEL_ORDER == 1
#define SKYPHD_FOURCC 0x30314752 /* 'RG10' = V4L2_PIX_FMT_SRGGB10 */
#elif SKYPHD_PIXEL_ORDER == 2
#define SKYPHD_FOURCC 0x30314742 /* 'BG10' = V4L2_PIX_FMT_SBGGR10 */
#else
#define SKYPHD_FOURCC 0x30314247 /* 'GB10' = V4L2_PIX_FMT_SGBRG10 */
#endif
#endif
#ifndef SKYPHD_MOUNT_ANGLE
#define SKYPHD_MOUNT_ANGLE 270
#endif

/* ---- analog gain: gain = (m0 * code + c0) / (m1 * code + c1) ----
 * PLACEHOLDERS until 0x0084-0x0093 are read (code min/max/step, then
 * m0, c0, m1, c1 as signed 16-bit values). */
#ifndef SKYPHD_AG_M0
#define SKYPHD_AG_M0 1
#define SKYPHD_AG_C0 0
#define SKYPHD_AG_M1 0
#define SKYPHD_AG_C1 32
#define SKYPHD_AG_CODE_MIN 32
#define SKYPHD_AG_CODE_MAX 256
#define SKYPHD_AG_CODE_STEP 1
#endif
#define AG_GAIN(c) ((float)(SKYPHD_AG_M0 * (c) + SKYPHD_AG_C0) / \
                    (float)(SKYPHD_AG_M1 * (c) + SKYPHD_AG_C1))
#ifndef SKYPHD_MAX_GAIN
#define SKYPHD_MAX_GAIN 8.0f    /* analog gain at code max, at most */
#endif

/* ---- compile-time checks against the sensor limits ---- */
_Static_assert(SKYPHD_PRE_DIV >= LIM_PRE_DIV_MIN && SKYPHD_PRE_DIV <= LIM_PRE_DIV_MAX, "pre_pll_div");
_Static_assert(PLL_IP_KHZ >= LIM_PLL_IP_MIN_KHZ && PLL_IP_KHZ <= LIM_PLL_IP_MAX_KHZ, "PLL input");
_Static_assert(SKYPHD_PLL_MUL >= LIM_PLL_MUL_MIN && SKYPHD_PLL_MUL <= LIM_PLL_MUL_MAX, "multiplier");
_Static_assert(PLL_OP_KHZ >= LIM_PLL_OP_MIN_KHZ && PLL_OP_KHZ <= LIM_PLL_OP_MAX_KHZ, "PLL output");
_Static_assert(SKYPHD_VT_SYS_DIV <= LIM_SYS_DIV_MAX && SKYPHD_OP_SYS_DIV <= LIM_SYS_DIV_MAX, "sys div");
_Static_assert(VT_SYS_KHZ >= LIM_SYS_MIN_KHZ && VT_SYS_KHZ <= LIM_SYS_MAX_KHZ, "vt_sys_clk");
_Static_assert(OP_SYS_KHZ >= LIM_SYS_MIN_KHZ && OP_SYS_KHZ <= LIM_SYS_MAX_KHZ, "op_sys_clk");
_Static_assert(SKYPHD_VT_PIX_DIV >= LIM_PIX_DIV_MIN && SKYPHD_VT_PIX_DIV <= LIM_PIX_DIV_MAX, "vt_pix_div");
_Static_assert(SKYPHD_OP_PIX_DIV >= LIM_PIX_DIV_MIN && SKYPHD_OP_PIX_DIV <= LIM_PIX_DIV_MAX, "op_pix_div");
_Static_assert(VT_PIX_KHZ >= LIM_PIX_MIN_KHZ && VT_PIX_KHZ <= LIM_PIX_MAX_KHZ, "vt_pix_clk");
_Static_assert(OP_PIX_KHZ >= LIM_PIX_MIN_KHZ && OP_PIX_KHZ <= LIM_PIX_MAX_KHZ, "op_pix_clk");
_Static_assert(OP_SYS_KHZ / SKYPHD_LANES <= BOARD_MAX_LANE_KBPS, "lane rate above MaxCsiFreqMhz");
_Static_assert(SKYPHD_MODULE_LIMIT_INTEGRATION_TIME_CAPABILITY == 0, "fine integration is fixed");
_Static_assert(SKYPHD_LINE_LEN != 1602 || SKYPHD_FINE_INT == 0x0450, "idle block fine_integration_time follows line 1602");
_Static_assert(SKYPHD_LINE_LEN >= FULL_W + LIM_MIN_LINE_BLANK, "line blanking");
_Static_assert(SKYPHD_FRAME_LEN >= FULL_H + LIM_MIN_FRAME_BLANK && SKYPHD_FRAME_LEN >= LIM_MIN_FRAME_LINES, "frame blanking");
/* The RAW10 line (+ packet header/footer and ~500 bits of HS entry/exit)
 * must leave the lane within one line period. */
_Static_assert((long long)SKYPHD_LINE_LEN * OP_SYS_KHZ >=
               (long long)(FULL_W * 10 + 48 + 500) * VT_PIX_KHZ, "CSI-2 bandwidth");
_Static_assert((SKYPHD_Y_BASE & 1) == 0 && (X0(M0_W) & 1) == 0 && (X0(M1_W) & 1) == 0 &&
               (Y0(M2_H) & 1) == 0, "windows on even rows and columns");
_Static_assert(Y0(FULL_H) + FULL_H - 1 <= LIM_Y_ADDR_MAX, "window inside the address range");
_Static_assert(SKYPHD_AG_M0 == 0 || SKYPHD_AG_M1 == 0, "SMIA: m0 or m1 is zero");

/* ---- power: as the PEP of CAMF (L17, GPIO 18, LVS2, MCLK2) ---- */
static struct k_power_setting power_up[] = {
    { .seq_type = SEQ_VREG, .seq_val = VREG_VANA, .config_val = 0,         .delay = 1 },
    { .seq_type = SEQ_GPIO, .seq_val = GPIO_VDIG, .config_val = GPIO_HIGH, .delay = 1 },
    { .seq_type = SEQ_VREG, .seq_val = VREG_VIO,  .config_val = 0,         .delay = 1 },
    { .seq_type = SEQ_CLK,  .seq_val = 0,         .config_val = SKYPHD_EXTCLK_HZ, .delay = 10 },
};

static struct k_power_setting power_down[] = {
    { .seq_type = SEQ_CLK,  .seq_val = 0,         .config_val = 0,         .delay = 1 },
    { .seq_type = SEQ_VREG, .seq_val = VREG_VIO,  .config_val = 0,         .delay = 1 },
    { .seq_type = SEQ_GPIO, .seq_val = GPIO_VDIG, .config_val = GPIO_LOW,  .delay = 1 },
    { .seq_type = SEQ_VREG, .seq_val = VREG_VANA, .config_val = 0,         .delay = 0 },
};

static struct k_slave_info slave_info = {
    .sensor_name = SENSOR_NAME,
    .camera_id = 1,
    .slave_addr = 0x20,
    .addr_type = I2C_WORD_ADDR,
    .sensor_id_info = { .sensor_id_reg_addr = 0x0000, .sensor_id = 0x6a30 },
    .power_setting_array = {
        .power_setting = power_up, .size = COUNT(power_up),
        .power_down_setting = power_down, .size_down = COUNT(power_down),
    },
};

static struct k_sensor_init_params init_params = {
    .modes_supported = 0,
    .position = 1,              /* front */
    .sensor_mount_angle = SKYPHD_MOUNT_ANGLE,
};

static struct b_output output = { OUT_BAYER, CONN_MIPI, RAW_10BIT };
static struct b_output_reg_addr output_reg_addr = { 0x034c, 0x034e, 0x0342, 0x0340 };
static struct b_exp_gain_info exp_gain_info = { 0x0202, 0x0204, LIM_MARGIN };
static struct b_aec_info aec_info = { .max_gain = SKYPHD_MAX_GAIN, .max_linecount = 65535 - LIM_MARGIN };

/* 1408x960 at 1.75 um = 2.46 x 1.68 mm behind 1.96 mm: 64.3 x 46.4 degrees. */
static struct b_lens_info lens_info = {
    SKYPHD_MODULE_FOCAL_LENGTH_X100 / 100.0f, SKYPHD_MODULE_PIXEL_SIZE_X1000 / 1000.0f,
    SKYPHD_MODULE_F_NUMBER_X100 / 100.0f, 1.2f, 64.3f, 46.4f,
};

static struct k_csi_lane_params csi_lane_params = {
    .csi_lane_assign = SKYPHD_LANE_ASSIGN, .csi_lane_mask = SKYPHD_LANE_MASK, .csi_if = 1,
    .csid_core = { 2, 0 }, .csi_phy_sel = 2,
};

/* RAW10 pixels on CID 0 and the SMIA embedded data lines (data type 0x12)
 * on CID 1: the daemon links the HAL metadata stream to the META port. */
static struct k_csid_vc_cfg cid_cfg[] = {
    { .cid = 0, .dt = 0x2b, .decode_format = CSI_DECODE_10BIT },
    { .cid = 1, .dt = 0x12, .decode_format = CSI_DECODE_8BIT },
};

static uint32_t fourcc_raw10[] = { SKYPHD_FOURCC };
static uint32_t fourcc_meta[] = { 0x4154454d /* 'META' */ };
static struct b_stream_info stream_info[] = {
    { .vc_cfg_size = 1, .vc_cfg = &cid_cfg[0], .pix_fmt_fourcc = fourcc_raw10 },
    { .vc_cfg_size = 1, .vc_cfg = &cid_cfg[1], .pix_fmt_fourcc = fourcc_meta },
};
static struct b_stream_info_array stream_info_array = { stream_info, COUNT(stream_info) };

/* ---- register settings ---- */
#define R(a, d) { (a), (d), 0 }
#define R16(a, v) R((a), ((v) >> 8) & 0xff), R((a) + 1, (v) & 0xff)

static struct k_i2c_reg_array init_regs[] = {
    SKYPHD_MODULE_IDLE,
    R(0x0101, SKYPHD_ORIENTATION),
    R16(0x0112, 0x0a0a),                       /* RAW10 */
    R(0x0114, SKYPHD_LANES - 1),
    R16(0x0136, SKYPHD_EXTCLK_8_8),
    R16(0x0300, SKYPHD_VT_PIX_DIV),
    R16(0x0302, SKYPHD_VT_SYS_DIV),
    R16(0x0304, SKYPHD_PRE_DIV),
    R16(0x0306, SKYPHD_PLL_MUL),
    R16(0x0308, SKYPHD_OP_PIX_DIV),
    R16(0x030a, SKYPHD_OP_SYS_DIV),
    /* D-PHY timing computed by the sensor from the link rate (UI mode). */
    R16(0x0820, SKYPHD_LINK_MBPS), R16(0x0822, 0x0000),
    R(0x0808, 0x01),
};

static struct k_i2c_reg_setting init_settings[] = {
    { init_regs, COUNT(init_regs), I2C_WORD_ADDR, I2C_BYTE_DATA, 10 },
};
static struct b_reg_settings_array init_settings_array = { init_settings, COUNT(init_settings) };

static struct k_i2c_reg_array start_regs[] = {
#if SKYPHD_MODULE_STANDBY_EXIT_COUNT
    SKYPHD_MODULE_STANDBY_EXIT,
#endif
    R(0x0100, 0x01),
};
static struct k_i2c_reg_array stop_regs[] = {
    R(0x0100, 0x00),
#if SKYPHD_MODULE_STANDBY_ENTER_COUNT
    SKYPHD_MODULE_STANDBY_ENTER,
#endif
};
static struct k_i2c_reg_array groupon_regs[]  = { R(0x0104, 0x01) };
static struct k_i2c_reg_array groupoff_regs[] = { R(0x0104, 0x00) };
static struct k_i2c_reg_setting start_settings = { start_regs, COUNT(start_regs), I2C_WORD_ADDR, I2C_BYTE_DATA, 10 };
static struct k_i2c_reg_setting stop_settings = { stop_regs, COUNT(stop_regs), I2C_WORD_ADDR, I2C_BYTE_DATA, 10 };
static struct k_i2c_reg_setting groupon_settings = { groupon_regs, 1, I2C_WORD_ADDR, I2C_BYTE_DATA, 0 };
static struct k_i2c_reg_setting groupoff_settings = { groupoff_regs, 1, I2C_WORD_ADDR, I2C_BYTE_DATA, 0 };

/* Frame/line length, the module's fine integration time for that line
 * length, window, output size, then a starting exposure. No binning,
 * scaling or digital crop registers: the module modes use none, and they
 * stay at their power-on values. */
#define MODE_REGS(w, h)                                                    \
    R16(0x0340, SKYPHD_FRAME_LEN), R16(0x0342, SKYPHD_LINE_LEN),           \
    R16(0x0200, SKYPHD_FINE_INT),                                          \
    R16(0x0344, X0(w)), R16(0x0346, Y0(h)),                                \
    R16(0x0348, X0(w) + (w) - 1), R16(0x034a, Y0(h) + (h) - 1),            \
    R16(0x034c, (w)), R16(0x034e, (h)),                                    \
    R16(0x0408, 0), R16(0x040a, 0),                                        \
    R16(0x040c, (w)), R16(0x040e, (h)),                                    \
    R16(0x0202, 800), R16(0x0204, SKYPHD_AG_CODE_MIN)

static struct k_i2c_reg_array res0_regs[] = { MODE_REGS(M0_W, M0_H) };
static struct k_i2c_reg_array res1_regs[] = { MODE_REGS(M1_W, M1_H) };
static struct k_i2c_reg_array res2_regs[] = { MODE_REGS(M2_W, M2_H) };

static struct k_i2c_reg_setting res_settings[] = {
    { res0_regs, COUNT(res0_regs), I2C_WORD_ADDR, I2C_BYTE_DATA, 0 },
    { res1_regs, COUNT(res1_regs), I2C_WORD_ADDR, I2C_BYTE_DATA, 0 },
    { res2_regs, COUNT(res2_regs), I2C_WORD_ADDR, I2C_BYTE_DATA, 0 },
};
static struct b_reg_settings_array res_settings_array = { res_settings, COUNT(res_settings) };

static uint32_t res_cfg_type[] = {
    RES_CFG_STOP_STREAM, RES_CFG_CSIPHY, RES_CFG_LANE, RES_CFG_CHROMATIX,
    RES_CFG_SEND_EVENT, RES_CFG_CSID, RES_CFG_NEW_RESOLUTION,
};
static struct b_res_cfg_table res_cfg_table = { res_cfg_type, COUNT(res_cfg_type) };

#define OUT_INFO(w, h) {                                                   \
    .x_output = (w), .y_output = (h),                                      \
    .line_length_pclk = SKYPHD_LINE_LEN, .frame_length_lines = SKYPHD_FRAME_LEN, \
    .vt_pixel_clk = SKYPHD_VT_CLK, .op_pixel_clk = SKYPHD_OP_CLK, \
    .binning_factor = 1, .binning_method = 0,                              \
    .min_fps = 7.5f, .max_fps = SKYPHD_FPS, .mode = 1,                     \
}
static struct b_out_info out_info[] = {
    OUT_INFO(M0_W, M0_H), OUT_INFO(M1_W, M1_H), OUT_INFO(M2_W, M2_H),
};
static struct b_out_info_array out_info_array = { out_info, COUNT(out_info) };

static struct k_csi2_params csi2_params = {
    .csid_params = {
        .lane_cnt = SKYPHD_LANES, .lane_assign = 0x0, .phy_sel = 2,
        .lut_params = { .num_cid = 2, .vc_cfg = { &cid_cfg[0], &cid_cfg[1] } },
    },
    .csiphy_params = { .lane_cnt = SKYPHD_LANES, .settle_cnt = SKYPHD_SETTLE, .lane_mask = 0x0,
                       .combo_mode = 0, .csid_core = 2 },
};
static struct k_csi2_params *csi_params[] = { &csi2_params, &csi2_params, &csi2_params };
static struct b_csi_params_array csi_params_array = { csi_params, COUNT(csi_params) };

static struct b_crop crop[] = { { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 } };
static struct b_crop_array crop_array = { crop, COUNT(crop) };

/* Module tuning (libchromatix_skyphd_*, vendor tree): colour matrices,
 * white balance references and colour shading of the Skype_HD module. */
static struct b_chromatix chromatix[] = {
    { "libchromatix_skyphd_common.so", "libchromatix_skyphd_preview.so",
      "libchromatix_skyphd_snapshot.so", "libchromatix_skyphd_default_video.so",
      "libchromatix_skyphd_liveshot.so" },
    { "libchromatix_skyphd_common.so", "libchromatix_skyphd_preview.so",
      "libchromatix_skyphd_snapshot.so", "libchromatix_skyphd_default_video.so",
      "libchromatix_skyphd_liveshot.so" },
    { "libchromatix_skyphd_common.so", "libchromatix_skyphd_preview_binning.so",
      "libchromatix_skyphd_preview_binning.so", "libchromatix_skyphd_video_binning.so",
      "libchromatix_skyphd_liveshot.so" },
};
static struct b_chromatix_array chromatix_array = { chromatix, COUNT(chromatix) };

static struct b_meta_out_info_array meta_out_info_array = { 0, 0 };

_Static_assert(COUNT(res_settings) == COUNT(out_info) && COUNT(csi_params) == COUNT(out_info) &&
               COUNT(crop) == COUNT(out_info) && COUNT(chromatix) == COUNT(out_info), "mode tables");

/* ---- exposure ---- */
static uint16_t gain_to_code(float g)
{
    float c;
    uint16_t code;

    if (g < 1.0f)
        g = 1.0f;
    /* (m0 c + c0) / (m1 c + c1) = g  ->  c = (c0 - g c1) / (g m1 - m0) */
    c = ((float)SKYPHD_AG_C0 - g * (float)SKYPHD_AG_C1) /
        (g * (float)SKYPHD_AG_M1 - (float)SKYPHD_AG_M0);
    if (c < (float)SKYPHD_AG_CODE_MIN)
        c = (float)SKYPHD_AG_CODE_MIN;
    if (c > (float)SKYPHD_AG_CODE_MAX)
        c = (float)SKYPHD_AG_CODE_MAX;
    code = (uint16_t)c;   /* rounds down: the ISP makes up the rest */
    code -= (uint16_t)((code - SKYPHD_AG_CODE_MIN) % SKYPHD_AG_CODE_STEP);
    return code;
}

static int32_t skyphd_calc_exposure(float real_gain, uint16_t line_count, struct b_exposure *out)
{
    float sensor_gain;

    out->reg_gain = gain_to_code(real_gain);
    sensor_gain = AG_GAIN(out->reg_gain);
    out->sensor_real_gain = sensor_gain;
    out->sensor_digital_gain = 1.0f;
    out->digital_gain = real_gain / sensor_gain;
    if (out->digital_gain < 1.0f)
        out->digital_gain = 1.0f;
    out->line_count = line_count < LIM_COARSE_MIN ? LIM_COARSE_MIN : line_count;
    return 0;
}

static int32_t skyphd_fill_exposure(uint16_t gain, uint32_t line, uint32_t fl_lines,
                                    int32_t luma_avg, uint32_t fgain,
                                    struct k_i2c_reg_setting *rs)
{
    struct k_i2c_reg_array *r = rs->reg_setting;
    uint16_t n = 0;

    (void)luma_avg;
    (void)fgain;
    if (line < LIM_COARSE_MIN)
        line = LIM_COARSE_MIN;
    if (fl_lines < line + LIM_MARGIN)
        fl_lines = line + LIM_MARGIN;
    if (fl_lines > 0xffff)
        fl_lines = 0xffff;
    r[n].reg_addr = 0x0104; r[n].reg_data = 0x01; r[n++].delay = 0;
    r[n].reg_addr = 0x0340; r[n].reg_data = (fl_lines >> 8) & 0xff; r[n++].delay = 0;
    r[n].reg_addr = 0x0341; r[n].reg_data = fl_lines & 0xff; r[n++].delay = 0;
    r[n].reg_addr = 0x0202; r[n].reg_data = (line >> 8) & 0xff; r[n++].delay = 0;
    r[n].reg_addr = 0x0203; r[n].reg_data = line & 0xff; r[n++].delay = 0;
    r[n].reg_addr = 0x0204; r[n].reg_data = (gain >> 8) & 0xff; r[n++].delay = 0;
    r[n].reg_addr = 0x0205; r[n].reg_data = gain & 0xff; r[n++].delay = 0;
    r[n].reg_addr = 0x0104; r[n].reg_data = 0x00; r[n++].delay = 0;
    rs->size = n;
    rs->addr_type = I2C_WORD_ADDR;
    rs->data_type = I2C_BYTE_DATA;
    rs->delay = 0;
    return 0;
}

static struct b_exposure_func_table exposure_func_table = {
    skyphd_calc_exposure, skyphd_fill_exposure,
};

static struct b_sensor_lib sensor_lib = {
    .sensor_slave_info = &slave_info,
    .sensor_init_params = &init_params,
    .actuator_name = 0,
    .eeprom_name = 0,
    .sensor_output = &output,
    .output_reg_addr = &output_reg_addr,
    .exp_gain_info = &exp_gain_info,
    .aec_info = &aec_info,
    .snapshot_exp_wait_frames = 1,
    .sensor_num_frame_skip = 2,
    .sensor_num_HDR_frame_skip = 1,
    .exposure_table_size = 10,
    .default_lens_info = &lens_info,
    .csi_lane_params = &csi_lane_params,
    .csi_cid_params = cid_cfg,
    .sensor_stream_info_array = &stream_info_array,
    .csi_cid_params_size = COUNT(cid_cfg),
    .init_settings_array = &init_settings_array,
    .start_settings = &start_settings,
    .stop_settings = &stop_settings,
    .groupon_settings = &groupon_settings,
    .groupoff_settings = &groupoff_settings,
    .sensor_res_cfg_table = &res_cfg_table,
    .res_settings_array = &res_settings_array,
    .out_info_array = &out_info_array,
    .csi_params_array = &csi_params_array,
    .crop_params_array = &crop_array,
    .chromatix_array = &chromatix_array,
    .meta_data_out_info_array = &meta_out_info_array,
    .exposure_func_table = &exposure_func_table,
    .unknown_a0 = 2,
};

__attribute__((visibility("default"))) void *skyphd_open_lib(void)
{
    return &sensor_lib;
}
