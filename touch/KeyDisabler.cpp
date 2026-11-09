/*
 * Copyright (C) 2019 The LineageOS Project
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Lumia 1520: the capacitive keys are the Synaptics F1A 0D buttons. The
 * touch driver turns their interrupt on or off through its 0dbutton sysfs
 * attribute (1 = keys on). Based on the oppo msm8974-common KeyDisabler.
 */

#define LOG_TAG "KeyDisablerService"

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/strings.h>

#include "KeyDisabler.h"

namespace vendor {
namespace lineage {
namespace touch {
namespace V1_0 {
namespace implementation {

constexpr const char kControlPath[] = "/sys/devices/f9924000.i2c/i2c-2/2-004b/0dbutton";

// Methods from ::vendor::lineage::touch::V1_0::IKeyDisabler follow.
Return<bool> KeyDisabler::isEnabled() {
    std::string buf;

    if (!android::base::ReadFileToString(kControlPath, &buf)) {
        LOG(ERROR) << "Failed to read " << kControlPath;
        return false;
    }

    return std::stoi(android::base::Trim(buf)) == 0;
}

Return<bool> KeyDisabler::setEnabled(bool enabled) {
    if (!android::base::WriteStringToFile((enabled ? "0" : "1"), kControlPath)) {
        LOG(ERROR) << "Failed to write " << kControlPath;
        return false;
    }

    return true;
}

}  // namespace implementation
}  // namespace V1_0
}  // namespace touch
}  // namespace lineage
}  // namespace vendor
