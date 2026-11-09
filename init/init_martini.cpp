/*
 * Copyright (C) 2016 The CyanogenMod Project
 * Copyright (C) 2020-2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Vendor init for the Nokia Lumia 930. Replaces the Oppo msm8974 libinit;
 * the alarm boot handling is the same as in device/oppo/msm8974-common.
 */

#include <cstring>
#include <string>

#include <android-base/file.h>
#include <android-base/properties.h>
#include <android-base/strings.h>

#define _REALLY_INCLUDE_SYS__SYSTEM_PROPERTIES_H_
#include <sys/_system_properties.h>

#include "vendor_init.h"

using android::base::GetProperty;
using android::base::ReadFileToString;
using android::base::Trim;

static void property_override(char const prop[], char const value[], bool add = true)
{
    prop_info *pi = (prop_info *) __system_property_find(prop);

    if (pi)
        __system_property_update(pi, value, strlen(value));
    else if (add)
        __system_property_add(prop, strlen(prop), value, strlen(value));
}

// ro.alarm_boot is true when the PMIC reports an RTC power on (boot_reason 3).
static void init_alarm_boot_properties()
{
    std::string boot_reason;
    std::string alarm_boot = GetProperty("ro.boot.alarmboot", "");

    if (ReadFileToString("/proc/sys/kernel/boot_reason", &boot_reason)) {
        if (Trim(boot_reason) == "3" || alarm_boot == "true")
            property_override("ro.alarm_boot", "true");
        else
            property_override("ro.alarm_boot", "false");
    }
}

void vendor_load_properties()
{
    // Placeholder BT MAC address, replaced once the real one is read
    property_override("ro.boot.btmacaddr", "00:00:00:00:00:00");
    init_alarm_boot_properties();

    // The product code (RM-1045) is not passed by lk1st yet: only the
    // generic model is set.
    property_override("ro.product.model", "Lumia 930");
    property_override("ro.product.vendor.model", "Lumia 930");
}
