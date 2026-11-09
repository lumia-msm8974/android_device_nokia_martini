#
# Copyright (C) 2026 The TWRP Open Source Project
#
# SPDX-License-Identifier: Apache-2.0
#

# Release name
PRODUCT_RELEASE_NAME := martini

$(call inherit-product, $(SRC_TARGET_DIR)/product/full_base_telephony.mk)

# Inherit TWRP common configuration
$(call inherit-product, vendor/omni/config/common.mk)

# Device identifier
PRODUCT_DEVICE := martini
PRODUCT_NAME := omni_martini
PRODUCT_BRAND := Nokia
PRODUCT_MODEL := Lumia 930
PRODUCT_MANUFACTURER := Nokia

# Same identity as the LineageOS 18.1 build, so its zips install
PRODUCT_BUILD_PROP_OVERRIDES += \
    TARGET_DEVICE=martini \
    PRODUCT_NAME=martini
