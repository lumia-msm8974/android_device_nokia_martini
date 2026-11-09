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
 * The controller is an NXP PN544, wired as on the 1520: the stock SSDT
 * \_SB.NFC1 (NXP_NFCC) gives I2C6 address 0x2b, interrupt TLMM 59, VEN
 * TLMM 57 and firmware download TLMM 85, and the Windows NFC package ships
 * the PN544 C3 firmware. Kernel driver pn544-bandit, /dev/pn544.
 *
 * When the chip has nothing to send it answers a read with its own read
 * address, 0x57: libnfc-nxp skips that byte with the I2C workaround.
 *
 * EEPROM settings: the stack writes these after every initialisation.
 * They are the ones the stock Windows Phone NFC driver of the 930 writes
 * (registry SOFTWARE\Microsoft\NFCDevices\NFCDriverNXP, EEConfigParms,
 * 22 address/value triplets, RM-1045 FFU): clock and HW_CONF as on the
 * 1520, the RF/antenna tuning (0x9BD1-0x9BDD, 0x9929/0x992A) of the 930.
 * On the 1520 the same registry value agrees with the stock EEPROM read
 * from the chip on all the 11 of its 16 addresses that the dump covers.
 * Any entry can be tried at run time
 * with debug.nfc.eeprom.<ADDR> = <hex>. Unverified on a 930.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <hardware/hardware.h>
#include <hardware/nfc.h>

/* {0x00, ADDR_HI, ADDR_LO, VALUE}, in the order of EEConfigParms */
static uint8_t pn544_eedata_settings[][4] = {
    {0x00, 0x98, 0x10, 0x3C},
    {0x00, 0x98, 0x09, 0x02},
    {0x00, 0x9E, 0x74, 0x80},
    {0x00, 0x9F, 0x28, 0x01},
    {0x00, 0x9B, 0x4E, 0x1E},
    {0x00, 0x98, 0x00, 0x0E},
    {0x00, 0x98, 0x01, 0x13},
    {0x00, 0x9F, 0x0A, 0x01},
    {0x00, 0x9F, 0x19, 0xFF},
    {0x00, 0x9F, 0x1A, 0xFF},
    {0x00, 0x9B, 0x65, 0xF8},
    {0x00, 0x9B, 0xD1, 0x1E},
    {0x00, 0x9B, 0xD2, 0x53},
    {0x00, 0x9B, 0xD3, 0x1A},
    {0x00, 0x9B, 0xD4, 0x47},
    {0x00, 0x9B, 0xD5, 0x13},
    {0x00, 0x9B, 0xD6, 0x38},
    {0x00, 0x9B, 0xDC, 0x00},
    {0x00, 0x9B, 0xDD, 0x31},
    {0x00, 0x99, 0x29, 0xF8},
    {0x00, 0x99, 0x2A, 0xF8},
    {0x00, 0x9E, 0x77, 0x01},
};

#define PN544_EEDATA_COUNT \
    (sizeof(pn544_eedata_settings) / sizeof(pn544_eedata_settings[0]))

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
    dev->eeprom_settings = (uint8_t *)pn544_eedata_settings;
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
