#
# Copyright (C) 2026 The TWRP Open Source Project
#
# SPDX-License-Identifier: Apache-2.0
#
# TWRP for the Nokia Lumia 930 (martini, RM-1045), MSM8974, booted by lk1st
# (lk2nd) loaded by BootShim from the Windows Phone UEFI. Derived from the
# Lumia 1520 (bandit) tree. Built, never booted: no 930 has been tried yet.
#

DEVICE_PATH := device/nokia/martini

# Platform
TARGET_BOARD_PLATFORM := msm8974
TARGET_BOOTLOADER_BOARD_NAME := MSM8974
TARGET_NO_BOOTLOADER := true
TARGET_NO_RADIOIMAGE := true

# Architecture
TARGET_ARCH := arm
TARGET_ARCH_VARIANT := armv7-a-neon
TARGET_CPU_ABI := armeabi-v7a
TARGET_CPU_ABI2 := armeabi
TARGET_CPU_VARIANT := krait

TARGET_OTA_ASSERT_DEVICE := martini,RM-1045

# Kernel: prebuilt zImage with the appended DTB, from
# github.com/lumia-msm8974/android_kernel_nokia_msm8974 (lineage-18.1-martini),
# lineage_martini_defconfig; see prebuilt/README. The Windows Phone TrustZone
# needs msm_tz_restricted=1 (no SCM CPU boot address, no power collapse).
TARGET_PREBUILT_KERNEL := $(DEVICE_PATH)/prebuilt/zImage-dtb
BOARD_KERNEL_CMDLINE := androidboot.hardware=martini androidboot.bootdevice=msm_sdcc.1 \
    msm_tz_restricted=1 androidboot.selinux=permissive
BOARD_KERNEL_BASE := 0x10000000
BOARD_KERNEL_PAGESIZE := 2048
BOARD_MKBOOTIMG_ARGS := --ramdisk_offset 0x04000000 --tags_offset 0x03e00000

# Partitions (installer layout)
BOARD_FLASH_BLOCK_SIZE := 131072
BOARD_BOOTIMAGE_PARTITION_SIZE := 33554432
BOARD_RECOVERYIMAGE_PARTITION_SIZE := 33554432
BOARD_SYSTEMIMAGE_PARTITION_SIZE := 3221225472
BOARD_USERDATAIMAGE_PARTITION_SIZE := 8589934592
TARGET_USERIMAGES_USE_EXT4 := true
TARGET_USERIMAGES_USE_F2FS := true

# Recovery
TARGET_RECOVERY_DEVICE_DIRS += $(DEVICE_PATH)
BOARD_HAS_NO_SELECT_BUTTON := true
BOARD_SUPPRESS_SECURE_ERASE := true
# First kernel: MDSS stays off and fb0 is the UEFI framebuffer through
# simple-framebuffer (a8b8g8r8 = R,G,B,A bytes, 1080x1920, stride 4320).
# Format and stride are the 1520's: check them on the phone.
TARGET_RECOVERY_PIXEL_FORMAT := "RGBX_8888"
TARGET_RECOVERY_QCOM_RTC_FIX := true

# TWRP
TW_THEME := portrait_hdpi
TW_DEVICE_VERSION := 1
RECOVERY_SDCARD_ON_DATA := true
TW_EXTRA_LANGUAGES := true
TW_INCLUDE_CRYPTO := false
# AMOLED panel with DCS brightness: not reachable while the screen is the
# UEFI framebuffer, so no brightness control and the screen is never blanked
TW_NO_SCREEN_BLANK := true
TW_SCREEN_BLANK_ON_BOOT := false
TW_EXCLUDE_SUPERSU := true
TW_EXCLUDE_TWRPAPP := true
TW_USE_TOOLBOX := true

# USB: legacy android_usb gadget of the 3.4 kernel (adb on FunctionFS,
# MTP on /dev/mtp_usb). No USB mass storage: the kernel names its LUN "lun"
# and TWRP only builds paths with a LUN number.
# The TWRP MTP server opens TW_MTP_DEVICE and hands the descriptor to
# MtpDevHandle, whose start() opens /dev/mtp_usb again; f_mtp allows a
# single open (-EBUSY), so with the default the second open failed ("Failed
# to start usb driver"). Point the first open at /dev/null: start() then
# opens /dev/mtp_usb once.
TW_MTP_DEVICE := /dev/null
