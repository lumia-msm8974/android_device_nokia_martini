/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The 930 has no PMIC vibration motor: the motor driver takes its input from
 * the LINEOUT2/LINEOUT4 differential pair of the WCD9320 codec and is enabled
 * by PM8941 GPIO 8. A vibration is a stream of PCM samples on MultiMedia9,
 * routed to SLIMbus RX1 and from there to RX5 (LINEOUT2) and, inverted through
 * RDAC7, LINEOUT4. The kernel timed_output vibrator raises GPIO 8 for the
 * length of the burst.
 *
 * The waveform follows the module settings of the motor: a constant level
 * (signal type 0, frequency 0) with an acceleration step at the start and a
 * reversed braking step at the end.
 */

#define LOG_TAG "VibratorMartini"

#include "Vibrator.h"

#include <cutils/properties.h>
#include <log/log.h>
#include <tinyalsa/asoundlib.h>

#include <algorithm>
#include <fstream>
#include <string>
#include <vector>

namespace android {
namespace hardware {
namespace vibrator {
namespace V1_0 {
namespace implementation {

namespace {

constexpr unsigned kCard = 0;
constexpr unsigned kDevice = 35;  // MultiMedia9
constexpr unsigned kRate = 48000;
constexpr unsigned kChannels = 2;
constexpr unsigned kPeriodFrames = 240;  // 5 ms
constexpr unsigned kPeriodCount = 4;
constexpr unsigned kFramesPerMs = kRate / 1000;
// Silence kept playing after a burst, so that back-to-back requests (key
// clicks, patterns) do not reopen the stream every time.
constexpr unsigned kIdleMs = 250;

constexpr char kEnablePath[] = "/sys/class/timed_output/vibrator/enable";

// Module settings of the motor.
constexpr int kDefaultKick = 0x2800;
constexpr int kDefaultDrive = 0x1800;
constexpr int kDefaultBrake = 0x2800;
constexpr int kDefaultKickMs = 30;
constexpr int kDefaultBrakeMs = 20;

constexpr int kRxDigitalUnity = 84;  // 0 dB
constexpr int kDefaultLineoutGain = 12;

struct Ctl {
    const char *name;
    const char *value;
};

bool setCtl(struct mixer *mixer, const char *name, const char *value) {
    struct mixer_ctl *ctl = mixer_get_ctl_by_name(mixer, name);
    if (!ctl) {
        ALOGE("mixer control %s not found", name);
        return false;
    }
    int ret;
    if (mixer_ctl_get_type(ctl) == MIXER_CTL_TYPE_ENUM) {
        ret = mixer_ctl_set_enum_by_string(ctl, value);
    } else {
        int v = atoi(value);
        ret = 0;
        for (unsigned i = 0; i < mixer_ctl_get_num_values(ctl) && !ret; i++)
            ret = mixer_ctl_set_value(ctl, i, v);
    }
    if (ret) ALOGE("cannot set %s to %s: %d", name, value, ret);
    return !ret;
}

int16_t clampSample(long v) {
    return static_cast<int16_t>(std::clamp<long>(v, INT16_MIN, INT16_MAX));
}

}  // namespace

Vibrator::Vibrator() : mThread(&Vibrator::worker, this) {}

Vibrator::~Vibrator() {
    {
        std::lock_guard<std::mutex> lock(mLock);
        mExit = true;
        mUntil = 0;
    }
    mCond.notify_all();
    mThread.join();
}

Vibrator::Config Vibrator::readConfig() {
    Config c;
    c.kick = property_get_int32("persist.vendor.vibra.kick", kDefaultKick);
    c.drive = property_get_int32("persist.vendor.vibra.drive", kDefaultDrive);
    c.brake = property_get_int32("persist.vendor.vibra.brake", kDefaultBrake);
    c.kickMs = std::max(0, property_get_int32("persist.vendor.vibra.kick_ms", kDefaultKickMs));
    c.brakeMs = std::max(0, property_get_int32("persist.vendor.vibra.brake_ms", kDefaultBrakeMs));
    c.freq = std::max(0, property_get_int32("persist.vendor.vibra.freq", 0));
    c.lineoutGain = property_get_int32("persist.vendor.vibra.lineout_gain", kDefaultLineoutGain);
    c.hpf = property_get_int32("persist.vendor.vibra.rx_hpf", 1);
    return c;
}

void Vibrator::writeEnable(uint32_t ms) {
    std::ofstream enable(kEnablePath);
    if (!enable) {
        ALOGE("cannot open %s", kEnablePath);
        return;
    }
    enable << ms << std::endl;
}

void Vibrator::route(const Config &cfg, bool enable) {
    struct mixer *mixer = mixer_open(kCard);
    if (!mixer) {
        ALOGE("cannot open mixer %u", kCard);
        return;
    }
    if (enable) {
        std::string gain = std::to_string(cfg.lineoutGain);
        std::string hpf = std::to_string(cfg.hpf);
        std::string rxVol = std::to_string(kRxDigitalUnity);
        // SLIM RX1 MUX is shared with the audio HAL (earpiece, headset):
        // it is set but never cleared here.
        const Ctl on[] = {
                {"SLIM RX1 MUX", "AIF1_PB"},
                {"RX5 MIX1 INP1", "RX1"},
                {"RX5 HPF Switch", hpf.c_str()},
                {"RDAC7 MUX", "DEM5_INV"},
                {"RX5 Digital Volume", rxVol.c_str()},
                {"LINEOUT2 Volume", gain.c_str()},
                {"LINEOUT4 Volume", gain.c_str()},
                {"SLIMBUS_0_RX Audio Mixer MultiMedia9", "1"},
        };
        for (const auto &c : on) setCtl(mixer, c.name, c.value);
    } else {
        const Ctl off[] = {
                {"SLIMBUS_0_RX Audio Mixer MultiMedia9", "0"},
                {"RX5 MIX1 INP1", "ZERO"},
                {"RDAC7 MUX", "DEM6"},
                {"RX5 HPF Switch", "0"},
                {"RX5 Digital Volume", "0"},
                {"LINEOUT2 Volume", "0"},
                {"LINEOUT4 Volume", "0"},
        };
        for (const auto &c : off) setCtl(mixer, c.name, c.value);
    }
    mixer_close(mixer);
}

/*
 * Plays one burst: acceleration, steady level until the requested length,
 * braking, then silence for kIdleMs. A request that arrives during braking or
 * silence starts a new acceleration in the same stream. Returns false if the
 * stream could not be opened.
 */
bool Vibrator::burst(const Config &cfg) {
    route(cfg, true);

    struct pcm_config pc = {};
    pc.channels = kChannels;
    pc.rate = kRate;
    pc.format = PCM_FORMAT_S16_LE;
    pc.period_size = kPeriodFrames;
    pc.period_count = kPeriodCount;
    pc.start_threshold = kPeriodFrames;
    struct pcm *pcm = pcm_open(kCard, kDevice, PCM_OUT, &pc);
    if (!pcm || !pcm_is_ready(pcm)) {
        ALOGE("cannot open PCM %u,%u: %s", kCard, kDevice, pcm ? pcm_get_error(pcm) : "no memory");
        if (pcm) pcm_close(pcm);
        route(cfg, false);
        return false;
    }

    const uint64_t kickFrames = static_cast<uint64_t>(cfg.kickMs) * kFramesPerMs;
    const uint64_t brakeFrames = static_cast<uint64_t>(cfg.brakeMs) * kFramesPerMs;
    const uint64_t idleFrames = static_cast<uint64_t>(kIdleMs) * kFramesPerMs;
    const uint64_t halfPeriod = cfg.freq ? std::max<uint64_t>(1, kRate / (2 * cfg.freq)) : 0;

    std::vector<int16_t> buf(kPeriodFrames * kChannels);
    uint64_t segStart = 0;   // start of the current acceleration
    uint64_t brakeEnd = 0;   // end of the braking after the current request
    uint64_t enabledUntil = 0;
    bool driving = true;

    for (;;) {
        uint64_t pos, until;
        int drive;
        {
            std::lock_guard<std::mutex> lock(mLock);
            pos = mPlayed;
            until = mUntil;
            mPending = false;
            drive = cfg.drive * mAmplitude / 255;
        }
        if (until > pos && !driving) {
            // New request after the motor was released: accelerate again.
            driving = true;
            segStart = pos;
        }
        if (driving && until <= pos) {
            driving = false;
            // Brake only after the motor was actually driven.
            brakeEnd = pos > segStart ? pos + brakeFrames : pos;
        }
        if (!driving && pos >= brakeEnd + idleFrames) break;

        if (driving && until > enabledUntil) {
            // GPIO 8 high until the end of the braking, the kernel drops it.
            writeEnable(static_cast<uint32_t>((until - pos + brakeFrames) / kFramesPerMs) + 20);
            enabledUntil = until;
        }

        for (unsigned i = 0; i < kPeriodFrames; i++) {
            uint64_t f = pos + i;
            long level;
            if (driving && f < until)
                level = (f - segStart < kickFrames) ? cfg.kick : drive;
            else if (!driving && f < brakeEnd)
                level = -cfg.brake;
            else
                level = 0;
            if (halfPeriod && ((f / halfPeriod) & 1)) level = -level;
            int16_t s = clampSample(level);
            for (unsigned c = 0; c < kChannels; c++) buf[i * kChannels + c] = s;
        }
        if (pcm_write(pcm, buf.data(), buf.size() * sizeof(int16_t))) {
            ALOGE("PCM write failed: %s", pcm_get_error(pcm));
            break;
        }
        std::lock_guard<std::mutex> lock(mLock);
        mPlayed += kPeriodFrames;
    }

    pcm_close(pcm);
    route(cfg, false);
    writeEnable(0);
    return true;
}

void Vibrator::worker() {
    std::unique_lock<std::mutex> lock(mLock);
    for (;;) {
        mCond.wait(lock, [this] { return mExit || mPending; });
        if (mExit) return;
        lock.unlock();
        bool opened = burst(readConfig());
        lock.lock();
        // Keep what is left of a request that arrived after the stream closed.
        mUntil = opened && mUntil > mPlayed ? mUntil - mPlayed : 0;
        mPlayed = 0;
        if (!mUntil) mPending = false;
    }
}

Return<Status> Vibrator::on(uint32_t timeoutMs) {
    {
        std::lock_guard<std::mutex> lock(mLock);
        mUntil = mPlayed + static_cast<uint64_t>(timeoutMs) * kFramesPerMs;
        mPending = true;
    }
    mCond.notify_all();
    return Status::OK;
}

Return<Status> Vibrator::off() {
    std::lock_guard<std::mutex> lock(mLock);
    mUntil = std::min(mUntil, mPlayed);
    return Status::OK;
}

Return<bool> Vibrator::supportsAmplitudeControl() {
    return true;
}

Return<Status> Vibrator::setAmplitude(uint8_t amplitude) {
    if (amplitude == 0) return Status::BAD_VALUE;
    std::lock_guard<std::mutex> lock(mLock);
    mAmplitude = amplitude;
    return Status::OK;
}

Return<void> Vibrator::perform(Effect effect, EffectStrength strength, perform_cb _hidl_cb) {
    if (effect != Effect::CLICK) {
        _hidl_cb(Status::UNSUPPORTED_OPERATION, 0);
        return Void();
    }
    uint8_t amplitude;
    switch (strength) {
        case EffectStrength::LIGHT:
            amplitude = 128;
            break;
        case EffectStrength::MEDIUM:
            amplitude = 192;
            break;
        default:
            amplitude = 255;
            break;
    }
    Config cfg = readConfig();
    uint32_t ms = std::max(cfg.kickMs, 10u);
    setAmplitude(amplitude);
    on(ms);
    _hidl_cb(Status::OK, ms + cfg.brakeMs);
    return Void();
}

}  // namespace implementation
}  // namespace V1_0
}  // namespace vibrator
}  // namespace hardware
}  // namespace android
