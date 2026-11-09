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
#   QCOM/BT.PROVISION        -> /persist/bdaddr.txt
#     "AA:BB:CC:DD:EE:FF", read by the Bluetooth HAL and init.qcom.bt.sh
# Without the NV file the generic NV stays.
#
# The ROM never writes /persist, so a phone installed without the
# installer's provisioning step had the default addresses of the cfg.ini
# (the same on every phone). When wlan_mac.bin or bdaddr.txt is missing,
# create it here from the DPP partition, mounted read-only on
# /mnt/vendor/dpp by fstab.martini, with the offsets the installer uses
# (WLAN.PROVISION: four addresses from byte 3; BT.PROVISION: byte 2). With
# no usable DPP, write a random locally administered address once, so that
# it stays the same across boots.
#
# usage: init.martini.wifi.sh [<persist dir> <DPP QCOM dir> nobind]
#        (arguments only for tests; init runs it without any)

PERSIST=${1:-/persist}
DPP=${2:-/mnt/vendor/dpp/QCOM}
FW=/vendor/firmware/wlan/prima
NV=$PERSIST/WCNSS_qcom_wlan_nv.bin
MAC=$PERSIST/wlan_mac.bin
BDADDR=$PERSIST/bdaddr.txt
OUT=/data/vendor/wifi

# <file> <offset> <count>: bytes as uppercase hex, no separators
hexbytes() {
    od -An -tx1 -j "$2" -N "$3" "$1" 2>/dev/null | tr -d ' \n' | tr a-f A-F
}

# 12 hex digits, not all zeros or all ones, unicast
valid_mac() {
    case "$1" in
    000000000000|FFFFFFFFFFFF) return 1 ;;
    ?[13579BDF]*) return 1 ;;
    esac
    [ ${#1} -eq 12 ] && [ -z "$(echo "$1" | tr -d 0-9A-F)" ]
}

# <mac> <n>: the same address with n added to the last byte
mac_add() {
    printf '%s%02X' "${1%??}" $(( (0x${1#??????????} + $2) & 255 ))
}

# random locally administered unicast address
random_mac() {
    echo "02$(hexbytes /dev/urandom 0 5)"
}

# write <file> from stdin, atomically
put() {
    cat > "$1.tmp" && chmod 0644 "$1.tmp" && mv -f "$1.tmp" "$1" && sync
}

if [ ! -s "$MAC" ]; then
    m0=$(hexbytes "$DPP/WLAN.PROVISION" 3 6)
    m1=$(hexbytes "$DPP/WLAN.PROVISION" 9 6)
    m2=$(hexbytes "$DPP/WLAN.PROVISION" 15 6)
    m3=$(hexbytes "$DPP/WLAN.PROVISION" 21 6)
    if valid_mac "$m0" && valid_mac "$m1" && valid_mac "$m2" && valid_mac "$m3"; then
        src=DPP
    else
        m0=$(random_mac)
        m1=$(mac_add "$m0" 1)
        m2=$(mac_add "$m0" 2)
        m3=$(mac_add "$m0" 3)
        src=random
    fi
    printf 'Intf0MacAddress=%s\nIntf1MacAddress=%s\nIntf2MacAddress=%s\nIntf3MacAddress=%s\n' \
        "$m0" "$m1" "$m2" "$m3" | put "$MAC" &&
        log -t martini-wifi "created $MAC from $src"
fi

if [ ! -s "$BDADDR" ]; then
    b=$(hexbytes "$DPP/BT.PROVISION" 2 6)
    if valid_mac "$b"; then
        src=DPP
    else
        b=$(random_mac)
        src=random
    fi
    echo "$b" | sed 's/\(..\)/\1:/g; s/:$//' | put "$BDADDR" &&
        log -t martini-wifi "created $BDADDR from $src"
fi

case " $* " in
*" nobind "*) exit 0 ;;
esac

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
