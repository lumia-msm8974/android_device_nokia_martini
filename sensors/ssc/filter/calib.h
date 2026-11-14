/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <hardware/sensors.h>

/* Load the estimates kept from the previous boot. */
void calib_init(void);

/* Learn from one event and correct it in place (poll thread only). */
void calib_event(sensors_event_t *e);
