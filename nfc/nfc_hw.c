/*
 * Copyright (C) 2011 The Android Open Source Project
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * PN544 NFC HAL for the Nokia Lumia 930, for the legacy NXP stack
 * (external/libnfc-nxp + the nxp variant of packages/apps/Nfc). Derived from
 * the AOSP PN544 HALs (crespo, tuna) through the Lumia 1520 (bandit) one.
 *
 * TODO: the NFC controller of the 930 is not identified yet. This HAL is
 * the 1520 one (NXP PN544 on /dev/pn544, I2C address 0x2b, driver
 * pn544-bandit) and only works if the 930 has the same chip. If it is an
 * NCI controller (PN547/PN548 or a Broadcom BCM2079x), this legacy stack
 * must be replaced by the NCI one. Check the DSDT/registry of the phone.
 *
 * When the chip has nothing to send it answers a read with its own read
 * address, 0x57: libnfc-nxp skips that byte with the I2C workaround.
 *
 * EEPROM settings: the stack writes these after every initialisation. On
 * the 1520 they are the values the chip held under the stock Windows Phone
 * firmware (read with kit/nfc/pn544-eeprom-read before any Android write),
 * i.e. the RF tuning of that antenna. They are not valid for the 930
 * antenna, so none is listed here. With an empty list the nxp NFC service
 * refuses to initialise the chip ("Could not load EEPROM settings"): NFC
 * stays off and nothing is written to an unidentified controller.
 * TODO: if the 930 has a PN544, read its stock EEPROM with the same tool
 * and fill the table below; then each entry can also be tried at run time
 * with debug.nfc.eeprom.<ADDR> = <hex>.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <hardware/hardware.h>
#include <hardware/nfc.h>

/*
 * {0x00, ADDR_HI, ADDR_LO, VALUE} entries from the 930 stock EEPROM.
 * Empty until they are read from the phone (see above).
 */
static uint8_t *const pn544_eedata_settings = NULL;
#define PN544_EEDATA_COUNT 0

static int pn544_close(hw_device_t *dev) {
    free(dev);
    return 0;
}

static int nfc_open(const hw_module_t *module, const char *name,
                    hw_device_t **device) {
    nfc_pn544_device_t *dev;

    if (strcmp(name, NFC_PN544_CONTROLLER) != 0)
        return -EINVAL;

    dev = calloc(1, sizeof(nfc_pn544_device_t));
    if (!dev)
        return -ENOMEM;

    dev->common.tag = HARDWARE_DEVICE_TAG;
    dev->common.version = 0;
    dev->common.module = (struct hw_module_t *)module;
    dev->common.close = pn544_close;

    dev->num_eeprom_settings = PN544_EEDATA_COUNT;
    dev->eeprom_settings = pn544_eedata_settings;
    dev->linktype = PN544_LINK_TYPE_I2C;
    dev->device_node = "/dev/pn544";
    dev->enable_i2c_workaround = 1;
    dev->i2c_device_address = 0x57;

    *device = (hw_device_t *)dev;
    return 0;
}

static struct hw_module_methods_t nfc_module_methods = {
    .open = nfc_open,
};

struct nfc_module_t HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .version_major = 1,
        .version_minor = 0,
        .id = NFC_HARDWARE_MODULE_ID,
        .name = "Lumia 930 PN544 NFC HAL",
        .author = "The LineageOS Project",
        .methods = &nfc_module_methods,
    },
};
