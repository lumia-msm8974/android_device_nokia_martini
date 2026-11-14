/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Gyroscope and magnetometer calibration for the Lumia 930 Sensor Core.
 *
 * On this ADSP image the Qualcomm HAL reports both sensors raw: the SAM
 * calibration algorithms it relies on are not there. Measured on an
 * RM-1045 at rest: gyroscope bias about 0.048 rad/s (2.8 deg/s), noise
 * 0.0015 rad/s; magnetometer offset of a few tens of uT.
 *
 * Gyroscope: the readings are averaged over WIN_NS windows; when the
 * gyroscope was still during a window (and the accelerometer too, when it
 * is running), the average is the bias. Magnetometer: hard-iron offset
 * from a least-squares sphere fit over one reading per direction (26
 * directions around the current estimate, the steadiest recent reading kept
 * in each), so that time spent still does not outweigh the turns and
 * readings taken while turning fast, which lag behind, are avoided; a fit is accepted
 * when enough directions are covered, the field strength is plausible and
 * the readings lie on the sphere. Calibrated events have the estimate subtracted and
 * their accuracy set; uncalibrated events carry it in their bias fields.
 * Both estimates are kept in persist.sys.martini_ssc.* across reboots.
 */

#define LOG_TAG "sensors.martini_ssc"

#include "calib.h"

#include <cutils/properties.h>
#include <log/log.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define WIN_NS 1500000000LL
#define ACC_STILL 0.06f      /* m/s^2, standard deviation per axis */
#define GYRO_STILL 0.008f    /* rad/s, standard deviation per axis */
#define GYRO_MAX_BIAS 0.3f   /* rad/s, larger averages are motion */
#define SAVE_NS 60000000000LL

#define MAG_STEP_NS 50000000LL   /* fit samples at most every 50 ms */
#define MAG_MIN_BINS 10          /* directions covered before a fit */
#define MAG_MIN_SPAN 30.0f       /* uT on each axis among the kept readings */
#define MAG_MIN_FIELD 25.0f      /* the Earth's field is 22-67 uT */
#define MAG_MAX_FIELD 65.0f
#define MAG_MAX_ERR 0.08f        /* RMS distance from the sphere / radius */
#define MAG_MAX_RAW 300.0f

#define PROP_GYRO "persist.sys.martini_ssc.gyro_bias"
#define PROP_MAG "persist.sys.martini_ssc.mag_offset"

struct window {
	long long start;
	int n;
	double sum[3], sq[3];
};

static struct {
	/* accelerometer stillness */
	struct window acc;
	long long acc_still_until;
	long long acc_last;
	/* gyroscope */
	struct window gyro;
	float bias[3];
	int gyro_ok;
	float saved_bias[3];
	long long gyro_saved_at;
	/* magnetometer */
	float bin[27][3];
	float bin_motion[27];
	long long bin_time[27];
	unsigned char filled[27];
	float prev_raw[3];
	int have_prev;
	int nbins;
	double guess_sum[3];
	int guess_n;
	long long mag_last;
	float offset[3];
	int mag_ok;          /* 0 none, 1 from the previous boot, 2 fitted now */
	float saved_offset[3];
	long long mag_saved_at;
	long long mag_solved_at;
} c;

static int load3(const char *prop, float v[3])
{
	char buf[PROPERTY_VALUE_MAX];

	if (property_get(prop, buf, "") <= 0)
		return 0;
	return sscanf(buf, "%f,%f,%f", &v[0], &v[1], &v[2]) == 3;
}

static void save3(const char *prop, const float v[3])
{
	char buf[PROPERTY_VALUE_MAX];

	snprintf(buf, sizeof(buf), "%.5f,%.5f,%.5f", v[0], v[1], v[2]);
	if (property_set(prop, buf))
		ALOGW("cannot set %s", prop);
}

void calib_init(void)
{
	memset(&c, 0, sizeof(c));
	if (load3(PROP_GYRO, c.bias)) {
		c.gyro_ok = 1;
		memcpy(c.saved_bias, c.bias, sizeof(c.bias));
		ALOGI("gyroscope bias from the previous boot: %.4f %.4f %.4f",
		      c.bias[0], c.bias[1], c.bias[2]);
	}
	if (load3(PROP_MAG, c.offset)) {
		c.mag_ok = 1;
		memcpy(c.saved_offset, c.offset, sizeof(c.offset));
		ALOGI("magnetometer offset from the previous boot: %.1f %.1f %.1f",
		      c.offset[0], c.offset[1], c.offset[2]);
	}
}

static void win_add(struct window *w, long long t, const float *v)
{
	if (!w->n)
		w->start = t;
	for (int i = 0; i < 3; i++) {
		w->sum[i] += v[i];
		w->sq[i] += (double)v[i] * v[i];
	}
	w->n++;
}

/* Returns 1 when the window is complete, with its mean and largest deviation. */
static int win_done(struct window *w, long long t, float mean[3], float *dev)
{
	if (!w->n || t - w->start < WIN_NS)
		return 0;
	*dev = 0;
	for (int i = 0; i < 3; i++) {
		double m = w->sum[i] / w->n, var = w->sq[i] / w->n - m * m;
		float d = var > 0 ? (float)sqrt(var) : 0;

		mean[i] = (float)m;
		if (d > *dev)
			*dev = d;
	}
	int ok = w->n >= 10;

	memset(w, 0, sizeof(*w));
	return ok;
}

static void accel(long long t, const float *v)
{
	float mean[3], dev;

	c.acc_last = t;
	win_add(&c.acc, t, v);
	if (win_done(&c.acc, t, mean, &dev) && dev < ACC_STILL)
		c.acc_still_until = t + WIN_NS;
}

static float dist3(const float *a, const float *b)
{
	float d = 0;

	for (int i = 0; i < 3; i++)
		d += (a[i] - b[i]) * (a[i] - b[i]);
	return sqrtf(d);
}

static void gyro_learn(long long t, const float *raw)
{
	float mean[3], dev;

	win_add(&c.gyro, t, raw);
	if (!win_done(&c.gyro, t, mean, &dev))
		return;
	/* still gyroscope, and a still accelerometer when it is running */
	if (dev > GYRO_STILL || (t - c.acc_last < 3 * WIN_NS && t > c.acc_still_until))
		return;
	if (sqrtf(mean[0] * mean[0] + mean[1] * mean[1] + mean[2] * mean[2]) > GYRO_MAX_BIAS)
		return;
	for (int i = 0; i < 3; i++)
		c.bias[i] = c.gyro_ok ? c.bias[i] + 0.3f * (mean[i] - c.bias[i]) : mean[i];
	if (!c.gyro_ok)
		ALOGI("gyroscope bias %.4f %.4f %.4f", c.bias[0], c.bias[1], c.bias[2]);
	c.gyro_ok = 1;
	if (dist3(c.bias, c.saved_bias) > 0.002f &&
	    (!c.gyro_saved_at || t - c.gyro_saved_at > SAVE_NS)) {
		save3(PROP_GYRO, c.bias);
		memcpy(c.saved_bias, c.bias, sizeof(c.bias));
		c.gyro_saved_at = t;
	}
}

/* Solve the 4x4 normal equations by Gauss-Jordan; 0 if singular. */
static int solve4(double m[4][5], double x[4])
{
	for (int col = 0; col < 4; col++) {
		int p = col;

		for (int r = col + 1; r < 4; r++)
			if (fabs(m[r][col]) > fabs(m[p][col]))
				p = r;
		if (fabs(m[p][col]) < 1e-9)
			return 0;
		if (p != col)
			for (int k = 0; k < 5; k++) {
				double tmp = m[col][k];

				m[col][k] = m[p][k];
				m[p][k] = tmp;
			}
		for (int r = 0; r < 4; r++) {
			if (r == col)
				continue;
			double f = m[r][col] / m[col][col];

			for (int k = col; k < 5; k++)
				m[r][k] -= f * m[col][k];
		}
	}
	for (int i = 0; i < 4; i++)
		x[i] = m[i][4] / m[i][i];
	return 1;
}

/* Sphere fit over the kept readings: |p - o|^2 = r^2 <=> 2 p.o + (r^2 - |o|^2) = |p|^2 */
static int mag_fit(float o[3], float *r, float *err)
{
	double m[4][5] = { { 0 } }, x[4];
	int n = 0;

	for (int k = 0; k < 27; k++) {
		if (!c.filled[k])
			continue;
		const float *p = c.bin[k];
		double row[4] = { 2.0 * p[0], 2.0 * p[1], 2.0 * p[2], 1.0 };
		double b = (double)p[0] * p[0] + (double)p[1] * p[1] + (double)p[2] * p[2];

		for (int i = 0; i < 4; i++) {
			for (int j = 0; j < 4; j++)
				m[i][j] += row[i] * row[j];
			m[i][4] += row[i] * b;
		}
		n++;
	}
	if (n < 4 || !solve4(m, x))
		return 0;
	double r2 = x[3] + x[0] * x[0] + x[1] * x[1] + x[2] * x[2];

	if (r2 <= 0)
		return 0;
	for (int i = 0; i < 3; i++)
		o[i] = (float)x[i];
	*r = (float)sqrt(r2);
	double e = 0;

	for (int k = 0; k < 27; k++) {
		if (!c.filled[k])
			continue;
		double d = dist3(c.bin[k], o) - *r;

		e += d * d;
	}
	*err = (float)sqrt(e / n);
	return 1;
}

static void mag_learn(long long t, const float *raw)
{
	float g[3], d[3], len;
	int key = 0;

	if (t - c.mag_last < MAG_STEP_NS)
		return;
	if (fabsf(raw[0]) > MAG_MAX_RAW || fabsf(raw[1]) > MAG_MAX_RAW || fabsf(raw[2]) > MAG_MAX_RAW)
		return;
	c.mag_last = t;
	/* change since the previous fit sample: readings taken while turning fast lag behind */
	float motion = c.have_prev ? dist3(raw, c.prev_raw) : 0;

	memcpy(c.prev_raw, raw, sizeof(c.prev_raw));
	c.have_prev = 1;

	/* Direction around the current estimate, or the mean so far before one exists. */
	if (c.mag_ok) {
		memcpy(g, c.offset, sizeof(g));
	} else {
		for (int i = 0; i < 3; i++)
			c.guess_sum[i] += raw[i];
		c.guess_n++;
		for (int i = 0; i < 3; i++)
			g[i] = (float)(c.guess_sum[i] / c.guess_n);
	}
	for (int i = 0; i < 3; i++)
		d[i] = raw[i] - g[i];
	len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
	if (len < 5.0f)
		return;
	for (int i = 0; i < 3; i++)
		key = key * 3 + (d[i] > 0.4f * len ? 2 : d[i] < -0.4f * len ? 0 : 1);
	/* keep the steadiest reading of each direction, or a newer one after a minute */
	if (!c.filled[key]) {
		c.filled[key] = 1;
		c.nbins++;
	} else if (motion > c.bin_motion[key] + 0.5f && t - c.bin_time[key] < 60000000000LL) {
		return;
	}
	memcpy(c.bin[key], raw, sizeof(c.bin[key]));
	c.bin_motion[key] = motion;
	c.bin_time[key] = t;

	if (c.nbins < MAG_MIN_BINS || t - c.mag_solved_at < 1000000000LL)
		return;
	/* the kept readings must spread on every axis */
	for (int i = 0; i < 3; i++) {
		float lo = 1e9f, hi = -1e9f;

		for (int k = 0; k < 27; k++)
			if (c.filled[k]) {
				lo = c.bin[k][i] < lo ? c.bin[k][i] : lo;
				hi = c.bin[k][i] > hi ? c.bin[k][i] : hi;
			}
		if (hi - lo < MAG_MIN_SPAN)
			return;
	}
	c.mag_solved_at = t;

	float o[3], r, err;

	if (!mag_fit(o, &r, &err) || r < MAG_MIN_FIELD || r > MAG_MAX_FIELD || err > MAG_MAX_ERR * r)
		return;
	if (c.mag_ok != 2 || dist3(o, c.offset) > 2.0f)
		ALOGI("magnetometer offset %.1f %.1f %.1f, field %.1f uT, fit error %.1f uT, %d directions",
		      o[0], o[1], o[2], r, err, c.nbins);
	memcpy(c.offset, o, sizeof(o));
	c.mag_ok = 2;
	if (dist3(c.offset, c.saved_offset) > 2.0f &&
	    (!c.mag_saved_at || t - c.mag_saved_at > SAVE_NS)) {
		save3(PROP_MAG, c.offset);
		memcpy(c.saved_offset, c.offset, sizeof(c.offset));
		c.mag_saved_at = t;
	}
}

void calib_event(sensors_event_t *e)
{
	switch (e->type) {
	case SENSOR_TYPE_ACCELEROMETER:
		accel(e->timestamp, e->data);
		break;
	case SENSOR_TYPE_GYROSCOPE:
		gyro_learn(e->timestamp, e->data);
		if (c.gyro_ok) {
			for (int i = 0; i < 3; i++)
				e->data[i] -= c.bias[i];
			e->gyro.status = SENSOR_STATUS_ACCURACY_HIGH;
		} else {
			e->gyro.status = SENSOR_STATUS_ACCURACY_LOW;
		}
		break;
	case SENSOR_TYPE_GYROSCOPE_UNCALIBRATED:
		gyro_learn(e->timestamp, e->uncalibrated_gyro.uncalib);
		for (int i = 0; i < 3; i++)
			e->uncalibrated_gyro.bias[i] = c.gyro_ok ? c.bias[i] : 0;
		break;
	case SENSOR_TYPE_MAGNETIC_FIELD:
		mag_learn(e->timestamp, e->data);
		if (c.mag_ok) {
			for (int i = 0; i < 3; i++)
				e->data[i] -= c.offset[i];
		}
		e->magnetic.status = c.mag_ok == 2 ? SENSOR_STATUS_ACCURACY_HIGH :
				     c.mag_ok == 1 ? SENSOR_STATUS_ACCURACY_MEDIUM :
						     SENSOR_STATUS_UNRELIABLE;
		break;
	case SENSOR_TYPE_MAGNETIC_FIELD_UNCALIBRATED:
		mag_learn(e->timestamp, e->uncalibrated_magnetic.uncalib);
		for (int i = 0; i < 3; i++)
			e->uncalibrated_magnetic.bias[i] = c.mag_ok ? c.offset[i] : 0;
		break;
	default:
		break;
	}
}
