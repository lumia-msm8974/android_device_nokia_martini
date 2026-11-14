/*
 * Copyright (c) 2026, Lorenzo Giannoccaro.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Preload shim for mm-qcamera-daemon.
 *
 * At every camera open the gyro module of the camera daemon (stats modules,
 * used for video stabilisation) connects to the SSC sensor daemon through
 * libsensor1. With the SSC stack off (persist.vendor.martini.ssc=0) the
 * socket never appears and sensor1_open retries for about 3 s before it
 * fails; the daemon then carries on without the gyro. Fail at once while
 * the socket is missing, and use the real function when it exists.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

#define SENSOR1_SOCKET "/dev/socket/sensor_ctl_socket"
#define SENSOR1_EFAILED 5

typedef int (*sensor1_open_fn)(void **hndl, void *data_cbf, intptr_t cb_data);

int sensor1_open(void **hndl, void *data_cbf, intptr_t cb_data)
{
	static sensor1_open_fn real;

	if (access(SENSOR1_SOCKET, F_OK) != 0) {
		if (hndl)
			*hndl = NULL;
		return SENSOR1_EFAILED;
	}
	if (!real)
		real = (sensor1_open_fn)dlsym(RTLD_NEXT, "sensor1_open");
	if (!real) {
		if (hndl)
			*hndl = NULL;
		return SENSOR1_EFAILED;
	}
	return real(hndl, data_cbf, cb_data);
}
