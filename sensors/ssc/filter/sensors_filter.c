/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Filter in front of the Qualcomm sensors HAL (sensors.msm8974) for the
 * Lumia 930. The Sensor Core of this ADSP image publishes the physical
 * sensors (accelerometer, magnetometer, gyroscope) but none of the SAM
 * algorithm services, while the Qualcomm HAL still lists gravity, linear
 * acceleration, rotation vector, orientation and a proximity sensor of its
 * own: they never report anything and would hide both the real proximity
 * sensor of sensors.martini and the virtual sensors that the framework
 * builds from the physical ones. Only the physical, non wake-up sensors are
 * listed; everything else is passed through to the Qualcomm HAL unchanged.
 *
 * The Qualcomm HAL queries the sensor list once, from sensors.qcom, and
 * keeps it. When it is opened before sensors.qcom serves its socket (the
 * daemon creates it only once the ADSP sensor manager answers), it gets no
 * sensors and later dereferences a NULL sensor in poll(): system_server
 * crashed every 10 s at boot. So wait for the socket, plus SETTLE_S for the
 * daemon's own start-up timeouts, before loading the Qualcomm HAL, and do
 * not open it if the socket never comes or no physical sensor is listed:
 * the multihal then skips this sub-HAL.
 *
 * The device handed to the multihal wraps the Qualcomm one, so that poll()
 * can run the gyroscope and magnetometer calibration of calib.c on the
 * events (this HAL reports both sensors raw).
 */

#define LOG_TAG "sensors.martini_ssc"

#include "calib.h"

#include <dlfcn.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <hardware/sensors.h>
#include <log/log.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define QCOM_HAL "/vendor/lib/hw/sensors.msm8974.so"
#define MAX_SENSORS 16
#define SENSOR1_SOCKET "/dev/socket/sensor_ctl_socket"
#define SETTLE_S 3
#define MAX_WAIT_S 45

static struct sensors_module_t *qcom;
static struct sensor_t list[MAX_SENSORS];
static int count = -1;
static pthread_once_t once = PTHREAD_ONCE_INIT;

static int wanted(const struct sensor_t *s)
{
	if (s->flags & SENSOR_FLAG_WAKE_UP)
		return 0;
	switch (s->type) {
	case SENSOR_TYPE_ACCELEROMETER:
	case SENSOR_TYPE_MAGNETIC_FIELD:
	case SENSOR_TYPE_MAGNETIC_FIELD_UNCALIBRATED:
	case SENSOR_TYPE_GYROSCOPE:
	case SENSOR_TYPE_GYROSCOPE_UNCALIBRATED:
		return 1;
	default:
		return 0;
	}
}

static long long boottime_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_BOOTTIME, &ts);
	return ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

/* Wait for the socket of sensors.qcom, then SETTLE_S more. */
static int wait_for_daemon(void)
{
	long long start = boottime_ns(), seen = 0;

	for (;;) {
		long long now = boottime_ns();

		if (!seen && access(SENSOR1_SOCKET, F_OK) == 0)
			seen = now;
		if (seen && now - seen >= SETTLE_S * 1000000000LL)
			break;
		if (now - start >= MAX_WAIT_S * 1000000000LL) {
			ALOGE("no %s after %d s", SENSOR1_SOCKET, MAX_WAIT_S);
			return -1;
		}
		usleep(200000);
	}
	ALOGI("waited %lld ms for sensors.qcom", (boottime_ns() - start) / 1000000);
	return 0;
}

static void load(void)
{
	struct sensor_t const *all;
	void *h;
	int n;

	count = 0;
	if (wait_for_daemon())
		return;
	h = dlopen(QCOM_HAL, RTLD_NOW);
	if (!h) {
		ALOGE("dlopen %s: %s", QCOM_HAL, dlerror());
		return;
	}
	qcom = dlsym(h, HAL_MODULE_INFO_SYM_AS_STR);
	if (!qcom) {
		ALOGE("%s has no module info", QCOM_HAL);
		return;
	}
	n = qcom->get_sensors_list(qcom, &all);
	for (int i = 0; i < n; i++) {
		if (wanted(&all[i]) && count < MAX_SENSORS)
			list[count++] = all[i];
		ALOGI("%s %d %s (type %d)", wanted(&all[i]) ? "keep" : "hide",
		      all[i].handle, all[i].name, all[i].type);
	}
	if (!count) {
		ALOGE("no physical sensor from %s: not opening it", QCOM_HAL);
		qcom = NULL;
	}
}

static int get_sensors_list(struct sensors_module_t *module, struct sensor_t const **out)
{
	(void)module;
	pthread_once(&once, load);
	*out = list;
	return count;
}

/* Device wrapper: every call goes to the Qualcomm device, poll() also calibrates. */
struct wrapper {
	struct sensors_poll_device_1 dev;	/* must stay first */
	struct sensors_poll_device_1 *inner;
};

static struct sensors_poll_device_1 *inner_of(const void *dev)
{
	return ((const struct wrapper *)dev)->inner;
}

static int w_activate(struct sensors_poll_device_t *dev, int handle, int enabled)
{
	struct sensors_poll_device_1 *in = inner_of(dev);

	return in->activate((struct sensors_poll_device_t *)in, handle, enabled);
}

static int w_set_delay(struct sensors_poll_device_t *dev, int handle, int64_t ns)
{
	struct sensors_poll_device_1 *in = inner_of(dev);

	return in->setDelay((struct sensors_poll_device_t *)in, handle, ns);
}

static int w_poll(struct sensors_poll_device_t *dev, sensors_event_t *data, int n)
{
	struct sensors_poll_device_1 *in = inner_of(dev);
	int got = in->poll((struct sensors_poll_device_t *)in, data, n);

	for (int i = 0; i < got; i++)
		calib_event(&data[i]);
	return got;
}

static int w_batch(struct sensors_poll_device_1 *dev, int handle, int flags,
		   int64_t period_ns, int64_t timeout)
{
	struct sensors_poll_device_1 *in = inner_of(dev);

	return in->batch(in, handle, flags, period_ns, timeout);
}

static int w_flush(struct sensors_poll_device_1 *dev, int handle)
{
	struct sensors_poll_device_1 *in = inner_of(dev);

	return in->flush(in, handle);
}

static int w_inject(struct sensors_poll_device_1 *dev, const sensors_event_t *data)
{
	struct sensors_poll_device_1 *in = inner_of(dev);

	return in->inject_sensor_data(in, data);
}

static int w_close(struct hw_device_t *dev)
{
	struct wrapper *w = (struct wrapper *)dev;
	int rc = w->inner->common.close(&w->inner->common);

	free(w);
	return rc;
}

static int open_sensors(const struct hw_module_t *module, const char *id, struct hw_device_t **dev)
{
	struct hw_device_t *in_dev;
	struct sensors_poll_device_1 *in;
	struct wrapper *w;
	int rc;

	pthread_once(&once, load);
	if (!qcom)
		return -ENODEV;
	rc = qcom->common.methods->open(&qcom->common, id, &in_dev);
	if (rc)
		return rc;
	in = (struct sensors_poll_device_1 *)in_dev;
	w = calloc(1, sizeof(*w));
	if (!w) {
		in_dev->close(in_dev);
		return -ENOMEM;
	}
	w->inner = in;
	w->dev.common = in->common;
	w->dev.common.module = (struct hw_module_t *)module;
	w->dev.common.close = w_close;
	w->dev.activate = w_activate;
	w->dev.setDelay = w_set_delay;
	w->dev.poll = w_poll;
	if (in->common.version >= SENSORS_DEVICE_API_VERSION_1_0) {
		w->dev.batch = in->batch ? w_batch : NULL;
		w->dev.flush = in->flush ? w_flush : NULL;
	}
	/* inject_sensor_data exists from 1.4 on: the Qualcomm device (1.3) ends before it */
	if (in->common.version >= SENSORS_DEVICE_API_VERSION_1_4)
		w->dev.inject_sensor_data = in->inject_sensor_data ? w_inject : NULL;
	calib_init();
	*dev = &w->dev.common;
	return 0;
}

static int set_operation_mode(unsigned int mode)
{
	pthread_once(&once, load);
	if (!qcom || !qcom->set_operation_mode)
		return mode ? -EINVAL : 0;
	return qcom->set_operation_mode(mode);
}

static struct hw_module_methods_t methods = {
	.open = open_sensors,
};

struct sensors_module_t HAL_MODULE_INFO_SYM = {
	.common = {
		.tag = HARDWARE_MODULE_TAG,
		.module_api_version = SENSORS_MODULE_API_VERSION_0_1,
		.hal_api_version = HARDWARE_HAL_API_VERSION,
		.id = SENSORS_HARDWARE_MODULE_ID,
		.name = "Lumia 930 Sensor Core filter",
		.author = "The LineageOS Project",
		.methods = &methods,
	},
	.get_sensors_list = get_sensors_list,
	.set_operation_mode = set_operation_mode,
};
