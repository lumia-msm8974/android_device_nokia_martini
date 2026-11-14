# VoLTE on the Lumia 930: phh IMS for Android 11

Voice over LTE without the Qualcomm IMS stack. The modem only carries the
IMS PDN (APN `ims`); registration, IPsec and calls are done on the
application processor by phh IMS, a userspace SIP/IMS stack that acts as the
device MmTel `ImsService`. The voice is an AP VoIP stream
(`config_use_voip_mode_for_ims`), not a modem voice session.

Status: built, **not yet tried on a 930**. Same design as the working VoLTE
of the Lumia 950 XL port (Android 16, Iliad Italy, 7/11/2026).

## Pieces

| Piece | Where | What it does |
|---|---|---|
| `PhhIms` | `ims/Android.bp`, `prebuilt/PhhIms.apk` | phh IMS (krazey fork) with the patches of `patches/`, re-signed with the platform key, in `priv-app` |
| `libril-lumia-pcscf` | `ril/lumia-ril-pcscf.c` | rild loads it instead of qcril (`vendor.rild.libpath`); it fills the P-CSCF of the data calls, which the MSM8974 qcril never reports |
| `lumia-ims-profile` | `ril/lumia-ims-profile.c` | sets the modem's `ims` 3GPP profiles to IPv4v6 with the P-CSCF asked in the PCO |
| overlays | `overlay/` | VoLTE available, no VT/WFC, VoIP audio mode for IMS calls, `config_ims_mmtel_package` = `me.phh.ims`, CarrierConfig for Iliad (222-50) |
| permissions | `ims/privapp-permissions-me.phh.ims.xml` | privileged permissions of the app |

### Why the RIL wrapper

Android 11 takes the P-CSCF only from the data call response. The qcril blob
has no P-CSCF code at all (it never calls `dsi_iface_ioctl()`), and on Iliad
the IMS PDN has no DNS server, so the 3GPP DNS fallback of phh cannot work
either. For every active call with an empty P-CSCF field the wrapper opens a
QMI WDS client on the same rmnet port, with the IP family of the call, and
reads the runtime settings (`GET_RUNTIME_SETTINGS`: P-CSCF IPv4 list `0x23`,
IPv6 list `0x2e`, plus the IP address, which must match the call). Results
are cached per call. `persist.vendor.lumia.ril_pcscf=0` makes it a pure
pass-through; setting `rild.libpath`/`vendor.rild.libpath` back to
`/system/vendor/lib/libril-qc-qmi-1.so` removes it.

When an `ims` call comes up without a P-CSCF the wrapper sets
`vendor.radio.lumia_ims_profile=retry`, which runs `lumia-ims-profile` again:
the profile that the vendor lookup (QDP) created for the APN gets the PCO
flag for the next attempt.

## Building the APK

phh IMS needs Kotlin 1.9 and the LineageOS 18.1 tree has 1.3.61, so the APK
is built in the LineageOS 23.2 tree of the 950 XL port, with
`min_sdk_version: "30"` and 32-bit JNI libraries linked against the NDK:

1. sources: [krazey/ims](https://github.com/krazey/ims) at `a3fec01`
   (submodules `app/jni/rnnoise` `f94e734`, `app/jni/webrtc-aec3`
   `6b11f0f` with its own `base/abseil` `852fc61`), then
   `git am patches/*.patch`:
   - `0001`: one 20 ms frame per input buffer of the AMR encoder (crash of
     the software encoder on the first microphone read, found on the 950 XL);
   - `0002`: Android 11 (API 30): per-slot `ImsService` entry points,
     subscription changes followed by the service, `PhoneStateListener`,
     registered PLMN from the operator numeric, Android 11 equivalents of
     the Android 12-13 calls, build flags.
2. build `PhhIms` in a LineageOS 21+ tree. The project builds it in the
   tree of the 950 XL port, next to that port's own PhhIms, so the modules
   get other names there (`build-env/build-phhims-a11.sh`, not part of this
   repository): the JNI libraries of `prebuilt/PhhIms.apk` are
   `librnnoise_jni_martini.so` and `libwebrtc_aec_jni_martini.so`, with
   `System.loadLibrary()` changed to match; nothing else differs.

`prebuilt/PhhIms.apk`: krazey `a3fec01` + `patches/` (phh branch
`martini-a11` @ `7d55254`), sha256 `4b54d568…`, `minSdkVersion` 30,
armeabi-v7a only.

## Trying it

After installing a build with these changes (no wipe needed):

```sh
adb shell getprop vendor.radio.lumia_ims_profile      # done
adb logcat -b main -s LumiaIMS LumiaRIL                # profile and P-CSCF
adb shell cmd phone ims get-ims-service -s 0           # me.phh.ims
adb logcat | grep -E 'PHH|SipHandler|ImsResolver|IpSec'
adb shell dumpsys telephony_ims | grep -i registr
```

Settings → Network → SIM → 4G calling must be on. Rollback without a new
build: `adb shell setprop persist.vendor.lumia.ril_pcscf 0` and
`adb shell pm disable me.phh.ims`, then reboot.

If qcril stops answering data requests after a crash of the IMS stack:
`setprop ctl.restart netmgrd`, then `setprop ctl.restart ril-daemon`.

## Limits

Voice only, on LTE: no SRVCC (the call drops when LTE is lost), no video
calls, no Wi-Fi calling (IWLAN needs IPsec tunnel interfaces that the 3.4
kernel lacks), SMS and emergency calls on CS.

## Credits

- Pierre-Hugues Husson (phhusson), [phhusson/ims](https://github.com/phhusson/ims):
  the userspace IMS stack (GPL-2.0).
- krazey, [krazey/ims](https://github.com/krazey/ims): the LineageOS fork
  used as the base (`a3fec01`).
- amikhasenko, [amikhasenko/ims](https://github.com/amikhasenko/ims) and
  [rnnoise](https://github.com/amikhasenko/rnnoise): in-tree `Android.bp`,
  P-CSCF DNS fallback, noise suppression submodule.
- ewan-xu, [AEC3](https://github.com/ewan-xu/AEC3): echo cancellation
  submodule.
- Langsdorff (clangsdorff), [clangsdorff/ims](https://github.com/clangsdorff/ims):
  the `config_use_voip_mode_for_ims` idea.
- TrebleDroid, [vendor_hardware_overlay](https://github.com/TrebleDroid/vendor_hardware_overlay):
  FLOSS-IMS overlay.
- Carrier data: [vorot93/pixel-carriersettings](https://github.com/vorot93/pixel-carriersettings)
  (`iliad_it`), [open-carrier-data](https://github.com/open-carrier-data/open-carrier-data),
  LineageOS `vendor/apn`.
- [libqmi](https://gitlab.freedesktop.org/mobile-broadband/libqmi): QMI WDS
  message and TLV definitions used to check the raw requests.
- The Lumia 950 XL port of this project (`cityman-ims-profile`, the
  `cityman` phh patches): the design this one follows.
