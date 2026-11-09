#
# Copyright (C) 2026 The LineageOS Project
#
# SPDX-License-Identifier: Apache-2.0
#

LOCAL_PATH := $(call my-dir)

# libnfc-nxp reads its build configuration from
# $(TARGET_OUT_HEADERS)/libnfc-nxp/nfc_custom_config.h when
# TARGET_HAS_NFC_CUSTOM_CONFIG is set. Only LOCAL_COPY_HEADERS may place
# files there (soong_ui removes anything else), and every C/C++ object waits
# for all_copied_headers, so this header-only module is enough.
include $(CLEAR_VARS)
LOCAL_MODULE := libnfc_martini_config
LOCAL_MODULE_TAGS := optional
LOCAL_COPY_HEADERS_TO := libnfc-nxp
LOCAL_COPY_HEADERS := nfc_custom_config.h
include $(BUILD_STATIC_LIBRARY)
