#
# Copyright (C) 2014 The CyanogenMod Project
# Copyright (C) 2026 The LineageOS Project
#
# SPDX-License-Identifier: Apache-2.0
#

DEVICE_PATH := device/nokia/martini

# Generic MSM8974 files (Wi-Fi/audio/media configuration, sepolicy) still
# come from the LineageOS msm8974 common tree of the OnePlus One / Find 7.
# Its BoardConfigCommon.mk is no longer included: the settings below are the
# generic ones, without the Oppo pieces (BOARD_VENDOR oppo, Oppo TrustZone
# OTA assertion and recovery updater, Oppo libinit, Oppo LED HAL,
# /proc/touchpanel double tap).
MSM8974_COMMON_PATH := device/oppo/msm8974-common

BOARD_VENDOR := nokia

# Lumia 930 product codes. RM-1045 is the Lumia 930; RM-1087 is reported
# as a 930 variant but not verified. The Lumia Icon / 929 (RM-927,
# "vanquish") ships a different Windows firmware set and is left out.
# TODO: confirm the product code of the phone (DPP / Windows registry).
TARGET_OTA_ASSERT_DEVICE := martini,RM-1045

# Bootloader
TARGET_BOOTLOADER_BOARD_NAME := MSM8974
TARGET_NO_BOOTLOADER := true
TARGET_NO_RADIOIMAGE := true

# Platform
TARGET_BOARD_PLATFORM := msm8974

# Architecture
TARGET_ARCH := arm
TARGET_ARCH_VARIANT := armv7-a-neon
TARGET_CPU_ABI := armeabi-v7a
TARGET_CPU_ABI2 := armeabi
TARGET_CPU_VARIANT := generic
TARGET_CPU_VARIANT_RUNTIME := krait

# Kernel
# Boot chain: Nokia UEFI -> bootmgr -> BootShim -> lk1st (lk2nd, branch
# martini) -> boot.img, as on the Lumia 1520. On the 930 the ELF that
# BootShim loads must have its entry point at 0x7cc00000 (0x0ff00000 on the
# 1520): that only concerns lk1st, not this boot image.
# lk1st picks the single DTB appended to zImage-dtb (msm8974-v2.2-martini,
# compatible "nokia,martini"). The Windows Phone
# TrustZone resets the SoC on SCM SMP bring-up and has no power collapse:
# the kernel handles it (msm_tz_restricted) and starts cores 1-3 through the
# UEFI parking protocol mailboxes instead (no CPU hotplug).
# The panel (AMOLED, DSI video mode, brightness through DCS commands, no
# WLED) is driven by MDSS with continuous splash taken over from the UEFI,
# the GPU by KGSL with OCMEM. No kernel console on the panel: the kernel
# is built without the framebuffer console; the log stays in dmesg and adb.
BOARD_KERNEL_IMAGE_NAME := zImage-dtb
BOARD_KERNEL_SEPARATED_DT := false
BOARD_KERNEL_PAGESIZE := 2048
BOARD_KERNEL_CMDLINE := androidboot.hardware=martini androidboot.bootdevice=msm_sdcc.1 \
    msm_tz_restricted=1 \
    androidboot.selinux=permissive
# lk1st loads the kernel at 0x10008000, ramdisk at 0x14000000, tags at
# 0x13e00000 (it ignores the header, these keep the image consistent);
# the kernel runs with PHYS_OFFSET 0x10000000. Same values as the 1520:
# TODO: check the load addresses of the martini lk1st on the phone.
BOARD_KERNEL_BASE := 0x10000000
BOARD_MKBOOTIMG_ARGS := --ramdisk_offset 0x04000000 --tags_offset 0x03e00000
BOARD_RAMDISK_USE_XZ := true
TARGET_KERNEL_SOURCE := kernel/nokia/msm8974
TARGET_KERNEL_CONFIG := lineage_martini_defconfig
TARGET_KERNEL_ADDITIONAL_FLAGS := \
    HOSTCFLAGS="-fuse-ld=lld -Wno-unused-command-line-argument"

# ANT+
BOARD_ANT_WIRELESS_DEVICE := "vfs-prerelease"

# Audio
AUDIO_FEATURE_ENABLED_COMPRESS_VOIP := false
AUDIO_FEATURE_ENABLED_EXTN_FORMATS := true
AUDIO_FEATURE_ENABLED_EXTN_POST_PROC := true
AUDIO_FEATURE_ENABLED_FLUENCE := true
AUDIO_FEATURE_ENABLED_HFP := true
AUDIO_FEATURE_ENABLED_HWDEP_CAL := true
AUDIO_FEATURE_ENABLED_MULTI_VOICE_SESSIONS := true
AUDIO_FEATURE_ENABLED_NEW_SAMPLE_RATE := true
AUDIO_FEATURE_ENABLED_PROXY_DEVICE := true
BOARD_USES_ALSA_AUDIO := true
USE_CUSTOM_AUDIO_POLICY := 1

# Binder
TARGET_USES_64_BIT_BINDER := true

# Bluetooth
BOARD_HAVE_BLUETOOTH_QCOM := true
BOARD_BLUETOOTH_BDROID_BUILDCFG_INCLUDE_DIR := $(DEVICE_PATH)/bluetooth

# Build
BUILD_BROKEN_DUP_RULES := true

# Charger
BOARD_HEALTHD_CUSTOM_CHARGER_RES := $(MSM8974_COMMON_PATH)/charger/images

# Dexpreopt
WITH_DEXPREOPT_DEBUG_INFO := false

# Display
# 5" 1080x1920 AMOLED. The Windows touch settings give 62.00 x 115.00 mm,
# that is 442 x 424 dpi (441 ppi for the nominal 5.0" diagonal). Use 480
# (xxhdpi) rather than 440: it is the bucket that 5" 1080p phones of the
# same density use in LineageOS (Nexus 5 hammerhead, 445 ppi; OnePlus X,
# 441 ppi AMOLED; Galaxy S4), so apps get xxhdpi assets without scaling
# and the layout matches those devices; 440 would only gain about 9%
# space. The 6" 1520 uses 400.
TARGET_SCREEN_DENSITY := 480

# Encryption: software only. Hardware FDE (cryptfshw) needs a QSEE app
# that the Windows Phone TrustZone does not have.

# Filesystem
TARGET_FS_CONFIG_GEN := $(DEVICE_PATH)/config.fs
BOARD_FLASH_BLOCK_SIZE := 131072
BOARD_CACHEIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_PERSISTIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_USERDATAIMAGE_FILE_SYSTEM_TYPE := ext4
TARGET_USERIMAGES_USE_EXT4 := true
TARGET_USERIMAGES_USE_F2FS := true

# Fonts
EXCLUDE_SERIF_FONTS := true
SMALLER_FONT_FOOTPRINT := true

# Graphics
TARGET_ADDITIONAL_GRALLOC_10_USAGE_BITS := 0x02000000U
TARGET_CONTINUOUS_SPLASH_ENABLED := true
TARGET_DISABLE_POSTRENDER_CLEANUP := true
TARGET_USES_ION := true

# HIDL
DEVICE_MANIFEST_FILE := $(DEVICE_PATH)/manifest.xml
# LiveDisplay (MDSS PCC) is not built in the software graphics mode
# (TARGET_MARTINI_GRAPHICS=software from the environment, see device.mk)
ifneq ($(TARGET_MARTINI_GRAPHICS),software)
DEVICE_MANIFEST_FILE += $(DEVICE_PATH)/configs/vintf/livedisplay.xml
endif

# Init
TARGET_INIT_VENDOR_LIB := //$(DEVICE_PATH):libinit_martini
TARGET_RECOVERY_DEVICE_MODULES := libinit_martini

# Lights (backlight-only HAL in liblight/)
TARGET_PROVIDES_LIBLIGHT := true

# Legacy memfd
TARGET_HAS_MEMFD_BACKPORT := true

# Partitions (provisional: Android partitions are created in Data, see README)
BOARD_BOOTIMAGE_PARTITION_SIZE     := 33554432
BOARD_RECOVERYIMAGE_PARTITION_SIZE := 33554432
BOARD_CACHEIMAGE_PARTITION_SIZE    := 268435456
BOARD_PERSISTIMAGE_PARTITION_SIZE  := 33554432
BOARD_SYSTEMIMAGE_PARTITION_SIZE   := 3221225472
BOARD_USERDATAIMAGE_PARTITION_SIZE := 8589934592
BOARD_ROOT_EXTRA_FOLDERS := firmware persist

# Power
TARGET_USES_INTERACTION_BOOST := true
# Double tap to wake: the power HAL writes the Synaptics driver's switch.
# Same controller (BLSP1 QUP2, cell-index 2, so i2c-2) and address (0x4b) as
# on the 1520; the Windows touch settings of the 930 enable the wakeup
# gesture (WakeupGestureSupported/EnableWakeupGesture = 1). Unverified on
# the phone.
TARGET_TAP_TO_WAKE_NODE := "/sys/devices/f9924000.i2c/i2c-2/2-004b/wake_gesture"

# Properties
TARGET_SYSTEM_PROP += $(MSM8974_COMMON_PATH)/system.prop $(DEVICE_PATH)/system.prop

# QCOM hardware
BOARD_USES_QCOM_HARDWARE := true

# Recovery
TARGET_RECOVERY_FSTAB := $(DEVICE_PATH)/rootdir/etc/fstab.recovery
# minui writes R,G,B,X bytes: the MDSS fb0 (MDP_RGBA_8888) and the
# simple-framebuffer fallback (a8b8g8r8 once lk1st fixes RGB0, as bandit-splash
# does on the 1520)
TARGET_RECOVERY_PIXEL_FORMAT := RGBX_8888

# TWRP (built from a twrp-11 manifest with WITH_TWRP=true)
ifeq ($(WITH_TWRP),true)
TARGET_RECOVERY_DEVICE_DIRS += $(DEVICE_PATH)/twrp
RECOVERY_SDCARD_ON_DATA := true
TW_THEME := portrait_hdpi
TW_BRIGHTNESS_PATH := "/sys/class/leds/lcd-backlight/brightness"
TW_MAX_BRIGHTNESS := 255
TW_NO_SCREEN_BLANK := true
TW_EXTRA_LANGUAGES := true
TW_INCLUDE_CRYPTO := false
endif

# SELinux (permissive from the command line during bring-up)
include device/qcom/sepolicy-legacy/sepolicy.mk
BOARD_SEPOLICY_DIRS += \
    $(MSM8974_COMMON_PATH)/sepolicy \
    $(DEVICE_PATH)/sepolicy

# Shims
TARGET_LD_SHIM_LIBS := \
    /system/vendor/lib/libril-qc-qmi-1.so|libaudioclient_shim.so

# NFC: libnfc-nxp configuration from nfc/nfc_custom_config.h. libnfc-nxp
# looks for it among the copied headers, so LOCAL_COPY_HEADERS is needed.
TARGET_HAS_NFC_CUSTOM_CONFIG := true
BUILD_BROKEN_USES_BUILD_COPY_HEADERS := true

# Wifi
BOARD_HAS_QCOM_WLAN              := true
BOARD_WLAN_DEVICE                := qcwcn
BOARD_WPA_SUPPLICANT_DRIVER      := NL80211
BOARD_WPA_SUPPLICANT_PRIVATE_LIB := lib_driver_cmd_qcwcn
BOARD_HOSTAPD_DRIVER             := NL80211
BOARD_HOSTAPD_PRIVATE_LIB        := lib_driver_cmd_qcwcn
PRODUCT_VENDOR_MOVE_ENABLED      := true
TARGET_DISABLE_WCNSS_CONFIG_COPY := true
# No QMI in wcnss_service: it asks the modem (off during the 1520 bring-up,
# not tested on the 930) for the MAC
# over QMI DMS first and waits forever, so it never opens /dev/wcnss_wlan,
# which is what loads the Pronto firmware. The MAC comes from /persist
# through init.martini.wifi.sh (WCNSS_qcom_cfg.ini).
TARGET_USES_QCOM_WCNSS_QMI       := false
WIFI_DRIVER_FW_PATH_STA          := "sta"
WIFI_DRIVER_FW_PATH_AP           := "ap"
WIFI_HIDL_UNIFIED_SUPPLICANT_SERVICE_RC_ENTRY := true
WPA_SUPPLICANT_VERSION           := VER_0_8_X

-include vendor/nokia/martini/BoardConfigVendor.mk
