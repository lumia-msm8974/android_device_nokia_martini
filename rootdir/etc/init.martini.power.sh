#!/vendor/bin/sh
#
# Copyright (C) 2026 The LineageOS Project
#
# SPDX-License-Identifier: Apache-2.0
#
# CPU-to-DDR bandwidth governor, as on the OnePlus One: cpubw_hwmon
# (registered by the Krait L2 performance monitor driver, krait-l2pm)
# votes the bandwidth the CPUs actually use, instead of the device tree
# default "cpufreq" governor that votes it from the CPU frequency.
#
# The devfreq device takes the name of its platform device, qcom,cpubw.<N>,
# where N counts the device tree nodes without a reg property populated
# before it: 40 on the OnePlus One, 41 on the Lumia 1520, and it moves
# whenever such a node is added to the device tree. Write every cpubw node
# instead of a fixed name.
#
# Run by init.martini.power.rc after boot when
# persist.vendor.martini.cpubw_hwmon is 1 (setprop it to 0 to keep the
# device tree governor). The GPU devfreq node is not touched: its default
# governor (msm-adreno-tz with the in-kernel DCVS) needs no TrustZone call.

for gov in /sys/class/devfreq/*cpubw*/governor; do
    [ -e "$gov" ] || continue
    echo cpubw_hwmon > "$gov"
done
