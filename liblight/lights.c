/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (C) 2014 The Linux Foundation. All rights reserved.
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Lights HAL for the Nokia Lumia 1520: LCD backlight and key backlight. The
 * LCD uses the lcd-backlight LED class device created by the MDSS panel
 * driver and, while MDSS is off, the PM8941 WLED directly (wled:backlight,
 * 0..max_brightness). The capacitive keys are lit by the four PM8941 KPDBL
 * rows (kpdbl-pwm-1..4), driven together.
 * Derived from the CAF/LineageOS msm8974 liblight.
 */

#define LOG_TAG "lights.martini"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <hardware/hardware.h>
#include <hardware/lights.h>
#include <log/log.h>

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static const char LCD_FILE[] = "/sys/class/leds/lcd-backlight/brightness";
static const char WLED_FILE[] = "/sys/class/leds/wled:backlight/brightness";
static const char WLED_MAX_FILE[] = "/sys/class/leds/wled:backlight/max_brightness";

/* Capacitive key backlight: the four PM8941 KPDBL rows, driven together */
static const char *const BUTTON_FILES[] = {
    "/sys/class/leds/kpdbl-pwm-1/brightness",
    "/sys/class/leds/kpdbl-pwm-2/brightness",
    "/sys/class/leds/kpdbl-pwm-3/brightness",
    "/sys/class/leds/kpdbl-pwm-4/brightness",
};

static int read_int(const char *path, int def)
{
    char buffer[16];
    int fd, n;

    fd = open(path, O_RDONLY);
    if (fd < 0)
        return def;
    n = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);
    if (n <= 0)
        return def;
    buffer[n] = '\0';
    return atoi(buffer);
}

static int write_int(const char *path, int value)
{
    static int already_warned;
    char buffer[20];
    int fd, bytes, written;

    fd = open(path, O_WRONLY);
    if (fd < 0) {
        if (!already_warned) {
            ALOGE("write_int failed to open %s (%s)", path, strerror(errno));
            already_warned = 1;
        }
        return -errno;
    }

    bytes = snprintf(buffer, sizeof(buffer), "%d\n", value);
    written = write(fd, buffer, bytes);
    close(fd);
    return written == bytes ? 0 : -errno;
}

static int rgb_to_brightness(const struct light_state_t *state)
{
    int color = state->color & 0x00ffffff;

    return ((77 * ((color >> 16) & 0xff)) + (150 * ((color >> 8) & 0xff)) +
            (29 * (color & 0xff))) >> 8;
}

static int set_light_backlight(struct light_device_t *dev,
                               const struct light_state_t *state)
{
    int err;

    if (!dev)
        return -1;

    int brightness = rgb_to_brightness(state);

    pthread_mutex_lock(&g_lock);
    if (access(LCD_FILE, W_OK) == 0) {
        err = write_int(LCD_FILE, brightness);
    } else {
        /* Scale 0..255 to the WLED range (4095 on the PM8941) */
        int max = read_int(WLED_MAX_FILE, 255);
        err = write_int(WLED_FILE, brightness * max / 255);
    }
    pthread_mutex_unlock(&g_lock);
    return err;
}

static int set_light_buttons(struct light_device_t *dev,
                             const struct light_state_t *state)
{
    unsigned int i;
    int err = 0;

    if (!dev)
        return -1;

    int brightness = rgb_to_brightness(state);

    pthread_mutex_lock(&g_lock);
    for (i = 0; i < sizeof(BUTTON_FILES) / sizeof(BUTTON_FILES[0]); i++) {
        int ret = write_int(BUTTON_FILES[i], brightness);

        if (ret)
            err = ret;
    }
    pthread_mutex_unlock(&g_lock);
    return err;
}

static int close_lights(struct light_device_t *dev)
{
    free(dev);
    return 0;
}

static int open_lights(const struct hw_module_t *module, const char *name,
                       struct hw_device_t **device)
{
    struct light_device_t *dev;

    int (*set_light)(struct light_device_t *dev,
                     const struct light_state_t *state);

    if (!strcmp(LIGHT_ID_BACKLIGHT, name))
        set_light = set_light_backlight;
    else if (!strcmp(LIGHT_ID_BUTTONS, name))
        set_light = set_light_buttons;
    else
        return -EINVAL;

    dev = calloc(1, sizeof(*dev));
    if (!dev)
        return -ENOMEM;

    dev->common.tag = HARDWARE_DEVICE_TAG;
    dev->common.version = 0;
    dev->common.module = (struct hw_module_t *) module;
    dev->common.close = (int (*)(struct hw_device_t *)) close_lights;
    dev->set_light = set_light;

    *device = (struct hw_device_t *) dev;
    return 0;
}

static struct hw_module_methods_t lights_module_methods = {
    .open = open_lights,
};

struct hw_module_t HAL_MODULE_INFO_SYM = {
    .tag = HARDWARE_MODULE_TAG,
    .version_major = 1,
    .version_minor = 0,
    .id = LIGHTS_HARDWARE_MODULE_ID,
    .name = "Lumia 930 lights module",
    .author = "The LineageOS Project",
    .methods = &lights_module_methods,
};
