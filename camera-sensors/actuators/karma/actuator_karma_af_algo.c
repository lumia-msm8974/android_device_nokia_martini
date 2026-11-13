/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AF algorithm tuning for the Karma actuator: libactuator_karma_camera.so and
 * libactuator_karma_camcorder.so (build with -DAF_MODE=camera or camcorder).
 *
 * The daemon loads libactuator_<actuator>_<mode>.so and calls
 * <actuator>_<mode>_af_algo_open_lib(). The tuning structure is the large
 * proprietary AF algorithm table of the daemon generation; until a Karma one
 * exists, these forward to the tuning that ships with the daemon for its own
 * 10-bit VCM (520 steps), which the step table of libactuator_karma.so is
 * sized for.
 *
 * Copyright (c) 2026, Lorenzo Giannoccaro.
 */
#include <dlfcn.h>
#include <stddef.h>

#ifndef AF_MODE
#error "build with -DAF_MODE=camera or -DAF_MODE=camcorder"
#endif
#define STR_(x) #x
#define STR(x) STR_(x)
#define CAT3_(a, b, c) a##b##c
#define CAT3(a, b, c) CAT3_(a, b, c)

#define REF_LIB "libactuator_dw9714_" STR(AF_MODE) ".so"
#define REF_SYM "dw9714_" STR(AF_MODE) "_af_algo_open_lib"

__attribute__((visibility("default")))
void *CAT3(karma_, AF_MODE, _af_algo_open_lib)(void)
{
    void *h = dlopen(REF_LIB, RTLD_NOW);
    void *(*open_lib)(void);

    if (!h)
        return NULL;
    open_lib = (void *(*)(void))dlsym(h, REF_SYM);
    return open_lib ? open_lib() : NULL;
}
