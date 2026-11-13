/* SPDX-License-Identifier: GPL-2.0-only */
/* Skype_HD front module register settings: {address, value, delay ms}. */
#pragma once
/* Module 0x0A/0x6A30 revision 0x03, file version 021.025. */
#define SKYPHD_MODULE_IDLE \
    { 0x3023, 0x20, 0 }, \
    { 0x3025, 0x40, 0 }, \
    { 0x3061, 0x55, 0 }, \
    { 0x3062, 0x54, 0 }, \
    { 0x5704, 0x07, 0 }, \
    { 0x5703, 0x07, 0 }, \
    { 0x300c, 0x2e, 0 }, \
    { 0x305e, 0x0d, 0 }, \
    { 0x305f, 0x2e, 0 }, \
    { 0x3052, 0x01, 0 }, \
    { 0x300b, 0x28, 0 }, \
    { 0x3004, 0x0a, 0 }, \
    { 0x5700, 0x08, 0 }, \
    { 0x3005, 0x3d, 0 }, \
    { 0x3008, 0x1e, 0 }, \
    { 0x3029, 0xff, 0 }, \
    { 0x302a, 0xff, 0 }, \
    { 0x3506, 0x00, 0 }, \
    { 0x3521, 0x01, 0 }, \
    { 0x3522, 0x01, 0 }, \
    { 0x3505, 0x41, 0 }, \
    { 0x3095, 0x13, 0 }, \
    { 0x3110, 0x01, 0 }, \
    { 0x3111, 0x62, 0 }, \
    { 0x3112, 0x0e, 0 }, \
    { 0x3113, 0xbc, 0 }, \
    { 0x311d, 0x30, 0 }, \
    { 0x311f, 0x40, 0 }, \
    { 0x0200, 0x04, 0 }, \
    { 0x0201, 0x50, 0 }
#define SKYPHD_MODULE_IDLE_COUNT 30
#define SKYPHD_MODULE_STANDBY_EXIT
#define SKYPHD_MODULE_STANDBY_EXIT_COUNT 0
#define SKYPHD_MODULE_STANDBY_ENTER
#define SKYPHD_MODULE_STANDBY_ENTER_COUNT 0
/* Limit overrides: used in the timing calculation, not written to the sensor. */
#define SKYPHD_MODULE_LIMIT_DPHY_CTRL_CAPABILITY 0x02 /* 0x1600 */
#define SKYPHD_MODULE_LIMIT_MIN_LINE_LENGTH_PCK 0x0642 /* 0x1144 */
#define SKYPHD_MODULE_LIMIT_INTEGRATION_TIME_CAPABILITY 0x0000 /* 0x1000 */
/* Output modes and optics. */
#define SKYPHD_MODULE_MODE0_WOI_W 1408
#define SKYPHD_MODULE_MODE0_WOI_H 960
#define SKYPHD_MODULE_MODE0_OUT_W 1408
#define SKYPHD_MODULE_MODE0_OUT_H 960
#define SKYPHD_MODULE_MODE1_WOI_W 1408
#define SKYPHD_MODULE_MODE1_WOI_H 792
#define SKYPHD_MODULE_MODE1_OUT_W 1408
#define SKYPHD_MODULE_MODE1_OUT_H 792
#define SKYPHD_MODULE_MODE2_WOI_W 1280
#define SKYPHD_MODULE_MODE2_WOI_H 960
#define SKYPHD_MODULE_MODE2_OUT_W 1280
#define SKYPHD_MODULE_MODE2_OUT_H 960
#define SKYPHD_MODULE_MODE_COUNT 3
#define SKYPHD_MODULE_F_NUMBER_X100 240
#define SKYPHD_MODULE_FOCAL_LENGTH_X100 196
#define SKYPHD_MODULE_PIXEL_SIZE_X1000 1750
