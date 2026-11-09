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
| Sensors | APDS-9930 light/proximity (BLSP1 I2C3 0x39, application processor); accelerometer (0x1e), magnetometer (0x0f) and gyroscope (0x68) on BLSP2 QUP6 behind the ADSP Sensor Core |
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
  mpdecision; it also holds the Adreno at 389 MHz or faster during touch,
  fling and launch boosts, `persist.vendor.perflock.gpu_floor_mhz`, 0 =
  off, and keeps the CPU boost through continuous scrolling),
  `libbtqmishim`, `libaudioclient_shim`;
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
- density 480 (5" 1080p), AMOLED video mode panel; always-on display (off
  by default) uses the display DOZE state, where the kernel keeps the MDP
  streaming, puts the panel in DCS idle mode (Windows Glance sequence) and
  holds a wakeup source (`doze_display_state_supported`, no doze suspend);
- lights HAL: only the `lcd-backlight` device of the MDSS panel driver (the
  AMOLED brightness is a DCS command): no WLED fallback, no key backlight;
- on-screen navigation bar (`config_showNavigationBar`,
  `qemu.hw.mainkeys=0`), no key disabler; hardware keys declared: camera
  and volume;
- no microSD (fstab, TWRP);
- sensors HAL: APDS-9930 light/proximity, also accepting the input names of
  the 1520 driver (`bandit-light`, `bandit-prox`). The 3-axis sensors are
  driven by the Qualcomm Sensor Core on the ADSP, as on Windows: the
  experimental stack in `sensors/ssc/` (registry of the 930 from its Windows
  qcSensors settings, `sensors.qcom`, `sensors.msm8974` next to
  `sensors.martini` through the 1.0 multihal) is built by default
  (`MARTINI_SENSORS_SSC=false` leaves it out) and has never run on a 930;
  rollback on the phone: `setprop persist.vendor.martini.ssc 0`, reboot;
  `sensors/ssc/check-sensor-def.py` checks the registry against the Windows
  values;
- NFC: the legacy PN544 path is kept so the tree builds, but no EEPROM
  tuning is listed (the 1520 values belong to its antenna), so the NFC
  service does not initialise the chip.
- thermal: `configs/thermal-engine-8974.conf` for the msm8974 blob, with
  the passive/critical temperatures of the stock DSDT thermal zones where
  they use a sensor the blob reads (TSENS, PM8941 die, PA_THERM): GPU
  step-wise at 85 C, CPU at 83 C (tsens 2) and 95 C (tsens 0), shutdown at
  105 C (90 C on tsens 2, 85 C on PA_THERM). Skin limits from the 14
  Windows zones on SYS_THERM2 (46-54 C: CPU, GPU, charging current,
  display, camera; shutdown at 56 C), read as `pa_therm1`: this needs the
  kernel that renames the ADC_TM zones (`martini-thermal-adc`). Core set
  point 75 C as on the OnePlus One; no hotplug actions;
- CPU bandwidth: `init.martini.power.sh` sets `cpubw_hwmon` on whatever
  `qcom,cpubw.<N>` node the kernel creates (opt out with
  `persist.vendor.martini.cpubw_hwmon=0`);
- power profile: BV-5QW 2420 mAh, cpufreq steps of a speed bin 2 MSM8974
  v2 (2150400 kHz top) with the Nexus 5 currents; unmeasured.

Layout: `BoardConfig.mk`, `device.mk`, `lineage_martini.mk`; `init/`
(`libinit_martini`), `liblight/`, `sensors/` (`sensors.martini`), `nfc/`
(`nfc.martini`), `ril/`, `perf/`, `libshims/`, `rootdir/`
(`fstab.martini`, `init.martini.*`, `ueventd.martini.rc`), `audio/`,
`overlay/`, `sepolicy/`, `twrp/`; `sensors/ssc/` (Sensor Core, optional).

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

## Device settings

[LumiaParts](https://github.com/lumia-msm8974/android_packages_apps_LumiaParts)
(`packages/apps/LumiaParts`, branch `lineage-18.1`, shared with the 1520)
adds *Settings > System > Lumia settings*, a launcher icon and two Quick
Settings tiles for what this tree provides:

- performance profiles: the five LineageOS profiles of
  `android.hardware.power-service-qti` (`TARGET_USES_INTERACTION_BOOST`),
  applied by `libmartini-perflock`;
- double tap to wake (`TARGET_TAP_TO_WAKE_NODE`, `config_supportDoubleTapWake`);
- capacitive keys: on-screen navigation bar through the key disabler
  (`vendor.lineage.touch@1.0-service.martini`) and the key backlight
  (PM8941 KPDBL, `config_deviceSupportsButtonBrightnessControl`).

It only changes the LineageOS and AOSP settings that LineageParts also
uses, so it needs no permission, init or SELinux change here. `device.mk`
adds it when the app is checked out.

## Modem (on, unverified on a 930)

The kernel starts the modem as on the 1520: MSA self-auth, MBA at
0x0d100000 and modem at 0x08000000 (their ELF addresses, inside the stock
UEFI "Subsystem Reserved 0" region), both SSD-encrypted with the 930's own
key and decrypted by the TrustZone while PIL loads them. The radio stack is
the 1520 one: `rmt_storage` on the EFS copies, `qmuxd`, `netmgrd`, the QMI
RIL blob, `lumia-uim-bootstrap` once `qmuxd` runs. No explicit ordering is
needed: the first QMI/SMD client blocks in the kernel until the modem is
up. GPS needs the modem (QMI LOC).

The kernel gate (`/sys/module/pil_q6v5_mss/parameters/`):

- at most `max_attempts` (3) starts per boot, restarts after a crash
  included; none at all after the PBL, the MBA or the TrustZone rejected an
  image; `attempts` shows how many were made;
- a modem that crashes when no start is left is shut down and stays off
  until the next boot (no subsystem restart, so no kernel panic loop).

Kill switch, from the lightest to the strongest:

- in Android: `setprop persist.vendor.martini.modem 0` (as root), reboot.
  `init.martini.qcom.rc` writes `boot_enabled=0` at early-boot, before
  `rmt_storage` and `qmuxd`; back on with
  `setprop persist.vendor.martini.modem 1` and a reboot;
- at run time, for the current boot only:
  `echo 0 > /sys/module/pil_q6v5_mss/parameters/boot_enabled`;
- if Android does not get that far: boot (do not flash) the same boot.img
  with `pil_q6v5_mss.boot_enabled=0` added to its command line
  (`fastboot boot`), then set the property from that boot.

The gate parameters carry their own label (`sysfs_pil_mss`) for
`vendor_init`, which runs the `write` commands of `/vendor/etc/init`.

## Bring-up settings (turn off for releases)

On non-user builds `device.mk` sets:

- `persist.sys.disable_rescue=true`: no RescueParty factory reset prompt
  after a crash loop;
- `persist.vendor.martini.bootlog=1`: `init.martini.bootlog.sh` copies
  `dmesg`, `ps -A`, `getprop` and the recent logcat to
  `/data/vendor/bootlog/` every 5 s for 30 minutes of each boot (previous
  boot in `*.prev.txt`), since the phone has no pstore or last_kmsg. The
  periodic `sync` costs eMMC writes: it must be off in release builds.

## SELinux

Builds are permissive from the kernel command line
(`androidboot.selinux=permissive`, `BoardConfig.mk`) until the policy has
been checked on a 930. `MARTINI_SELINUX_ENFORCING=true m bacon` builds the
same tree without it, so the phone starts enforcing; only the boot image
changes.

`sepolicy/` holds what the martini pieces need on top of
`device/qcom/sepolicy-legacy` and `device/oppo/msm8974-common/sepolicy`,
written from a review of the init scripts and services and from the
denials of the permissive 1520 boots (same stack):

- `lumia-uim-bootstrap` gets its own domain: without one init would not
  start it in enforcing mode (on the 1520 it ran as `init`);
- `vendor_init`: `/dev/wcnss_wlan` (Pronto firmware load), swappiness
  (`proc_swappiness`), the modem gate; the `persistent` (FRP) node moved
  to `ueventd.martini.rc`, as `chown` in an init script only changes the
  by-name link;
- `init.martini.wifi.sh` (`qti_init_shell`): the cfg file in
  `/data/vendor/wifi` and the bind mounts over `/vendor/firmware`;
- gralloc on `/dev/graphics/fb0`; `hci_qcomm_init` on the WCNSS
  `smd_channel_ready` node;
- `file_contexts` regexes for sysfs nodes whose path holds a device or bus
  number known only on the phone (battery and charger supplies, RTC, light
  sensor, touch attributes) and for the MDSS `lcd-backlight` LED, which
  would otherwise be plain `sysfs` and not writable by the light HAL;
- `sepolicy-swiftshader/` and `PRODUCT_REQUIRES_INSECURE_EXECMEM_FOR_SWIFTSHADER`
  only in the `software` and `mdss` graphics modes (shader JIT).

Checked offline: the vendor policy and the full policy compile with the
SDK 30 tools of the build (`checkpolicy`, `secilc`), with no neverallow
failure beyond those the userdebug tree already has, and `checkfc` accepts
`file_contexts`. Never booted: collect `dmesg` and `logcat | grep avc`
from a permissive boot before trying `fastboot boot` with an enforcing
boot image. `init.martini.bootlog.sh` stays permissive on userdebug builds.

## TODO

Read from the stock ACPI tables and registry of the RM-1045 FFU (done):
capacitive keys (Back, Start, Search on F1A, as on the 1520) and their
KPDBL backlight, NFC (NXP PN544 on I2C6 0x2b, EEPROM settings), touch and
double tap timing, auto brightness curve, I2C bus numbers (touch i2c-2,
light sensor i2c-4, NFC i2c-3).

Still open:

- [ ] accelerometer, gyroscope, magnetometer on the Sensor Core (DSDT
      `\_SB.SEN1`, QCOM2495, as on the Nexus 5): does the Windows ADSP image
      take the registry of `sensors.qcom`, and are the axes right
      (`sensors/ssc/`, on by default, `persist.vendor.martini.ssc 0` to turn it off)?
- [ ] product code(s) of the phone (RM-1045, RM-1087) for the OTA assert;
- [ ] audio: microphones (ACDB device IDs); vibrator: Windows drives it
      through the audio codec (`hwnvibraaudiocodec`), the PMIC vibrator
      with the GPIO 8 enable is what the 17.1 port uses.

To extract from the 930 Windows firmware (FFU or the phone's MainOS):

- [ ] ADSP, modem + MBA, WCNSS, Venus images and `qcwlan8974cfg.dat`;
- [ ] ACDB audio calibration files;
- [ ] DPP provisioning (Wi-Fi NV/MAC, Bluetooth address) through the
      installer.

To test on the phone:

- [ ] lk1st load addresses and boot of `boot.img`; SMP through the parking
      mailboxes; cpufreq range and speed bin (top step 1958400, 2150400 or
      2265600 kHz, `power_profile.xml`);
- [ ] thermal-engine: sections loaded, GPU device found, `cpubw_hwmon`
      active, temperatures under load; `pa_therm1` (skin) against the
      temperature of the back cover before trusting the skin rules;
- [ ] GPU floor: `min_pwrlevel` at 1 while scrolling, back to 3 after;
- [ ] MDSS panel and `lcd-backlight` range; always-on display: idle mode
      entry/exit, double tap from it, current draw against a normal screen
      on (`/sys/module/mdss_fb/parameters/video_doze` 1/0);
- [ ] touch, double tap to wake, keys and key backlight (which KPDBL row);
- [ ] NFC (PN544 with the 930 EEPROM settings, SIM card emulation);
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
- The Android Open Source Project: the Nexus 5 (hammerhead) power profile,
  the source of the CPU currents and placeholder values in
  `power_profile.xml`.
- Markynose (Mark Buchok), [lumia930-linux](https://github.com/Markynose/lumia930-linux):
  the 930 hardware map from its DSDT/SSDT (touch, light/proximity sensor,
  keys, battery), boot chain and SMP through the UEFI parking mailboxes.
- 2314372037 ("zh"), [android_device_nokia_martini](https://github.com/2314372037/android_device_nokia_martini)
  (LineageOS 17.1, branch `lineage-17.1`, `da9e108`): the first Android port
  that boots on the 930; used for comparison (TAS2552 speaker route,
  partition layout, key mappings).
- KorelisLabs, [lumia-1520-mainline](https://github.com/KorelisLabs/lumia-1520-mainline):
  boot chain, hardware map and TrustZone findings for the 1520 (also the
  addresses of its BLSP2 QUP6 sensors, which the shared Sensor Core
  configuration of the 930 points to).
- sonic011gamer, [Lumia-Drivers](https://github.com/sonic011gamer/Lumia-Drivers)
  (`c57daab`): the Windows driver packages of the 930; the Sensor Core
  registry in `sensors/ssc/` is decoded from the qcSensors settings of the
  `OEM.SOC_QC8974.NMO` package (and of the official FFU).
- Android4Lumia950 (djokcho, EpicLPer, j0sh1x, sonic011gamer and the
  community): the Lumia 950/950 XL Android port this project follows.
- msm8916-mainline, [lk2nd](https://github.com/msm8916-mainline/lk2nd), and
  Mainline4Lumia, [lk2nd](https://github.com/Mainline4Lumia/lk2nd) branch
  `martini-support`: base of lk1st.
- imbushuo, [boot-shim](https://github.com/imbushuo/boot-shim).
- René Lergner (HeathCliff), [WPinternals](https://github.com/ReneLergner/WPinternals).
- [TeamWin](https://github.com/TeamWin): TWRP.
