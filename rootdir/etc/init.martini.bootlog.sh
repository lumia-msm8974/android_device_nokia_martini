#!/vendor/bin/sh
#
# Copyright (C) 2026 The LineageOS Project
#
# SPDX-License-Identifier: Apache-2.0
#
# Bring-up aid (non-user builds, persist.vendor.martini.bootlog=1): the
# Lumia 930, like the 1520, has no pstore or last_kmsg (the RAM is cleared
# at every reset), so keep the kernel log, the process list, the properties
# and the recent logcat on /data every 5 s for the first 30 min of each
# boot. A boot that never reaches adb can then be read back over USB mass
# storage or from TWRP. The previous boot's copy is kept as *.prev.
# The periodic sync costs eMMC writes: turn it off for releases.
d=/data/vendor/bootlog
for f in dmesg ps props logcat uptime; do
    [ -f $d/$f.txt ] && mv -f $d/$f.txt $d/$f.prev.txt
done
n=0
while [ $n -lt 360 ]; do
    dmesg > $d/dmesg.txt 2>&1
    ps -A > $d/ps.txt 2>&1
    getprop > $d/props.txt 2>&1
    logcat -b all -d -t 4000 > $d/logcat.txt 2>&1
    cat /proc/uptime > $d/uptime.txt
    sync
    sleep 5
    n=$((n + 1))
done
