/*
 * Copyright (C) 2011 The Android Open Source Project
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * PN544 NFC HAL for the Nokia Lumia 1520, for the legacy NXP stack
 * (external/libnfc-nxp + the nxp variant of packages/apps/Nfc). Derived from
 * the AOSP PN544 HALs (crespo, tuna).
 *
 * The chip sits on /dev/pn544 (kernel driver pn544-martini), I2C address
 * 0x2b. When it has nothing to send it answers a read with its own read
 * address, 0x57: libnfc-nxp skips that byte with the I2C workaround.
 *
 * EEPROM settings: the stack writes these after every initialisation. The
 * values below are the ones the chip held under the stock Windows Phone
 * firmware (read on 2026-11-02 with kit/nfc/pn544-eeprom-read, before any
 * Android write), so the stack keeps the stock RF tuning of this antenna.
 * Any entry can be tried at run time with debug.nfc.eeprom.<ADDR> = <hex>.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <hardware/hardware.h>
#include <hardware/nfc.h>

static uint8_t pn544_eedata_settings[][4] = {
    /* RF: transmitter current thresholds and their GSP settings */
    {0x00, 0x9B, 0xD1, 0x26},
    {0x00, 0x9B, 0xD2, 0x60},
    {0x00, 0x9B, 0xD3, 0x10},
    {0x00, 0x9B, 0xD4, 0x47},
    {0x00, 0x9B, 0xD5, 0x0C},
    {0x00, 0x9B, 0xD6, 0x37},
    {0x00, 0x9B, 0xDD, 0x33},
    {0x00, 0x9B, 0x84, 0x00},   /* ANACM2 */
    /* Load modulation amplitude: type A, B, B', FeliCa */
    {0x00, 0x99, 0x29, 0xF3},
    {0x00, 0x99, 0x2A, 0xF3},
    {0x00, 0x99, 0x2B, 0xFF},
    {0x00, 0x99, 0x85, 0x08},
    /* RF level detector (ANARFLDWU) */
    {0x00, 0x99, 0x23, 0x01},
    /* Secure element configuration and PBTF */
    {0x00, 0x98, 0x00, 0x0E},
    {0x00, 0x9F, 0x09, 0x02},
    {0x00, 0x9F, 0x0A, 0x01},
    {0x00, 0x9E, 0xD1, 0xA1},
    /* Low-power polling */
    {0x00, 0x9E, 0x74, 0x80},
    {0x00, 0x9E, 0x7D, 0x00},
    {0x00, 0x9F, 0x28, 0x01},
    /* Card emulation timeout after leaving the field */
    {0x00, 0x9F, 0x35, 0x04},
    {0x00, 0x9F, 0x36, 0x11},
    /* LLC retries */
    {0x00, 0x9C, 0x19, 0x0A},
    {0x00, 0x9C, 0x1A, 0x0A},
    /* NFC-DEP target waiting time */
    {0x00, 0x98, 0xA2, 0x09},
    /* NFC-T ATQA */
    {0x00, 0x98, 0x7D, 0x08},
    {0x00, 0x98, 0x7E, 0x00},
    /* CEA detection */
    {0x00, 0x9F, 0xC8, 0x00},
};

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

    dev->num_eeprom_settings = sizeof(pn544_eedata_settings) / 4;
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
