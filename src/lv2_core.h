/* SPDX-License-Identifier: ISC
 * Follows lv2/core/lv2.h:
 *   Copyright 2006-2020 David Robillard <d@drobilla.net>
 *   Copyright 2006-2012 Steve Harris <steve@plugin.org.uk>
 *   Copyright 2000-2002 Richard W.E. Furse, Paul Barton-Davis, Stefan Westerfeld.
 * See LICENSE-LV2 for the permission notice. */
/*
 * Minimal LV2 core ABI declarations, compatible with <lv2/core/lv2.h>.
 * Used only when the official LV2 headers are not installed
 * (build with -DHAVE_LV2 to use the system headers instead).
 */
#ifndef BROWNSTONE_LV2_CORE_H
#define BROWNSTONE_LV2_CORE_H

#ifdef HAVE_LV2
#  include <lv2/core/lv2.h>
#else

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* LV2_Handle;

typedef struct {
    const char* URI;
    void*       data;
} LV2_Feature;

typedef struct LV2_Descriptor {
    const char* URI;
    LV2_Handle (*instantiate)(const struct LV2_Descriptor* descriptor,
                              double                       sample_rate,
                              const char*                  bundle_path,
                              const LV2_Feature* const*    features);
    void (*connect_port)(LV2_Handle instance, uint32_t port, void* data_location);
    void (*activate)(LV2_Handle instance);
    void (*run)(LV2_Handle instance, uint32_t sample_count);
    void (*deactivate)(LV2_Handle instance);
    void (*cleanup)(LV2_Handle instance);
    const void* (*extension_data)(const char* uri);
} LV2_Descriptor;

#ifdef _WIN32
#  define LV2_SYMBOL_EXPORT __declspec(dllexport)
#else
#  define LV2_SYMBOL_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
}
#endif

#endif /* HAVE_LV2 */
#endif /* BROWNSTONE_LV2_CORE_H */
