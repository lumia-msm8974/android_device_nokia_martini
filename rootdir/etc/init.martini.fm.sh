#!/vendor/bin/sh
#
# Copyright (C) 2026 The LineageOS Project
#
# SPDX-License-Identifier: Apache-2.0
#
# FM radio on the WCN3680 (the iris FM core of the Pronto subsystem).
# Started by init as vendor.fm (init.martini.fm.rc) when the FM JNI
# (vendor/qcom/opensource/libfmjni, used by packages/apps/FMRadio) powers
# the radio up: the JNI opens /dev/radio0, sets vendor.hw.fm.mode=normal
# and vendor.hw.fm.init=0, starts this service and waits up to 9 s for
# vendor.hw.fm.init=1.
#
# radio-iris-transport opens the APPS_FM SMD channel to the Pronto firmware
# when 1 is written to fmsmd_set; radio-iris closes it again and resets
# fmsmd_set when the JNI releases /dev/radio0. If the channel did not open
# the driver still leaves fmsmd_set at 1, so a stale 1 is cleared first.
#
# The Qualcomm init.qcom.fm.sh also runs fm_qsoc_patches here: patches for
# the external FM chips (WCN2243 and older) and a stored calibration for the
# WCN36xx. Neither the binary nor its source is in this tree. The WCN3680 FM
# core runs from the Pronto firmware, so the radio works without it, just
# uncalibrated; Windows does calibrate it (qcfmminiport.dll DoCalibration).

param=/sys/module/radio_iris_transport/parameters/fmsmd_set

mode=$(getprop vendor.hw.fm.mode)
case "$mode" in
normal|"")
    [ "$(cat $param 2>/dev/null)" = 1 ] && echo 0 > $param
    if ! echo 1 > $param; then
        log -p e -t martini-fm "cannot enable the FM SMD transport"
        exit 1
    fi
    ;;
*)
    # wa_enable / wa_disable: workarounds of the external FM chips
    log -t martini-fm "FM mode $mode: nothing to do on the WCN3680"
    ;;
esac

setprop vendor.hw.fm.init 1
exit 0
