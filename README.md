# Device tree for the Nokia Lumia 1520 (martini) — LineageOS 18.1

**Status: bring-up. Builds; never booted.**

| | |
|---|---|
| SoC | Qualcomm MSM8974 (Snapdragon 800), 4x Krait 400, ARMv7 |
| GPU | Adreno 330 |
| RAM / storage | 2 GB / 16-32 GB eMMC |
| PMIC / codec | PM8941 + PM8841 / WCD9320 |
| Wi-Fi / BT | WCNSS Pronto (WCN3680) |
| Display | 1080x1920 DSI, 6" |
| Variants | RM-937, RM-938, RM-939, RM-940 |

## Boot chain

The 1520 has no Android bootloader. Android is started from the Windows
Phone boot manager:

```
Nokia UEFI -> bootmgr -> BCD entry "Android (lk1st)" -> BootShim -> lk1st -> boot.img
```

- lk1st: [giannoccarol/lk2nd](https://github.com/giannoccarol/lk2nd), branch `martini`.
- BootShim: imbushuo's `elf-loader-pre3` release.
- The bootloader must be unlocked with WPinternals first. Holding volume up
  at power-on still reaches flash mode for recovery.

## Known platform constraints

The Windows Phone TrustZone does not implement the SCM calls Android
kernels use on MSM8974:

- SMP bring-up through SCM resets the SoC: single core (`nosmp`) until the
  UEFI parking-mailbox backend (proven on the Lumia 930) is ported;
- deep idle (power collapse) goes through TrustZone: disabled;
- PAS/PIL works (the ADSP boots), so remote processors can be loaded.

## Layout

- `BoardConfig.mk`, `device.mk`: own board and product configuration. They
  no longer inherit `device/oppo/msm8974-common`; they only reuse its generic
  MSM8974 files (Wi-Fi/audio/media/GPS configuration, overlay, sepolicy).
  Left out on purpose: NFC, Oppo touch gestures HAL, Oppo LED HAL and
  libinit, ConfigPanel, Oppo recovery updater and the Oppo TrustZone
  assertion in the OTA package.
- `init/`: vendor init (`libinit_martini`), `liblight/`: backlight-only
  lights HAL, `libshims/`: `libaudioclient_shim` for the Qualcomm RIL blob.
- `sensors/`: `sensors.martini`, sensors HAL on the `martini-*` input devices
  of the kernel drivers (LIS2HH12, L3GD20, HSCDTD008A, APDS-9930). Scales,
  rates and mounting matrices are in `sensors/sensors_martini.h`. The
  Qualcomm ADSP sensor daemon (`sensors.qcom`) is kept off.
- `rootdir/`: `init.martini.rc`, `init.martini.qcom.rc` and `ueventd.martini.rc`
  (from msm8974-common without the Oppo pieces), `init.martini.power.rc`
  (never enables power collapse, never onlines cpu1-3, no mpdecision).
- `audio/`: audio policy and mixer paths. `mixer_paths.xml` comes from the
  OnePlus One (same WCD9320 codec) and is provisional until the 1520
  speaker/microphone wiring is known.
- `system.prop` (QMI RIL library, CABL off), `bluetooth/` (Pronto build
  config), `config.fs` (`/persist` permissions).
- `overlay/`: values that replace the Oppo ones (no lid, no double tap, no
  LED, no hardware keys, on-screen navigation bar).
- Files of the 1520 itself (firmware, Wi-Fi config, audio calibration) are
  taken from your own phone by `vendor/nokia/martini/extract-local.sh` and are
  never committed; see the vendor README.
- Kernel: [giannoccarol/android_kernel_nokia_msm8974](https://github.com/giannoccarol/android_kernel_nokia_msm8974),
  branch `lineage-18.1-martini`.
- Vendor: [giannoccarol/android_vendor_nokia_martini](https://github.com/giannoccarol/android_vendor_nokia_martini).
- Manifest: [giannoccarol/martini-manifests](https://github.com/giannoccarol/martini-manifests).

## Credits

- [LineageOS](https://github.com/LineageOS): `android_device_oneplus_bacon`,
  `android_device_oppo_msm8974-common`, `android_device_oppo_common` and
  `android_kernel_oppo_msm8974`, used as the base of this tree and kernel.
- KorelisLabs, [lumia-1520-mainline](https://github.com/KorelisLabs/lumia-1520-mainline):
  boot chain, hardware map and TrustZone findings for the 1520.
- Markynose (Mark Buchok), [lumia930-linux](https://github.com/Markynose/lumia930-linux):
  SMP through the stock UEFI parking mailboxes on MSM8974 Lumias.
- imbushuo, [boot-shim](https://github.com/imbushuo/boot-shim).
- msm8916-mainline, [lk2nd](https://github.com/msm8916-mainline/lk2nd).
- René Lergner (HeathCliff), [WPinternals](https://github.com/ReneLergner/WPinternals).
- Android4Lumia950 (djokcho, EpicLPer, j0sh1x and the community): the Lumia
  950/950 XL Android port this project follows.
