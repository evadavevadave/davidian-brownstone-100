/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian */
/* Port indices and URIs shared by the DSP plugin and the GUI.
 * Must match brownstone100.lv2/brownstone100.ttl. */
#ifndef BROWNSTONE_PORTS_H
#define BROWNSTONE_PORTS_H

#define BROWNSTONE_URI    "urn:davidian:brownstone-100"
#define BROWNSTONE_UI_URI "urn:davidian:brownstone-100#ui"

typedef enum {
    P_IN = 0,
    P_OUT,
    P_CHANNEL,      /* 0 rhythm clean, 1 rhythm crunch, 2 lead */
    P_INPUT_LOW,    /* J39 low-gain input */
    P_RHY_PRE,
    P_RHY_POST,
    P_LEAD_PRE,
    P_LEAD_POST,
    P_LOW,
    P_MID,
    P_HIGH,
    P_PRESENCE,
    P_RESONANCE,
    P_CAB,
    P_IN_TRIM,
    P_OUT_LEVEL,
    P_COUNT
} PortIndex;

#endif
