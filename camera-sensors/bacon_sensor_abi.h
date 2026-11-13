/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Independently specified 32-bit sensor plug-in interface of the 2014
 * MSM8974 mm-camera2 daemon shipped with OnePlus One (bacon) / OPPO Find 7
 * (libmmcamera2_sensor_modules.so, SHA-256 0159cd48...). This is not the
 * Qualcomm proprietary SDK header: every offset below was measured on the
 * shipped libmmcamera_imx214.so / libmmcamera_ov5648.so descriptors and on
 * the consumer's access sites (see README.md), and is pinned with
 * compile-time checks. Field names are descriptive; fields marked
 * "unknown" are kept zero by our plug-ins.
 *
 * Kernel-facing records (slave info, power settings, I2C arrays, CSI
 * parameters) mirror include/media/msm_cam_sensor.h of the 3.4 MSM8974
 * kernel; their sizes are checked too.
 */
#ifndef LUMIA8974_BACON_SENSOR_ABI_H
#define LUMIA8974_BACON_SENSOR_ABI_H

typedef __UINT8_TYPE__ uint8_t;
typedef __UINT16_TYPE__ uint16_t;
typedef __UINT32_TYPE__ uint32_t;
typedef __INT32_TYPE__ int32_t;

#define ABI_OFFSET(t, m, v) _Static_assert(__builtin_offsetof(t, m) == (v), #t "." #m " ABI offset")
#define ABI_SIZE(t, v) _Static_assert(sizeof(t) == (v), #t " ABI size")
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

/* ---- kernel records (include/media/msm_cam_sensor.h, 3.4) ---- */
enum { I2C_BYTE_ADDR = 1, I2C_WORD_ADDR = 2 };
enum { I2C_BYTE_DATA = 1, I2C_WORD_DATA = 2 };
enum { SEQ_CLK = 0, SEQ_GPIO = 1, SEQ_VREG = 2, SEQ_I2C_MUX = 3 };
enum { GPIO_RESET = 0, GPIO_STANDBY = 1, GPIO_AF_PWDM = 2, GPIO_VIO = 3,
       GPIO_VANA = 4, GPIO_VDIG = 5, GPIO_VAF = 6 };
enum { VREG_VDIG = 0, VREG_VIO = 1, VREG_VANA = 2, VREG_VAF = 3 };
#define GPIO_LOW 0
#define GPIO_HIGH 2
#define CSI_DECODE_8BIT 1
#define CSI_DECODE_10BIT 2
#define KERNEL_MAX_CID 16

struct k_i2c_reg_array { uint16_t reg_addr, reg_data; uint32_t delay; };
struct k_i2c_reg_setting {
    struct k_i2c_reg_array *reg_setting;
    uint16_t size;
    uint32_t addr_type, data_type;
    uint16_t delay;
};
struct k_power_setting {
    uint32_t seq_type;
    uint16_t seq_val;
    int32_t config_val;
    uint16_t delay;
    void *data[10];
};
struct k_power_setting_array {
    struct k_power_setting *power_setting;
    uint16_t size;
    struct k_power_setting *power_down_setting;
    uint16_t size_down;
};
struct k_sensor_id_info { uint16_t sensor_id_reg_addr, sensor_id; };
struct k_sensor_init_params { int32_t modes_supported; uint32_t position, sensor_mount_angle; };
struct k_slave_info {
    char sensor_name[32], eeprom_name[32], actuator_name[32];
    uint32_t camera_id;
    uint16_t slave_addr;
    uint32_t addr_type;
    struct k_sensor_id_info sensor_id_info;
    struct k_power_setting_array power_setting_array;
    uint8_t is_init_params_valid;
    struct k_sensor_init_params sensor_init_params;
};
struct k_csid_vc_cfg { uint8_t cid, dt, decode_format; };
struct k_csid_lut_params { uint8_t num_cid; struct k_csid_vc_cfg *vc_cfg[KERNEL_MAX_CID]; };
struct k_csid_params { uint8_t lane_cnt; uint16_t lane_assign; uint8_t phy_sel; struct k_csid_lut_params lut_params; };
struct k_csiphy_params { uint8_t lane_cnt, settle_cnt; uint16_t lane_mask; uint8_t combo_mode, csid_core; };
struct k_csi2_params { struct k_csid_params csid_params; struct k_csiphy_params csiphy_params; };
struct k_csi_lane_params { uint16_t csi_lane_assign; uint8_t csi_lane_mask, csi_if, csid_core[2], csi_phy_sel; };

ABI_SIZE(struct k_i2c_reg_array, 8);
ABI_SIZE(struct k_i2c_reg_setting, 20);
ABI_SIZE(struct k_power_setting, 56);
ABI_SIZE(struct k_power_setting_array, 16);
ABI_OFFSET(struct k_slave_info, camera_id, 0x60);
ABI_OFFSET(struct k_slave_info, slave_addr, 0x64);
ABI_OFFSET(struct k_slave_info, addr_type, 0x68);
ABI_OFFSET(struct k_slave_info, sensor_id_info, 0x6c);
ABI_OFFSET(struct k_slave_info, power_setting_array, 0x70);
ABI_OFFSET(struct k_slave_info, is_init_params_valid, 0x80);   /* consumer writes 1 here */
ABI_OFFSET(struct k_slave_info, sensor_init_params, 0x84);     /* consumer copies 12 bytes */
ABI_SIZE(struct k_slave_info, 0x90);
ABI_SIZE(struct k_csi2_params, 84);
ABI_OFFSET(struct k_csi2_params, csiphy_params, 0x4c);
ABI_SIZE(struct k_csi_lane_params, 8);

/* ---- plug-in records (pointer-based, 2014 generation) ---- */
enum { OUT_BAYER = 0, OUT_YCBCR = 1 };
enum { CONN_PARALLEL = 0, CONN_MIPI = 1 };
enum { RAW_8BIT = 0, RAW_10BIT = 1, RAW_12BIT = 2 };
struct b_output { uint32_t output_format, connection_mode, raw_output; };
struct b_output_reg_addr { uint16_t x_output, y_output, line_length_pclk, frame_length_lines; };
struct b_exp_gain_info { uint16_t coarse_int_time_addr, global_gain_addr; uint32_t vert_offset; };
struct b_aec_info { uint32_t unknown[7]; float max_gain; uint32_t max_linecount; };
struct b_lens_info {
    float focal_length, pix_size, f_number, total_f_dist, hor_view_angle, ver_view_angle;
};
struct b_stream_info { uint16_t vc_cfg_size; struct k_csid_vc_cfg *vc_cfg; uint32_t *pix_fmt_fourcc; };
struct b_stream_info_array { struct b_stream_info *sensor_stream_info; uint32_t size; };
struct b_reg_settings_array { struct k_i2c_reg_setting *reg_settings; uint32_t size; };
struct b_res_cfg_table { uint32_t *res_cfg_type; uint32_t size; };
struct b_out_info {
    uint16_t x_output, y_output, line_length_pclk, frame_length_lines;
    uint32_t vt_pixel_clk, op_pixel_clk;
    uint16_t binning_factor, binning_method;
    float min_fps, max_fps;
    uint32_t mode;
};
struct b_out_info_array { struct b_out_info *out_info; uint32_t size; };
struct b_csi_params_array { struct k_csi2_params **csi2_params; uint32_t size; };
struct b_crop { uint16_t top_crop, bottom_crop, left_crop, right_crop; };
struct b_crop_array { struct b_crop *crop_params; uint32_t size; };
struct b_chromatix {
    const char *common, *preview, *snapshot, *camcorder, *liveshot;
};
struct b_chromatix_array { struct b_chromatix *sensor_lib_chromatix; uint32_t size; };
struct b_meta_out_info { uint32_t width, height, stats_type; };
struct b_meta_out_info_array { struct b_meta_out_info *meta_data_out_info; uint32_t size; };

/* AEC register output written by the fill callback. */
struct b_exposure {
    uint16_t reg_gain, line_count;   /* register code, lines */
    float digital_gain;               /* residual gain for the ISP */
    float sensor_real_gain;
    float sensor_digital_gain;
};
typedef int32_t (*b_calc_exposure_fn)(float real_gain, uint16_t line_count, struct b_exposure *out);
typedef int32_t (*b_fill_exposure_fn)(uint16_t gain, uint32_t line, uint32_t fl_lines,
                                      int32_t luma_avg, uint32_t fgain,
                                      struct k_i2c_reg_setting *reg_setting);
struct b_exposure_func_table { b_calc_exposure_fn calc_exp; b_fill_exposure_fn fill_exp; };

struct b_sensor_lib {
    struct k_slave_info *sensor_slave_info;              /* 0x00 */
    struct k_sensor_init_params *sensor_init_params;     /* 0x04 */
    const char *actuator_name;                           /* 0x08 libactuator_<name>.so */
    const char *eeprom_name;                             /* 0x0c libmmcamera_<name>_eeprom.so */
    struct b_output *sensor_output;                      /* 0x10 */
    struct b_output_reg_addr *output_reg_addr;           /* 0x14 */
    struct b_exp_gain_info *exp_gain_info;               /* 0x18 */
    struct b_aec_info *aec_info;                         /* 0x1c */
    uint16_t snapshot_exp_wait_frames;                   /* 0x20 */
    uint16_t sensor_num_frame_skip;                      /* 0x22 */
    uint16_t sensor_num_HDR_frame_skip;                  /* 0x24 */
    uint32_t unknown_28;                                 /* 0x28 0 in both references */
    uint32_t exposure_table_size;                        /* 0x2c imx214 30, ov5648 10 */
    struct b_lens_info *default_lens_info;               /* 0x30 */
    struct k_csi_lane_params *csi_lane_params;           /* 0x34 */
    struct k_csid_vc_cfg *csi_cid_params;                /* 0x38 */
    struct b_stream_info_array *sensor_stream_info_array;/* 0x3c */
    uint32_t csi_cid_params_size;                        /* 0x40 */
    struct b_reg_settings_array *init_settings_array;    /* 0x44 */
    struct k_i2c_reg_setting *start_settings;            /* 0x48 */
    struct k_i2c_reg_setting *stop_settings;             /* 0x4c */
    struct k_i2c_reg_setting *groupon_settings;          /* 0x50 */
    struct k_i2c_reg_setting *groupoff_settings;         /* 0x54 */
    struct b_res_cfg_table *sensor_res_cfg_table;        /* 0x58 */
    struct b_reg_settings_array *res_settings_array;     /* 0x5c */
    struct b_out_info_array *out_info_array;             /* 0x60 size <= 10 */
    struct b_csi_params_array *csi_params_array;         /* 0x64 */
    struct b_crop_array *crop_params_array;              /* 0x68 */
    struct b_chromatix_array *chromatix_array;           /* 0x6c */
    struct b_meta_out_info_array *meta_data_out_info_array; /* 0x70 */
    struct b_exposure_func_table *exposure_func_table;   /* 0x74 */
    uint32_t unknown_78[4];                              /* 0x78 zero in both */
    uint32_t video_hdr_capable;                          /* 0x88 imx214 1 */
    void *video_hdr_ops;                                 /* 0x8c imx214 {fn, 2, 0x8100}; consumer reads +8 */
    uint32_t unknown_90[4];                              /* 0x90 (0x94/0x98/0x9c read by capabilities) */
    uint32_t unknown_a0;                                 /* 0xa0 ov5648 2 */
    uint32_t unknown_a4;                                 /* 0xa4 */
};
ABI_SIZE(struct b_output, 12);
ABI_SIZE(struct b_aec_info, 36);
ABI_SIZE(struct b_lens_info, 24);
ABI_SIZE(struct b_stream_info, 12);
ABI_SIZE(struct b_out_info, 32);
ABI_SIZE(struct b_chromatix, 20);
ABI_SIZE(struct b_crop, 8);
ABI_OFFSET(struct b_sensor_lib, snapshot_exp_wait_frames, 0x20);
ABI_OFFSET(struct b_sensor_lib, exposure_table_size, 0x2c);
ABI_OFFSET(struct b_sensor_lib, default_lens_info, 0x30);
ABI_OFFSET(struct b_sensor_lib, csi_cid_params_size, 0x40);
ABI_OFFSET(struct b_sensor_lib, sensor_res_cfg_table, 0x58);
ABI_OFFSET(struct b_sensor_lib, out_info_array, 0x60);
ABI_OFFSET(struct b_sensor_lib, exposure_func_table, 0x74);
ABI_OFFSET(struct b_sensor_lib, video_hdr_ops, 0x8c);
ABI_SIZE(struct b_sensor_lib, 0xa8);

/* Resolution configuration actions (res_cfg_type values seen in imx214). */
enum {
    RES_CFG_STOP_STREAM = 0, RES_CFG_NEW_RESOLUTION = 1, RES_CFG_CSIPHY = 2,
    RES_CFG_CSID = 3, RES_CFG_LANE = 4, RES_CFG_CHROMATIX = 5,
    RES_CFG_SEND_EVENT = 6,
};
#endif
