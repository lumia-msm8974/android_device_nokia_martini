/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Focus actuator description of the Karma rear module (Lumia 930/1520) for
 * the bacon/find7 mm-camera2 daemon: libactuator_karma.so, entry point
 * actuator_driver_open_lib(). Written from scratch; the layout was measured
 * on the daemon's own actuator library and on the code of
 * libmmcamera2_sensor_modules.so that copies it into
 * struct msm_actuator_set_info_t (offsets asserted below).
 *
 * The VCM driver is reached through the sensor (see msm_actuator_lumia.c in
 * the kernel): the daemon talks to the sensor address (0x20, word address,
 * byte data) and asks for plain 10-bit DAC codes; the kernel turns each code
 * into the sensor's serial-master writes. No init settings: the module
 * settings do not configure the VCM driver.
 *
 * Steps: the AF algorithm tuning used with this library has 520 steps
 * (total_steps = 519 + 1). Codes span the range given by the module
 * settings without the per-unit calibration (far end 150, infinity and
 * hyperfocal 300, macro 650, near end 850): 1 code per step from 150 to 490,
 * then 2 per step up to 850.
 *
 * Copyright (c) 2026, Lorenzo Giannoccaro.
 */
#include <stddef.h>
#include <stdint.h>

#define ABI_OFF(t, f, o) _Static_assert(offsetof(t, f) == (o), #t "." #f)
#define ABI_SIZE(t, s) _Static_assert(sizeof(t) == (s), "sizeof " #t)

/* kernel enums (include/media/msm_cam_sensor.h) */
#define ACTUATOR_VCM            0
#define MSM_ACTUATOR_WRITE_DAC  1
#define MSM_ACTUATOR_BYTE_DATA  1
#define MSM_ACTUATOR_WORD_ADDR  2

struct a_reg_params {                   /* struct msm_actuator_reg_params_t */
    uint32_t reg_write_type;
    uint32_t hw_mask;
    uint16_t reg_addr, hw_shift, data_shift;
};
ABI_SIZE(struct a_reg_params, 16);

struct a_reg_settings {                 /* struct reg_settings_t */
    uint16_t reg_addr;
    uint32_t addr_type;
    uint16_t reg_data;
    uint32_t data_type, i2c_operation, delay;
};
ABI_SIZE(struct a_reg_settings, 24);

struct a_region {                       /* struct region_params_t */
    uint16_t step_bound[2];             /* [0] towards macro, [1] towards infinity */
    uint16_t code_per_step;
};
ABI_SIZE(struct a_region, 6);

struct a_damping {                      /* struct damping_params_t */
    uint32_t damping_step, damping_delay, hw_params;
};
ABI_SIZE(struct a_damping, 12);

struct a_driver {
    char module_name[32];
    char actuator_name[32];
    uint32_t i2c_addr;                  /* 8-bit */
    uint32_t i2c_data_type;
    uint32_t i2c_addr_type;
    uint32_t act_type;
    uint16_t data_size;                 /* DAC bits */
    uint16_t unknown_52;                /* 0 in the reference */
    uint8_t reg_tbl_size;
    struct a_reg_params reg_tbl[8];
    uint16_t init_setting_size;
    struct a_reg_settings init_settings[12];
    uint32_t unknown_1fc;               /* 10 in the reference, not read by the daemon */
    /* tuned parameters */
    uint16_t scenario_size[2];          /* per direction: 0 near, 1 far */
    uint16_t ringing_scenario[2][8];    /* step-count thresholds */
    int16_t initial_code;
    uint16_t region_size;
    struct a_region region_params[5];
    struct a_damping damping[2][8][5];
};
ABI_OFF(struct a_driver, actuator_name, 0x020);
ABI_OFF(struct a_driver, i2c_addr, 0x040);
ABI_OFF(struct a_driver, i2c_data_type, 0x044);
ABI_OFF(struct a_driver, i2c_addr_type, 0x048);
ABI_OFF(struct a_driver, act_type, 0x04c);
ABI_OFF(struct a_driver, data_size, 0x050);
ABI_OFF(struct a_driver, reg_tbl_size, 0x054);
ABI_OFF(struct a_driver, reg_tbl, 0x058);
ABI_OFF(struct a_driver, init_setting_size, 0x0d8);
ABI_OFF(struct a_driver, init_settings, 0x0dc);
ABI_OFF(struct a_driver, unknown_1fc, 0x1fc);
ABI_OFF(struct a_driver, scenario_size, 0x200);
ABI_OFF(struct a_driver, ringing_scenario, 0x204);
ABI_OFF(struct a_driver, initial_code, 0x224);
ABI_OFF(struct a_driver, region_size, 0x226);
ABI_OFF(struct a_driver, region_params, 0x228);
ABI_OFF(struct a_driver, damping, 0x248);
ABI_SIZE(struct a_driver, 0x608);

#define KARMA_STEPS     520
#define KARMA_DAMP      { 0x3ff, 10000, 0 }     /* one jump, 10 ms settle */

static struct a_driver karma_actuator = {
    .module_name = "lumia",
    .actuator_name = "karma",
    .i2c_addr = 0x20,                           /* the sensor */
    .i2c_data_type = MSM_ACTUATOR_BYTE_DATA,
    .i2c_addr_type = MSM_ACTUATOR_WORD_ADDR,
    .act_type = ACTUATOR_VCM,
    .data_size = 10,
    .reg_tbl_size = 1,
    .reg_tbl = {
        { .reg_write_type = MSM_ACTUATOR_WRITE_DAC, .reg_addr = 0xffff },
    },
    .init_setting_size = 0,
    .unknown_1fc = 10,
    .scenario_size = { 1, 1 },
    .ringing_scenario = { { KARMA_STEPS }, { KARMA_STEPS } },
    .initial_code = 150,
    .region_size = 2,
    .region_params = {
        { .step_bound = { 340, 0 }, .code_per_step = 1 },
        { .step_bound = { KARMA_STEPS, 340 }, .code_per_step = 2 },
    },
    .damping = {
        { { KARMA_DAMP, KARMA_DAMP } },
        { { KARMA_DAMP, KARMA_DAMP } },
    },
};

/* The daemon reads { driver params, ... } through the returned pointer. */
__attribute__((visibility("default"))) void *actuator_driver_open_lib(void)
{
    return &karma_actuator;
}
