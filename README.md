# TWRP device tree for the Nokia Lumia 930 (martini)

TWRP 3.7.0_9 for the Nokia Lumia 930 (RM-1045, Snapdragon 800 / MSM8974)
running Android through the Windows Phone boot chain: Windows Phone UEFI →
bootmgr → BootShim → lk1st (lk2nd) → boot or recovery partition.

**Status: built, never booted.** No Lumia 930 has been tried yet. Start it
only in RAM (`fastboot boot`), never flash it before it has been seen working.

## Build

Minimal TWRP manifest, `twrp-9.0` branch:

```bash
repo init --depth=1 -u https://github.com/minimal-manifest-twrp/platform_manifest_twrp_omni.git -b twrp-9.0
repo sync
git clone https://github.com/lumia-msm8974/android_device_nokia_martini -b twrp-9.0 device/nokia/martini
export ALLOW_MISSING_DEPENDENCIES=true
. build/envsetup.sh
lunch omni_martini-eng
mka recoveryimage
```

The Android 9 build system needs python2 as `python` and `LC_ALL=C` on newer
hosts (Ubuntu 20.04 used here).

## Differences from the Lumia 1520 tree

- Kernel `lineage-18.1-martini` (see `prebuilt/README`): for the first boot the
  screen is the framebuffer the UEFI leaves running (simple-framebuffer), MDSS
  stays off. Pixel format and stride are taken from the 1520 and must be
  checked on the phone.
- AMOLED panel with DCS brightness: no brightness control in this first build,
  the screen is never blanked.
- No microSD slot: no `external_sd` entry.
- Same Android partition layout as the 1520 installer, same Synaptics touch bus
  (BLSP1 I2C2, 0x4b), same legacy FunctionFS adb setup.

## What to check first on the phone

1. `fastboot boot` from lk1st: does adb come up (`adb devices` shows recovery)?
2. Is the TWRP interface on screen, with the right colours and orientation?
3. Touch: taps land where the finger is.
4. `adb shell cat /proc/partitions` and the by-name links match the installer
   layout; `/data` and `/cache` mount.

## Credits

- [TeamWin](https://github.com/TeamWin): TWRP.
- [LineageOS](https://github.com/LineageOS): MSM8974 kernel and device trees.
- Markynose (Mark Buchok), [lumia930-linux](https://github.com/Markynose/lumia930-linux):
  Lumia 930 hardware map (touch, framebuffer, keys) and boot chain.
- zh / hao (2314372037), [android_device_nokia_martini](https://github.com/2314372037/android_device_nokia_martini):
  LineageOS 17.1 and TWRP work on the Lumia 930.
- KorelisLabs, [lumia-1520-mainline](https://github.com/KorelisLabs/lumia-1520-mainline):
  the Lumia 1520 boot chain this port follows.
- imbushuo (Bingxing Wang), [boot-shim](https://github.com/imbushuo/boot-shim);
  msm8916-mainline, [lk2nd](https://github.com/msm8916-mainline/lk2nd).
