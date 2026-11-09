# Device tree for the Nokia Lumia 930 (martini) — LineageOS 18.1

**Status: not booted — the device is not available yet.** The tree is
written to be consistent and ready to build, but no image built from it has
ever run on a Lumia 930. Nothing here is verified on the hardware.

| | |
|---|---|
| SoC | Qualcomm MSM8974 (Snapdragon 800), 4x Krait 400, ARMv7 |
| GPU | Adreno 330 |
| RAM / storage | 2 GB / 32 GB eMMC, no microSD |
| PMIC / codec | PM8941 + PM8841 / WCD9320, TAS2552 speaker amplifier |
| Wi-Fi / BT | WCNSS Pronto (WCN3680) |
| Display | 5" 1080x1920 AMOLED, DSI video mode, ~441 ppi (density 480) |
| Touch | Synaptics RMI4, BLSP1 I2C2 0x4b, IRQ GPIO61 |
| Sensors | APDS-9930 light/proximity (BLSP1 I2C3 0x39); accelerometer, gyroscope, magnetometer not identified |
| Battery | BV-5QW, 2420 mAh (2500 mAh in some sources), Qi wireless charging |
| Cameras | 20 MP PureView rear (ACPI QCOM2436), 1.2 MP front (QCOM2437): not supported |
| Keys | volume up (PM8941 GPIO5), volume down (RESIN), camera focus/shutter (PM8941 GPIO4/3), power; on-screen navigation |
| Variants | RM-1045 (RM-1087 reported, unverified) |

The Lumia Icon / 929 (RM-927, "vanquish") ships a different Windows
firmware set and is not a target of this tree.

## Origin: the Lumia 1520 (bandit) tree

This tree is a copy of
[android_device_nokia_bandit](https://github.com/lumia-msm8974/android_device_nokia_bandit)
(branch `lineage-18.1`, commit `7262727`), the LineageOS 18.1 port of the
Lumia 1520, which has the same SoC and the same Windows Phone firmware
family and boots. The first commit is that tree with every name changed from
bandit to martini; each later commit is one adaptation, so
`git log -p` shows the whole difference from the 1520.

Inherited unchanged in principle (same SoC/firmware, still to be verified on
the 930):

- boot chain: Nokia UEFI → bootmgr → BootShim → lk1st (`lk2nd`, branch
  `martini`) → `boot.img` (zImage + appended DTB `msm8974-v2.2-martini`,
  base `0x10000000`). The ELF loaded by BootShim must have its entry point
  at `0x7cc00000` on the 930 (`0x0ff00000` on the 1520);
- restricted Windows Phone TrustZone (`msm_tz_restricted=1`): no SCM CPU
  bring-up (cores 1-3 through the UEFI parking mailboxes), no power
  collapse, no hotplug, no QSEE (software keymaster, gatekeeper, FDE);
- helpers: `libmartini-perflock` (power HAL boosts without
  mpdecision), `libbtqmishim`, `libaudioclient_shim`;
- modem: PIL with SSD decryption in the kernel, `lumia-uim-bootstrap`
  (opens the USIM provisioning session the Windows MPSS leaves DETECTED; it
  accepts `ro.boot.hardware` bandit and martini), QMI RIL from the generic
  MSM8974 blobs, `rmt_storage` on the copied EFS partitions;
- Wi-Fi/Bluetooth: Pronto with the phone's own NV, MAC and BD address
  provisioned into `/persist` from the Windows DPP partition
  (`init.martini.wifi.sh`, `init.qcom.bt.sh`);
- audio: WCD9320 mixer paths with the loudspeaker on the TAS2552 amplifier
  on the secondary MI2S (BLSP1 QUP5 0x40, enable PM8941 GPIO20);
- GPS through the msm8974-common location HAL and QMI LOC;
- partition layout created inside the Windows Data space by the installer.

Adapted for the 930:

- product `lineage_martini`, model "Lumia 930", OTA assert `martini,RM-1045`;
- density 480 (5" 1080p), AMOLED video mode panel: no doze display state;
- lights HAL: only the `lcd-backlight` device of the MDSS panel driver (the
  AMOLED brightness is a DCS command): no WLED fallback, no key backlight;
- on-screen navigation bar (`config_showNavigationBar`,
  `qemu.hw.mainkeys=0`), no key disabler; hardware keys declared: camera
  and volume;
- no microSD (fstab, TWRP);
- sensors HAL: APDS-9930 light/proximity, also accepting the input names of
  the 1520 driver (`bandit-light`, `bandit-prox`); the 3-axis sensors stay
  unlisted until their chips are known;
- NFC: the legacy PN544 path is kept so the tree builds, but no EEPROM
  tuning is listed (the 1520 values belong to its antenna), so the NFC
  service does not initialise the chip.

Layout: `BoardConfig.mk`, `device.mk`, `lineage_martini.mk`; `init/`
(`libinit_martini`), `liblight/`, `sensors/` (`sensors.martini`), `nfc/`
(`nfc.martini`), `ril/`, `perf/`, `libshims/`, `rootdir/`
(`fstab.martini`, `init.martini.*`, `ueventd.martini.rc`), `audio/`,
`overlay/`, `sepolicy/`, `twrp/`.

## Building

With the local manifest of
[martini-manifests](https://github.com/lumia-msm8974/martini-manifests) in
a LineageOS 18.1 checkout:

```
source build/envsetup.sh
lunch lineage_martini-userdebug
m bacon
```

`TARGET_MARTINI_GRAPHICS=software` builds the fallback display stack on the
UEFI framebuffer (see `device.mk`) if the MDSS panel driver does not work.

The phone's own firmware and calibration are taken from its Windows
firmware by `vendor/nokia/martini/extract-local.sh` and never committed.

## TODO

To read from the phone's DSDT/SSDT and registry:

- [ ] accelerometer, gyroscope, magnetometer: chips, buses, addresses,
      mounting matrices (`sensors/sensors_martini.h`, `ueventd.martini.rc`,
      `sepolicy/genfs_contexts`);
- [ ] NFC controller: chip (PN544 like the 1520, or an NCI chip), bus,
      GPIOs, clock source, UICC/SWP wiring (`nfc/`);
- [ ] capacitive navigation keys: does the touch controller report Back,
      Start and Search (Synaptics F1A)? The DSDT functions known so far are
      F01, F11 and F12 (`keylayout/synaptics_rmi4_i2c.kl`);
- [ ] product code(s) of the phone (RM-1045, RM-1087) for the OTA assert;
- [ ] I2C bus numbers of the martini kernel DT (sensor and touch sysfs
      paths, `TARGET_TAP_TO_WAKE_NODE`);
- [ ] audio: TAS2552 wiring and microphones (ACDB device IDs).

To extract from the 930 Windows firmware (FFU or the phone's MainOS):

- [ ] ADSP, modem + MBA, WCNSS, Venus images and `qcwlan8974cfg.dat`;
- [ ] ACDB audio calibration files;
- [ ] DPP provisioning (Wi-Fi NV/MAC, Bluetooth address) through the
      installer.

To test on the phone:

- [ ] lk1st load addresses and boot of `boot.img`; SMP through the parking
      mailboxes; cpufreq range;
- [ ] MDSS panel and `lcd-backlight` range; doze;
- [ ] touch, double tap to wake, keys;
- [ ] Wi-Fi, Bluetooth, modem (UIM bootstrap, SIM, data, calls), GPS;
- [ ] audio routes (speaker, earpiece, headset, microphones);
- [ ] battery and Qi charging (power supply sysfs paths), vibrator;
- [ ] installer partition layout against the 930 GPT (unique names: no
      second `userdata`).

## Credits

- [LineageOS](https://github.com/LineageOS): `android_device_oppo_msm8974-common`,
  `android_device_oneplus_bacon`, `android_device_oppo_common` and the
  msm8974 kernel tree (OnePlus One / Find 7), the base of the bandit and
  martini trees.
- [TheMuppets](https://github.com/TheMuppets): `proprietary_vendor_oppo`
  (generic MSM8974 blobs).
- Markynose (Mark Buchok), [lumia930-linux](https://github.com/Markynose/lumia930-linux):
  the 930 hardware map from its DSDT/SSDT (touch, light/proximity sensor,
  keys, battery), boot chain and SMP through the UEFI parking mailboxes.
- 2314372037 ("zh"), [android_device_nokia_martini](https://github.com/2314372037/android_device_nokia_martini)
  (LineageOS 17.1, branch `lineage-17.1`, `da9e108`): the first Android port
  that boots on the 930; used for comparison (TAS2552 speaker route,
  partition layout, key mappings).
- KorelisLabs, [lumia-1520-mainline](https://github.com/KorelisLabs/lumia-1520-mainline):
  boot chain, hardware map and TrustZone findings for the 1520.
- Android4Lumia950 (djokcho, EpicLPer, j0sh1x, sonic011gamer and the
  community): the Lumia 950/950 XL Android port this project follows.
- msm8916-mainline, [lk2nd](https://github.com/msm8916-mainline/lk2nd), and
  Mainline4Lumia, [lk2nd](https://github.com/Mainline4Lumia/lk2nd) branch
  `martini-support`: base of lk1st.
- imbushuo, [boot-shim](https://github.com/imbushuo/boot-shim).
- René Lergner (HeathCliff), [WPinternals](https://github.com/ReneLergner/WPinternals).
- [TeamWin](https://github.com/TeamWin): TWRP.
