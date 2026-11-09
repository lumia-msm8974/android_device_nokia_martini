#
# Copyright (C) 2014 The CyanogenMod Project
# Copyright (C) 2026 The LineageOS Project
#
# SPDX-License-Identifier: Apache-2.0
#

LOCAL_PATH := device/nokia/martini

# Generic MSM8974 configuration files still come from the LineageOS msm8974
# common tree (OnePlus One / Find 7). Its msm8974.mk is no longer inherited:
# the list below is the generic part of it, without the Oppo pieces (NFC,
# touch gestures HAL, Oppo LED HAL and libinit, ConfigPanel, Oppo recovery
# updater and OTA TrustZone assertion) and with martini init scripts.
MSM8974_COMMON_PATH := device/oppo/msm8974-common

# Proprietary files first: files taken from the 1520 (proprietary-local/)
# take precedence over the generic ones listed below.
$(call inherit-product, vendor/nokia/martini/martini-vendor.mk)

# Overlays (martini values first, they override the msm8974-common ones)
DEVICE_PACKAGE_OVERLAYS += \
    $(LOCAL_PATH)/overlay \
    $(MSM8974_COMMON_PATH)/overlay

# Screen (1080x1920, 5" AMOLED)
TARGET_SCREEN_HEIGHT := 1920
TARGET_SCREEN_WIDTH := 1080
PRODUCT_AAPT_CONFIG := normal
PRODUCT_AAPT_PREF_CONFIG := xxhdpi

# ANT+
PRODUCT_PACKAGES += \
    AntHalService \
    com.dsi.ant.antradio_library \
    libantradio

# APEX
PRODUCT_COPY_FILES += \
    $(MSM8974_COMMON_PATH)/configs/ld.config.txt:$(TARGET_COPY_OUT_SYSTEM)/etc/swcodec/ld.config.txt

# Audio
# Policy and mixer paths: the msm8974 common tree leaves them to each device.
# mixer_paths.xml starts from the OnePlus One one (same WCD9320 codec) with
# the 1520 microphones; audio_platform_info.xml moves the loudspeaker to the
# secondary MI2S amplifier.
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/audio/audio_platform_info.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_platform_info.xml \
    $(LOCAL_PATH)/audio/audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_configuration.xml \
    $(LOCAL_PATH)/audio/mixer_paths.xml:$(TARGET_COPY_OUT_VENDOR)/etc/mixer_paths.xml \
    frameworks/av/services/audiopolicy/config/a2dp_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/a2dp_audio_policy_configuration.xml \
    frameworks/av/services/audiopolicy/config/audio_policy_volumes.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_volumes.xml \
    frameworks/av/services/audiopolicy/config/default_volume_tables.xml:$(TARGET_COPY_OUT_VENDOR)/etc/default_volume_tables.xml \
    frameworks/av/services/audiopolicy/config/r_submix_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/r_submix_audio_policy_configuration.xml \
    frameworks/av/services/audiopolicy/config/usb_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/usb_audio_policy_configuration.xml

# The MTP ACDB files are generic fallbacks: the 1520's own calibration
# (vendor proprietary-local/) wins when present.
PRODUCT_COPY_FILES += \
    $(MSM8974_COMMON_PATH)/audio/audio_effects.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_effects.xml \
    $(MSM8974_COMMON_PATH)/audio/acdb/MTP_Bluetooth_cal.acdb:$(TARGET_COPY_OUT_SYSTEM)/etc/acdbdata/MTP/MTP_Bluetooth_cal.acdb \
    $(MSM8974_COMMON_PATH)/audio/acdb/MTP_General_cal.acdb:$(TARGET_COPY_OUT_SYSTEM)/etc/acdbdata/MTP/MTP_General_cal.acdb \
    $(MSM8974_COMMON_PATH)/audio/acdb/MTP_Global_cal.acdb:$(TARGET_COPY_OUT_SYSTEM)/etc/acdbdata/MTP/MTP_Global_cal.acdb \
    $(MSM8974_COMMON_PATH)/audio/acdb/MTP_Handset_cal.acdb:$(TARGET_COPY_OUT_SYSTEM)/etc/acdbdata/MTP/MTP_Handset_cal.acdb \
    $(MSM8974_COMMON_PATH)/audio/acdb/MTP_Hdmi_cal.acdb:$(TARGET_COPY_OUT_SYSTEM)/etc/acdbdata/MTP/MTP_Hdmi_cal.acdb \
    $(MSM8974_COMMON_PATH)/audio/acdb/MTP_Headset_cal.acdb:$(TARGET_COPY_OUT_SYSTEM)/etc/acdbdata/MTP/MTP_Headset_cal.acdb \
    $(MSM8974_COMMON_PATH)/audio/acdb/MTP_Speaker_cal.acdb:$(TARGET_COPY_OUT_SYSTEM)/etc/acdbdata/MTP/MTP_Speaker_cal.acdb

PRODUCT_PACKAGES += \
    android.hardware.audio@2.0-impl \
    android.hardware.audio@2.0-service \
    android.hardware.audio.effect@2.0-impl \
    audio.a2dp.default \
    audio.primary.msm8974 \
    audio.r_submix.default \
    audio.usb.default \
    libaudioclient_shim \
    libqcompostprocbundle \
    libqcomvisualizer \
    libqcomvoiceprocessing

# Bluetooth
PRODUCT_PACKAGES += \
    android.hardware.bluetooth@1.0-impl \
    android.hardware.bluetooth@1.0-service \
    libbt-vendor

# Boot animation
TARGET_BOOTANIMATION_HALF_RES := true

# Camera (no camera HAL for the 1520 yet: the provider reports no camera).
# Snap is left out until there is one: it only showed a black window.
PRODUCT_PACKAGES += \
    android.hardware.camera.provider@2.4-impl \
    camera.device@1.0-impl

# Charger
PRODUCT_PACKAGES += \
    charger_res_images
WITH_LINEAGE_CHARGER := false

# Data
PRODUCT_PACKAGES += \
    librmnetctl

# DRM
PRODUCT_PACKAGES += \
    android.hardware.drm@1.0-impl \
    android.hardware.drm@1.0-service \
    android.hardware.drm@1.3-service.clearkey

# Gatekeeper
PRODUCT_PACKAGES += \
    android.hardware.gatekeeper@1.0-service.software

# Graphics
#
# TARGET_MARTINI_GRAPHICS selects the display stack (override from the
# environment, e.g. TARGET_MARTINI_GRAPHICS=software m bacon):
#
#  qcom (default):
#    MDSS display HAL (gralloc.msm8974, hwcomposer.msm8974 through
#    HWC2On1Adapter: layers composed by the MDP and streamed to the video
#    mode AMOLED panel) and the Adreno 330 GPU blobs. Needs the kernel with
#    MDSS continuous splash, the martini panel driver, OCMEM and KGSL.
#    TODO: the martini MDSS panel is not tested yet; if it does not light
#    up, build with TARGET_MARTINI_GRAPHICS=software (UEFI framebuffer).
#
#  mdss:
#    Same display HAL with OpenGL ES from SwiftShader, to test the panel
#    driver without the GPU.
#
#  software:
#    Android draws on the framebuffer left by the UEFI (simple-framebuffer,
#    /dev/graphics/fb0). gralloc.default allocates and posts to fb0; with
#    no hwcomposer module the composer service falls back to the gralloc
#    framebuffer device (HWC2OnFbAdapter); OpenGL ES comes from SwiftShader.
#    Needs a kernel with simple-framebuffer enabled and MDSS disabled in
#    the device tree; hwcomposer.msm8974 must not be installed.
TARGET_MARTINI_GRAPHICS ?= qcom

PRODUCT_PACKAGES += \
    android.hardware.graphics.allocator@2.0-impl \
    android.hardware.graphics.allocator@2.0-service \
    android.hardware.graphics.composer@2.1-service \
    android.hardware.graphics.mapper@2.0-impl-2.1 \
    android.hardware.memtrack@1.0-impl \
    android.hardware.memtrack@1.0-service \
    memtrack.msm8974 \
    liboverlay

ifneq ($(filter software mdss,$(TARGET_MARTINI_GRAPHICS)),)
# Rendering in software: window and transition animations off by default
# (first boot / new data); OpenGL ES from SwiftShader
DEVICE_PACKAGE_OVERLAYS := $(LOCAL_PATH)/overlay-software $(DEVICE_PACKAGE_OVERLAYS)

PRODUCT_PACKAGES += \
    libEGL_swiftshader \
    libGLESv1_CM_swiftshader \
    libGLESv2_swiftshader

PRODUCT_PROPERTY_OVERRIDES += \
    ro.hardware.egl=swiftshader \
    ro.zygote.disable_gl_preload=true
endif

ifeq ($(TARGET_MARTINI_GRAPHICS),software)
PRODUCT_PACKAGES += \
    gralloc.default

PRODUCT_PROPERTY_OVERRIDES += \
    ro.hardware.gralloc=default
else
PRODUCT_PACKAGES += \
    gralloc.msm8974 \
    hwcomposer.msm8974
endif

ifeq ($(TARGET_MARTINI_GRAPHICS),qcom)
# Adreno 330 blobs: libEGL_adreno / libGLESv*_adreno in vendor/lib/egl
PRODUCT_PROPERTY_OVERRIDES += \
    ro.hardware.egl=adreno
endif

# GPS: the msm8974-common location HAL (gps.msm8974, built from source)
# behind the LineageOS legacy GNSS service. It talks QMI LOC to the modem
# through the libloc_api_v02 blob. See kit/gps/README.md.
PRODUCT_PACKAGES += \
    android.hardware.gnss@1.0-impl.legacy \
    android.hardware.gnss@1.0-service.legacy \
    gps.msm8974

PRODUCT_COPY_FILES += \
    $(MSM8974_COMMON_PATH)/gps/flp.conf:$(TARGET_COPY_OUT_SYSTEM)/etc/flp.conf \
    $(LOCAL_PATH)/gps/gps.conf:$(TARGET_COPY_OUT_SYSTEM)/etc/gps.conf \
    $(MSM8974_COMMON_PATH)/gps/izat.conf:$(TARGET_COPY_OUT_SYSTEM)/etc/izat.conf \
    $(MSM8974_COMMON_PATH)/gps/sap.conf:$(TARGET_COPY_OUT_SYSTEM)/etc/sap.conf

# Healthd
PRODUCT_PACKAGES += \
    android.hardware.health@2.1-impl \
    android.hardware.health@2.1-impl.recovery \
    android.hardware.health@2.1-service

# HIDL
PRODUCT_ENFORCE_VINTF_MANIFEST_OVERRIDE := true

# IPC router config
PRODUCT_COPY_FILES += \
    $(MSM8974_COMMON_PATH)/configs/sec_config:$(TARGET_COPY_OUT_VENDOR)/etc/sec_config

# Keymaster: no keystore.msm8974 module (it would need a QSEE app), so the
# 3.0 service runs its software implementation.
PRODUCT_PACKAGES += \
    android.hardware.keymaster@3.0-impl \
    android.hardware.keymaster@3.0-service

# Lights (display backlight only, liblight/)
PRODUCT_PACKAGES += \
    android.hardware.light@2.0-impl \
    android.hardware.light@2.0-service \
    lights.msm8974

# LiveDisplay: colour calibration through the MDSS PCC (fb0/rgb, created by
# mdss_livedisplay). The simple-framebuffer of the software mode has none.
ifneq ($(TARGET_MARTINI_GRAPHICS),software)
# (manifest fragment: DEVICE_MANIFEST_FILE in BoardConfig.mk)
PRODUCT_PACKAGES += \
    vendor.lineage.livedisplay@2.0-service-sysfs
endif

# Media profile
PRODUCT_COPY_FILES += \
    frameworks/av/media/libstagefright/data/media_codecs_google_audio.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs_google_audio.xml \
    frameworks/av/media/libstagefright/data/media_codecs_google_telephony.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs_google_telephony.xml \
    frameworks/av/media/libstagefright/data/media_codecs_google_video.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs_google_video.xml \
    $(MSM8974_COMMON_PATH)/configs/media_codecs.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs.xml \
    $(MSM8974_COMMON_PATH)/configs/media_codecs_performance.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs_performance.xml \
    $(MSM8974_COMMON_PATH)/configs/media_profiles_V1_0.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_profiles_V1_0.xml

# Media
PRODUCT_PACKAGES += \
    libc2dcolorconvert \
    libOmxAacEnc \
    libOmxAmrEnc \
    libOmxCore \
    libOmxEvrcEnc \
    libOmxQcelp13Enc \
    libOmxVdec \
    libOmxVenc \
    libstagefrighthw

# Misc dependency packages
PRODUCT_PACKAGES += \
    libcnefeatureconfig \
    libtinyxml \
    libxml2

# Capacitive keys
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/keylayout/synaptics_rmi4_i2c.kl:$(TARGET_COPY_OUT_VENDOR)/usr/keylayout/synaptics_rmi4_i2c.kl

PRODUCT_PACKAGES += \
    vendor.lineage.touch@1.0-service.martini

# Radio: the Windows modem firmware leaves the USIM DETECTED until the host
# opens the provisioning session (ril/martini-uim-bootstrap.c)
PRODUCT_PACKAGES += \
    martini-uim-bootstrap

# NFC: NXP PN544 (HCI, pre-NCI), driven by the legacy NXP stack
# (external/libnfc-nxp and the nxp variant of packages/apps/Nfc).
PRODUCT_PACKAGES += \
    libnfc \
    libnfc_jni \
    nfc.martini \
    Nfc \
    Tag

# Permissions
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.nfc.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.nfc.xml \
    external/ant-wireless/antradio-library/com.dsi.ant.antradio_library.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/com.dsi.ant.antradio_library.xml \
    frameworks/native/data/etc/handheld_core_hardware.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/handheld_core_hardware.xml \
    frameworks/native/data/etc/android.hardware.audio.low_latency.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.audio.low_latency.xml \
    frameworks/native/data/etc/android.hardware.bluetooth_le.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.bluetooth_le.xml \
    frameworks/native/data/etc/android.hardware.camera.flash-autofocus.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.camera.flash-autofocus.xml \
    frameworks/native/data/etc/android.hardware.camera.front.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.camera.front.xml \
    frameworks/native/data/etc/android.hardware.location.gps.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.location.gps.xml \
    frameworks/native/data/etc/android.hardware.sensor.accelerometer.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.accelerometer.xml \
    frameworks/native/data/etc/android.hardware.sensor.compass.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.compass.xml \
    frameworks/native/data/etc/android.hardware.sensor.light.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.light.xml \
    frameworks/native/data/etc/android.hardware.sensor.gyroscope.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.gyroscope.xml \
    frameworks/native/data/etc/android.hardware.sensor.proximity.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.proximity.xml \
    frameworks/native/data/etc/android.hardware.sensor.stepcounter.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.stepcounter.xml \
    frameworks/native/data/etc/android.hardware.sensor.stepdetector.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.stepdetector.xml \
    frameworks/native/data/etc/android.hardware.telephony.gsm.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.telephony.gsm.xml \
    frameworks/native/data/etc/android.hardware.touchscreen.multitouch.jazzhand.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.touchscreen.multitouch.jazzhand.xml \
    frameworks/native/data/etc/android.hardware.usb.accessory.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.usb.accessory.xml \
    frameworks/native/data/etc/android.hardware.usb.host.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.usb.host.xml \
    frameworks/native/data/etc/android.hardware.wifi.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.wifi.xml \
    frameworks/native/data/etc/android.hardware.wifi.direct.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.wifi.direct.xml \
    frameworks/native/data/etc/android.software.midi.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.midi.xml \
    frameworks/native/data/etc/android.software.sip.voip.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.sip.voip.xml

# Privapp Whitelist
PRODUCT_COPY_FILES += \
    $(MSM8974_COMMON_PATH)/configs/privapp-permissions-qti.xml:$(TARGET_COPY_OUT_SYSTEM)/etc/permissions/privapp-permissions-qti.xml

# Power
# The power HAL takes its boosts from libmartini-perflock (perf/), named by
# ro.vendor.extension_library in system.prop: mpdecision, which serves them
# on the OnePlus One, cannot run without CPU hotplug.
PRODUCT_PACKAGES += \
    android.hardware.power-service-qti \
    libmartini-perflock

# Rootdir
PRODUCT_PACKAGES += \
    fstab.martini \
    init.martini.rc \
    init.martini.power.rc \
    init.martini.qcom.rc \
    init.martini.wifi.sh \
    init.qcom.bt.sh \
    libbtqmishim \
    init.qcom.usb.rc \
    ueventd.martini.rc

# Seccomp
PRODUCT_COPY_FILES += \
    $(MSM8974_COMMON_PATH)/seccomp/mediacodec.policy:$(TARGET_COPY_OUT_VENDOR)/etc/seccomp_policy/mediacodec.policy

# Sensors: sensors.martini on the kernel input drivers (sensors/), loaded in
# passthrough mode by system_server (group input) through the 1.0 impl.
PRODUCT_PACKAGES += \
    android.hardware.sensors@1.0-impl \
    sensors.martini

# Thermal config
PRODUCT_COPY_FILES += \
    $(MSM8974_COMMON_PATH)/configs/thermal-engine.conf:$(TARGET_COPY_OUT_VENDOR)/etc/thermal-engine-8974.conf

# TimeKeep
PRODUCT_PACKAGES += \
    timekeep \
    TimeKeep

# Trust HAL
PRODUCT_PACKAGES += \
    vendor.lineage.trust@1.0-service

# USB
PRODUCT_PACKAGES += \
    android.hardware.usb@1.0-service.basic

# Vibrator: PM8941 vibration motor (timed_output/vibrator, kernel
# qcom,vibrator@c000), driven by vibrator.default through the 1.0 HAL.
# The HAL must only be declared while that device exists: a failing
# vibrator HAL blocks system_server in VibratorService.vibratorInit.
PRODUCT_PACKAGES += \
    android.hardware.vibrator@1.0-impl \
    android.hardware.vibrator@1.0-service

# VNDK
PRODUCT_COPY_FILES += \
    prebuilts/vndk/v29/arm/arch-arm-armv7-a-neon/shared/vndk-core/libprotobuf-cpp-lite.so:$(TARGET_COPY_OUT_VENDOR)/lib/libprotobuf-cpp-lite-v29.so

# WiFi
# WCNSS_cfg.dat is a generic fallback: the 1520's own qcwlan8974cfg.dat
# (vendor proprietary-local/) wins when present. The NV file is generic.
PRODUCT_COPY_FILES += \
    $(MSM8974_COMMON_PATH)/wifi/WCNSS_cfg.dat:$(TARGET_COPY_OUT_VENDOR)/firmware/wlan/prima/WCNSS_cfg.dat \
    $(MSM8974_COMMON_PATH)/wifi/WCNSS_qcom_cfg.ini:$(TARGET_COPY_OUT_VENDOR)/firmware/wlan/prima/WCNSS_qcom_cfg.ini \
    $(MSM8974_COMMON_PATH)/wifi/WCNSS_qcom_wlan_nv.bin:$(TARGET_COPY_OUT_VENDOR)/firmware/wlan/prima/WCNSS_qcom_wlan_nv.bin

PRODUCT_PACKAGES += \
    android.hardware.wifi@1.0-service.legacy \
    hostapd \
    wcnss_service \
    wpa_supplicant

PRODUCT_PACKAGES += \
    wpa_supplicant.conf

# The msm8974-common modules for these are only defined for bacon/find7/n3
PRODUCT_COPY_FILES += \
    $(MSM8974_COMMON_PATH)/wifi/wpa_supplicant_overlay.conf:$(TARGET_COPY_OUT_VENDOR)/etc/wifi/wpa_supplicant_overlay.conf \
    $(MSM8974_COMMON_PATH)/wifi/p2p_supplicant_overlay.conf:$(TARGET_COPY_OUT_VENDOR)/etc/wifi/p2p_supplicant_overlay.conf

# Audio bring-up tools (userdebug/eng)
PRODUCT_PACKAGES_DEBUG += \
    tinycap \
    tinymix \
    tinyplay

# Bring-up debugging: adbd runs as root from the first boot (userdebug,
# ro.debuggable=1); adb is enabled by default on debuggable builds and
# SELinux is permissive from the kernel command line.
PRODUCT_SYSTEM_DEFAULT_PROPERTIES += \
    service.adb.root=1

$(call inherit-product, $(SRC_TARGET_DIR)/product/product_launched_with_k.mk)

# Recovery: blocking FunctionFS for adbd (kernel 3.4)
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/rootdir/etc/init.recovery.martini.rc:$(TARGET_COPY_OUT_RECOVERY)/root/init.recovery.martini.rc
