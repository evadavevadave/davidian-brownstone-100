/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian */
/*
 * Davidian Brownstone 100 — LV2 amplifier simulation modeled on the circuit of a classic high-gain valve head.
 *
 * Signal chain (component designators refer to the original circuit):
 *
 *   Input (J37 high / J39 low) -> V1A (12AX7, R2 220k, R12 1.82k || C3 1uF)
 *   -> C6 .022 -> channel network (simulated as a circuit, linnet.h):
 *        Lead   : R29 470k || C17 2.2n into R41 1M and VR2 Ultra Pre, with R40 1M and
 *                 C19 1n across the pot's upper half
 *        Rhythm : R19 470k || C51 270p, R21 1M, K4B shunt R23 39k + C63 .1u, VR1 Clean Pre
 *                 with C27 180p bright; S1C Crunch shorts R75 so C10 470p bypasses R21
 *   -> R82 470k grid stopper -> V1B (R3 100k, R17 1.82k || C4 1uF, + C26 22uF in Rhythm)
 *   -> C2 .022 -> R6 470k / R9 1M (+ R20 220k shunt in Rhythm via K1B)
 *   -> V2A (R1 100k, R15 39k unbypassed "cold clipper")
 *   -> C1 .022, R11 330k || C48 1nF, R7 220k -> V2B (R4 220k, R13 1.82k || C5 1uF)
 *   -> R32 1M, C57 .022, R101 100k -> V5B (R96 220k, R102 2.2k unbypassed)
 *   -> R87 1M, C56 .022 -> V5A (R86 100k, R93 2.2k) with R88 1M plate-to-grid shunt
 *      feedback and R91 1M to ground: a near-unity inverting stage
 *   -> C58 .022, R89 470k -> tone stack input loaded by R24 33k and C59 .01 + R94 47k
 *        (R28 47k slope, C14 470p || C60 100p, C18/C21 .022, VR5 250k high, VR3 1M low,
 *         VR4 50k mid; output loaded by both Post pots)
 *   -> VR7 Ultra Post / VR6 Clean Post -> V3B cathode follower -> loop (C11)
 *   -> V3A recovery (R5 68k, R14 3.3k || C61 4.7n)
 *   -> V4 long-tail phase inverter (tail R52 470 + R51 10k + R57 4.7k) -> 4x 6L6GC -> T2
 *   -> negative feedback: R56 39k into the tail at R57 via C30, with Presence (C8 .1u,
 *      C62 .033u, VR9 10k) and Resonance (C12 6.8n || VR8 1M).
 *
 * Triodes use the Koren 12AX7 model, solved against each stage's AC load line
 * into lookup tables at instantiate time. Passive networks are simulated by modified
 * nodal analysis with trapezoidal capacitors. Nonlinear processing runs at 4x
 * oversampling. V5A's shunt feedback and the power-amp feedback loop are solved
 * implicitly (Newton) every sample.
 *
 * Published methods used (equations re-implemented from the papers; no code copied):
 *   [1] N. Koren, "Improved vacuum tube models for SPICE simulations", Glass Audio 8(5), 1996.
 *       Triode equation and 12AX7 parameters (koren_12ax7).
 *   [2] C.-W. Ho, A. E. Ruehli and P. A. Brennan, "The modified nodal approach to network
 *       analysis", IEEE Trans. Circuits and Systems 22(6), 1975. Nodal analysis of the
 *       passive networks (linnet.h).
 *   [3] R. Bristow-Johnson, "Cookbook formulae for audio EQ biquad filter coefficients"
 *       (Audio EQ Cookbook). Low-pass, high-pass and peaking biquads (bq_*).
 *   [4] V. Zavalishin, "The Art of VA Filter Design", Native Instruments, 2012-2020.
 *       Topology-preserving (trapezoidal) one-pole filters (OnePole) and the instantaneous
 *       response used to solve the feedback loops (power_run, linnet_affine).
 * See README.md, "References".
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "lv2_core.h"

#if defined(_M_X64) || defined(__x86_64__) || defined(__SSE__)
#  include <xmmintrin.h>
#  define BS_HAVE_SSE 1
#endif

#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif

#include "ports.h"

#define OS        4      /* oversampling factor */
#define TABLE_N   4096   /* triode transfer table size */
#define AA_STAGES 6      /* biquads in each anti-alias filter (12th order) */

/* ------------------------------------------------------------------ ports */


/* --------------------------------------------------------------- helpers */

static double clampd(double x, double lo, double hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}

/* Audio ("A") taper pot: 10% resistance at mid travel. */
static double taper_audio(double p)
{
    p = clampd(p, 0.0, 1.0);
    return (pow(81.0, p) - 1.0) / 80.0;
}

/* Pre-gain pots (VR1/VR2, marked "A3"): steeper audio taper, ~3% at mid travel. */
static double taper_pre(double p)
{
    p = clampd(p, 0.0, 1.0);
    return (pow(1045.0, p) - 1.0) / 1044.0;
}

/* ---------------------------------------------------- TPT one-pole filter */
/* Zavalishin's topology-preserving transform one-pole [4]. */

typedef struct {
    double G; /* g / (1 + g) */
    double s;
} OnePole;

static void op_set(OnePole* f, double fc, double fs)
{
    double g = tan(M_PI * clampd(fc, 1.0, 0.45 * fs) / fs);
    f->G = g / (1.0 + g);
}

static inline double op_lp(OnePole* f, double x)
{
    double v = (x - f->s) * f->G;
    double y = v + f->s;
    f->s     = y + v;
    return y;
}

static inline double op_hp(OnePole* f, double x)
{
    return x - op_lp(f, x);
}

/* Low shelf: gain g at DC, unity at HF, pole (LP cutoff) set in f. */
static inline double shelf_low(OnePole* f, double g, double x)
{
    return x + (g - 1.0) * op_lp(f, x);
}

/* ------------------------------------------------------------ biquad DF2T */
/* Coefficients from the Audio EQ Cookbook [3]. */

typedef struct {
    double b0, b1, b2, a1, a2;
    double z1, z2;
} Biquad;

static inline double bq_run(Biquad* q, double x)
{
    double y = q->b0 * x + q->z1;
    q->z1    = q->b1 * x - q->a1 * y + q->z2;
    q->z2    = q->b2 * x - q->a2 * y;
    return y;
}

static void bq_lowpass(Biquad* q, double fc, double Q, double fs)
{
    double w = 2.0 * M_PI * fc / fs, c = cos(w), a = sin(w) / (2.0 * Q);
    double a0 = 1.0 + a;
    q->b0 = (1.0 - c) / 2.0 / a0;
    q->b1 = (1.0 - c) / a0;
    q->b2 = q->b0;
    q->a1 = -2.0 * c / a0;
    q->a2 = (1.0 - a) / a0;
}

static void bq_highpass(Biquad* q, double fc, double Q, double fs)
{
    double w = 2.0 * M_PI * fc / fs, c = cos(w), a = sin(w) / (2.0 * Q);
    double a0 = 1.0 + a;
    q->b0 = (1.0 + c) / 2.0 / a0;
    q->b1 = -(1.0 + c) / a0;
    q->b2 = q->b0;
    q->a1 = -2.0 * c / a0;
    q->a2 = (1.0 - a) / a0;
}

static void bq_peak(Biquad* q, double fc, double Q, double gain_db, double fs)
{
    double A = pow(10.0, gain_db / 40.0);
    double w = 2.0 * M_PI * fc / fs, c = cos(w), a = sin(w) / (2.0 * Q);
    double a0 = 1.0 + a / A;
    q->b0 = (1.0 + a * A) / a0;
    q->b1 = -2.0 * c / a0;
    q->b2 = (1.0 - a * A) / a0;
    q->a1 = -2.0 * c / a0;
    q->a2 = (1.0 - a / A) / a0;
}

#include "cabinet.h"

/* ---------------------------------------------------- 12AX7 (Koren model) */
/* Koren's triode equation and published 12AX7 parameters [1]. */

static double koren_12ax7(double vpk, double vgk)
{
    const double mu = 100.0, ex = 1.4, kg1 = 1060.0, kp = 600.0, kvb = 300.0;
    if (vpk <= 0.0) return 0.0;
    double a  = kp * (1.0 / mu + vgk / sqrt(kvb + vpk * vpk));
    double sp = a > 30.0 ? a : log1p(exp(a));
    double e1 = vpk / kp * sp;
    return e1 > 0.0 ? 2.0 * pow(e1, ex) / kg1 : 0.0;
}

typedef struct {
    double B;     /* supply */
    double Ra;    /* DC plate resistor */
    double Rac;   /* AC load: Ra || next stage */
    double Rk;    /* cathode resistor */
    double ip0;   /* quiescent current */
    double vp0;   /* quiescent plate voltage (to ground) */
    double vk0;   /* quiescent cathode voltage */
} StageDC;

/* Plate current for grid voltage vin (to ground), on the AC load line.
 * rk_dyn: cathode resistance seen by the signal (0 when bypassed). */
static double solve_ip(const StageDC* d, double vin, double rk_dyn)
{
    double lo = 0.0, hi = d->ip0 + d->vp0 / d->Rac + 0.01;
    for (int i = 0; i < 64; ++i) {
        double ip = 0.5 * (lo + hi);
        double vk = rk_dyn > 0.0 ? ip * rk_dyn : d->vk0;
        double vp = d->vp0 - (ip - d->ip0) * d->Rac;
        double f  = ip - koren_12ax7(vp - vk, vin - vk);
        if (f > 0.0) hi = ip; else lo = ip;
    }
    return 0.5 * (lo + hi);
}

static void solve_dc(StageDC* d)
{
    double lo = 0.0, hi = d->B / (d->Ra + d->Rk);
    for (int i = 0; i < 64; ++i) {
        double ip = 0.5 * (lo + hi);
        double vk = ip * d->Rk;
        double f  = ip - koren_12ax7(d->B - ip * d->Ra - vk, -vk);
        if (f > 0.0) hi = ip; else lo = ip;
    }
    d->ip0 = 0.5 * (lo + hi);
    d->vk0 = d->ip0 * d->Rk;
    d->vp0 = d->B - d->ip0 * d->Ra;
}

/* A common-cathode triode gain stage. */
typedef struct {
    float   table[TABLE_N]; /* AC plate voltage (inverting) vs grid voltage */
    double  lo, hi, inv_step;
    double  grid_thr;       /* grid voltage where grid conduction starts */
    double  grid_knee;      /* softness of grid-conduction limiting */
    int     bypassed;
    double  cath_g;         /* low-frequency gain ratio for a bypassed cathode */
    double  Rk;
    OnePole cath;           /* cathode bypass shelf */
    OnePole miller;         /* grid-stopper / Miller-capacitance low-pass */
    int     use_miller;
} Triode;

static void triode_build(Triode* t, double B, double Ra, double Rnext, double Rk,
                         int bypassed, double knee)
{
    StageDC d;
    d.B   = B;
    d.Ra  = Ra;
    d.Rk  = Rk;
    d.Rac = Ra * Rnext / (Ra + Rnext);
    solve_dc(&d);

    double rk_dyn = bypassed ? 0.0 : Rk;
    t->bypassed   = bypassed;
    t->Rk         = Rk;
    t->grid_knee  = knee;

    /* Grid-conduction threshold: vin where Vgk reaches 0. */
    if (bypassed) {
        t->grid_thr = d.vk0;
    } else {
        double lo = 0.0, hi = 400.0;
        for (int i = 0; i < 60; ++i) {
            double v = 0.5 * (lo + hi);
            if (v - solve_ip(&d, v, Rk) * Rk > 0.0) hi = v; else lo = v;
        }
        t->grid_thr = 0.5 * (lo + hi);
    }

    t->lo       = fmin(t->grid_thr, 0.0) - 60.0;
    t->hi       = t->grid_thr + 4.0 * knee + 0.5;
    t->inv_step = (TABLE_N - 1) / (t->hi - t->lo);

    for (int i = 0; i < TABLE_N; ++i) {
        double vin  = t->lo + i / t->inv_step;
        double ip   = solve_ip(&d, vin, rk_dyn);
        double vp   = d.vp0 - (ip - d.ip0) * d.Rac;
        t->table[i] = (float)(vp - d.vp0);
    }
    /* Remove the offset at the quiescent input (vin = 0). */
    {
        double off = d.vp0 - (solve_ip(&d, 0.0, rk_dyn) - d.ip0) * d.Rac - d.vp0;
        for (int i = 0; i < TABLE_N; ++i) t->table[i] -= (float)off;
    }

    /* Small-signal gain ratio unbypassed / bypassed, for the cathode shelf. */
    t->cath_g = 1.0;
    if (bypassed) {
        const double dv = 0.01;
        double gb = (solve_ip(&d, dv, 0.0) - solve_ip(&d, -dv, 0.0));
        double gu = (solve_ip(&d, dv, Rk) - solve_ip(&d, -dv, Rk));
        if (gb > 0.0) t->cath_g = gu / gb;
    }
    t->cath.s     = 0.0;
    t->miller.s   = 0.0;
    t->use_miller = 0;
}

static void triode_set_cathode_cap(Triode* t, double Ck, double fs)
{
    if (!t->bypassed) return;
    /* zero at 1/(2 pi Rk Ck), pole at zero / g */
    double fz = 1.0 / (2.0 * M_PI * t->Rk * Ck);
    op_set(&t->cath, fz / t->cath_g, fs);
}

static void triode_set_miller(Triode* t, double fc, double fs)
{
    t->use_miller = fc > 0.0;
    if (t->use_miller) op_set(&t->miller, fc, fs);
}

static inline double triode_run(Triode* t, double x)
{
    if (t->use_miller) x = op_lp(&t->miller, x);
    if (t->bypassed) x = shelf_low(&t->cath, t->cath_g, x);

    /* grid conduction: soft limit above the threshold */
    if (x > t->grid_thr) {
        double k = t->grid_knee;
        x = t->grid_thr + k * tanh((x - t->grid_thr) / k);
    }

    double pos = (x - t->lo) * t->inv_step;
    if (pos <= 0.0) return t->table[0];
    if (pos >= TABLE_N - 1) return t->table[TABLE_N - 1];
    int    i  = (int)pos;
    double fr = pos - i;
    return t->table[i] + fr * (t->table[i + 1] - t->table[i]);
}


/* Static transfer of a stage (grid limiting + table), with no filters: for solvers. */
static inline double triode_static(const Triode* t, double x)
{
    if (x > t->grid_thr) {
        double k = t->grid_knee;
        x = t->grid_thr + k * tanh((x - t->grid_thr) / k);
    }
    double pos = (x - t->lo) * t->inv_step;
    if (pos <= 0.0) return t->table[0];
    if (pos >= TABLE_N - 1) return t->table[TABLE_N - 1];
    int    i  = (int)pos;
    double fr = pos - i;
    return t->table[i] + fr * (t->table[i + 1] - t->table[i]);
}

/* ------------------------------------------- V5A: shunt-feedback gain stage */
/* R87 1M feeds the grid, R91 1M goes to ground and R88 1M feeds the plate signal
 * (after C58) back to the grid. The grid voltage solves
 *     vg = a vin + b T(vg),  a = G87 / G,  b = G88 / G,  G = G87 + G88 + G91,
 * where T is the stage's (inverting) plate transfer. T is decreasing, so the residual is
 * strictly increasing and Newton converges from any start. */
typedef struct {
    Triode t;
    double a, b;
    double vg;
} ShuntStage;

static void shunt_setup(ShuntStage* s, double r_in, double r_fb, double r_gnd)
{
    double g_in = 1.0 / r_in, g_fb = 1.0 / r_fb, g = g_in + g_fb + 1.0 / r_gnd;
    s->a = g_in / g;
    s->b = g_fb / g;
    s->vg = 0.0;
}

/* Safeguarded Newton: Newton steps inside a bracket that always holds the root, with a
 * bisection whenever a step would leave it. Plain Newton can cycle on the clipped,
 * piecewise-linear transfer, which shows up as spurious low tones under heavy drive. */
static inline double shunt_run(ShuntStage* s, double vin)
{
    const double h = 0.01;
    /* |T| is bounded by the table, so the root lies within this bracket. */
    const double tmax = fmax(fabs(s->t.table[0]), fabs(s->t.table[TABLE_N - 1]));
    double lo = s->a * vin - s->b * tmax - 1.0, hi = s->a * vin + s->b * tmax + 1.0;
    double vg = clampd(s->vg, lo, hi);
    for (int it = 0; it < 40; ++it) {
        double f = vg - s->b * triode_static(&s->t, vg) - s->a * vin;
        if (f > 0.0) hi = vg; else lo = vg;
        if (fabs(f) < 1e-7 || hi - lo < 1e-7) break;
        double dt = (triode_static(&s->t, vg + h) - triode_static(&s->t, vg - h)) / (2.0 * h);
        double next = vg - f / (1.0 - s->b * dt);
        vg = (next > lo && next < hi) ? next : 0.5 * (lo + hi);
    }
    s->vg = vg;
    return triode_static(&s->t, vg);
}

/* ------------------------------------------------------- passive networks */
/* Every passive network between the stages is simulated as a circuit (linnet.h) with
 * the original component values, so pots, loading and source impedances interact as
 * they do in the amp. */

#include "linnet.h"

/* Pot values from the original circuit */
#define VR_PRE     1e6    /* VR1 Clean Pre, VR2 Ultra Pre */
#define VR_POST    1e6    /* VR6 Clean Post, VR7 Ultra Post */
#define VR_HIGH    250e3  /* VR5 */
#define VR_LOW     1e6    /* VR3 */
#define VR_MID     50e3   /* VR4 */
#define VR_PRES    10e3   /* VR9 */
#define VR_RES     1e6    /* VR8 */
#define POT_MIN    50.0   /* end resistance of a pot track */

static double pot_part(double frac, double total)
{
    return fmax(clampd(frac, 0.0, 1.0) * total, POT_MIN);
}

/* Input network after V1A: C6, then the Rhythm (K4A rest position) or Lead network.
 * Node numbers for the front network. */
enum { FN_P = 1, FN_S, FN_L2, FN_WL, FN_RR, FN_M, FN_D, FN_Q, FN_WR, FN_NODES = FN_WR };

/* Source impedance of V1A's plate: R2 220k || ra. */
#define V1A_ROUT 48e3

static void front_build(LinNet* n, int channel, double pre, double fs)
{
    const int lead = channel == 2;
    linnet_init(n, FN_NODES, fs);
    linnet_source(n, FN_P, V1A_ROUT);
    linnet_c(n, FN_P, FN_S, 22e-9);                      /* C6 */
    /* Lead network (always hangs on C6; in Rhythm it is only a load) */
    linnet_r(n, FN_S, FN_L2, 470e3);                     /* R29 */
    linnet_c(n, FN_S, FN_L2, 2.2e-9);                    /* C17 */
    linnet_r(n, FN_L2, 0, 1e6);                          /* R41 */
    linnet_r(n, FN_L2, FN_WL, pot_part(1.0 - (lead ? pre : 0.5), VR_PRE)); /* VR2 upper */
    linnet_r(n, FN_WL, 0, pot_part(lead ? pre : 0.5, VR_PRE));             /* VR2 lower */
    linnet_r(n, FN_L2, FN_WL, 1e6);                      /* R40 */
    linnet_c(n, FN_L2, FN_WL, 1e-9);                     /* C19 */
    if (lead) {
        /* K4A energized: the rhythm network is disconnected. Keep its nodes grounded
         * through large resistors so the matrix stays regular. */
        linnet_r(n, FN_RR, 0, 1e9); linnet_r(n, FN_M, 0, 1e9); linnet_r(n, FN_D, 0, 1e9);
        linnet_r(n, FN_Q, 0, 1e9); linnet_r(n, FN_WR, 0, 1e9);
        return;
    }
    /* Rhythm network (K4A and K4B at rest) */
    linnet_r(n, FN_S, FN_RR, 470e3);                     /* R19 */
    linnet_c(n, FN_S, FN_RR, 270e-12);                   /* C51 */
    linnet_r(n, FN_RR, FN_D, 1e6);                       /* R21 */
    linnet_c(n, FN_RR, FN_M, 470e-12);                   /* C10 */
    linnet_r(n, FN_M, FN_D, channel == 1 ? 1.0 : 2.2e6); /* R75, shorted by S1C Crunch */
    linnet_r(n, FN_D, FN_Q, 39e3);                       /* R23 via K4B */
    linnet_c(n, FN_Q, 0, 0.1e-6);                        /* C63 */
    linnet_r(n, FN_D, FN_WR, pot_part(1.0 - pre, VR_PRE)); /* VR1 upper */
    linnet_r(n, FN_WR, 0, pot_part(pre, VR_PRE));          /* VR1 lower */
    linnet_c(n, FN_D, FN_WR, 180e-12);                   /* C27 bright */
}

/* Tone stack, driven from V5A through C58 and R89 470k, loaded by R24 33k, C59 + R94 and
 * the two Post pots. */
enum { TN_P = 1, TN_N, TN_A, TN_B, TN_T, TN_W, TN_X, TN_Y, TN_Z, TN_NODES = TN_Z };

/* V5A drives the stack from a low impedance: its shunt feedback lowers its output
 * impedance well below R86. */
#define V5A_ROUT 8e3

static void stack_build(LinNet* n, double treble, double mid, double bass, double fs)
{
    linnet_init(n, TN_NODES, fs);
    linnet_source(n, TN_P, V5A_ROUT);
    linnet_c(n, TN_P, TN_N, 22e-9);                      /* C58 */
    linnet_r(n, TN_N, 0, 1e6);                           /* R88 into the near-virtual-ground grid */
    linnet_r(n, TN_N, TN_A, 470e3);                      /* R89 */
    linnet_r(n, TN_A, 0, 33e3);                          /* R24 */
    linnet_c(n, TN_A, TN_B, 10e-9);                      /* C59 */
    linnet_r(n, TN_B, 0, 47e3);                          /* R94 */
    linnet_c(n, TN_A, TN_T, 570e-12);                    /* C14 470p || C60 100p */
    linnet_r(n, TN_T, TN_W, pot_part(1.0 - treble, VR_HIGH)); /* VR5 upper */
    linnet_r(n, TN_W, TN_X, pot_part(treble, VR_HIGH));       /* VR5 lower */
    linnet_r(n, TN_W, 0, VR_POST * 0.5);                 /* VR6 || VR7 */
    linnet_r(n, TN_A, TN_Y, 47e3);                       /* R28 slope */
    linnet_c(n, TN_Y, TN_X, 22e-9);                      /* C18 */
    linnet_c(n, TN_Y, TN_Z, 22e-9);                      /* C21 */
    linnet_r(n, TN_X, TN_Z, pot_part(taper_audio(bass), VR_LOW)); /* VR3 */
    linnet_r(n, TN_Z, 0, pot_part(mid, VR_MID));                  /* VR4 */
}

/* Power-amp negative feedback: speaker -> R56 39k -> presence and resonance network ->
 * C30 -> the phase inverter's tail (R57 4.7k, in parallel with R51 and the cathodes). */
enum { NF_N = 1, NF_L, NF_M, NF_Q, NF_WP, NF_NODES = NF_WP };

#define SPK_V     40.0   /* speaker volts per unit of normalized output (120 W into 8 ohms) */
#define PI_TAIL   3.3e3  /* R57 4.7k || (R51 10k + R52 470 + cathodes) */

static void nfb_build(LinNet* n, double presence, double resonance, double fs)
{
    linnet_init(n, NF_NODES, fs);
    linnet_source(n, NF_N, 39e3);                        /* R56 */
    linnet_c(n, NF_N, NF_L, 6.8e-9);                     /* C12 */
    linnet_r(n, NF_N, NF_L, pot_part(taper_audio(resonance), VR_RES)); /* VR8 */
    linnet_c(n, NF_L, NF_M, 22e-6);                      /* C30 */
    linnet_r(n, NF_M, 0, PI_TAIL);
    linnet_c(n, NF_N, NF_Q, 0.1e-6);                     /* C8 */
    linnet_r(n, NF_Q, NF_WP, pot_part(presence, VR_PRES));       /* VR9, top to wiper */
    linnet_r(n, NF_WP, 0, pot_part(1.0 - presence, VR_PRES));    /* VR9, wiper to ground */
    linnet_c(n, NF_N, NF_WP, 33e-9);                     /* C62 */
}

/* ------------------------------------------------------------ power amp */

#define PI_GAIN   25.0   /* long-tail pair gain, grid to each plate      */
#define PI_SWING  70.0   /* PI output swing limit (V)                    */
#define PWR_BIAS  45.0   /* 6L6GC grid bias (V)                          */

/* PI + push-pull 6L6GC class AB transfer, normalized output. */
static inline double power_h(double e)
{
    double pi = PI_SWING * tanh(PI_GAIN * e / PI_SWING);
    double w  = pi / PWR_BIAS;
    w = 1.3 * tanh(w / 1.3);                 /* power-tube grid conduction */
    double za = 1.0 + w, zb = 1.0 - w;       /* each side, cut off below 0 */
    double ia = za > 0.0 ? za * sqrt(za) : 0.0;
    double ib = zb > 0.0 ? zb * sqrt(zb) : 0.0;
    return tanh((ia - ib) / 3.0);            /* plate-swing saturation */
}

typedef struct {
    OnePole ot_hp, ot_lp;   /* output transformer bandwidth */
    LinNet  nfb;            /* feedback network */
    double  e;              /* last solution, Newton start point */
} PowerAmp;

/* x: PI input (volts). Returns the normalized speaker signal. The loop through the
 * transformer and the feedback network is solved implicitly each sample. */
static double power_run(PowerAmp* p, double x)
{
    /* Affine response of the transformer filters: y = a h + b */
    double G1 = p->ot_hp.G, s1 = p->ot_hp.s;
    double G2 = p->ot_lp.G, s2 = p->ot_lp.s;
    double a = (1.0 - G1) * G2;
    double b = -(1.0 - G1) * s1 * G2 + (1.0 - G2) * s2;
    /* Feedback at the PI tail: v = k * (SPK_V y) + c0 */
    double k, c0;
    linnet_affine(&p->nfb, NF_M, &k, &c0);
    double L = SPK_V * k * a, target = x - (SPK_V * k * b + c0);

    /* Solve e + L h(e) = target */
    double e = p->e;
    for (int it = 0; it < 6; ++it) {
        const double d = 1e-4;
        double h  = power_h(e);
        double g  = e + L * h - target;
        double dg = 1.0 + L * (power_h(e + d) - power_h(e - d)) / (2.0 * d);
        double step = g / dg;
        e -= step;
        if (fabs(step) < 1e-9) break;
    }
    p->e = e;

    double y = op_lp(&p->ot_lp, op_hp(&p->ot_hp, power_h(e)));
    linnet_step(&p->nfb, SPK_V * y, NF_M);
    return y;
}

/* ----------------------------------------------------------- plugin */

#define CTRL_BLOCK 32   /* samples between control and network updates */
#define OUT_SCALE  0.35 /* normalized speaker signal to digital full scale */

typedef struct {
    const float* port[P_COUNT];
    float*       out;

    double fs, fso;

    /* oversampling */
    Biquad up[AA_STAGES], down[AA_STAGES];

    /* input */
    OnePole in_hp;          /* C15 coupling */

    Triode     v1a, v1b, v2a, v2b, v5b, v3a;
    ShuntStage v5a;

    LinNet  front;          /* C6 and the channel networks */
    LinNet  stack;          /* C58, R89 and the tone stack */

    /* interstage coupling */
    OnePole c2_hp, c1_hp, c48_lp, c57_hp, c56_hp, loop_hp, c49_hp;

    PowerAmp pa;

    /* cabinet */
    CabSim cab;

    /* control values gliding toward their targets */
    double g_pre, g_post, g_in, g_out;
    double g_t, g_m, g_l, g_pres, g_res;
    int    primed;
    int    channel;
} Brownstone;

static void set_channel(Brownstone* s, int ch)
{
    s->channel = ch;
    /* K1A at rest (Rhythm) grounds C26 22uF beside C4 1uF; in Lead C26 sits in series with
     * R18 47k, which leaves essentially C4 alone. */
    triode_set_cathode_cap(&s->v1b, ch == 2 ? 1e-6 : 23e-6, s->fso);
    front_build(&s->front, ch, s->g_pre, s->fso);
    linnet_prepare(&s->front);
    linnet_reset(&s->front);
}

static void build_aa(Biquad* f, double fc, double fs)
{
    for (int k = 0; k < AA_STAGES; ++k) {
        double Q = 1.0 / (2.0 * sin((2.0 * (k + 1) - 1.0) * M_PI / (4.0 * AA_STAGES)));
        bq_lowpass(&f[k], fc, Q, fs);
        f[k].z1 = f[k].z2 = 0.0;
    }
}

/* Preamp supplies (the "V1" and "V2" nodes). */
#define B_V1 300.0
#define B_V2 280.0

static LV2_Handle instantiate(const LV2_Descriptor* desc, double rate,
                              const char* path, const LV2_Feature* const* features)
{
    (void)desc; (void)path; (void)features;
    Brownstone* s = (Brownstone*)calloc(1, sizeof(Brownstone));
    if (!s) return NULL;

    s->fs  = rate;
    s->fso = rate * OS;
    double fso = s->fso;

    build_aa(s->up,   fmin(0.45 * rate, 20000.0), fso);
    build_aa(s->down, fmin(0.45 * rate, 20000.0), fso);

    op_set(&s->in_hp, 1.0 / (2.0 * M_PI * 1e6 * 0.1e-6), fso); /* C15 .1u into R25 1M */

    /*              supply Ra     next load Rk      bypass knee
     * V1A and V5B drive networks that are simulated with their source impedance, so
     * their tables are unloaded (open-circuit plate voltage). */
    triode_build(&s->v1a,   B_V1, 220e3, 1e12,   1.82e3, 1, 0.6);
    triode_build(&s->v1b,   B_V1, 100e3, 1.47e6, 1.82e3, 1, 0.35);
    triode_build(&s->v2a,   B_V1, 100e3, 330e3,  39e3,   0, 0.4);
    triode_build(&s->v2b,   B_V2, 220e3, 1.1e6,  1.82e3, 1, 0.45);
    triode_build(&s->v5b,   B_V2, 220e3, 1e12,   2.2e3,  0, 0.5);
    triode_build(&s->v5a.t, B_V2, 100e3, 333e3,  2.2e3,  0, 0.3);
    triode_build(&s->v3a,   B_V2, 68e3,  1.1e6,  3.3e3,  1, 0.6);

    triode_set_cathode_cap(&s->v1a, 1e-6, fso);
    triode_set_cathode_cap(&s->v2b, 1e-6, fso);
    triode_set_cathode_cap(&s->v3a, 4.7e-9, fso);

    /* Grid stopper x Miller capacitance low-passes */
    triode_set_miller(&s->v1a, 22000.0, fso); /* R22 68k  */
    triode_set_miller(&s->v1b, 3000.0,  fso); /* R82 470k */
    triode_set_miller(&s->v2b, 4700.0,  fso); /* R7 220k  */
    triode_set_miller(&s->v5b, 28000.0, fso); /* R101 node */

    /* V5A: R87 1M plus V5B's output impedance (R96 220k || ra), R88 1M, R91 1M */
    shunt_setup(&s->v5a, 1e6 + 124e3, 1e6, 1e6);

    /* Interstage coupling */
    op_set(&s->c2_hp,  1.0 / (2.0 * M_PI * 1.47e6 * 22e-9), fso);
    op_set(&s->c1_hp,  1.0 / (2.0 * M_PI * 330e3 * 22e-9), fso);
    op_set(&s->c48_lp, 1.0 / (2.0 * M_PI * 77e3 * 1e-9), fso);
    op_set(&s->c57_hp, 1.0 / (2.0 * M_PI * 1.1e6 * 22e-9), fso);
    op_set(&s->c56_hp, 1.0 / (2.0 * M_PI * 1.1e6 * 22e-9), fso);
    op_set(&s->loop_hp, 1.0 / (2.0 * M_PI * 1.0e6 * 47e-9), fso); /* C11 */
    op_set(&s->c49_hp, 1.0 / (2.0 * M_PI * 1.1e6 * 22e-9), fso);

    /* Power amp */
    op_set(&s->pa.ot_hp, 40.0, fso);
    op_set(&s->pa.ot_lp, 12000.0, fso);

    /* Networks are built at the first run(), once the control values are known. */
    s->g_pre = 0.5;
    s->g_t = s->g_m = s->g_l = s->g_pres = s->g_res = 0.5;
    nfb_build(&s->pa.nfb, 0.5, 0.5, fso);
    linnet_prepare(&s->pa.nfb);
    stack_build(&s->stack, 0.5, 0.5, 0.5, fso);
    linnet_prepare(&s->stack);
    front_build(&s->front, 2, 0.5, fso);
    linnet_prepare(&s->front);

    /* Cabinet simulation at base rate; run() selects the model from the ports. */
    cab_init(&s->cab, rate, 0);

    s->channel = -1;
    return (LV2_Handle)s;
}

static void connect_port(LV2_Handle h, uint32_t port, void* data)
{
    Brownstone* s = (Brownstone*)h;
    if (port == P_OUT) s->out = (float*)data;
    else if (port < P_COUNT) s->port[port] = (const float*)data;
}

static double port_val(const Brownstone* s, int p, double def)
{
    return s->port[p] ? (double)*s->port[p] : def;
}

static void reset_state(Brownstone* s)
{
    for (int k = 0; k < AA_STAGES; ++k) {
        s->up[k].z1 = s->up[k].z2 = 0.0;
        s->down[k].z1 = s->down[k].z2 = 0.0;
    }
    cab_reset(&s->cab);
    Triode* tr[] = { &s->v1a, &s->v1b, &s->v2a, &s->v2b, &s->v5b, &s->v5a.t, &s->v3a };
    for (int k = 0; k < 7; ++k) tr[k]->cath.s = tr[k]->miller.s = 0.0;
    OnePole* op[] = { &s->in_hp, &s->c2_hp, &s->c1_hp, &s->c48_lp, &s->c57_hp, &s->c56_hp,
                      &s->loop_hp, &s->c49_hp, &s->pa.ot_hp, &s->pa.ot_lp };
    for (size_t k = 0; k < sizeof(op) / sizeof(op[0]); ++k) op[k]->s = 0.0;
    linnet_reset(&s->front);
    linnet_reset(&s->stack);
    linnet_reset(&s->pa.nfb);
    s->v5a.vg = 0.0;
    s->pa.e = 0.0;
    s->primed = 0;
}

static void activate(LV2_Handle h)
{
    reset_state((Brownstone*)h);
}

#ifndef BS_PROBE
#  define BS_PROBE(i, v) ((void)0)
#endif

/* Process one oversampled sample through the amp. */
static inline double amp_tick(Brownstone* s, double x, double post, int lead)
{
    /* Input jack + V1A */
    x = op_hp(&s->in_hp, x);
    x = triode_run(&s->v1a, x); BS_PROBE(0, x);

    /* C6 and the channel network (Pre pot inside) */
    x = linnet_step(&s->front, x, lead ? FN_WL : FN_WR);

    /* R82 -> V1B */
    x = triode_run(&s->v1b, x); BS_PROBE(1, x);

    /* C2, R6/R9 divider (+ R20 220k shunt in Rhythm, K1B at rest) -> V2A */
    x = op_hp(&s->c2_hp, x) * (lead ? 0.68 : 0.277);
    x = triode_run(&s->v2a, x); BS_PROBE(2, x);

    /* C1, R11 || C48 -> R7 -> V2B */
    x = op_lp(&s->c48_lp, op_hp(&s->c1_hp, x));
    x = triode_run(&s->v2b, x); BS_PROBE(3, x);

    /* R32, C57, R101 -> V5B */
    x = op_hp(&s->c57_hp, x) * 0.087;
    x = triode_run(&s->v5b, x); BS_PROBE(4, x);

    /* R87, C56 -> V5A with R88 shunt feedback */
    x = op_hp(&s->c56_hp, x);
    x = shunt_run(&s->v5a, x); BS_PROBE(5, x);

    /* C58, R89 -> tone stack -> Post pot -> V3B cathode follower -> loop (C11) -> V3A */
    x = linnet_step(&s->stack, x, TN_W) * post; BS_PROBE(6, x);
    x = op_hp(&s->loop_hp, x);
    x = triode_run(&s->v3a, x); BS_PROBE(7, x);

    /* C49 -> phase inverter -> power amp with NFB */
    x = op_hp(&s->c49_hp, x);
    return power_run(&s->pa, x);
}

/* Moves v toward target by at most a fraction of the distance (per control block). */
static int glide(double* v, double target, double k)
{
    double d = target - *v;
    if (fabs(d) < 1e-6) {
        if (*v != target) { *v = target; return 1; }
        return 0;
    }
    *v += d * k;
    return 1;
}

static void run(LV2_Handle h, uint32_t n)
{
    Brownstone* s = (Brownstone*)h;
    const float* in = s->port[P_IN];
    float* out = s->out;
    if (!in || !out) return;

#ifdef BS_HAVE_SSE
    unsigned int csr = _mm_getcsr();
    _mm_setcsr(csr | 0x8040); /* FTZ | DAZ */
#endif

    int ch = (int)lrint(clampd(port_val(s, P_CHANNEL, 2.0), 0.0, 2.0));
    int lead = ch == 2;

    double pre_t  = taper_pre(port_val(s, lead ? P_LEAD_PRE : P_RHY_PRE, 5.0) / 10.0);
    double post_t = taper_audio(port_val(s, lead ? P_LEAD_POST : P_RHY_POST, 5.0) / 10.0);
    double in_t   = pow(10.0, clampd(port_val(s, P_IN_TRIM, 0.0), -24.0, 24.0) / 20.0)
                  * (port_val(s, P_INPUT_LOW, 0.0) > 0.5 ? 0.5 : 1.0);
    double out_t  = pow(10.0, clampd(port_val(s, P_OUT_LEVEL, 0.0), -60.0, 12.0) / 20.0);
    double t_t    = clampd(port_val(s, P_HIGH, 5.0) / 10.0, 0.0, 1.0);
    double m_t    = clampd(port_val(s, P_MID, 5.0) / 10.0, 0.0, 1.0);
    double l_t    = clampd(port_val(s, P_LOW, 5.0) / 10.0, 0.0, 1.0);
    double pres_t = clampd(port_val(s, P_PRESENCE, 5.0) / 10.0, 0.0, 1.0);
    double res_t  = clampd(port_val(s, P_RESONANCE, 5.0) / 10.0, 0.0, 1.0);
    int    cab       = port_val(s, P_CAB, 1.0) > 0.5;
    int    cab_model = (int)lrint(clampd(port_val(s, P_CAB_MODEL, 0.0), 0.0, CAB_MODELS - 1));
    cab_select(&s->cab, cab ? cab_model : CAB_OFF);

    if (!s->primed) {
        /* First block after activation: start at the targets. */
        s->g_pre = pre_t; s->g_post = post_t; s->g_in = in_t; s->g_out = out_t;
        s->g_t = t_t; s->g_m = m_t; s->g_l = l_t; s->g_pres = pres_t; s->g_res = res_t;
        stack_build(&s->stack, t_t, m_t, l_t, s->fso);
        linnet_prepare(&s->stack);
        nfb_build(&s->pa.nfb, pres_t, res_t, s->fso);
        linnet_prepare(&s->pa.nfb);
        s->channel = -1;
        s->primed = 1;
    }
    if (ch != s->channel) {
        s->g_pre = pre_t; /* a channel switch selects another pot */
        set_channel(s, ch);
    }

    /* Glide time constant of about 15 ms, applied once per control block. */
    const double k = 1.0 - exp(-(double)CTRL_BLOCK / (0.015 * s->fs));

    for (uint32_t i0 = 0; i0 < n; i0 += CTRL_BLOCK) {
        const uint32_t m = n - i0 < CTRL_BLOCK ? n - i0 : CTRL_BLOCK;

        if (glide(&s->g_pre, pre_t, k)) {
            front_build(&s->front, ch, s->g_pre, s->fso);
            linnet_prepare(&s->front);
        }
        {
            int a = glide(&s->g_t, t_t, k), b = glide(&s->g_m, m_t, k), c = glide(&s->g_l, l_t, k);
            if (a || b || c) {
                stack_build(&s->stack, s->g_t, s->g_m, s->g_l, s->fso);
                linnet_prepare(&s->stack);
            }
        }
        {
            int a = glide(&s->g_pres, pres_t, k), b = glide(&s->g_res, res_t, k);
            if (a || b) {
                nfb_build(&s->pa.nfb, s->g_pres, s->g_res, s->fso);
                linnet_prepare(&s->pa.nfb);
            }
        }
        glide(&s->g_post, post_t, k);
        glide(&s->g_in, in_t, k);
        glide(&s->g_out, out_t, k);

        for (uint32_t i = i0; i < i0 + m; ++i) {
            double x = (double)in[i] * s->g_in;
            double acc = 0.0;
            for (int o = 0; o < OS; ++o) {
                double u = (o == 0) ? x * OS : 0.0;
                for (int q = 0; q < AA_STAGES; ++q) u = bq_run(&s->up[q], u);
                double y = amp_tick(s, u, s->g_post, lead);
                for (int q = 0; q < AA_STAGES; ++q) y = bq_run(&s->down[q], y);
                if (o == OS - 1) acc = y;
            }
            double y = cab_run(&s->cab, acc * OUT_SCALE) * s->g_out;
            if (!(y == y)) { reset_state(s); y = 0.0; } /* NaN guard */
            out[i] = (float)y;
        }
    }

#ifdef BS_HAVE_SSE
    _mm_setcsr(csr);
#endif
}

static void deactivate(LV2_Handle h) { (void)h; }

static void cleanup(LV2_Handle h) { free(h); }

static const void* extension_data(const char* uri)
{
    (void)uri;
    return NULL;
}

static const LV2_Descriptor descriptor = {
    BROWNSTONE_URI,
    instantiate,
    connect_port,
    activate,
    run,
    deactivate,
    cleanup,
    extension_data
};

/* The DSP entry points, for wrappers in other plugin formats. */
const LV2_Descriptor* brownstone_dsp_descriptor(void)
{
    return &descriptor;
}

#ifndef BROWNSTONE_NO_LV2_EXPORT
LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t index)
{
    return index == 0 ? &descriptor : NULL;
}
#endif
