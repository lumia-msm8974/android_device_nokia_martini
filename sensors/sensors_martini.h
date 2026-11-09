/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Lumia 1520 (martini) sensors: everything that depends on the kernel
 * drivers or on how the chips are mounted is in this file.
 *
 * Kernel interface (one input device per sensor, polled by the driver):
 *   martini-accel  EV_ABS ABS_X/Y/Z  raw counts, chip axes   (LIS2HH12)
 *   martini-gyro   EV_ABS ABS_X/Y/Z  raw counts, chip axes   (L3GD20)
 *   martini-mag    EV_ABS ABS_X/Y/Z  raw counts, chip axes   (HSCDTD008A)
 *   martini-light  EV_ABS ABS_MISC   lux                     (APDS-9930)
 *   martini-prox   EV_ABS ABS_DISTANCE 0 near, 5 far (cm)    (APDS-9930)
 * Each one has "enable" (0/1) and "poll_delay" (ms) in
 * /sys/class/input/inputN/.
 */

#ifndef SENSORS_MARTINI_H
#define SENSORS_MARTINI_H

#include <math.h>

#define MARTINI_GRAVITY          9.80665f    /* m/s^2 per g */
#define MARTINI_DEG_TO_RAD       ((float)(M_PI / 180.0))

/*
 * Scales: SI unit per raw LSB, as documented at the top of each kernel
 * driver. Drop the final values of the drivers in here.
 */

/* LIS2HH12, FS = +-2 g: 0.061 mg/LSB = 5.98206e-4 m/s^2 */
#define ACCEL_MG_PER_LSB        0.061f
#define ACCEL_SCALE             (ACCEL_MG_PER_LSB / 1000.0f * MARTINI_GRAVITY)
#define ACCEL_RANGE             (2.0f * MARTINI_GRAVITY)
#define ACCEL_RESOLUTION        ACCEL_SCALE

/* L3GD20, FS = 2000 dps: 70 mdps/LSB = 1.221730e-3 rad/s */
#define GYRO_MDPS_PER_LSB       70.0f
#define GYRO_SCALE              (GYRO_MDPS_PER_LSB / 1000.0f * MARTINI_DEG_TO_RAD)
#define GYRO_RANGE              (2000.0f * MARTINI_DEG_TO_RAD)
#define GYRO_RESOLUTION         GYRO_SCALE

/* HSCDTD008A: 0.15 uT/LSB, +-2400 uT (provisional: driver not written yet) */
#define MAG_UT_PER_LSB          0.15f
#define MAG_SCALE               MAG_UT_PER_LSB
#define MAG_RANGE               2400.0f
#define MAG_RESOLUTION          MAG_SCALE

/* APDS-9930 ALS: the driver already reports lux, clamped to 200000 */
#define LIGHT_SCALE             1.0f
#define LIGHT_RANGE             200000.0f
#define LIGHT_RESOLUTION        1.0f

/* APDS-9930 proximity: the driver reports 0 (near) or 5 (far), in cm */
#define PROX_SCALE              1.0f
#define PROX_RANGE              5.0f
#define PROX_RESOLUTION         5.0f

/*
 * Current draw in mA (datasheet typical values, active mode). Only used by
 * the framework for battery accounting.
 */
#define ACCEL_POWER_MA          0.18f
#define GYRO_POWER_MA           6.1f
#define MAG_POWER_MA            0.25f
#define LIGHT_POWER_MA          0.2f
#define PROX_POWER_MA           0.25f

/*
 * poll_delay limits in ms written to sysfs (the drivers clamp again on
 * their side). The drivers poll with delayed work and HZ = 300, so a period
 * is rounded up to whole 3.33 ms jiffies: the requested sampling period is
 * truncated to ms, not rounded up, so that 3334 us still gives one jiffy.
 *
 * *_MIN_DELAY_US is the advertised minDelay of the continuous sensors
 * (fastest period really delivered: one jiffy). The on-change sensors
 * (light, proximity) advertise minDelay 0; their limits only bound the rate
 * at which the driver polls the chip.
 */
#define ACCEL_DELAY_MIN_MS      3       /* driver: [3, 1000], ODR up to 400 Hz */
#define ACCEL_DELAY_MAX_MS      1000
#define ACCEL_MIN_DELAY_US      3334
#define GYRO_DELAY_MIN_MS       2       /* driver: [2, 1000], ODR up to 760 Hz */
#define GYRO_DELAY_MAX_MS       1000
#define GYRO_MIN_DELAY_US       3334
#define MAG_DELAY_MIN_MS        20      /* provisional: driver not written yet */
#define MAG_DELAY_MAX_MS        1000
#define MAG_MIN_DELAY_US        20000
#define LIGHT_DELAY_MIN_MS      110     /* driver: one chip cycle, ~107 ms */
#define LIGHT_DELAY_MAX_MS      1000
#define PROX_DELAY_MIN_MS       50      /* driver: [10, 10000] */
#define PROX_DELAY_MAX_MS       100     /* keep it responsive during calls */

/*
 * On-change sensors must report their state right after activation. The
 * APDS-9930 driver forces its first report after "enable"; as a safety net
 * (the input core drops a value equal to the last one reported), if nothing
 * arrives within poll_delay + this slack the HAL reports the value held by
 * the input core (EVIOCGABS), which is then the current one.
 */
#define LIGHT_INITIAL_SLACK_MS  500
#define PROX_INITIAL_SLACK_MS   300

/*
 * Mounting matrices: Android device vector = M * chip vector, row major.
 * Keep them here so they can be flipped after testing on the phone.
 *
 * Gyroscope and magnetometer: the ACPI matrices of the Windows Phone DSDT.
 *
 * Accelerometer: the ACPI matrix is [0 1 0; 1 0 0; 0 0 1], which has
 * determinant -1 (a mirror, impossible for a chip soldered on a board).
 * Windows reports gravity (face up: z = -1 g) where Android reports the
 * reaction to it (face up: z = +9.81), so the Android matrix is the ACPI one
 * negated: [0 -1 0; -1 0 0; 0 0 -1], a proper rotation and the same as the
 * other two chips. Verify on the phone: flat face up must read about
 * (0, 0, +9.8); standing upright (portrait) about (0, +9.8, 0).
 */
#define ACCEL_MATRIX { \
     0, -1,  0, \
    -1,  0,  0, \
     0,  0, -1, \
}

#define GYRO_MATRIX { \
     0, -1,  0, \
    -1,  0,  0, \
     0,  0, -1, \
}

#define MAG_MATRIX { \
     0, -1,  0, \
    -1,  0,  0, \
     0,  0, -1, \
}

#endif /* SENSORS_MARTINI_H */
