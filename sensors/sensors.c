/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Sensors HAL for the Lumia 1520 (martini).
 *
 * The kernel drivers expose one polled input device per sensor (see
 * sensors_martini.h). At load time the HAL scans /dev/input/event* and keeps
 * open the nodes whose EVIOCGNAME is one of the martini-* names; sensors whose
 * device is missing are not listed. activate/batch write "enable" and
 * "poll_delay" in the sysfs directory of the input device; poll() waits with
 * poll(2) on the event nodes and on a wake pipe (flush, activation), turns
 * raw counts into SI units, rotates them into the Android device frame and
 * returns them with CLOCK_BOOTTIME timestamps.
 *
 * Linux 3.4 drops an EV_ABS value equal to the previous one, and sends no
 * EV_SYN at all if no axis changed: the last value of every axis is kept
 * here, and a continuous sensor that goes quiet (device perfectly still)
 * gets its last sample repeated at the requested rate.
 */

#define _GNU_SOURCE /* pipe2 */
#define LOG_TAG "sensors.martini"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <hardware/hardware.h>
#include <hardware/sensors.h>
#include <log/log.h>

#include "sensors_martini.h"

/* Android 3.4 evdev: hold a wakelock while events are queued on the fd */
#ifndef EVIOCSSUSPENDBLOCK
#define EVIOCSSUSPENDBLOCK _IOW('E', 0x91, int)
#endif

#define NS_PER_MS 1000000LL
#define NS_PER_S  1000000000LL

/* Repeat the last sample of a quiet continuous sensor (see above) */
#define REPEAT_QUIET_SAMPLES 1

#define QUEUE_SIZE 128

enum {
    ID_ACCEL,
    ID_MAG,
    ID_GYRO,
    ID_LIGHT,
    ID_PROX,
    ID_COUNT,
};

/* Handle 0 is reserved: flush complete events carry sensor 0 */
#define ID_TO_HANDLE(id) ((id) + 1)
#define HANDLE_TO_ID(h)  ((h) - 1)

struct sensor_desc {
    const char *input_name;
    int naxes;              /* 3 (x, y, z) or 1 */
    int codes[3];           /* EV_ABS codes of the axes */
    float scale;            /* SI unit per raw LSB */
    int matrix[9];          /* device = matrix * chip, 3-axis sensors only */
    int8_t status;          /* accuracy reported with 3-axis samples */
    bool on_change;
    bool wake_up;
    int delay_min_ms;
    int delay_max_ms;
    int initial_slack_ms;   /* on-change sensors only */
    struct sensor_t info;
};

static const struct sensor_desc g_desc[ID_COUNT] = {
    [ID_ACCEL] = {
        .input_name = "martini-accel",
        .naxes = 3,
        .codes = { ABS_X, ABS_Y, ABS_Z },
        .scale = ACCEL_SCALE,
        .matrix = ACCEL_MATRIX,
        .status = SENSOR_STATUS_ACCURACY_HIGH,
        .delay_min_ms = ACCEL_DELAY_MIN_MS,
        .delay_max_ms = ACCEL_DELAY_MAX_MS,
        .info = {
            .name = "LIS2HH12 Accelerometer",
            .vendor = "STMicroelectronics",
            .version = 1,
            .handle = ID_TO_HANDLE(ID_ACCEL),
            .type = SENSOR_TYPE_ACCELEROMETER,
            .maxRange = ACCEL_RANGE,
            .resolution = ACCEL_RESOLUTION,
            .power = ACCEL_POWER_MA,
            .minDelay = ACCEL_MIN_DELAY_US,
            .stringType = SENSOR_STRING_TYPE_ACCELEROMETER,
            .requiredPermission = "",
            .maxDelay = ACCEL_DELAY_MAX_MS * 1000,
            .flags = SENSOR_FLAG_CONTINUOUS_MODE,
        },
    },
    [ID_MAG] = {
        .input_name = "martini-mag",
        .naxes = 3,
        .codes = { ABS_X, ABS_Y, ABS_Z },
        .scale = MAG_SCALE,
        .matrix = MAG_MATRIX,
        /* No hard-iron calibration in the HAL yet */
        .status = SENSOR_STATUS_ACCURACY_MEDIUM,
        .delay_min_ms = MAG_DELAY_MIN_MS,
        .delay_max_ms = MAG_DELAY_MAX_MS,
        .info = {
            .name = "HSCDTD008A Magnetometer",
            .vendor = "Alps",
            .version = 1,
            .handle = ID_TO_HANDLE(ID_MAG),
            .type = SENSOR_TYPE_MAGNETIC_FIELD,
            .maxRange = MAG_RANGE,
            .resolution = MAG_RESOLUTION,
            .power = MAG_POWER_MA,
            .minDelay = MAG_MIN_DELAY_US,
            .stringType = SENSOR_STRING_TYPE_MAGNETIC_FIELD,
            .requiredPermission = "",
            .maxDelay = MAG_DELAY_MAX_MS * 1000,
            .flags = SENSOR_FLAG_CONTINUOUS_MODE,
        },
    },
    [ID_GYRO] = {
        .input_name = "martini-gyro",
        .naxes = 3,
        .codes = { ABS_X, ABS_Y, ABS_Z },
        .scale = GYRO_SCALE,
        .matrix = GYRO_MATRIX,
        .status = SENSOR_STATUS_ACCURACY_HIGH,
        .delay_min_ms = GYRO_DELAY_MIN_MS,
        .delay_max_ms = GYRO_DELAY_MAX_MS,
        .info = {
            .name = "L3GD20 Gyroscope",
            .vendor = "STMicroelectronics",
            .version = 1,
            .handle = ID_TO_HANDLE(ID_GYRO),
            .type = SENSOR_TYPE_GYROSCOPE,
            .maxRange = GYRO_RANGE,
            .resolution = GYRO_RESOLUTION,
            .power = GYRO_POWER_MA,
            .minDelay = GYRO_MIN_DELAY_US,
            .stringType = SENSOR_STRING_TYPE_GYROSCOPE,
            .requiredPermission = "",
            .maxDelay = GYRO_DELAY_MAX_MS * 1000,
            .flags = SENSOR_FLAG_CONTINUOUS_MODE,
        },
    },
    [ID_LIGHT] = {
        .input_name = "martini-light",
        .naxes = 1,
        .codes = { ABS_MISC },
        .scale = LIGHT_SCALE,
        .on_change = true,
        .delay_min_ms = LIGHT_DELAY_MIN_MS,
        .delay_max_ms = LIGHT_DELAY_MAX_MS,
        .initial_slack_ms = LIGHT_INITIAL_SLACK_MS,
        .info = {
            .name = "APDS-9930 Light",
            .vendor = "Avago",
            .version = 1,
            .handle = ID_TO_HANDLE(ID_LIGHT),
            .type = SENSOR_TYPE_LIGHT,
            .maxRange = LIGHT_RANGE,
            .resolution = LIGHT_RESOLUTION,
            .power = LIGHT_POWER_MA,
            .minDelay = 0,
            .stringType = SENSOR_STRING_TYPE_LIGHT,
            .requiredPermission = "",
            .maxDelay = 0,
            .flags = SENSOR_FLAG_ON_CHANGE_MODE,
        },
    },
    [ID_PROX] = {
        .input_name = "martini-prox",
        .naxes = 1,
        .codes = { ABS_DISTANCE },
        .scale = PROX_SCALE,
        .on_change = true,
        .wake_up = true,
        .delay_min_ms = PROX_DELAY_MIN_MS,
        .delay_max_ms = PROX_DELAY_MAX_MS,
        .initial_slack_ms = PROX_INITIAL_SLACK_MS,
        .info = {
            .name = "APDS-9930 Proximity",
            .vendor = "Avago",
            .version = 1,
            .handle = ID_TO_HANDLE(ID_PROX),
            .type = SENSOR_TYPE_PROXIMITY,
            .maxRange = PROX_RANGE,
            .resolution = PROX_RESOLUTION,
            .power = PROX_POWER_MA,
            .minDelay = 0,
            .stringType = SENSOR_STRING_TYPE_PROXIMITY,
            .requiredPermission = "",
            .maxDelay = 0,
            .flags = SENSOR_FLAG_WAKE_UP | SENSOR_FLAG_ON_CHANGE_MODE,
        },
    },
};

/* Input devices found at load time (shared by every opened device) */
struct sensor_input {
    int fd;                 /* -1: sensor not present */
    clockid_t clock;        /* clock of the input event timestamps */
    char sysfs[64];         /* /sys/class/input/eventN/device */
};

static struct sensor_input g_input[ID_COUNT];
static struct sensor_t g_list[ID_COUNT];
static int g_list_count;
static pthread_once_t g_probe_once = PTHREAD_ONCE_INIT;

struct sensor_state {
    bool enabled;
    int delay_ms;           /* last value written to poll_delay */
    int raw[3];             /* last value of every axis */
    bool dropped;           /* SYN_DROPPED seen: resync at next SYN_REPORT */
    int64_t last_ts;        /* timestamp of the last sample returned */
    int64_t next_repeat;    /* continuous: repeat the last sample at */
    int64_t initial_deadline; /* on-change: report EVIOCGABS value at, or 0 */
    int flush_pending;
};

struct martini_sensors {
    sensors_poll_device_1_t device; /* must be first */
    pthread_mutex_t lock;   /* protects state[] and the queue */
    struct sensor_state state[ID_COUNT];
    int wake_fds[2];
    sensors_event_t queue[QUEUE_SIZE];
    int queue_head;
    int queue_count;
};

/* ------------------------------------------------------------------------ */

static int64_t clock_ns(clockid_t clock)
{
    struct timespec ts;

    clock_gettime(clock, &ts);
    return (int64_t)ts.tv_sec * NS_PER_S + ts.tv_nsec;
}

static int64_t now_ns(void)
{
    return clock_ns(CLOCK_BOOTTIME);
}

static int sysfs_write_int(const char *dir, const char *attr, int value)
{
    char path[96], buf[16];
    int fd, len, ret = 0;

    snprintf(path, sizeof(path), "%s/%s", dir, attr);
    fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        ret = -errno;
        ALOGE("open %s: %s", path, strerror(errno));
        return ret;
    }
    len = snprintf(buf, sizeof(buf), "%d\n", value);
    if (write(fd, buf, len) != len) {
        ret = -errno;
        ALOGE("write %d to %s: %s", value, path, strerror(errno));
    }
    close(fd);
    return ret;
}

/*
 * Event timestamps: Linux 3.4 evdev only offers CLOCK_REALTIME (default) and
 * CLOCK_MONOTONIC. Ask for CLOCK_BOOTTIME in case a later kernel has it,
 * otherwise use CLOCK_MONOTONIC and add the BOOTTIME - MONOTONIC offset.
 */
static clockid_t select_clock(int fd)
{
    static const clockid_t clocks[] = { CLOCK_BOOTTIME, CLOCK_MONOTONIC };
    size_t i;

    for (i = 0; i < sizeof(clocks) / sizeof(clocks[0]); i++) {
        int id = clocks[i];

        if (ioctl(fd, EVIOCSCLOCKID, &id) == 0)
            return clocks[i];
    }
    return CLOCK_REALTIME;
}

static void probe_inputs(void)
{
    DIR *dir;
    struct dirent *de;
    int id;

    for (id = 0; id < ID_COUNT; id++)
        g_input[id].fd = -1;

    dir = opendir("/dev/input");
    if (!dir) {
        ALOGE("opendir /dev/input: %s", strerror(errno));
        return;
    }

    while ((de = readdir(dir)) != NULL) {
        char path[64], name[64];
        int fd;

        if (strncmp(de->d_name, "event", 5) != 0)
            continue;

        snprintf(path, sizeof(path), "/dev/input/%s", de->d_name);
        fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            ALOGW("open %s: %s", path, strerror(errno));
            continue;
        }

        memset(name, 0, sizeof(name));
        if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) < 0) {
            close(fd);
            continue;
        }

        for (id = 0; id < ID_COUNT; id++) {
            if (g_input[id].fd < 0 && strcmp(name, g_desc[id].input_name) == 0)
                break;
        }
        if (id == ID_COUNT) {
            close(fd);
            continue;
        }

        g_input[id].fd = fd;
        g_input[id].clock = select_clock(fd);
        snprintf(g_input[id].sysfs, sizeof(g_input[id].sysfs),
                 "/sys/class/input/%s/device", de->d_name);

        if (g_desc[id].wake_up) {
            int on = 1;

            if (ioctl(fd, EVIOCSSUSPENDBLOCK, &on) < 0)
                ALOGW("%s: no evdev suspend block: %s", name, strerror(errno));
        }

        ALOGI("%s: %s, %s, clock %d", g_desc[id].info.name, path,
              g_input[id].sysfs, (int)g_input[id].clock);
    }
    closedir(dir);

    g_list_count = 0;
    for (id = 0; id < ID_COUNT; id++) {
        if (g_input[id].fd >= 0)
            g_list[g_list_count++] = g_desc[id].info;
        else
            ALOGW("%s: input device %s not found, not listed",
                  g_desc[id].info.name, g_desc[id].input_name);
    }
}

static int handle_to_id(int handle)
{
    int id = HANDLE_TO_ID(handle);

    if (id < 0 || id >= ID_COUNT || g_input[id].fd < 0)
        return -1;
    return id;
}

/* ------------------------------------------------------------------------ */

static void wake_poll(struct martini_sensors *ctx)
{
    char c = 1;

    if (write(ctx->wake_fds[1], &c, 1) < 0 && errno != EAGAIN)
        ALOGE("wake pipe: %s", strerror(errno));
}

static void queue_push(struct martini_sensors *ctx, const sensors_event_t *ev)
{
    int tail;

    if (ctx->queue_count == QUEUE_SIZE) {
        /* Full: drop the oldest event */
        ctx->queue_head = (ctx->queue_head + 1) % QUEUE_SIZE;
        ctx->queue_count--;
    }
    tail = (ctx->queue_head + ctx->queue_count) % QUEUE_SIZE;
    ctx->queue[tail] = *ev;
    ctx->queue_count++;
}

static int queue_pop(struct martini_sensors *ctx, sensors_event_t *data, int count)
{
    int n = 0;

    while (n < count && ctx->queue_count > 0) {
        data[n++] = ctx->queue[ctx->queue_head];
        ctx->queue_head = (ctx->queue_head + 1) % QUEUE_SIZE;
        ctx->queue_count--;
    }
    return n;
}

/* Re-read the current value of every axis from the input core */
static void resync_axes(int id, struct sensor_state *s)
{
    const struct sensor_desc *d = &g_desc[id];
    int i;

    for (i = 0; i < d->naxes; i++) {
        struct input_absinfo abs;

        if (ioctl(g_input[id].fd, EVIOCGABS(d->codes[i]), &abs) == 0)
            s->raw[i] = abs.value;
    }
}

/* Queue a sample of sensor id with the current raw values (lock held) */
static void emit_sample(struct martini_sensors *ctx, int id, int64_t ts)
{
    const struct sensor_desc *d = &g_desc[id];
    struct sensor_state *s = &ctx->state[id];
    sensors_event_t ev;
    int r;

    if (ts <= s->last_ts)
        ts = s->last_ts + 1;
    s->last_ts = ts;
    s->initial_deadline = 0;

    memset(&ev, 0, sizeof(ev));
    ev.version = sizeof(ev);
    ev.sensor = d->info.handle;
    ev.type = d->info.type;
    ev.timestamp = ts;

    if (d->naxes == 3) {
        for (r = 0; r < 3; r++) {
            const int *m = &d->matrix[r * 3];

            ev.acceleration.v[r] = d->scale *
                (float)(m[0] * s->raw[0] + m[1] * s->raw[1] + m[2] * s->raw[2]);
        }
        /* same field for acceleration, magnetic and gyro */
        ev.acceleration.status = d->status;
    } else {
        /* light and distance share data[0] */
        ev.data[0] = d->scale * (float)s->raw[0];
    }

    queue_push(ctx, &ev);
}

static void emit_flush_complete(struct martini_sensors *ctx, int id)
{
    sensors_event_t ev;

    memset(&ev, 0, sizeof(ev));
    ev.version = META_DATA_VERSION;
    ev.type = SENSOR_TYPE_META_DATA;
    ev.sensor = 0;
    ev.timestamp = 0;
    ev.meta_data.what = META_DATA_FLUSH_COMPLETE;
    ev.meta_data.sensor = g_desc[id].info.handle;
    queue_push(ctx, &ev);
}

/* Repeat period of a quiet continuous sensor: two missed polls */
static int64_t quiet_timeout_ns(const struct sensor_state *s)
{
    return 2LL * s->delay_ms * NS_PER_MS + 10 * NS_PER_MS;
}

/* Read everything queued on the event node of sensor id (lock held) */
static void read_input(struct martini_sensors *ctx, int id)
{
    const struct sensor_desc *d = &g_desc[id];
    struct sensor_state *s = &ctx->state[id];
    struct input_event buf[32];
    int64_t offset;
    ssize_t len;

    /* BOOTTIME - event clock, sampled once per read */
    offset = g_input[id].clock == CLOCK_BOOTTIME ? 0 :
            now_ns() - clock_ns(g_input[id].clock);

    while ((len = read(g_input[id].fd, buf, sizeof(buf))) > 0) {
        size_t n = len / sizeof(buf[0]), i;

        for (i = 0; i < n; i++) {
            const struct input_event *ie = &buf[i];
            int axis;

            if (ie->type == EV_ABS) {
                for (axis = 0; axis < d->naxes; axis++) {
                    if (ie->code == d->codes[axis]) {
                        s->raw[axis] = ie->value;
                        break;
                    }
                }
            } else if (ie->type == EV_SYN && ie->code == SYN_DROPPED) {
                s->dropped = true;
            } else if (ie->type == EV_SYN && ie->code == SYN_REPORT) {
                if (s->dropped) {
                    s->dropped = false;
                    resync_axes(id, s);
                }
                if (s->enabled) {
                    int64_t ts = (int64_t)ie->time.tv_sec * NS_PER_S +
                                 (int64_t)ie->time.tv_usec * 1000 + offset;

                    emit_sample(ctx, id, ts);
                    s->next_repeat = s->last_ts + quiet_timeout_ns(s);
                }
            }
        }
    }
    if (len < 0 && errno != EAGAIN && errno != EINTR)
        ALOGE("%s: read: %s", d->input_name, strerror(errno));
}

/*
 * Timed work (lock held): initial reports of on-change sensors and repeats
 * of quiet continuous sensors. Returns the poll(2) timeout in ms until the
 * next deadline, -1 if none.
 */
static int timed_work(struct martini_sensors *ctx)
{
    int64_t now = now_ns(), next = -1;
    int id;

    for (id = 0; id < ID_COUNT; id++) {
        struct sensor_state *s = &ctx->state[id];
        int64_t deadline;

        if (g_input[id].fd < 0 || !s->enabled)
            continue;

        if (g_desc[id].on_change) {
            if (!s->initial_deadline)
                continue;
            if (now >= s->initial_deadline) {
                resync_axes(id, s);
                emit_sample(ctx, id, now);
                continue;
            }
            deadline = s->initial_deadline;
        } else {
            if (!REPEAT_QUIET_SAMPLES || s->next_repeat == INT64_MAX)
                continue;
            if (now >= s->next_repeat) {
                emit_sample(ctx, id, now);
                /* next repeats at the requested rate */
                s->next_repeat = now + s->delay_ms * NS_PER_MS;
            }
            deadline = s->next_repeat;
        }
        if (next < 0 || deadline < next)
            next = deadline;
    }

    if (next < 0)
        return -1;
    next = (next - now + NS_PER_MS - 1) / NS_PER_MS;
    return next > 60000 ? 60000 : (int)next;
}

/* ------------------------------------------------------------------------ */

static int clamp_delay(int id, int64_t period_ns)
{
    const struct sensor_desc *d = &g_desc[id];
    int64_t ms = period_ns / NS_PER_MS; /* truncate, see sensors_martini.h */

    if (ms < d->delay_min_ms)
        ms = d->delay_min_ms;
    if (ms > d->delay_max_ms)
        ms = d->delay_max_ms;
    return (int)ms;
}

static int martini_activate(struct sensors_poll_device_t *dev, int handle, int enabled)
{
    struct martini_sensors *ctx = (struct martini_sensors *)dev;
    struct sensor_state *s;
    int id = handle_to_id(handle), ret = 0;

    if (id < 0)
        return -EINVAL;
    s = &ctx->state[id];
    enabled = !!enabled;

    pthread_mutex_lock(&ctx->lock);
    if (enabled == s->enabled)
        goto out;

    if (enabled) {
        int64_t now;

        /*
         * Drop stale events before "enable": what the driver sends from now
         * on (the APDS-9930 forces a first report) stays queued in evdev
         * until poll() reads it, after the lock is released.
         */
        read_input(ctx, id);
        s->dropped = false;

        sysfs_write_int(g_input[id].sysfs, "poll_delay", s->delay_ms);
        ret = sysfs_write_int(g_input[id].sysfs, "enable", 1);
        if (ret)
            goto out;

        /* Start from the input core's values */
        resync_axes(id, s);
        s->enabled = true;

        /* Repeats only start after the first real sample */
        s->next_repeat = INT64_MAX;

        now = now_ns();
        if (g_desc[id].on_change)
            s->initial_deadline = now +
                (int64_t)(s->delay_ms + g_desc[id].initial_slack_ms) * NS_PER_MS;
    } else {
        ret = sysfs_write_int(g_input[id].sysfs, "enable", 0);
        s->enabled = false;
        s->initial_deadline = 0;
    }
    ALOGD("%s %s", g_desc[id].info.name, enabled ? "on" : "off");
out:
    pthread_mutex_unlock(&ctx->lock);
    if (!ret)
        wake_poll(ctx);
    return ret;
}

static int martini_batch(struct sensors_poll_device_1 *dev, int handle, int flags,
                        int64_t period_ns, int64_t timeout)
{
    struct martini_sensors *ctx = (struct martini_sensors *)dev;
    struct sensor_state *s;
    int id = handle_to_id(handle), ms, ret = 0;

    (void)flags;
    (void)timeout;  /* no hardware FIFO: max_report_latency is ignored */

    if (id < 0 || period_ns < 0)
        return -EINVAL;
    s = &ctx->state[id];
    ms = clamp_delay(id, period_ns);

    pthread_mutex_lock(&ctx->lock);
    if (ms != s->delay_ms) {
        ret = sysfs_write_int(g_input[id].sysfs, "poll_delay", ms);
        if (!ret)
            s->delay_ms = ms;
    }
    pthread_mutex_unlock(&ctx->lock);

    wake_poll(ctx);
    return ret;
}

static int martini_set_delay(struct sensors_poll_device_t *dev, int handle, int64_t period_ns)
{
    return martini_batch((struct sensors_poll_device_1 *)dev, handle, 0, period_ns, 0);
}

static int martini_flush(struct sensors_poll_device_1 *dev, int handle)
{
    struct martini_sensors *ctx = (struct martini_sensors *)dev;
    int id = handle_to_id(handle);

    if (id < 0)
        return -EINVAL;

    pthread_mutex_lock(&ctx->lock);
    ctx->state[id].flush_pending++;
    pthread_mutex_unlock(&ctx->lock);

    wake_poll(ctx);
    return 0;
}

static int martini_poll(struct sensors_poll_device_t *dev, sensors_event_t *data, int count)
{
    struct martini_sensors *ctx = (struct martini_sensors *)dev;
    struct pollfd pfd[ID_COUNT + 1];
    int pfd_id[ID_COUNT + 1];
    int npfd = 0, id, n, timeout, ret;

    if (count <= 0)
        return -EINVAL;

    pfd[npfd].fd = ctx->wake_fds[0];
    pfd[npfd].events = POLLIN;
    pfd_id[npfd++] = -1;
    for (id = 0; id < ID_COUNT; id++) {
        if (g_input[id].fd < 0)
            continue;
        pfd[npfd].fd = g_input[id].fd;
        pfd[npfd].events = POLLIN;
        pfd_id[npfd++] = id;
    }

    for (;;) {
        pthread_mutex_lock(&ctx->lock);
        timeout = timed_work(ctx);
        n = queue_pop(ctx, data, count);
        pthread_mutex_unlock(&ctx->lock);
        if (n > 0)
            return n;

        ret = poll(pfd, npfd, timeout);
        if (ret < 0) {
            if (errno == EINTR)
                continue;
            ALOGE("poll: %s", strerror(errno));
            return -errno;
        }

        if (pfd[0].revents & POLLIN) {
            char buf[16];

            while (read(ctx->wake_fds[0], buf, sizeof(buf)) > 0)
                ;
        }

        pthread_mutex_lock(&ctx->lock);
        for (n = 1; n < npfd; n++) {
            if (pfd[n].revents & POLLIN)
                read_input(ctx, pfd_id[n]);
        }
        /* After the samples already read, so that they come first */
        for (id = 0; id < ID_COUNT; id++) {
            while (ctx->state[id].flush_pending > 0) {
                ctx->state[id].flush_pending--;
                emit_flush_complete(ctx, id);
            }
        }
        pthread_mutex_unlock(&ctx->lock);
    }
}

static int martini_close(struct hw_device_t *dev)
{
    struct martini_sensors *ctx = (struct martini_sensors *)dev;
    int id;

    for (id = 0; id < ID_COUNT; id++) {
        if (g_input[id].fd >= 0 && ctx->state[id].enabled)
            sysfs_write_int(g_input[id].sysfs, "enable", 0);
    }
    close(ctx->wake_fds[0]);
    close(ctx->wake_fds[1]);
    pthread_mutex_destroy(&ctx->lock);
    free(ctx);
    return 0;
}

static int open_sensors(const struct hw_module_t *module, const char *name,
                        struct hw_device_t **device)
{
    struct martini_sensors *ctx;
    int id;

    if (strcmp(name, SENSORS_HARDWARE_POLL) != 0)
        return -EINVAL;

    pthread_once(&g_probe_once, probe_inputs);

    ctx = calloc(1, sizeof(*ctx));
    if (!ctx)
        return -ENOMEM;

    if (pipe2(ctx->wake_fds, O_CLOEXEC | O_NONBLOCK) < 0) {
        int ret = -errno;

        ALOGE("pipe2: %s", strerror(errno));
        free(ctx);
        return ret;
    }
    pthread_mutex_init(&ctx->lock, NULL);

    for (id = 0; id < ID_COUNT; id++) {
        struct sensor_state *s = &ctx->state[id];

        if (g_input[id].fd < 0)
            continue;
        /* Known starting point: off, at the slowest rate */
        s->delay_ms = g_desc[id].delay_max_ms;
        sysfs_write_int(g_input[id].sysfs, "enable", 0);
        sysfs_write_int(g_input[id].sysfs, "poll_delay", s->delay_ms);
    }

    ctx->device.common.tag = HARDWARE_DEVICE_TAG;
    ctx->device.common.version = SENSORS_DEVICE_API_VERSION_1_3;
    ctx->device.common.module = (struct hw_module_t *)module;
    ctx->device.common.close = martini_close;
    ctx->device.activate = martini_activate;
    ctx->device.setDelay = martini_set_delay;
    ctx->device.poll = martini_poll;
    ctx->device.batch = martini_batch;
    ctx->device.flush = martini_flush;

    *device = &ctx->device.common;
    return 0;
}

static int get_sensors_list(struct sensors_module_t *module, struct sensor_t const **list)
{
    (void)module;

    pthread_once(&g_probe_once, probe_inputs);
    *list = g_list;
    return g_list_count;
}

static int set_operation_mode(unsigned int mode)
{
    return mode == SENSOR_HAL_NORMAL_MODE ? 0 : -EINVAL;
}

static struct hw_module_methods_t sensors_module_methods = {
    .open = open_sensors,
};

struct sensors_module_t HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = SENSORS_MODULE_API_VERSION_0_1,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = SENSORS_HARDWARE_MODULE_ID,
        .name = "Lumia 930 sensors HAL",
        .author = "The LineageOS Project",
        .methods = &sensors_module_methods,
    },
    .get_sensors_list = get_sensors_list,
    .set_operation_mode = set_operation_mode,
};
