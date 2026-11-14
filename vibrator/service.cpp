/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "android.hardware.vibrator@1.0-service.martini"

#include <android-base/logging.h>
#include <hidl/HidlTransportSupport.h>

#include "Vibrator.h"

using android::hardware::configureRpcThreadpool;
using android::hardware::joinRpcThreadpool;
using android::hardware::vibrator::V1_0::IVibrator;
using android::hardware::vibrator::V1_0::implementation::Vibrator;

int main() {
    android::sp<IVibrator> vibrator = new Vibrator();

    configureRpcThreadpool(1, true /* callerWillJoin */);
    if (vibrator->registerAsService() != android::OK) {
        LOG(ERROR) << "Cannot register the vibrator HAL service";
        return 1;
    }
    joinRpcThreadpool();
    return 1;  // joinRpcThreadpool should never return
}
