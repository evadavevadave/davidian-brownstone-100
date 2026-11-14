/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian */
/* Plugin description for the DPF build (VST3 and Audio Unit). The LV2 version is built
 * separately by the Makefile / build.bat with its own hand-written metadata. */
#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

#define DISTRHO_PLUGIN_BRAND   "Davidian"
#define DISTRHO_PLUGIN_NAME    "Brownstone 100"
#define DISTRHO_PLUGIN_URI     "urn:davidian:brownstone-100"
#define DISTRHO_PLUGIN_CLAP_ID "com.davidian.brownstone-100"

/* Four-character codes: AU manufacturer and subtype (also part of the VST3 class ID). */
#define DISTRHO_PLUGIN_BRAND_ID  Dvdn
#define DISTRHO_PLUGIN_UNIQUE_ID Bs1h

#define DISTRHO_PLUGIN_HAS_UI      1
#define DISTRHO_PLUGIN_IS_RT_SAFE  1
#define DISTRHO_PLUGIN_NUM_INPUTS  1
#define DISTRHO_PLUGIN_NUM_OUTPUTS 1

#define DISTRHO_PLUGIN_VST3_CATEGORIES "Fx|Distortion|Mono"
#define DISTRHO_PLUGIN_CLAP_FEATURES   "audio-effect", "distortion", "mono"

/* The house-style panel is drawn by its own software renderer and shown as an OpenGL
 * texture, at a fixed 960 x 470 (scaled by the host's UI scale factor). */
#define DISTRHO_UI_USE_NANOVG      0
#define DISTRHO_UI_USER_RESIZABLE  0
#define DISTRHO_UI_DEFAULT_WIDTH   960
#define DISTRHO_UI_DEFAULT_HEIGHT  470

#endif
