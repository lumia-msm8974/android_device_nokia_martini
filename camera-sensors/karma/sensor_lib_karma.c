/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Sensor plug-in for the rear "Karma" camera module of the Nokia Lumia 930
 * (and 1520), for the 2014 MSM8974 mm-camera2 daemon of OnePlus One (bacon).
 * Written against camera-sensors/bacon_sensor_abi.h.
 *
 * Module: SMIA++ module 0x4541 (manufacturer 0x10, revision 7 on the test
 * phone), Toshiba die 0x2170, CCI master 0 at 7-bit 0x10, 4 CSI-2 lanes,
 * data pedestal 16 at 8 bits. The image is turned by 180 degrees in the
 * sensor (0x0101 = 3), which makes the Bayer order GBRG.
 *
 * The module register settings (defect correction, analog setup, calibration
 * loads, standby entry and exit) are in karma_module_regs.h, applied in the
 * order idle -> standby exit -> stream on, and standby entry after stream
 * off. Clock tree, windows and timing below come from the SMIA++ limit
 * registers of the sensor.
 *
 * Mode 0 (snapshot/ZSL): 4:3 still, 4992x3744 (x 197..5188, y 4..3747, full
 * array height), no binning or scaling, RAW10, line_length 12000 (25 us
 * lines, about 500 Mbit/s per lane), frame_length 3770: 10.6 frames/s. The
 * dual VFE (persist.camera.isp.dualisp=1) takes half the columns each. The
 * sensor scaler (0x0400) gave an almost black picture, so it stays off.
 *
 * Mode 1 (preview/video): 4:3 viewfinder, 4992x3744 (x 197..5188,
 * y 4..3747), 2x2 binning (0x0900 = 1, 0x0901 = 0x22), 2496x1872 RAW10. The
 * power-on PLL is kept for the pixel array (EXTCLK 9.6 MHz, pre-div 2,
 * multiplier 1600, vt_sys_div 2, vt_pix_div 8: vt_pix_clk 480 MHz, measured
 * as 63 frames/s with the default 4000 x 1902 timing). line_length 8000
 * halves the line rate (31.5 frames/s), so the output PLL can drop to
 * multiplier 1000 (op_sys 2400 MHz over 4 lanes, about 600 Mbit/s per lane)
 * with op_pix_div 10 for RAW10.
 *
 * Mode 2 (16:9 video): 5376x3024 (x 5..5380, y 364..3387), 2x2 binning,
 * 2688x1512 RAW10, line_length 8000, frame_length 2000: 30 frames/s.
 *
 * Copyright (c) 2026, Lorenzo Giannoccaro.
 */
#include "../bacon_sensor_abi.h"
#include "karma_module_regs.h"

#define SENSOR_NAME "karma"

#define KARMA_X_OUT       2496   /* mode 1, binned 4:3 viewfinder */
#define KARMA_Y_OUT       1872
#define KARMA_LINE_LEN    8000
#define KARMA_FRAME_LEN   1902
#define KARMA_FULL_X      4992   /* mode 0, 4:3 still */
#define KARMA_FULL_Y      3744
#define KARMA_FULL_LINE   12000
#define KARMA_FULL_FRAME  3770
#define KARMA_V_X_OUT     2688   /* mode 2, binned 16:9 video */
#define KARMA_V_Y_OUT     1512
#define KARMA_V_FRAME_LEN 2000   /* line 8000: 30 frames/s */
#define KARMA_VT_CLK      480000000
#define KARMA_OP_CLK      240000000
#define KARMA_MARGIN      8      /* coarse_integration_time_max_margin */
#define KARMA_GAIN_CODE_MIN 55   /* gain = code / 55 (m0 1, c1 55) */
#define KARMA_GAIN_CODE_MAX 440

/* ---- power: I/O rail, standby GPIO, analog rail, then MCLK ---- */
static struct k_power_setting power_up[] = {
    { .seq_type = SEQ_VREG, .seq_val = VREG_VIO,  .config_val = 0,         .delay = 1 },
    { .seq_type = SEQ_GPIO, .seq_val = GPIO_VDIG, .config_val = GPIO_HIGH, .delay = 10 },
    { .seq_type = SEQ_VREG, .seq_val = VREG_VANA, .config_val = 0,         .delay = 2 },
    { .seq_type = SEQ_CLK,  .seq_val = 0,         .config_val = 9600000,   .delay = 5 },
};

static struct k_power_setting power_down[] = {
    { .seq_type = SEQ_CLK,  .seq_val = 0,         .config_val = 0,         .delay = 1 },
    { .seq_type = SEQ_VREG, .seq_val = VREG_VANA, .config_val = 0,         .delay = 5 },
    { .seq_type = SEQ_GPIO, .seq_val = GPIO_VDIG, .config_val = GPIO_LOW,  .delay = 1 },
    { .seq_type = SEQ_VREG, .seq_val = VREG_VIO,  .config_val = 0,         .delay = 0 },
};

static struct k_slave_info slave_info = {
    .sensor_name = SENSOR_NAME,
    .actuator_name = "karma",   /* kernel: subdev from qcom,actuator-src */
    .camera_id = 0,
    .slave_addr = 0x20,
    .addr_type = I2C_WORD_ADDR,
    .sensor_id_info = { .sensor_id_reg_addr = 0x0000, .sensor_id = 0x4541 },
    .power_setting_array = {
        .power_setting = power_up, .size = COUNT(power_up),
        .power_down_setting = power_down, .size_down = COUNT(power_down),
    },
};

static struct k_sensor_init_params init_params = {
    .modes_supported = 0,
    .position = 0,              /* back */
    .sensor_mount_angle = 90,   /* with the sensor image turned by 0x0101 = 3 */
};

static struct b_output output = { OUT_BAYER, CONN_MIPI, RAW_10BIT };
static struct b_output_reg_addr output_reg_addr = { 0x034c, 0x034e, 0x0342, 0x0340 };
static struct b_exp_gain_info exp_gain_info = { 0x0202, 0x0204, KARMA_MARGIN };
static struct b_aec_info aec_info = { .max_gain = 8.0f, .max_linecount = 65535 - KARMA_MARGIN };

/* 4.46 mm, 1.12 um, f/2.43; 5376x3744 area. */
static struct b_lens_info lens_info = { 4.46f, 1.12f, 2.43f, 1.97f, 68.0f, 50.4f };

static struct k_csi_lane_params csi_lane_params = {
    .csi_lane_assign = 0x4320, .csi_lane_mask = 0x1f, .csi_if = 1,
    .csid_core = { 0, 0 }, .csi_phy_sel = 0,
};

/* RAW10 pixels on CID 0 and the SMIA embedded data lines (data type 0x12)
 * on CID 1, as the bacon plug-ins declare them: the daemon links the
 * metadata stream of the HAL to the sensor's META port. */
static struct k_csid_vc_cfg cid_cfg[] = {
    { .cid = 0, .dt = 0x2b, .decode_format = CSI_DECODE_10BIT },
    { .cid = 1, .dt = 0x12, .decode_format = CSI_DECODE_8BIT },
};

/* GBRG with the 180 degree turn and windows starting on an odd column
 * (checked on the phone against the other three orders). */
#ifndef KARMA_FOURCC
#define KARMA_FOURCC 0x30314247 /* 'GB10' = V4L2_PIX_FMT_SGBRG10 */
#endif
static uint32_t fourcc_rggb10[] = { KARMA_FOURCC };
static uint32_t fourcc_meta[] = { 0x4154454d /* 'META' */ };
static struct b_stream_info stream_info[] = {
    { .vc_cfg_size = 1, .vc_cfg = &cid_cfg[0], .pix_fmt_fourcc = fourcc_rggb10 },
    { .vc_cfg_size = 1, .vc_cfg = &cid_cfg[1], .pix_fmt_fourcc = fourcc_meta },
};
static struct b_stream_info_array stream_info_array = { stream_info, COUNT(stream_info) };

/* ---- register settings ---- */
#define R(a, d) { (a), (d), 0 }
#ifndef KARMA_RED_GAIN
#define KARMA_RED_GAIN 0x0100
#endif
#ifndef KARMA_BLUE_GAIN
#define KARMA_BLUE_GAIN 0x0100
#endif

static struct k_i2c_reg_array init_regs[] = {
    KARMA_MODULE_IDLE,
    R(0x0101, 0x03),                  /* image orientation: H mirror + V flip */
    R(0x0112, 0x0a), R(0x0113, 0x0a), /* RAW10 */
    R(0x0114, 0x03),                  /* 4 lanes */
    R(0x0136, 0x09), R(0x0137, 0x9a), /* EXTCLK 9.6 MHz */
    R(0x0300, 0x00), R(0x0301, 0x08), /* vt_pix_clk_div 8 */
    R(0x0302, 0x00), R(0x0303, 0x02), /* vt_sys_clk_div 2 */
    R(0x0304, 0x00), R(0x0305, 0x02), /* pre_pll_clk_div 2 */
    R(0x0306, 0x06), R(0x0307, 0x40), /* pll_multiplier 1600 */
    R(0x0308, 0x00), R(0x0309, 0x0a), /* op_pix_clk_div 10 */
    R(0x030a, 0x00), R(0x030b, 0x02), /* op_sys_clk_div 2 */
    R(0x030c, 0x00), R(0x030d, 0x02), /* op_pre_pll_clk_div 2 */
    R(0x030e, 0x03), R(0x030f, 0xe8), /* op_pll_multiplier 1000 */
    /* Per-channel digital gain (8.8): blue raised to pull the auto white
     * balance away from the yellow cast seen on the phone. */
    R(0x020e, 0x01), R(0x020f, 0x00), /* green (red row) */
    R(0x0210, KARMA_RED_GAIN >> 8), R(0x0211, KARMA_RED_GAIN & 0xff),
    R(0x0212, KARMA_BLUE_GAIN >> 8), R(0x0213, KARMA_BLUE_GAIN & 0xff),
    R(0x0214, 0x01), R(0x0215, 0x00), /* green (blue row) */
};

static struct k_i2c_reg_setting init_settings[] = {
    { init_regs, COUNT(init_regs), I2C_WORD_ADDR, I2C_BYTE_DATA, 10 },
};
static struct b_reg_settings_array init_settings_array = { init_settings, COUNT(init_settings) };

static struct k_i2c_reg_array start_regs[] = {
    KARMA_MODULE_STANDBY_EXIT,
    R(0x0100, 0x01),
};
static struct k_i2c_reg_array stop_regs[] = {
    R(0x0100, 0x00),
    KARMA_MODULE_STANDBY_ENTER,
};
static struct k_i2c_reg_array groupon_regs[]  = { R(0x0104, 0x01) };
static struct k_i2c_reg_array groupoff_regs[] = { R(0x0104, 0x00) };
static struct k_i2c_reg_setting start_settings = { start_regs, COUNT(start_regs), I2C_WORD_ADDR, I2C_BYTE_DATA, 10 };
static struct k_i2c_reg_setting stop_settings = { stop_regs, COUNT(stop_regs), I2C_WORD_ADDR, I2C_BYTE_DATA, 10 };
static struct k_i2c_reg_setting groupon_settings = { groupon_regs, 1, I2C_WORD_ADDR, I2C_BYTE_DATA, 0 };
static struct k_i2c_reg_setting groupoff_settings = { groupoff_regs, 1, I2C_WORD_ADDR, I2C_BYTE_DATA, 0 };

static struct k_i2c_reg_array res_full_regs[] = {
    R(0x0340, KARMA_FULL_FRAME >> 8), R(0x0341, KARMA_FULL_FRAME & 0xff),
    R(0x0342, KARMA_FULL_LINE >> 8),  R(0x0343, KARMA_FULL_LINE & 0xff),
    R(0x0344, 0x00), R(0x0345, 0xc5),             /* x_addr_start 197 */
    R(0x0346, 0x00), R(0x0347, 0x04),             /* y_addr_start 4 */
    R(0x0348, 0x14), R(0x0349, 0x44),             /* x_addr_end 5188 */
    R(0x034a, 0x0e), R(0x034b, 0xa3),             /* y_addr_end 3747 */
    R(0x034c, KARMA_FULL_X >> 8), R(0x034d, KARMA_FULL_X & 0xff),
    R(0x034e, KARMA_FULL_Y >> 8), R(0x034f, KARMA_FULL_Y & 0xff),
    R(0x0900, 0x00), R(0x0901, 0x11),             /* no binning */
    R(0x0400, 0x00),                              /* no scaling */
    R(0x0408, 0x00), R(0x0409, 0x00), R(0x040a, 0x00), R(0x040b, 0x00),
    R(0x040c, KARMA_FULL_X >> 8), R(0x040d, KARMA_FULL_X & 0xff),
    R(0x040e, KARMA_FULL_Y >> 8), R(0x040f, KARMA_FULL_Y & 0xff),
    R(0x0202, 0x0c), R(0x0203, 0x00),             /* coarse integration 3072 */
    R(0x0204, 0x00), R(0x0205, KARMA_GAIN_CODE_MIN),
};

static struct k_i2c_reg_array res0_regs[] = {
    R(0x0340, KARMA_FRAME_LEN >> 8), R(0x0341, KARMA_FRAME_LEN & 0xff),
    R(0x0342, KARMA_LINE_LEN >> 8),  R(0x0343, KARMA_LINE_LEN & 0xff),
    R(0x0344, 0x00), R(0x0345, 0xc5),             /* x_addr_start 197 */
    R(0x0346, 0x00), R(0x0347, 0x04),             /* y_addr_start 4 */
    R(0x0348, 0x14), R(0x0349, 0x44),             /* x_addr_end 5188 */
    R(0x034a, 0x0e), R(0x034b, 0xa3),             /* y_addr_end 3747 */
    R(0x034c, KARMA_X_OUT >> 8), R(0x034d, KARMA_X_OUT & 0xff),
    R(0x034e, KARMA_Y_OUT >> 8), R(0x034f, KARMA_Y_OUT & 0xff),
    R(0x0900, 0x01), R(0x0901, 0x22),             /* 2x2 binning */
    R(0x0400, 0x00),                              /* no scaling */
    R(0x0408, 0x00), R(0x0409, 0x00), R(0x040a, 0x00), R(0x040b, 0x00),
    R(0x040c, KARMA_X_OUT >> 8), R(0x040d, KARMA_X_OUT & 0xff),
    R(0x040e, KARMA_Y_OUT >> 8), R(0x040f, KARMA_Y_OUT & 0xff),
    R(0x0202, 0x07), R(0x0203, 0x00),             /* coarse integration 1792 */
    R(0x0204, 0x00), R(0x0205, KARMA_GAIN_CODE_MIN),
};

static struct k_i2c_reg_array res1_regs[] = {
    R(0x0340, KARMA_V_FRAME_LEN >> 8), R(0x0341, KARMA_V_FRAME_LEN & 0xff),
    R(0x0342, KARMA_LINE_LEN >> 8),  R(0x0343, KARMA_LINE_LEN & 0xff),
    R(0x0344, 0x00), R(0x0345, 0x05),             /* x_addr_start 5 */
    R(0x0346, 0x01), R(0x0347, 0x6c),             /* y_addr_start 364 */
    R(0x0348, 0x15), R(0x0349, 0x04),             /* x_addr_end 5380 */
    R(0x034a, 0x0d), R(0x034b, 0x3b),             /* y_addr_end 3387 */
    R(0x034c, KARMA_V_X_OUT >> 8), R(0x034d, KARMA_V_X_OUT & 0xff),
    R(0x034e, KARMA_V_Y_OUT >> 8), R(0x034f, KARMA_V_Y_OUT & 0xff),
    R(0x0900, 0x01), R(0x0901, 0x22),             /* 2x2 binning */
    R(0x0400, 0x00),                              /* no scaling */
    R(0x0408, 0x00), R(0x0409, 0x00), R(0x040a, 0x00), R(0x040b, 0x00),
    R(0x040c, KARMA_V_X_OUT >> 8), R(0x040d, KARMA_V_X_OUT & 0xff),
    R(0x040e, KARMA_V_Y_OUT >> 8), R(0x040f, KARMA_V_Y_OUT & 0xff),
    R(0x0202, 0x07), R(0x0203, 0x00),             /* coarse integration 1792 */
    R(0x0204, 0x00), R(0x0205, KARMA_GAIN_CODE_MIN),
};

static struct k_i2c_reg_setting res_settings[] = {
    { res_full_regs, COUNT(res_full_regs), I2C_WORD_ADDR, I2C_BYTE_DATA, 0 },
    { res0_regs, COUNT(res0_regs), I2C_WORD_ADDR, I2C_BYTE_DATA, 0 },
    { res1_regs, COUNT(res1_regs), I2C_WORD_ADDR, I2C_BYTE_DATA, 0 },
};
static struct b_reg_settings_array res_settings_array = { res_settings, COUNT(res_settings) };

static uint32_t res_cfg_type[] = {
    RES_CFG_STOP_STREAM, RES_CFG_CSIPHY, RES_CFG_LANE, RES_CFG_CHROMATIX,
    RES_CFG_SEND_EVENT, RES_CFG_CSID, RES_CFG_NEW_RESOLUTION,
};
static struct b_res_cfg_table res_cfg_table = { res_cfg_type, COUNT(res_cfg_type) };

static struct b_out_info out_info[] = {
    {
        .x_output = KARMA_FULL_X, .y_output = KARMA_FULL_Y,
        .line_length_pclk = KARMA_FULL_LINE, .frame_length_lines = KARMA_FULL_FRAME,
        .vt_pixel_clk = KARMA_VT_CLK, .op_pixel_clk = KARMA_OP_CLK,
        .binning_factor = 1, .binning_method = 0,
        .min_fps = 7.5f, .max_fps = 10.6f, .mode = 1,
    },
    {
        .x_output = KARMA_X_OUT, .y_output = KARMA_Y_OUT,
        .line_length_pclk = KARMA_LINE_LEN, .frame_length_lines = KARMA_FRAME_LEN,
        .vt_pixel_clk = KARMA_VT_CLK, .op_pixel_clk = KARMA_OP_CLK,
        .binning_factor = 2, .binning_method = 0,
        .min_fps = 7.5f, .max_fps = 31.5f, .mode = 1,
    },
    {
        .x_output = KARMA_V_X_OUT, .y_output = KARMA_V_Y_OUT,
        .line_length_pclk = KARMA_LINE_LEN, .frame_length_lines = KARMA_V_FRAME_LEN,
        .vt_pixel_clk = KARMA_VT_CLK, .op_pixel_clk = KARMA_OP_CLK,
        .binning_factor = 2, .binning_method = 0,
        .min_fps = 7.5f, .max_fps = 30.0f, .mode = 1,
    },
};
static struct b_out_info_array out_info_array = { out_info, COUNT(out_info) };

static struct k_csi2_params csi2_mode0 = {
    .csid_params = {
        .lane_cnt = 4, .lane_assign = 0x0, .phy_sel = 0,
        .lut_params = { .num_cid = 2, .vc_cfg = { &cid_cfg[0], &cid_cfg[1] } },
    },
    .csiphy_params = { .lane_cnt = 4, .settle_cnt = 20, .lane_mask = 0x0,
                       .combo_mode = 0, .csid_core = 0 },
};
static struct k_csi2_params *csi_params[] = { &csi2_mode0, &csi2_mode0, &csi2_mode0 };
static struct b_csi_params_array csi_params_array = { csi_params, COUNT(csi_params) };

static struct b_crop crop[] = { { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 } };
static struct b_crop_array crop_array = { crop, COUNT(crop) };

/* Stand-in tuning from the bacon IMX214 (same ISP, 1.12 um BSI) until the
 * Karma one exists. */
static struct b_chromatix chromatix[] = {
    { "libchromatix_karma_common.so", "libchromatix_karma_preview.so",
      "libchromatix_karma_snapshot.so", "libchromatix_karma_default_video.so",
      "libchromatix_karma_liveshot.so" },
    { "libchromatix_karma_common.so", "libchromatix_karma_preview_binning.so",
      "libchromatix_karma_preview_binning.so", "libchromatix_karma_video_binning.so",
      "libchromatix_karma_liveshot.so" },
    { "libchromatix_karma_common.so", "libchromatix_karma_preview_binning.so",
      "libchromatix_karma_preview_binning.so", "libchromatix_karma_video_binning.so",
      "libchromatix_karma_liveshot.so" },
};
static struct b_chromatix_array chromatix_array = { chromatix, COUNT(chromatix) };

static struct b_meta_out_info_array meta_out_info_array = { 0, 0 };

/* ---- exposure ---- */
static uint16_t gain_to_code(float g)
{
    float c = g * (float)KARMA_GAIN_CODE_MIN;

    if (c < (float)KARMA_GAIN_CODE_MIN)
        c = (float)KARMA_GAIN_CODE_MIN;
    if (c > (float)KARMA_GAIN_CODE_MAX)
        c = (float)KARMA_GAIN_CODE_MAX;
    return (uint16_t)c;
}

static int32_t karma_calc_exposure(float real_gain, uint16_t line_count, struct b_exposure *out)
{
    float sensor_gain;

    out->reg_gain = gain_to_code(real_gain);
    sensor_gain = (float)out->reg_gain / (float)KARMA_GAIN_CODE_MIN;
    out->sensor_real_gain = sensor_gain;
    out->sensor_digital_gain = 1.0f;
    out->digital_gain = real_gain / sensor_gain;
    if (out->digital_gain < 1.0f)
        out->digital_gain = 1.0f;
    out->line_count = line_count;
    return 0;
}

static int32_t karma_fill_exposure(uint16_t gain, uint32_t line, uint32_t fl_lines,
                                   int32_t luma_avg, uint32_t fgain,
                                   struct k_i2c_reg_setting *rs)
{
    struct k_i2c_reg_array *r = rs->reg_setting;
    uint16_t n = 0;

    (void)luma_avg;
    (void)fgain;
    if (fl_lines < line + KARMA_MARGIN)
        fl_lines = line + KARMA_MARGIN;
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
    karma_calc_exposure, karma_fill_exposure,
};

static struct b_sensor_lib sensor_lib = {
    .sensor_slave_info = &slave_info,
    .sensor_init_params = &init_params,
    .actuator_name = "karma",   /* libactuator_karma.so */
    .eeprom_name = 0,
    .sensor_output = &output,
    .output_reg_addr = &output_reg_addr,
    .exp_gain_info = &exp_gain_info,
    .aec_info = &aec_info,
    .snapshot_exp_wait_frames = 1,
    .sensor_num_frame_skip = 2,
    .sensor_num_HDR_frame_skip = 2,
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

__attribute__((visibility("default"))) void *karma_open_lib(void)
{
    return &sensor_lib;
}
