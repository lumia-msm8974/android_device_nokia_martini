/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Perf lock provider for the MSM8974 power HAL on the Lumia 930 (from the
 * Lumia 1520 port).
 *
 * The power HAL (vendor/qcom/opensource/power, power-8974.c) asks for its
 * boosts through perf_lock_acq()/perf_lock_rel() of the library named by
 * ro.vendor.extension_library. On the OnePlus One that library forwards the
 * requests to mpdecision, which also hotplugs cores. mpdecision cannot run
 * on the MSM8974 Lumias (the Windows Phone TrustZone refuses CPU hotplug),
 * so every interaction, launch, display and profile hint failed.
 *
 * This library implements the same requests directly in the HAL process:
 * the MSM8974 perf lock opcodes used by power-8974.c are applied to the
 * cpufreq and interactive governor sysfs files, every active lock is kept
 * in a table and a timer thread drops the timed ones. Requests that need
 * hotplug or power collapse are accepted and ignored: all four cores are
 * always online and idle in WFI.
 *
 * Opcodes (16 bit, 0xRRVV: resource RR, value VV):
 *   0x02-0x05  CPU0-3 minimum frequency, VV x 100 MHz (0xFE/0xFF = max)
 *   0x0E       interactive timer_rate, (0xFF - VV) x 10 ms
 *   0x0F       interactive hispeed_freq, VV x 100 MHz
 *   0x10       interactive go_hispeed_load, VV %
 *   0x15-0x18  CPU0-3 maximum frequency, VV x 100 MHz
 *   0x1B       interactive io_is_busy, VV
 *   others     (power collapse, hotplug, bus, migration) ignored
 * Frequencies are rounded up to the next available one, like mpdecision.
 *
 * Baselines: when the library is loaded it writes the full frequency range
 * and the governor values of init (vendor.perflock.base.* properties, set
 * by init.martini.power.rc next to the values themselves), whatever is in
 * sysfs. A power HAL that died with a boost held (or a lock without
 * timeout) would otherwise leave it in place for good, and the next
 * instance would take the boosted values as its baseline.
 */

#define LOG_TAG "martini-perflock"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/system_properties.h>
#include <log/log.h>

#define NR_CPUS 4
#define MAX_LOCKS 32
#define MAX_RES 32
#define MAX_FREQS 32

#define CPUFREQ "/sys/devices/system/cpu/cpu%d/cpufreq/%s"
#define INTERACTIVE "/sys/devices/system/cpu/cpufreq/interactive/"

enum tunable { T_TIMER_RATE, T_HISPEED_FREQ, T_GO_HISPEED_LOAD, T_IO_IS_BUSY, NR_TUNABLES };

static const char* const tunable_prop[NR_TUNABLES] = {
        "vendor.perflock.base.timer_rate",
        "vendor.perflock.base.hispeed_freq",
        "vendor.perflock.base.go_hispeed_load",
        "vendor.perflock.base.io_is_busy",
};

static const char* const tunable_path[NR_TUNABLES] = {
        INTERACTIVE "timer_rate",
        INTERACTIVE "hispeed_freq",
        INTERACTIVE "go_hispeed_load",
        INTERACTIVE "io_is_busy",
};

struct perf_lock {
    int handle;
    unsigned long seq; /* order of the requests, for the governor tunables */
    int indefinite;
    struct timespec expiry;
    int min_freq[NR_CPUS]; /* 0 = no request */
    int max_freq[NR_CPUS]; /* 0 = no request */
    int tunable[NR_TUNABLES]; /* -1 = no request */
};

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t timer_cond = PTHREAD_COND_INITIALIZER;
static struct perf_lock locks[MAX_LOCKS];
static int next_handle = 1;
static unsigned long next_seq = 1;
static int timer_started;

static int freqs[MAX_FREQS], nr_freqs;
static int cpu_min_hw, cpu_max_hw;
/* Values last written, so that nothing is rewritten when nothing changes */
static int cur_min[NR_CPUS], cur_max[NR_CPUS];
static int cur_tunable[NR_TUNABLES];
/* Governor values set by init, read before the first override */
static int base_tunable[NR_TUNABLES];
static int base_saved[NR_TUNABLES];

static int read_int(const char* path, int* value) {
    char buf[32];
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -errno;
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return -EIO;
    buf[n] = '\0';
    *value = atoi(buf);
    return 0;
}

static int write_int(const char* path, int value) {
    char buf[16];
    int len = snprintf(buf, sizeof(buf), "%d", value);
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        ALOGE("open %s: %s", path, strerror(errno));
        return -errno;
    }
    int ret = write(fd, buf, len) == len ? 0 : -errno;
    if (ret) ALOGE("write %d to %s: %s", value, path, strerror(-ret));
    close(fd);
    return ret;
}

static int write_cpufreq(int cpu, const char* file, int value) {
    char path[96];
    snprintf(path, sizeof(path), CPUFREQ, cpu, file);
    return write_int(path, value);
}

static void load_frequencies(void) {
    char path[96], buf[512];
    if (nr_freqs) return;

    snprintf(path, sizeof(path), CPUFREQ, 0, "scaling_available_frequencies");
    FILE* f = fopen(path, "re");
    if (f) {
        if (fgets(buf, sizeof(buf), f)) {
            char* save = NULL;
            for (char* t = strtok_r(buf, " \n", &save); t && nr_freqs < MAX_FREQS;
                 t = strtok_r(NULL, " \n", &save))
                freqs[nr_freqs++] = atoi(t);
        }
        fclose(f);
    }
    snprintf(path, sizeof(path), CPUFREQ, 0, "cpuinfo_min_freq");
    if (read_int(path, &cpu_min_hw)) cpu_min_hw = nr_freqs ? freqs[0] : 300000;
    snprintf(path, sizeof(path), CPUFREQ, 0, "cpuinfo_max_freq");
    if (read_int(path, &cpu_max_hw)) cpu_max_hw = nr_freqs ? freqs[nr_freqs - 1] : 2150400;
    for (int cpu = 0; cpu < NR_CPUS; cpu++) {
        cur_min[cpu] = cpu_min_hw;
        cur_max[cpu] = cpu_max_hw;
    }
    for (int t = 0; t < NR_TUNABLES; t++) cur_tunable[t] = -1;
    ALOGI("%d frequencies, %d-%d kHz", nr_freqs, cpu_min_hw, cpu_max_hw);

    /*
     * Start from the baselines, written unconditionally: no lock is held
     * yet, so nothing of a previous instance must survive. Minimum first,
     * then maximum: both orders keep min <= max from any leftover state.
     */
    for (int cpu = 0; cpu < NR_CPUS; cpu++) {
        write_cpufreq(cpu, "scaling_min_freq", cpu_min_hw);
        write_cpufreq(cpu, "scaling_max_freq", cpu_max_hw);
    }
    for (int t = 0; t < NR_TUNABLES; t++) {
        char value[PROP_VALUE_MAX];
        char* end = NULL;
        long base;

        if (__system_property_get(tunable_prop[t], value) <= 0) continue;
        base = strtol(value, &end, 10);
        if (end == value || *end || base < 0) continue;
        base_tunable[t] = (int)base;
        base_saved[t] = 1;
        if (!write_int(tunable_path[t], base_tunable[t])) cur_tunable[t] = base_tunable[t];
    }
}

/* At load time, before the power HAL asks for anything */
__attribute__((constructor)) static void perflock_init(void) {
    pthread_mutex_lock(&lock);
    load_frequencies();
    pthread_mutex_unlock(&lock);
}

/* VV x 100 MHz rounded up to an available frequency; 0xFE/0xFF = maximum */
static int opcode_freq(int value) {
    if (value >= 0xFE) return cpu_max_hw;
    int khz = value * 100000;
    for (int i = 0; i < nr_freqs; i++)
        if (freqs[i] >= khz) return freqs[i];
    return cpu_max_hw;
}

static void parse_request(struct perf_lock* l, const int* list, int n) {
    for (int cpu = 0; cpu < NR_CPUS; cpu++) l->min_freq[cpu] = l->max_freq[cpu] = 0;
    for (int t = 0; t < NR_TUNABLES; t++) l->tunable[t] = -1;

    for (int i = 0; i < n; i++) {
        int res = (list[i] >> 8) & 0xFF;
        int val = list[i] & 0xFF;

        if (list[i] & ~0xFFFF) continue; /* MPCTL v3 opcodes: not for 8974 */

        if (res >= 0x02 && res <= 0x05) {
            l->min_freq[res - 0x02] = opcode_freq(val);
        } else if (res >= 0x15 && res <= 0x18) {
            l->max_freq[res - 0x15] = opcode_freq(val);
        } else if (res == 0x0E) {
            l->tunable[T_TIMER_RATE] = (0xFF - val) * 10000;
        } else if (res == 0x0F) {
            l->tunable[T_HISPEED_FREQ] = opcode_freq(val);
        } else if (res == 0x10) {
            l->tunable[T_GO_HISPEED_LOAD] = val;
        } else if (res == 0x1B) {
            l->tunable[T_IO_IS_BUSY] = val ? 1 : 0;
        }
    }
}

/* Called with the mutex held: write the combination of all active locks */
static void apply_locks(void) {
    int min[NR_CPUS], max[NR_CPUS], tunable[NR_TUNABLES];
    unsigned long newest[NR_TUNABLES];

    for (int cpu = 0; cpu < NR_CPUS; cpu++) {
        min[cpu] = cpu_min_hw;
        max[cpu] = cpu_max_hw;
    }
    for (int t = 0; t < NR_TUNABLES; t++) {
        tunable[t] = -1;
        newest[t] = 0;
    }

    for (int i = 0; i < MAX_LOCKS; i++) {
        struct perf_lock* l = &locks[i];
        if (!l->handle) continue;
        for (int cpu = 0; cpu < NR_CPUS; cpu++) {
            if (l->min_freq[cpu] > min[cpu]) min[cpu] = l->min_freq[cpu];
            if (l->max_freq[cpu] && l->max_freq[cpu] < max[cpu]) max[cpu] = l->max_freq[cpu];
        }
        /* governor tunables: the most recent request wins */
        for (int t = 0; t < NR_TUNABLES; t++) {
            if (l->tunable[t] >= 0 && l->seq > newest[t]) {
                tunable[t] = l->tunable[t];
                newest[t] = l->seq;
            }
        }
    }

    for (int cpu = 0; cpu < NR_CPUS; cpu++) {
        if (min[cpu] > max[cpu]) min[cpu] = max[cpu];
        /* keep min <= max at every step: lower first, then raise */
        if (max[cpu] < cur_max[cpu]) {
            if (min[cpu] != cur_min[cpu] && !write_cpufreq(cpu, "scaling_min_freq", min[cpu]))
                cur_min[cpu] = min[cpu];
            if (!write_cpufreq(cpu, "scaling_max_freq", max[cpu])) cur_max[cpu] = max[cpu];
        } else {
            if (max[cpu] != cur_max[cpu] && !write_cpufreq(cpu, "scaling_max_freq", max[cpu]))
                cur_max[cpu] = max[cpu];
            if (min[cpu] != cur_min[cpu] && !write_cpufreq(cpu, "scaling_min_freq", min[cpu]))
                cur_min[cpu] = min[cpu];
        }
    }

    for (int t = 0; t < NR_TUNABLES; t++) {
        int value = tunable[t];
        if (value >= 0 && !base_saved[t]) {
            /* first override of this tunable: remember what init set */
            if (!read_int(tunable_path[t], &base_tunable[t])) {
                base_saved[t] = 1;
                cur_tunable[t] = base_tunable[t];
            }
        }
        if (value < 0) {
            if (!base_saved[t]) continue;
            value = base_tunable[t];
        }
        if (value != cur_tunable[t] && !write_int(tunable_path[t], value)) cur_tunable[t] = value;
    }
}

static int ts_before(const struct timespec* a, const struct timespec* b) {
    return a->tv_sec < b->tv_sec || (a->tv_sec == b->tv_sec && a->tv_nsec < b->tv_nsec);
}

static void* timer_thread(void* arg) {
    (void)arg;
    pthread_mutex_lock(&lock);
    for (;;) {
        struct timespec now, next = {0, 0};
        int expired = 0, pending = 0;

        clock_gettime(CLOCK_MONOTONIC, &now);
        for (int i = 0; i < MAX_LOCKS; i++) {
            struct perf_lock* l = &locks[i];
            if (!l->handle || l->indefinite) continue;
            if (!ts_before(&now, &l->expiry)) {
                l->handle = 0;
                expired = 1;
            } else if (!pending || ts_before(&l->expiry, &next)) {
                next = l->expiry;
                pending = 1;
            }
        }
        if (expired) apply_locks();

        if (pending)
            pthread_cond_timedwait(&timer_cond, &lock, &next);
        else
            pthread_cond_wait(&timer_cond, &lock);
    }
    return NULL;
}

static void start_timer_locked(void) {
    pthread_condattr_t attr;
    pthread_t thread;

    if (timer_started) return;
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    pthread_cond_destroy(&timer_cond);
    pthread_cond_init(&timer_cond, &attr);
    pthread_condattr_destroy(&attr);
    if (pthread_create(&thread, NULL, timer_thread, NULL)) {
        ALOGE("cannot start the timer thread");
        return;
    }
    pthread_detach(thread);
    timer_started = 1;
}

static struct perf_lock* find_lock(int handle) {
    if (handle <= 0) return NULL;
    for (int i = 0; i < MAX_LOCKS; i++)
        if (locks[i].handle == handle) return &locks[i];
    return NULL;
}

int perf_lock_acq(int handle, int duration, int list[], int numArgs) {
    struct perf_lock* l;

    if (!list || numArgs <= 0 || numArgs > MAX_RES || duration < 0) return -1;

    pthread_mutex_lock(&lock);
    load_frequencies();
    start_timer_locked();

    /* an active handle is renewed in place, otherwise a new one is made */
    l = find_lock(handle);
    if (!l) {
        for (int i = 0; i < MAX_LOCKS && !l; i++)
            if (!locks[i].handle) l = &locks[i];
        if (!l) {
            pthread_mutex_unlock(&lock);
            ALOGE("no free perf lock");
            return -1;
        }
        l->handle = next_handle++;
        if (next_handle <= 0) next_handle = 1;
    }

    parse_request(l, list, numArgs);
    l->seq = next_seq++;
    l->indefinite = duration == 0;
    if (!l->indefinite) {
        clock_gettime(CLOCK_MONOTONIC, &l->expiry);
        l->expiry.tv_sec += duration / 1000;
        l->expiry.tv_nsec += (long)(duration % 1000) * 1000000L;
        if (l->expiry.tv_nsec >= 1000000000L) {
            l->expiry.tv_sec++;
            l->expiry.tv_nsec -= 1000000000L;
        }
    }

    apply_locks();
    handle = l->handle;
    pthread_cond_signal(&timer_cond);
    pthread_mutex_unlock(&lock);
    return handle;
}

int perf_lock_rel(int handle) {
    struct perf_lock* l;

    pthread_mutex_lock(&lock);
    l = find_lock(handle);
    if (l) {
        l->handle = 0;
        apply_locks();
        pthread_cond_signal(&timer_cond);
    }
    pthread_mutex_unlock(&lock);
    return l ? 0 : -1;
}

/* Named hints are a perfd feature of later SoCs: power-8974.c never uses them */
int perf_hint(int hint, const char* pkg, int duration, int type) {
    (void)hint;
    (void)pkg;
    (void)duration;
    (void)type;
    return -1;
}
