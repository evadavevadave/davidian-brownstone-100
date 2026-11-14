/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian */
/* The GUI panel: layout, drawing and mouse handling, independent of any windowing system.
 *
 * House style (style.h, font.h, canvas.h) shared with the other plugin GUIs. The layout
 * follows the amp's controls (two channels' gain, tone stack, power-amp feedback) under an
 * original name, with none of the original amplifier's branding or panel artwork. */
#ifndef UI_PANEL_H
#define UI_PANEL_H

#include <stdio.h>
#include <stdlib.h>

#include "../ports.h"
#include "canvas.h"
#include "font.h"
#include "style.h"

#define PANEL_W 960
#define PANEL_H 470

#define PANEL_NAME     "BROWNSTONE 100"
#define PANEL_SUBTITLE "TWO-CHANNEL 120 WATT VALVE AMPLIFIER - FIVE 12AX7 GAIN STAGES - 6L6 POWER"

/* W_SELECT: a stepped rotary selector whose pointer aims at the chosen name in a list. */
enum { W_KNOB, W_TRIM, W_SWITCH, W_FOOT, W_SELECT };

#define MAX_OPTS 4
#define SEL_LIST_DX 32.0f /* list starts this far right of the selector's centre */
#define SEL_ROW     20.0f /* list row spacing */

typedef struct {
    int kind, port;
    float x, y, r;
    float min, max, def;
    const char* label; /* on the panel */
    const char* name;  /* in the display */
    const char* lo;    /* scale end labels */
    const char* hi;
    const char* opts[MAX_OPTS];
    int nopts;
} Widget;

static const Widget WIDGETS[] = {
    { W_KNOB, P_RHY_PRE, 81, 150, 30, 0, 10, 4, "PRE", "RHYTHM PRE GAIN", "0", "10", {0}, 0 },
    { W_KNOB, P_RHY_POST, 211, 150, 30, 0, 10, 6, "POST", "RHYTHM POST GAIN", "0", "10", {0}, 0 },
    { W_KNOB, P_LEAD_PRE, 351, 150, 30, 0, 10, 6, "PRE", "LEAD PRE GAIN", "0", "10", {0}, 0 },
    { W_KNOB, P_LEAD_POST, 481, 150, 30, 0, 10, 3, "POST", "LEAD POST GAIN", "0", "10", {0}, 0 },
    { W_KNOB, P_LOW, 621, 150, 30, 0, 10, 5, "LOW", "LOW", "0", "10", {0}, 0 },
    { W_KNOB, P_MID, 750, 150, 30, 0, 10, 5, "MID", "MID", "0", "10", {0}, 0 },
    { W_KNOB, P_HIGH, 879, 150, 30, 0, 10, 5, "HIGH", "HIGH", "0", "10", {0}, 0 },
    { W_SWITCH, P_INPUT_LOW, 50, 376, 0, 0, 1, 0, "JACK", "INPUT JACK", 0, 0, { "HIGH", "LOW -6" }, 2 },
    { W_TRIM, P_IN_TRIM, 141, 372, 17, -24, 24, 0, "TRIM", "INPUT TRIM", 0, 0, {0}, 0 },
    { W_SWITCH, P_CHANNEL, 230, 376, 0, 0, 2, 2, "SELECT", "CHANNEL", 0, 0, { "RHYTHM", "CRUNCH", "LEAD" }, 3 },
    { W_FOOT, P_CAB, 398, 386, 25, 0, 1, 1, "CAB SIM", "CABINET SIM", 0, 0, { "OFF", "ON" }, 2 },
    { W_SELECT, P_CAB_MODEL, 482, 372, 19, 0, 3, 0, "MODEL", "CABINET MODEL", 0, 0,
      { "4X12 MODERN", "4X12 VINTAGE", "2X12 OPEN", "1X12 COMBO" }, 4 },
    { W_KNOB, P_PRESENCE, 676, 364, 26, 0, 10, 5, "PRESENCE", "PRESENCE", "0", "10", {0}, 0 },
    { W_KNOB, P_RESONANCE, 768, 364, 26, 0, 10, 5, "RESONANCE", "RESONANCE", "0", "10", {0}, 0 },
    { W_TRIM, P_OUT_LEVEL, 885, 372, 17, -60, 12, 0, "LEVEL", "OUTPUT LEVEL", 0, 0, {0}, 0 },
};
#define N_WIDGETS ((int)(sizeof WIDGETS / sizeof WIDGETS[0]))

#define KNOB_SPAN (0.75f * CV_PI) /* +/-135 degrees */

typedef void (*PanelWrite)(void* host, int port, float value);

typedef struct {
    uint32_t* px; /* what the window shows */
    uint32_t* bg; /* static artwork */
    Canvas c;
    float val[P_COUNT];
    int drag;      /* widget being dragged, or -1 */
    float drag_x, drag_y, drag_norm;
    int shown;     /* widget in the display, or -1 */
    int dirty;
    PanelWrite write;
    void* host;
} Panel;

/* ---- Values ---- */

static float w_norm(const Widget* w, float v)
{
    float n = (v - w->min) / (w->max - w->min);
    return n < 0.0f ? 0.0f : (n > 1.0f ? 1.0f : n);
}

static float w_value(const Widget* w, float n)
{
    n = n < 0.0f ? 0.0f : (n > 1.0f ? 1.0f : n);
    return w->min + n * (w->max - w->min);
}

static int w_index(const Panel* p, const Widget* w)
{
    int i = (int)(p->val[w->port] + 0.5f);
    int n = w->kind == W_FOOT ? 2 : w->nopts;
    return i < 0 ? 0 : (i >= n ? n - 1 : i);
}

/* Selector geometry: list row i and the pointer angle that aims at it. */
static float sel_row_y(const Widget* w, int i)
{
    return w->y + SEL_ROW * (i - 0.5f * (w->nopts - 1));
}

static float sel_angle(const Widget* w, int i)
{
    return atan2f(SEL_LIST_DX - 6.0f, -(sel_row_y(w, i) - w->y));
}

static void w_format(const Panel* p, const Widget* w, char* buf, size_t n)
{
    const float v = p->val[w->port];
    switch (w->kind) {
    case W_SWITCH:
    case W_FOOT:
    case W_SELECT:
        snprintf(buf, n, "%s", w->opts[w_index(p, w)]);
        break;
    default:
        if (w->port == P_IN_TRIM || w->port == P_OUT_LEVEL)
            snprintf(buf, n, "%+.1f DB", v);
        else
            snprintf(buf, n, "%.1f", v);
    }
}

/* ---- Static artwork ---- */

static void draw_screw(Canvas* c, float x, float y)
{
    cv_circle(c, x + 1, y + 1.5f, 6, 0x000000, 0.5f);
    cv_shaded_circle(c, x, y, 6, 0xb8bbc2, 0x4a4d54, -0.4f, -0.5f, 1);
    cv_line(c, x - 4, y + 2, x + 4, y - 2, 1.6f, 0x2a2c30, 0.9f);
}

static void draw_section(Canvas* c, float x0, float y0, float x1, float y1, const char* title)
{
    const float tw = text_width(title, TXT_SECTION, 2.5f);
    const float cx = 0.5f * (x0 + x1);
    cv_rrect(c, x0, y0, x1, y1, 10, 0x000000, 0.18f);
    cv_rrect_line(c, x0, y0, x1, y1, 10, 1.4f, COL_LINE, 0.9f);
    /* Title sits in a gap in the top edge. */
    cv_rrect(c, cx - 0.5f * tw - 8, y0 - 6, cx + 0.5f * tw + 8, y0 + 6, 3,
             mix(COL_PANEL_TOP, COL_PANEL_BOT, y0 / PANEL_H), 1);
    text_center(c, cx, y0 - 4.5f, title, TXT_SECTION, 2.5f, 1.5f, COL_AMBER, 1);
}

static void draw_knob_scale(Canvas* c, const Widget* w)
{
    const int big = w->kind == W_KNOB;
    const int ticks = big ? 11 : 7;
    int i;
    for (i = 0; i < ticks; ++i) {
        const float a = -KNOB_SPAN + 2.0f * KNOB_SPAN * i / (ticks - 1);
        const float r0 = w->r + (big ? 9.0f : 6.0f), r1 = r0 + ((i % 5 == 0 || !big) ? 6.0f : 3.5f);
        cv_line(c, cv_ax(w->x, r0, a), cv_ay(w->y, r0, a), cv_ax(w->x, r1, a), cv_ay(w->y, r1, a),
                1.3f, COL_INK, 0.8f);
    }
    /* Dim track under the value arc. */
    cv_arc(c, w->x, w->y, w->r + 4.5f, 2.2f, -KNOB_SPAN, KNOB_SPAN, 0x000000, 0.55f);
    if (w->lo) {
        const float rr = w->r + 22.0f;
        text_center(c, cv_ax(w->x, rr, -KNOB_SPAN) - 2, cv_ay(w->y, rr, -KNOB_SPAN) - 3, w->lo, TXT_SMALL, 1, 1.1f, COL_DIM, 1);
        text_center(c, cv_ax(w->x, rr, KNOB_SPAN) + 2, cv_ay(w->y, rr, KNOB_SPAN) - 3, w->hi, TXT_SMALL, 1, 1.1f, COL_DIM, 1);
    }
    text_center(c, w->x, w->y + w->r + (big ? 20.0f : 14.0f), w->label, big ? TXT_LABEL : 8.0f, 2.0f,
                big ? 1.7f : 1.4f, COL_INK, 1);
}

static void draw_switch_static(Canvas* c, const Widget* w)
{
    text_center(c, w->x + 14, w->y - 44, w->label, 8, 2.0f, 1.4f, COL_INK, 1);
    /* Mounting plate and nut. */
    cv_rrect(c, w->x - 9, w->y - 25, w->x + 9, w->y + 25, 4, 0x000000, 0.35f);
    cv_rrect_line(c, w->x - 9, w->y - 25, w->x + 9, w->y + 25, 4, 1, 0x71757d, 0.7f);
    cv_circle(c, w->x + 1, w->y + 2, 8.5f, 0x000000, 0.5f);
    cv_shaded_circle(c, w->x, w->y, 8.5f, COL_METAL_LIGHT, COL_METAL_DARK, -0.4f, -0.6f, 1);
    cv_circle(c, w->x, w->y, 5, 0x2d2f33, 1);
}

static void draw_foot_static(Canvas* c, const Widget* w)
{
    cv_circle(c, w->x + 2, w->y + 4, w->r + 9, 0x000000, 0.5f);
    cv_shaded_circle(c, w->x, w->y, w->r + 8, 0xa9adb5, 0x3c3f45, -0.5f, -0.7f, 1);
    cv_ring(c, w->x, w->y, w->r + 8, 1.2f, 0x202226, 0.8f);
    text_center(c, w->x, w->y + w->r + 18, w->label, TXT_LABEL, 2.5f, 1.7f, COL_INK, 1);
    /* LED bezel */
    cv_circle(c, w->x, w->y - w->r - 26, 7.5f, 0x0e0e10, 1);
    cv_ring(c, w->x, w->y - w->r - 26, 7.5f, 1.5f, 0x8a8e96, 0.8f);
}

static void draw_select_static(Canvas* c, const Widget* w)
{
    int i;
    /* Detent marks, one per position, each aiming at its name. */
    for (i = 0; i < w->nopts; ++i) {
        const float a = sel_angle(w, i);
        cv_line(c, cv_ax(w->x, w->r + 5.0f, a), cv_ay(w->y, w->r + 5.0f, a), cv_ax(w->x, w->r + 10.0f, a),
                cv_ay(w->y, w->r + 10.0f, a), 1.3f, COL_INK, 0.8f);
    }
    text_center(c, w->x, w->y + w->r + 14.0f, w->label, 8, 2.0f, 1.4f, COL_INK, 1);
}

static void draw_background(Panel* p)
{
    Canvas c;
    int i, y;
    cv_init(&c, p->bg, PANEL_W, PANEL_H);
    for (y = 0; y < PANEL_H; ++y) {
        uint32_t col = mix(COL_PANEL_TOP, COL_PANEL_BOT, (float)y / PANEL_H);
        int x;
        for (x = 0; x < PANEL_W; ++x)
            p->bg[y * PANEL_W + x] = col;
    }
    cv_texture(&c, PANEL_TEXTURE);
    /* Bevelled edge */
    cv_rrect_line(&c, 1.5f, 1.5f, PANEL_W - 1.5f, PANEL_H - 1.5f, 6, 1.5f, 0x55595f, 0.8f);
    cv_rrect_line(&c, 3.5f, 3.5f, PANEL_W - 3.5f, PANEL_H - 3.5f, 5, 1.5f, 0x000000, 0.4f);

    /* Header */
    text_draw(&c, 30, 18, "DAVIDIAN " PANEL_NAME, TXT_TITLE, 3.2f, 2.8f, COL_INK, 1);
    text_draw(&c, 32, 46, PANEL_SUBTITLE, TXT_SMALL, 2.0f, 1.1f, COL_DIM, 1);
    cv_line(&c, 30, 60, 590, 60, 1.2f, COL_AMBER, 0.55f);
    /* Readout window */
    cv_rrect(&c, 616, 14, 944, 56, 6, 0x000000, 0.6f);
    cv_rrect(&c, 618, 16, 942, 54, 5, 0x0d0a06, 1);
    cv_rrect_line(&c, 616, 14, 944, 56, 6, 1.2f, 0x6d7179, 0.8f);

    draw_section(&c, 16, 76, 276, 246, "RHYTHM");
    draw_section(&c, 286, 76, 546, 246, "LEAD");
    draw_section(&c, 556, 76, 944, 246, "TONE");
    draw_section(&c, 16, 266, 176, 456, "INPUT");
    draw_section(&c, 186, 266, 336, 456, "CHANNEL");
    draw_section(&c, 346, 266, 616, 456, "CABINET");
    draw_section(&c, 626, 266, 816, 456, "POWER AMP");
    draw_section(&c, 826, 266, 944, 456, "OUTPUT");

    for (i = 0; i < N_WIDGETS; ++i) {
        const Widget* w = &WIDGETS[i];
        switch (w->kind) {
        case W_KNOB:
        case W_TRIM: draw_knob_scale(&c, w); break;
        case W_SWITCH: draw_switch_static(&c, w); break;
        case W_FOOT: draw_foot_static(&c, w); break;
        case W_SELECT: draw_select_static(&c, w); break;
        }
    }

    draw_screw(&c, 14, 14);
    draw_screw(&c, PANEL_W - 14, 14);
    draw_screw(&c, 14, PANEL_H - 14);
    draw_screw(&c, PANEL_W - 14, PANEL_H - 14);
}

/* ---- Moving parts ---- */

static void draw_knob(Canvas* c, const Widget* w, float n, int active)
{
    const float r = w->r, x = w->x, y = w->y;
    const float ang = -KNOB_SPAN + 2.0f * KNOB_SPAN * n;
    const int knurls = w->kind == W_KNOB ? 36 : 20;
    int i;
    cv_arc(c, x, y, r + 4.5f, 2.2f, -KNOB_SPAN, ang, active ? 0xffd27f : COL_AMBER, 0.95f);
    cv_circle(c, x + 2, y + 4, r + 1, 0x000000, 0.5f);
    cv_shaded_circle(c, x, y, r, COL_KNOB_LIGHT, COL_KNOB_DARK, -0.45f, -0.65f, 1);
    for (i = 0; i < knurls; ++i) {
        const float a = ang + 2.0f * CV_PI * i / knurls;
        cv_line(c, cv_ax(x, r * 0.84f, a), cv_ay(y, r * 0.84f, a), cv_ax(x, r * 0.98f, a),
                cv_ay(y, r * 0.98f, a), w->kind == W_KNOB ? 1.6f : 1.2f, 0x000000, 0.55f);
    }
    cv_shaded_circle(c, x, y, r * 0.74f, 0x3c3e44, 0x18191c, -0.35f, -0.55f, 1);
    cv_ring(c, x, y, r * 0.74f, 1.0f, 0x6a6d74, 0.5f);
    cv_line(c, cv_ax(x, r * 0.2f, ang), cv_ay(y, r * 0.2f, ang), cv_ax(x, r * 0.93f, ang),
            cv_ay(y, r * 0.93f, ang), w->kind == W_KNOB ? 3.0f : 2.2f, COL_INK, 1);
}

static void draw_switch(Canvas* c, const Widget* w, int idx)
{
    const float x = w->x, y = w->y;
    float ey;
    int i;
    for (i = 0; i < w->nopts; ++i) {
        const float oy = w->nopts == 3 ? y - 20.0f + 20.0f * i : y - 20.0f + 40.0f * i;
        text_draw(c, x + 15, oy - 3.5f, w->opts[i], TXT_SMALL, 1.6f, 1.25f, i == idx ? COL_AMBER : COL_DIM, 1);
    }
    if (w->nopts == 3 && idx == 1) {
        /* Centre: the lever points at the viewer. */
        cv_circle(c, x + 1, y + 2, 5.5f, 0x000000, 0.5f);
        cv_shaded_circle(c, x, y, 5.5f, 0xf2f3f5, 0x8d9097, -0.3f, -0.4f, 1);
        return;
    }
    ey = (idx == 0) ? y - 19.0f : y + 19.0f;
    cv_line(c, x + 1.5f, y + 2, x + 1.5f, ey + 2, 6, 0x000000, 0.45f);
    cv_line(c, x, y, x, ey, 5.5f, 0xb6b9bf, 1);
    cv_line(c, x - 1.2f, y, x - 1.2f, ey, 1.6f, 0xf0f1f3, 0.7f);
    cv_shaded_circle(c, x, ey, 4.8f, 0xf4f5f7, 0x7c7f86, -0.35f, -0.45f, 1);
}

static void draw_select(Canvas* c, const Widget* w, int idx, int active)
{
    const float r = w->r, x = w->x, y = w->y;
    const float ang = sel_angle(w, idx);
    int i;
    for (i = 0; i < w->nopts; ++i)
        text_draw(c, x + SEL_LIST_DX, sel_row_y(w, i) - 3.5f, w->opts[i], TXT_SMALL, 1.6f, 1.25f,
                  i == idx ? COL_AMBER : COL_DIM, 1);
    /* Lit detent mark for the chosen position. */
    cv_line(c, cv_ax(x, r + 5.0f, ang), cv_ay(y, r + 5.0f, ang), cv_ax(x, r + 10.0f, ang),
            cv_ay(y, r + 10.0f, ang), 2.2f, active ? 0xffd27f : COL_AMBER, 1);
    /* Black knurled knob, same finish as the others. */
    cv_circle(c, x + 2, y + 4, r + 1, 0x000000, 0.5f);
    cv_shaded_circle(c, x, y, r, COL_KNOB_LIGHT, COL_KNOB_DARK, -0.45f, -0.65f, 1);
    for (i = 0; i < 20; ++i) {
        const float a = ang + 2.0f * CV_PI * i / 20;
        cv_line(c, cv_ax(x, r * 0.84f, a), cv_ay(y, r * 0.84f, a), cv_ax(x, r * 0.98f, a),
                cv_ay(y, r * 0.98f, a), 1.2f, 0x000000, 0.55f);
    }
    cv_shaded_circle(c, x, y, r * 0.74f, 0x3c3e44, 0x18191c, -0.35f, -0.55f, 1);
    cv_ring(c, x, y, r * 0.74f, 1.0f, 0x6a6d74, 0.5f);
    cv_line(c, cv_ax(x, r * 0.2f, ang), cv_ay(y, r * 0.2f, ang), cv_ax(x, r * 0.93f, ang),
            cv_ay(y, r * 0.93f, ang), 2.2f, COL_INK, 1);
}

static void draw_foot(Canvas* c, const Widget* w, int on)
{
    const float lx = w->x, ly = w->y - w->r - 26;
    cv_shaded_circle(c, w->x, w->y, w->r, 0xe3e5e9, 0x6b6e75, -0.4f, -0.55f, 1);
    cv_ring(c, w->x, w->y, w->r * 0.62f, 1.2f, 0x8e9198, 0.6f);
    cv_ring(c, w->x, w->y, w->r, 1.0f, 0x34363b, 0.8f);
    if (on) {
        cv_glow(c, lx, ly, 5, 16, COL_RED, 0.35f);
        cv_shaded_circle(c, lx, ly, 5.5f, 0xffd0c8, COL_RED, -0.3f, -0.4f, 1);
    } else {
        cv_shaded_circle(c, lx, ly, 5.5f, 0x5a1f1b, 0x240a08, -0.3f, -0.4f, 1);
    }
}

static void draw_display(Panel* p)
{
    char val[64];
    Canvas* c = &p->c;
    if (p->shown < 0) {
        text_draw(c, 632, 22, PANEL_NAME " VALVE HEAD", 8, 2.2f, 1.3f, COL_AMBER_DK, 1);
        text_draw(c, 632, 36, "DRAG KNOBS - DOUBLE-CLICK RESETS", TXT_SMALL, 1.6f, 1.2f, 0x9c6a24, 1);
        return;
    }
    w_format(p, &WIDGETS[p->shown], val, sizeof val);
    text_draw(c, 632, 22, WIDGETS[p->shown].name, 8, 2.2f, 1.3f, 0xc58b33, 1);
    text_draw(c, 632, 35, val, 13, 2.0f, 2.0f, COL_AMBER, 1);
}

static void panel_draw(Panel* p)
{
    int i;
    memcpy(p->px, p->bg, sizeof(uint32_t) * PANEL_W * PANEL_H);
    for (i = 0; i < N_WIDGETS; ++i) {
        const Widget* w = &WIDGETS[i];
        switch (w->kind) {
        case W_KNOB:
        case W_TRIM: draw_knob(&p->c, w, w_norm(w, p->val[w->port]), p->drag == i); break;
        case W_SWITCH: draw_switch(&p->c, w, w_index(p, w)); break;
        case W_FOOT: draw_foot(&p->c, w, w_index(p, w)); break;
        case W_SELECT: draw_select(&p->c, w, w_index(p, w), p->drag == i); break;
        }
    }
    draw_display(p);
    p->dirty = 0;
}

/* ---- Setup and host values ---- */

static int panel_init(Panel* p, PanelWrite write, void* host)
{
    int i;
    memset(p, 0, sizeof *p);
    p->px = (uint32_t*)malloc(sizeof(uint32_t) * PANEL_W * PANEL_H);
    p->bg = (uint32_t*)malloc(sizeof(uint32_t) * PANEL_W * PANEL_H);
    if (!p->px || !p->bg) {
        free(p->px);
        free(p->bg);
        return 0;
    }
    cv_init(&p->c, p->px, PANEL_W, PANEL_H);
    for (i = 0; i < N_WIDGETS; ++i)
        p->val[WIDGETS[i].port] = WIDGETS[i].def;
    p->drag = -1;
    p->shown = -1;
    p->write = write;
    p->host = host;
    draw_background(p);
    panel_draw(p);
    return 1;
}

static void panel_free(Panel* p)
{
    free(p->px);
    free(p->bg);
    p->px = p->bg = NULL;
}

/* A value from the host. */
static void panel_set(Panel* p, int port, float v)
{
    if (port < 0 || port >= P_COUNT || !isfinite(v))
        return;
    if (p->val[port] != v) {
        p->val[port] = v;
        p->dirty = 1;
    }
}

/* ---- Mouse ---- */

static void set_value(Panel* p, int wi, float v)
{
    const Widget* w = &WIDGETS[wi];
    if (w->kind == W_SWITCH || w->kind == W_FOOT || w->kind == W_SELECT)
        v = floorf(v + 0.5f);
    if (v < w->min) v = w->min;
    if (v > w->max) v = w->max;
    p->shown = wi;
    p->dirty = 1;
    if (p->val[w->port] != v) {
        p->val[w->port] = v;
        if (p->write)
            p->write(p->host, w->port, v);
    }
}

static int hit(float x, float y)
{
    int i;
    for (i = 0; i < N_WIDGETS; ++i) {
        const Widget* w = &WIDGETS[i];
        const float dx = x - w->x, dy = y - w->y;
        switch (w->kind) {
        case W_KNOB:
        case W_TRIM:
        case W_FOOT:
            if (dx * dx + dy * dy <= (w->r + 8) * (w->r + 8))
                return i;
            break;
        case W_SWITCH:
            if (dx >= -14 && dx <= 62 && dy >= -30 && dy <= 30)
                return i;
            break;
        case W_SELECT:
            if (dx * dx + dy * dy <= (w->r + 10) * (w->r + 10))
                return i;
            if (dx >= SEL_LIST_DX - 4 && dx <= SEL_LIST_DX + 90 &&
                fabsf(dy) <= SEL_ROW * 0.5f * w->nopts)
                return i;
            break;
        }
    }
    return -1;
}

/* The list row under y, or -1 when y is outside the list. */
static int sel_row_at(const Widget* w, float x, float y)
{
    int i;
    if (x - w->x < SEL_LIST_DX - 4)
        return -1;
    for (i = 0; i < w->nopts; ++i)
        if (fabsf(y - sel_row_y(w, i)) <= 0.5f * SEL_ROW)
            return i;
    return -1;
}

static int switch_pos_at(const Widget* w, float y)
{
    const float dy = y - w->y;
    if (w->nopts == 3)
        return dy < -8.0f ? 0 : (dy > 8.0f ? 2 : 1);
    return dy < 0.0f ? 0 : 1;
}

static void panel_mouse_down(Panel* p, float x, float y, int dbl)
{
    const int i = hit(x, y);
    const Widget* w;
    if (i < 0)
        return;
    w = &WIDGETS[i];
    switch (w->kind) {
    case W_KNOB:
    case W_TRIM:
        if (dbl) {
            set_value(p, i, w->def);
            return;
        }
        p->drag = i;
        p->drag_y = y;
        p->drag_norm = w_norm(w, p->val[w->port]);
        p->shown = i;
        p->dirty = 1;
        break;
    case W_SWITCH:
        set_value(p, i, (float)switch_pos_at(w, y));
        break;
    case W_FOOT:
        set_value(p, i, w_index(p, w) ? 0.0f : 1.0f);
        break;
    case W_SELECT: {
        /* Click a name to pick it; on the knob, drag up/down to step. */
        const int row = sel_row_at(w, x, y);
        if (row >= 0) {
            set_value(p, i, (float)row);
            return;
        }
        if (dbl) {
            set_value(p, i, w->def);
            return;
        }
        p->drag = i;
        p->drag_y = y;
        p->drag_norm = (float)w_index(p, w);
        p->shown = i;
        p->dirty = 1;
        break;
    }
    }
}

static void panel_mouse_move(Panel* p, float x, float y, int fine)
{
    const Widget* w;
    (void)x;
    if (p->drag < 0)
        return;
    w = &WIDGETS[p->drag];
    if (w->kind == W_SELECT) {
        /* One step per 24 px from where the drag started; dragging down moves down the list. */
        set_value(p, p->drag, p->drag_norm + floorf((y - p->drag_y) / 24.0f + 0.5f));
        return;
    }
    {
        const float n = p->drag_norm + (p->drag_y - y) / (fine ? 900.0f : 180.0f);
        /* Fine mode needs a fresh reference so switching modes mid-drag doesn't jump. */
        set_value(p, p->drag, w_value(w, n));
        p->drag_norm = w_norm(w, p->val[w->port]);
        p->drag_y = y;
    }
}

static void panel_mouse_up(Panel* p)
{
    if (p->drag >= 0)
        p->dirty = 1;
    p->drag = -1;
}

static void panel_wheel(Panel* p, float x, float y, float steps, int fine)
{
    const int i = hit(x, y);
    const Widget* w;
    if (i < 0)
        return;
    w = &WIDGETS[i];
    if (w->kind == W_SWITCH || w->kind == W_SELECT) {
        int n = w_index(p, w) - (steps > 0 ? 1 : -1);
        set_value(p, i, (float)(n < 0 ? 0 : (n >= w->nopts ? w->nopts - 1 : n)));
    } else if (w->kind != W_FOOT) {
        set_value(p, i, w_value(w, w_norm(w, p->val[w->port]) + steps * (fine ? 0.004f : 0.025f)));
    }
}

#endif
