/* SPDX-License-Identifier: GPL-3.0-or-later AND ISC
 * Copyright (C) 2026 Davidian
 * The LV2 interface declarations in this file follow the ISC-licensed LV2 headers;
 * see LICENSE-LV2 for their copyright and permission notice. */
/* Loads the built plugin (and optionally the GUI) from the bundle with dlopen, the way a
 * Linux or macOS host does, and runs each through its life cycle. On Linux run it under
 * a virtual display (xvfb-run) to exercise the X11 GUI.
 * Usage: load_check <bundle folder> <.so|.dylib> [--no-ui] */
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/lv2_core.h"
#include "../src/ports.h"

typedef void (*WriteFn)(void*, uint32_t, uint32_t, uint32_t, const void*);
typedef struct {
    const char* URI;
    void* (*instantiate)(const void*, const char*, const char*, WriteFn, void*, void**, const LV2_Feature* const*);
    void (*cleanup)(void*);
    void (*port_event)(void*, uint32_t, uint32_t, uint32_t, const void*);
    const void* (*extension_data)(const char*);
} UiDesc;
typedef struct { int (*idle)(void* ui); } IdleIface;

static int failures;

static void check(const char* what, int ok)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        ++failures;
}

static void on_write(void* c, uint32_t p, uint32_t s, uint32_t f, const void* b)
{
    (void)c; (void)p; (void)s; (void)f; (void)b;
}

static void check_plugin(const char* bundle, const char* ext)
{
    char path[1024];
    void* lib;
    const LV2_Descriptor* (*get)(uint32_t);
    const LV2_Descriptor* d;
    LV2_Handle h;
    float ctl[P_COUNT] = { 0 };
    float in[512], out[512];
    int i, finite = 1;
    double peak = 0;

    snprintf(path, sizeof path, "%s/brownstone100%s", bundle, ext);
    lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    check("plugin library loads", lib != NULL);
    if (!lib) {
        printf("  %s\n", dlerror());
        return;
    }
    *(void**)&get = dlsym(lib, "lv2_descriptor");
    check("exports lv2_descriptor", get != NULL);
    if (!get)
        return;
    d = get(0);
    check("descriptor has the plugin's URI", d && strcmp(d->URI, BROWNSTONE_URI) == 0);
    h = d->instantiate(d, 48000.0, bundle, NULL);
    check("instantiates at 48 kHz", h != NULL);
    if (!h)
        return;
    ctl[P_CHANNEL] = 2; ctl[P_LEAD_PRE] = 7; ctl[P_LEAD_POST] = 3;
    ctl[P_LOW] = ctl[P_MID] = ctl[P_HIGH] = 5; ctl[P_PRESENCE] = ctl[P_RESONANCE] = 5;
    ctl[P_CAB] = 1; ctl[P_CAB_MODEL] = 1;
    for (i = 2; i < P_COUNT; ++i)
        d->connect_port(h, (uint32_t)i, &ctl[i]);
    d->connect_port(h, P_IN, in);
    d->connect_port(h, P_OUT, out);
    d->activate(h);
    for (int block = 0; block < 200; ++block) {
        for (i = 0; i < 512; ++i)
            in[i] = 0.2f * (float)sin(2 * 3.14159265 * 110.0 * (block * 512 + i) / 48000.0);
        if (block == 100)
            ctl[P_CAB_MODEL] = 3;
        d->run(h, 512);
        for (i = 0; i < 512; ++i) {
            if (!isfinite(out[i]))
                finite = 0;
            peak = fmax(peak, fabs(out[i]));
        }
    }
    printf("         output peak %.3f\n", peak);
    check("runs 2 s of a 110 Hz tone with finite, non-silent output", finite && peak > 1e-3 && peak < 4.0);
    d->deactivate(h);
    d->cleanup(h);
    dlclose(lib);
}

static void check_ui(const char* bundle, const char* ext)
{
    char path[1024];
    void* lib;
    const UiDesc* (*get)(uint32_t);
    const UiDesc* d;
    const IdleIface* idle;
    void* ui;
    void* widget = NULL;
    int i;
    static const float vals[P_COUNT] = { 0, 0, 2, 0, 3, 4, 6, 3, 5, 5, 5, 5, 5, 1, 0, 0, 2 };

    snprintf(path, sizeof path, "%s/brownstone100_ui%s", bundle, ext);
    lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    check("GUI library loads", lib != NULL);
    if (!lib) {
        printf("  %s\n", dlerror());
        return;
    }
    *(void**)&get = dlsym(lib, "lv2ui_descriptor");
    check("exports lv2ui_descriptor", get != NULL);
    if (!get)
        return;
    d = get(0);
    check("descriptor has the GUI's URI", d && strcmp(d->URI, BROWNSTONE_UI_URI) == 0);
    ui = d->instantiate(d, BROWNSTONE_URI, bundle, on_write, NULL, &widget, NULL);
    check("instantiates a top-level window (no parent given)", ui != NULL && widget != NULL);
    if (!ui)
        return;
    for (i = P_CHANNEL; i < P_COUNT; ++i)
        d->port_event(ui, (uint32_t)i, sizeof(float), 0, &vals[i]);
    idle = (const IdleIface*)d->extension_data("http://lv2plug.in/ns/extensions/ui#idleInterface");
    check("offers the idle interface", idle != NULL);
    if (idle) {
        int ok = 1;
        for (i = 0; i < 20; ++i)
            ok &= idle->idle(ui) == 0;
        check("idle runs and keeps the window open", ok);
    }
    d->cleanup(ui);
    check("cleanup returns", 1);
    dlclose(lib);
}

int main(int argc, char** argv)
{
    if (argc < 3) {
        printf("usage: load_check <bundle folder> <.so|.dylib> [--no-ui]\n");
        return 2;
    }
    check_plugin(argv[1], argv[2]);
    if (argc < 4 || strcmp(argv[3], "--no-ui") != 0)
        check_ui(argv[1], argv[2]);
    printf(failures ? "\n%d FAILED\n" : "\nAll passed\n", failures);
    return failures ? 1 : 0;
}
