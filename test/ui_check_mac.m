/* SPDX-License-Identifier: GPL-3.0-or-later AND ISC
 * Copyright (C) 2026 Davidian
 * The LV2 interface declarations in this file follow the ISC-licensed LV2 headers;
 * see LICENSE-LV2 for their copyright and permission notice. */
/* macOS counterpart of ui_check.c: loads the built Cocoa GUI the way a host does, embeds
 * it in a window, drives it with mouse events and checks the values it sends back. Also
 * checks the drawn panel is the right way up and saves a screenshot.
 * Build: clang -x objective-c -fno-objc-arc test/ui_check_mac.m -framework Cocoa -o ui_check_mac
 * Usage: ui_check_mac <bundle folder> [screenshot.png] */
#import <Cocoa/Cocoa.h>
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
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

static int failures;
static int last_port = -1, writes;
static float last_value;

static void check(const char* what, int ok)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        ++failures;
}

static void on_write(void* controller, uint32_t port, uint32_t size, uint32_t format, const void* buf)
{
    (void)controller;
    if (size == sizeof(float) && format == 0) {
        last_port = (int)port;
        last_value = *(const float*)buf;
        ++writes;
    }
}

/* A mouse event at panel point (x, y), top-left origin. */
static NSEvent* mouse(NSEventType type, NSView* v, float x, float y, int clicks)
{
    NSPoint p = [v convertPoint:NSMakePoint(x, y) toView:nil];
    return [NSEvent mouseEventWithType:type location:p modifierFlags:0 timestamp:0
                          windowNumber:[[v window] windowNumber] context:nil eventNumber:0
                            clickCount:clicks pressure:1.0f];
}

static void click(NSView* v, float x, float y)
{
    [v mouseDown:mouse(NSEventTypeLeftMouseDown, v, x, y, 1)];
    [v mouseUp:mouse(NSEventTypeLeftMouseUp, v, x, y, 1)];
}

static void drag(NSView* v, float x, float y, float dy)
{
    int i;
    [v mouseDown:mouse(NSEventTypeLeftMouseDown, v, x, y, 1)];
    for (i = 1; i <= 6; ++i)
        [v mouseDragged:mouse(NSEventTypeLeftMouseDragged, v, x, y + dy * i / 6, 1)];
    [v mouseUp:mouse(NSEventTypeLeftMouseUp, v, x, y + dy, 1)];
}

/* Brightness of the panel pixel at (x, y), top-left origin, whatever the backing scale. */
static float brightness(NSBitmapImageRep* rep, float x, float y)
{
    const float s = (float)[rep pixelsWide] / 960.0f;
    NSColor* c = [[rep colorAtX:(NSInteger)(x * s) y:(NSInteger)(y * s)]
        colorUsingColorSpace:[NSColorSpace deviceRGBColorSpace]];
    return (float)(([c redComponent] + [c greenComponent] + [c blueComponent]) / 3.0);
}

int main(int argc, char** argv)
{
    @autoreleasepool {
        char path[1024], label[160];
        void* lib;
        const UiDesc* (*get)(uint32_t);
        const UiDesc* d;
        NSWindow* win;
        NSView* parent;
        NSView* v;
        LV2_Feature parent_feature;
        const LV2_Feature* features[2];
        void* ui;
        void* widget = NULL;
        int i;

        if (argc < 2) {
            printf("usage: ui_check_mac <bundle folder> [screenshot.png]\n");
            return 2;
        }
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];

        snprintf(path, sizeof path, "%s/brownstone100_ui.dylib", argv[1]);
        lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        check("GUI library loads", lib != NULL);
        if (!lib) {
            printf("  %s\n", dlerror());
            return 1;
        }
        *(void**)&get = dlsym(lib, "lv2ui_descriptor");
        check("exports lv2ui_descriptor", get != NULL);
        if (!get)
            return 1;
        d = get(0);
        check("descriptor has the GUI's URI", d && strcmp(d->URI, BROWNSTONE_UI_URI) == 0);
        check("only one GUI in the library", get(1) == NULL);

        win = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 960, 470)
                                          styleMask:NSWindowStyleMaskBorderless
                                            backing:NSBackingStoreBuffered
                                              defer:NO];
        parent = [win contentView];
        parent_feature.URI = "http://lv2plug.in/ns/extensions/ui#parent";
        parent_feature.data = parent;
        features[0] = &parent_feature;
        features[1] = NULL;

        check("refuses another plugin's URI",
              d->instantiate(d, "urn:other", argv[1], on_write, NULL, &widget, features) == NULL);
        ui = d->instantiate(d, BROWNSTONE_URI, argv[1], on_write, NULL, &widget, features);
        check("instantiates inside the host's view", ui != NULL && widget != NULL);
        if (!ui)
            return 1;
        v = (NSView*)widget;
        check("the widget is a subview of the host's view", [v superview] == parent);
        snprintf(label, sizeof label, "view is %.0fx%.0f", [v frame].size.width, [v frame].size.height);
        check(label, [v frame].size.width == 960 && [v frame].size.height == 470);
        check("offers the idle interface",
              d->extension_data("http://lv2plug.in/ns/extensions/ui#idleInterface") != NULL);

        {
            static const float vals[P_COUNT] = { 0, 0, 2, 0, 4, 6, 6, 3, 5, 5, 5, 5, 5, 1, 0, 0, 0 };
            for (i = P_CHANNEL; i < P_COUNT; ++i)
                d->port_event(ui, (uint32_t)i, sizeof(float), 0, &vals[i]);
        }
        check("port updates from the host write nothing back", writes == 0);

        drag(v, 351, 150, 36);
        snprintf(label, sizeof label, "dragging Lead Pre down 36 pt sends port %d = %.3f", last_port, last_value);
        check(label, last_port == P_LEAD_PRE && fabsf(last_value - 4.0f) < 0.01f);

        drag(v, 211, 150, 1);
        [v mouseDown:mouse(NSEventTypeLeftMouseDown, v, 211, 150, 2)];
        [v mouseUp:mouse(NSEventTypeLeftMouseUp, v, 211, 150, 2)];
        snprintf(label, sizeof label, "double-click resets Rhythm Post to %.2f", last_value);
        check(label, last_port == P_RHY_POST && fabsf(last_value - 6.0f) < 1e-4f);

        click(v, 230, 376 - 18);
        check("top of the channel switch selects Rhythm", last_port == P_CHANNEL && last_value == 0.0f);
        click(v, 398, 386);
        check("cabinet footswitch turns the cab sim off", last_port == P_CAB && last_value == 0.0f);
        click(v, 540, 382);
        check("clicking \"2X12 OPEN\" selects cabinet model 2", last_port == P_CAB_MODEL && last_value == 2.0f);
        drag(v, 482, 372, 30);
        check("dragging the selector down a step selects model 3", last_port == P_CAB_MODEL && last_value == 3.0f);

        /* Draw the view and check it is the right way up: the readout window at the top
         * right is near-black, the panel below it is graphite. */
        {
            NSBitmapImageRep* rep = [v bitmapImageRepForCachingDisplayInRect:[v bounds]];
            float top, below;
            [v cacheDisplayInRect:[v bounds] toBitmapImageRep:rep];
            top = brightness(rep, 900, 50);
            below = brightness(rep, 900, 64);
            snprintf(label, sizeof label, "panel draws the right way up (readout %.3f, panel %.3f)", top, below);
            check(label, top < 0.08f && below > top + 0.04f);
            if (argc > 2) {
                NSData* png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
                check("screenshot written", [png writeToFile:[NSString stringWithUTF8String:argv[2]] atomically:YES]);
                printf("         %s\n", argv[2]);
            }
        }

        d->cleanup(ui);
        check("cleanup removes the view from the host", [[parent subviews] count] == 0);
        [win release];
        printf(failures ? "\n%d FAILED\n" : "\nAll passed\n", failures);
    }
    return failures ? 1 : 0;
}
