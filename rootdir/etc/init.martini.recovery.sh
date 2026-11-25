#!/vendor/bin/sh
# Gives the LineageOS Updater a recovery to reboot into. The installer leaves
# the recovery partition empty (TWRP only runs from the PC): when it holds no
# boot image, this writes the recovery of this build there, through the
# install-recovery.sh the build generates. A recovery already there, TWRP or
# ours, is left alone: TWRP also installs Updater packages from the block map.
# "Update recovery" in the Updater (persist.vendor.recovery_update) keeps the
# LineageOS recovery in step with every build, and would overwrite TWRP.
part=/dev/block/platform/msm_sdcc.1/by-name/recovery
if [ "$(/vendor/bin/head -c 8 $part 2>/dev/null)" = "ANDROID!" ]; then
    /vendor/bin/log -t recovery "Recovery partition holds a boot image, left as it is"
    exit 0
fi
/vendor/bin/log -t recovery "Recovery partition empty, writing the LineageOS recovery"
exec /vendor/bin/sh /vendor/bin/install-recovery.sh
