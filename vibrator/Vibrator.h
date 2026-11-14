/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <android/hardware/vibrator/1.0/IVibrator.h>

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

namespace android {
namespace hardware {
namespace vibrator {
namespace V1_0 {
namespace implementation {

class Vibrator : public IVibrator {
  public:
    Vibrator();
    ~Vibrator();

    Return<Status> on(uint32_t timeoutMs) override;
    Return<Status> off() override;
    Return<bool> supportsAmplitudeControl() override;
    Return<Status> setAmplitude(uint8_t amplitude) override;
    Return<void> perform(Effect effect, EffectStrength strength, perform_cb _hidl_cb) override;

  private:
    // Waveform and route settings, read again at the start of every burst so
    // that they can be tuned at runtime (persist.vendor.vibra.*).
    struct Config {
        int kick;          // acceleration amplitude (PCM sample)
        int drive;         // steady amplitude at full strength
        int brake;         // braking amplitude, played with reversed sign
        unsigned kickMs;   // acceleration length
        unsigned brakeMs;  // braking length
        unsigned freq;     // 0: constant level (DC), else square wave at this frequency
        int lineoutGain;   // LINEOUT2/LINEOUT4 Volume
        int hpf;           // RX5 HPF Switch
    };

    void worker();
    bool burst(const Config &cfg);
    void route(const Config &cfg, bool enable);
    void writeEnable(uint32_t ms);
    static Config readConfig();

    std::mutex mLock;
    std::condition_variable mCond;
    std::thread mThread;
    bool mExit = false;
    // Requested vibration, in frames from the start of the burst being played.
    uint64_t mPlayed = 0;
    uint64_t mUntil = 0;
    bool mPending = false;
    uint8_t mAmplitude = 255;
};

}  // namespace implementation
}  // namespace V1_0
}  // namespace vibrator
}  // namespace hardware
}  // namespace android
