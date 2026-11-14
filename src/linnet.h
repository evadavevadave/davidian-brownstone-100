/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian */
/* A tiny linear circuit simulator for the amp's passive networks (channel networks, tone
 * stack, feedback network). It uses modified nodal analysis with trapezoidal capacitor
 * companions, which is the bilinear transform done node by node: stable at any sample
 * rate, and exact for the component values, with no curve fitting.
 *
 * Use: add resistors, capacitors and one Norton source, call linnet_prepare() whenever a
 * value changes (it inverts the conductance matrix), then linnet_step() once per sample.
 * Node 0 is ground; other nodes are numbered from 1. */
#ifndef BROWNSTONE_LINNET_H
#define BROWNSTONE_LINNET_H

#include <string.h>

#define LN_MAX_NODES 10
#define LN_MAX_R     16
#define LN_MAX_C     8

typedef struct {
    int n1, n2;
    double v;   /* resistance (ohms) or capacitance (farads) */
} LnElem;

typedef struct {
    int    nodes;               /* not counting ground */
    int    nr, nc;
    LnElem r[LN_MAX_R];
    LnElem c[LN_MAX_C];
    int    src_node;            /* node the source drives through src_r */
    double src_r;
    double fs;
    /* prepared */
    double inv[LN_MAX_NODES][LN_MAX_NODES];
    double gc[LN_MAX_C];        /* companion conductances 2C/T */
    /* state */
    double ieq[LN_MAX_C];       /* companion history currents */
    double v[LN_MAX_NODES + 1]; /* node voltages, v[0] = ground */
} LinNet;

/* Starts a (re)description of the network. The simulation state (capacitor history and
 * node voltages) is kept, so a network rebuilt with the same elements in the same order
 * (a pot moving) continues smoothly; call linnet_reset() when the circuit itself changes. */
static void linnet_init(LinNet* n, int nodes, double fs)
{
    n->nodes = nodes;
    n->fs = fs;
    n->nr = n->nc = 0;
    n->src_node = 0;
    n->src_r = 1.0;
}

static int linnet_r(LinNet* n, int a, int b, double ohms)
{
    n->r[n->nr].n1 = a; n->r[n->nr].n2 = b; n->r[n->nr].v = ohms;
    return n->nr++;
}

static int linnet_c(LinNet* n, int a, int b, double farads)
{
    n->c[n->nc].n1 = a; n->c[n->nc].n2 = b; n->c[n->nc].v = farads;
    return n->nc++;
}

static void linnet_source(LinNet* n, int node, double ohms)
{
    n->src_node = node;
    n->src_r = ohms;
}

static void ln_stamp(double m[LN_MAX_NODES][LN_MAX_NODES], int a, int b, double g)
{
    if (a) m[a - 1][a - 1] += g;
    if (b) m[b - 1][b - 1] += g;
    if (a && b) { m[a - 1][b - 1] -= g; m[b - 1][a - 1] -= g; }
}

/* Builds and inverts the matrix. Keeps the capacitor history, so values can change
 * smoothly while running. Returns 0 if the matrix is singular. */
static int linnet_prepare(LinNet* n)
{
    double a[LN_MAX_NODES][LN_MAX_NODES];
    const int N = n->nodes;
    int i, j, k;
    memset(a, 0, sizeof a);
    for (i = 0; i < n->nr; ++i) {
        double ohms = n->r[i].v > 1e-3 ? n->r[i].v : 1e-3;
        ln_stamp(a, n->r[i].n1, n->r[i].n2, 1.0 / ohms);
    }
    for (i = 0; i < n->nc; ++i) {
        n->gc[i] = 2.0 * n->c[i].v * n->fs;
        ln_stamp(a, n->c[i].n1, n->c[i].n2, n->gc[i]);
    }
    if (n->src_node)
        a[n->src_node - 1][n->src_node - 1] += 1.0 / n->src_r;
    /* Gauss-Jordan inversion with partial pivoting */
    for (i = 0; i < N; ++i)
        for (j = 0; j < N; ++j)
            n->inv[i][j] = (i == j);
    for (k = 0; k < N; ++k) {
        int p = k;
        double best = fabs(a[k][k]), f;
        for (i = k + 1; i < N; ++i)
            if (fabs(a[i][k]) > best) { best = fabs(a[i][k]); p = i; }
        if (best < 1e-30)
            return 0;
        if (p != k)
            for (j = 0; j < N; ++j) {
                double t = a[k][j]; a[k][j] = a[p][j]; a[p][j] = t;
                t = n->inv[k][j]; n->inv[k][j] = n->inv[p][j]; n->inv[p][j] = t;
            }
        f = 1.0 / a[k][k];
        for (j = 0; j < N; ++j) { a[k][j] *= f; n->inv[k][j] *= f; }
        for (i = 0; i < N; ++i) {
            if (i == k || a[i][k] == 0.0) continue;
            f = a[i][k];
            for (j = 0; j < N; ++j) { a[i][j] -= f * a[k][j]; n->inv[i][j] -= f * n->inv[k][j]; }
        }
    }
    return 1;
}

static void linnet_reset(LinNet* n)
{
    memset(n->ieq, 0, sizeof n->ieq);
    memset(n->v, 0, sizeof n->v);
}

/* History-only right-hand side (capacitor companions). */
static void ln_rhs_hist(const LinNet* n, double* rhs)
{
    int i;
    for (i = 0; i < n->nodes; ++i) rhs[i] = 0.0;
    for (i = 0; i < n->nc; ++i) {
        const int a = n->c[i].n1, b = n->c[i].n2;
        if (a) rhs[a - 1] += n->ieq[i];
        if (b) rhs[b - 1] -= n->ieq[i];
    }
}

/* The output at node `out` is k * vin + c0 for the coming sample: lets a nonlinear
 * loop solve for vin before the network is stepped. */
static void linnet_affine(const LinNet* n, int out, double* k, double* c0)
{
    double rhs[LN_MAX_NODES];
    double s = 0.0;
    int j;
    ln_rhs_hist(n, rhs);
    for (j = 0; j < n->nodes; ++j) s += n->inv[out - 1][j] * rhs[j];
    *c0 = s;
    *k = n->src_node ? n->inv[out - 1][n->src_node - 1] / n->src_r : 0.0;
}

/* Advances one sample with source voltage vin; returns the voltage at node `out`. */
static double linnet_step(LinNet* n, double vin, int out)
{
    double rhs[LN_MAX_NODES];
    int i, j;
    ln_rhs_hist(n, rhs);
    if (n->src_node) rhs[n->src_node - 1] += vin / n->src_r;
    for (i = 0; i < n->nodes; ++i) {
        double s = 0.0;
        for (j = 0; j < n->nodes; ++j) s += n->inv[i][j] * rhs[j];
        n->v[i + 1] = s;
    }
    /* trapezoidal companion update: i = gc v - ieq, next ieq = gc v + i = 2 gc v - ieq */
    for (i = 0; i < n->nc; ++i) {
        const double vb = n->v[n->c[i].n1] - n->v[n->c[i].n2];
        n->ieq[i] = 2.0 * n->gc[i] * vb - n->ieq[i];
    }
    return n->v[out];
}

#endif
