#!/vendor/bin/sh
#
# Copyright (C) 2026 The LineageOS Project
#
# SPDX-License-Identifier: Apache-2.0
#
# Use the Lumia 930's own Wi-Fi calibration and MAC addresses when the
# installer has provisioned them into /persist from the Windows DPP partition
# (as on the Lumia 1520 and 950 XL ports; the 930 has the same WCN3680
# Pronto and DPP provisioning files, not yet read from a 930):
#   QCOM/WLAN_CLPC.PROVISION -> /persist/WCNSS_qcom_wlan_nv.bin
#     same format as Android's WCNSS NV, with the RF calibration of the phone
#   QCOM/WLAN.PROVISION      -> /persist/wlan_mac.bin
#     "IntfNMacAddress=XXXXXXXXXXXX" lines, read by prima from the cfg.ini
# Without these files the generic NV and the default addresses stay.

FW=/vendor/firmware/wlan/prima
NV=/persist/WCNSS_qcom_wlan_nv.bin
MAC=/persist/wlan_mac.bin
OUT=/data/vendor/wifi

if [ -s "$NV" ]; then
    mount -o bind "$NV" "$FW/WCNSS_qcom_wlan_nv.bin" &&
        log -t martini-wifi "WCNSS NV from $NV"
fi

if [ -s "$MAC" ]; then
    mkdir -p "$OUT"
    cp "$FW/WCNSS_qcom_cfg.ini" "$OUT/WCNSS_qcom_cfg.ini"
    for i in 0 1 2 3; do
        v=$(grep -E "^Intf${i}MacAddress=[0-9A-Fa-f]{12}\$" "$MAC" | head -n 1 | cut -d= -f2)
        [ -n "$v" ] && sed -i "s/^Intf${i}MacAddress=.*/Intf${i}MacAddress=$v/" "$OUT/WCNSS_qcom_cfg.ini"
    done
    chmod 0644 "$OUT/WCNSS_qcom_cfg.ini"
    mount -o bind "$OUT/WCNSS_qcom_cfg.ini" "$FW/WCNSS_qcom_cfg.ini" &&
        log -t martini-wifi "MAC addresses from $MAC"
fi

exit 0
