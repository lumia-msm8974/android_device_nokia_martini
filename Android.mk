#
# Copyright (C) 2026 The LineageOS Project
#
# SPDX-License-Identifier: Apache-2.0
#

LOCAL_PATH := $(call my-dir)

ifeq ($(TARGET_DEVICE),martini)
include $(call all-makefiles-under,$(LOCAL_PATH))

# Open source location HAL of msm8974-common (gps.msm8974, libloc_eng,
# libloc_core, libgps.utils): its own Android.mk only builds for Oppo devices.
include device/oppo/msm8974-common/gps/Android.mk
endif
