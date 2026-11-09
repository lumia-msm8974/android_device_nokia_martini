/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * libnfc-nxp build configuration for the Nokia Lumia 930 (PN544 assumed).
 *
 * TODO: these are the Lumia 1520 values, kept only so that the legacy PN544
 * stack builds. The NFC chip of the 930, its clock source and its UICC/SWP
 * wiring are not known yet; read them from the DSDT and from the chip under
 * Windows before enabling NFC on the phone.
 *
 * libnfc-nxp writes the clock, power and UICC access registers during every
 * initialisation from these values. Its defaults assume a crystal; on the
 * 1520 the chip runs from the 19.2 MHz BBCLK2 buffer. The values match the
 * stock Windows Phone configuration read from the 1520 chip:
 *   0x9E71 CLK_REQ 0x00, 0x9809 CLK_INPUT 0x02, 0x9810 HW_CONF 0x3C,
 *   0x9EB4 UICC power request 0x00, 0x9C01 SWP bit rate 0x08,
 *   0x9ED9-0x9EDE UICC gate access FF FF 23 21 FF 24.
 */

#ifndef NFC_CUSTOM_CONFIG_H
#define NFC_CUSTOM_CONFIG_H

/* No clock request: the clock buffer is voted by the kernel driver. */
#define NXP_DEFAULT_CLK_REQUEST         0x00U

/* External input clock, 19.2 MHz. */
#define NXP_DEFAULT_INPUT_CLK           0x02U

/* HW_CONF: stock value; its two low bits are the TX LDO (3.0 V). */
#define NFC_DEV_HWCONF_DEFAULT          0x3CU
#define NXP_DEFAULT_TX_LDO              0x00U

#define NXP_UICC_PWR_REQUEST            0x00U
#define NXP_UICC_BIT_RATE               0x08U

/* UICC card emulation through types A, B and F; no reader rights. */
#define NXP_UICC_CE_RIGHTS              0x0BU
#define NXP_UICC_RD_RIGHTS              0x00U

#endif /* NFC_CUSTOM_CONFIG_H */
