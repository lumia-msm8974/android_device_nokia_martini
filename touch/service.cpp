/*
 * Copyright (C) 2019 The LineageOS Project
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "vendor.lineage.touch@1.0-service.martini"

#include <android-base/logging.h>
#include <binder/ProcessState.h>
#include <hidl/HidlTransportSupport.h>

#include "KeyDisabler.h"

using android::sp;
using android::OK;

using ::vendor::lineage::touch::V1_0::IKeyDisabler;
using ::vendor::lineage::touch::V1_0::implementation::KeyDisabler;

int main() {
    sp<IKeyDisabler> keyDisabler = new KeyDisabler();

    android::hardware::configureRpcThreadpool(1, true /*callerWillJoin*/);

    if (keyDisabler->registerAsService() != OK) {
        LOG(ERROR) << "Cannot register keydisabler HAL service.";
        return 1;
    }

    LOG(INFO) << "Touch HAL service is ready.";
    android::hardware::joinRpcThreadpool();

    LOG(ERROR) << "Touch HAL service failed to join thread pool.";
    return 1;
}
