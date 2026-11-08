/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian */
/*
 * Davidian Brownstone 100 — LV2 amplifier simulation modeled on the circuit of a classic high-gain valve head.
 *
 * Signal chain (component designators refer to the original circuit):
 *
 *   Input (J37 high / J39 low) -> V1A (12AX7, R2 220k, R12 1.82k || C3 1uF)
 *   -> C6 .022 -> channel network:
 *        Lead   : R29 470k || C17 2.2n into R41||VR2 (Ultra Pre)
 *        Rhythm : R19 470k || C51 270p, R21 1M, VR1 (Clear Pre) + C27 180p bright,
 *                 S1C crunch switch
 *   -> R82 470k grid stopper -> V1B (R3 100k, R17 1.82k || C4 1uF, + C26 22uF on lead via K1A)
 *   -> C2 .022 -> R6 470k / R9 1M (+ R20 220k shunt on rhythm via K1B)
 *   -> V2A (R1 100k, R15 39k unbypassed "cold clipper")
 *   -> C1 .022, R11 330k || C48 1nF, R7 220k -> V2B (R4 220k, R13 1.82k || C5 1uF)
 *   -> R32 1M, C57 .022, R101 100k -> V5B (R96 220k, R102 2.2k unbypassed)
 *   -> R87 1M, C56 .022, R91 1M, R88 1M -> V5A (R86 100k, R93 2.2k unbypassed)
 *   -> C58 .022, R89 470k -> V3B cathode follower -> tone stack
 *        (R26 47k slope, C14 470p, C18/C21 .022, VR5 250k high, VR3 1M low, VR4 50k mid)
 *   -> VR7 Ultra Post / VR6 Clean Post -> V3A recovery (R5 68k, R14 3.3k || C61 4.7n)
 *   -> V4 long-tail phase inverter -> 4x 6L6GC push-pull -> T2
 *   -> negative feedback (R56 39k) with presence (VR9 + C62) and resonance (VR8 + C12).
 *
 * Triodes use the Koren 12AX7 model, solved against each stage's AC load line
 * into lookup tables at instantiate time. Nonlinear processing runs at 4x oversampling.
 * The power-amp feedback loop is solved implicitly (Newton) every sample.
 *
 * Published methods used (equations re-implemented from the papers; no code copied):
 *   [1] N. Koren, "Improved vacuum tube models for SPICE simulations", Glass Audio 8(5), 1996.
 *       Triode equation and 12AX7 parameters (koren_12ax7).
 *   [2] D. T. Yeh and J. O. Smith, "Discretization of the '59 Fender Bassman tone stack",
 *       Proc. DAFx-06, Montreal, 2006. Analytic tone stack transfer function (tonestack_design).
 *   [3] R. Bristow-Johnson, "Cookbook formulae for audio EQ biquad filter coefficients"
 *       (Audio EQ Cookbook). Low-pass, high-pass and peaking biquads (bq_*).
 *   [4] V. Zavalishin, "The Art of VA Filter Design", Native Instruments, 2012-2020.
 *       Topology-preserving (trapezoidal) one-pole filters (OnePole) and the instantaneous
 *       response used to solve the feedback loop (power_run).
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

/* ------------------------------------------------- FMV tone stack (Yeh) */
/* Third-order transfer function of the passive tone stack and its bilinear transform,
 * as derived by Yeh and Smith [2], with this amp's component values. */

typedef struct {
    double b[4], a[4];
    double z[3];
    double cur_t, cur_m, cur_l;
    int    valid;
} ToneStack;

static void tonestack_design(ToneStack* ts, double treble, double mid, double bass, double fs)
{
    const double R1 = 250e3;  /* VR5 high   */
    const double R2 = 1e6;    /* VR3 low    */
    const double R3 = 50e3;   /* VR4 mid    */
    const double R4 = 47e3;   /* R26 slope  */
    const double C1 = 470e-12;/* C14        */
    const double C2 = 22e-9;  /* C18        */
    const double C3 = 22e-9;  /* C21        */

    double t = clampd(treble, 0.0, 1.0);
    double m = clampd(mid, 0.0, 1.0);
    double l = taper_audio(bass);

    double b1 = t * C1 * R1 + m * C3 * R3 + l * (C1 * R2 + C2 * R2) + (C1 * R3 + C2 * R3);
    double b2 = t * (C1 * C2 * R1 * R4 + C1 * C3 * R1 * R4)
              - m * m * (C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
              + m * (C1 * C3 * R1 * R3 + C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
              + l * (C1 * C2 * R1 * R2 + C1 * C2 * R2 * R4 + C1 * C3 * R2 * R4)
              + l * m * (C1 * C3 * R2 * R3 + C2 * C3 * R2 * R3)
              + (C1 * C2 * R1 * R3 + C1 * C2 * R3 * R4 + C1 * C3 * R3 * R4);
    double b3 = l * m * (C1 * C2 * C3 * R1 * R2 * R3 + C1 * C2 * C3 * R2 * R3 * R4)
              - m * m * (C1 * C2 * C3 * R1 * R3 * R3 + C1 * C2 * C3 * R3 * R3 * R4)
              + m * (C1 * C2 * C3 * R1 * R3 * R3 + C1 * C2 * C3 * R3 * R3 * R4)
              + t * C1 * C2 * C3 * R1 * R3 * R4
              - t * m * C1 * C2 * C3 * R1 * R3 * R4
              + t * l * C1 * C2 * C3 * R1 * R2 * R4;
    double a0 = 1.0;
    double a1 = (C1 * R1 + C1 * R3 + C2 * R3 + C2 * R4 + C3 * R4)
              + m * C3 * R3 + l * (C1 * R2 + C2 * R2);
    double a2 = m * (C1 * C3 * R1 * R3 - C2 * C3 * R3 * R4 + C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
              + l * m * (C1 * C3 * R2 * R3 + C2 * C3 * R2 * R3)
              - m * m * (C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
              + l * (C1 * C2 * R2 * R4 + C1 * C2 * R1 * R2 + C1 * C3 * R2 * R4 + C2 * C3 * R2 * R4)
              + (C1 * C2 * R1 * R4 + C1 * C3 * R1 * R4 + C1 * C2 * R3 * R4
                 + C1 * C2 * R1 * R3 + C1 * C3 * R3 * R4 + C2 * C3 * R3 * R4);
    double a3 = l * m * (C1 * C2 * C3 * R1 * R2 * R3 + C1 * C2 * C3 * R2 * R3 * R4)
              - m * m * (C1 * C2 * C3 * R1 * R3 * R3 + C1 * C2 * C3 * R3 * R3 * R4)
              + m * (C1 * C2 * C3 * R3 * R3 * R4 + C1 * C2 * C3 * R1 * R3 * R3
                     - C1 * C2 * C3 * R1 * R3 * R4)
              + l * C1 * C2 * C3 * R1 * R2 * R4
              + C1 * C2 * C3 * R1 * R3 * R4;

    /* bilinear transform, s = c (1 - z^-1) / (1 + z^-1) */
    double c = 2.0 * fs, c2 = c * c, c3 = c2 * c;
    double B0 = -b1 * c - b2 * c2 - b3 * c3;
    double B1 = -b1 * c + b2 * c2 + 3.0 * b3 * c3;
    double B2 =  b1 * c + b2 * c2 - 3.0 * b3 * c3;
    double B3 =  b1 * c - b2 * c2 + b3 * c3;
    double A0 = -a0 - a1 * c - a2 * c2 - a3 * c3;
    double A1 = -3.0 * a0 - a1 * c + a2 * c2 + 3.0 * a3 * c3;
    double A2 = -3.0 * a0 + a1 * c + a2 * c2 - 3.0 * a3 * c3;
    double A3 = -a0 + a1 * c - a2 * c2 + a3 * c3;

    ts->b[0] = B0 / A0; ts->b[1] = B1 / A0; ts->b[2] = B2 / A0; ts->b[3] = B3 / A0;
    ts->a[0] = 1.0;     ts->a[1] = A1 / A0; ts->a[2] = A2 / A0; ts->a[3] = A3 / A0;
}

static inline double tonestack_run(ToneStack* ts, double x)
{
    double y  = ts->b[0] * x + ts->z[0];
    ts->z[0]  = ts->b[1] * x - ts->a[1] * y + ts->z[1];
    ts->z[1]  = ts->b[2] * x - ts->a[2] * y + ts->z[2];
    ts->z[2]  = ts->b[3] * x - ts->a[3] * y;
    return y;
}

/* ------------------------------------------------------------ power amp */

#define PI_GAIN   25.0   /* long-tail pair differential gain        */
#define PI_SWING  70.0   /* PI output swing limit (V)               */
#define PWR_BIAS  45.0   /* 6L6 grid bias (V)                       */
#define NFB_BETA  4.3    /* R56/R57 feedback, volts per unit output */

/* Rhythm-channel input network: R19 470k || C51 270p, then R21 1M into VR1.
 * Without Crunch, K4B shunts that node to ground through R23 39k + C63 .1u.
 * HF gain = divider with C51 shorting R19; LF ratio relative to HF. */
#define RHY_CLEAN_HF   (39e3 / (1e6 + 39e3))
#define RHY_CLEAN_LF   ((39e3 / (470e3 + 1e6 + 39e3)) / RHY_CLEAN_HF)
#define RHY_CRUNCH_HF  (1e6 / 2e6)
#define RHY_CRUNCH_LF  ((1e6 / 2.47e6) / RHY_CRUNCH_HF)
#define C51_ZERO_HZ    (1.0 / (2.0 * M_PI * 470e3 * 270e-12))

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
    OnePole res_lp, pres_lp;/* feedback network */
    double  kL, kH;         /* feedback shelf gains (resonance / presence) */
    double  e;              /* last solution, Newton start point */
} PowerAmp;

static double power_run(PowerAmp* p, double x)
{
    /* Affine response of the linear feedback chain: fb_net = a*h + b. */
    double G1 = p->ot_hp.G, s1 = p->ot_hp.s;
    double G2 = p->ot_lp.G, s2 = p->ot_lp.s;
    double G3 = p->res_lp.G, s3 = p->res_lp.s;
    double G4 = p->pres_lp.G, s4 = p->pres_lp.s;

    /* OT high-pass: (1-G1) h - (1-G1) s1 */
    double a = 1.0 - G1, b = -(1.0 - G1) * s1;
    /* OT low-pass: G2 in + (1-G2) s2 */
    a = G2 * a; b = G2 * b + (1.0 - G2) * s2;
    /* resonance low shelf: in (1 + (kL-1) G3) + (kL-1)(1-G3) s3 */
    {
        double k = 1.0 + (p->kL - 1.0) * G3;
        b = k * b + (p->kL - 1.0) * (1.0 - G3) * s3;
        a = k * a;
    }
    /* presence high shelf: in (kH + (1-kH) G4) + (1-kH)(1-G4) s4 */
    {
        double k = p->kH + (1.0 - p->kH) * G4;
        b = k * b + (1.0 - p->kH) * (1.0 - G4) * s4;
        a = k * a;
    }

    /* Solve e = x - beta * (a h(e) + b) */
    double L = NFB_BETA * a, target = x - NFB_BETA * b;
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

    /* Commit filter states with the solved output. */
    double h   = power_h(e);
    double y   = op_lp(&p->ot_lp, op_hp(&p->ot_hp, h));
    double r   = shelf_low(&p->res_lp, p->kL, y);
    double lp4 = op_lp(&p->pres_lp, r);
    (void)lp4;
    return y;
}

/* ----------------------------------------------------------- plugin */

typedef struct {
    const float* port[P_COUNT];
    float*       out;

    double fs, fso;

    /* oversampling */
    Biquad up[AA_STAGES], down[AA_STAGES];

    /* input */
    OnePole in_hp;          /* C15 coupling */

    Triode  v1a, v1b, v2a, v2b, v5b, v5a, v3a;

    /* channel network */
    OnePole lead_shelf;     /* R29 || C17 into R41 || VR2 */
    OnePole rhy_shelf;      /* R19 || C51 into R21 / VR1  */
    OnePole rhy_bright;     /* C27 across VR1             */
    double  rhy_lf, rhy_hf; /* K4B / S1C network gains    */

    /* interstage coupling */
    OnePole c2_hp, c1_hp, c48_lp, c57_hp, c56_hp, c58_hp, c49_hp, loop_hp;

    /* cathode follower + tone stack */
    ToneStack ts;

    PowerAmp pa;

    /* cabinet */
    Biquad cab[6];

    /* smoothed controls */
    double sm_pre, sm_post, sm_attn, sm_out, sm_in;
    double sm_coef;

    int    channel;
    double last_pres, last_res;
} Brownstone;

static void set_channel(Brownstone* s, int ch)
{
    s->channel = ch;
    /* K1A: lead grounds C26 (22uF) in parallel with C4 (1uF) */
    triode_set_cathode_cap(&s->v1b, ch == 2 ? 23e-6 : 1e-6, s->fso);
    /* rhythm shelf pole = C51 zero / LF ratio */
    s->rhy_lf = ch == 0 ? RHY_CLEAN_LF : RHY_CRUNCH_LF;
    s->rhy_hf = ch == 0 ? RHY_CLEAN_HF : RHY_CRUNCH_HF;
    op_set(&s->rhy_shelf, C51_ZERO_HZ / s->rhy_lf, s->fso);
}

static void set_feedback(Brownstone* s, double pres, double res)
{
    s->pa.kH = 1.0 - 0.85 * clampd(pres, 0.0, 1.0);
    s->pa.kL = 1.0 - 0.85 * clampd(res, 0.0, 1.0);
    s->last_pres = pres;
    s->last_res  = res;
}

static void build_aa(Biquad* f, double fc, double fs)
{
    for (int k = 0; k < AA_STAGES; ++k) {
        double Q = 1.0 / (2.0 * sin((2.0 * (k + 1) - 1.0) * M_PI / (4.0 * AA_STAGES)));
        bq_lowpass(&f[k], fc, Q, fs);
        f[k].z1 = f[k].z2 = 0.0;
    }
}

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

    /*            supply Ra     next load Rk     bypass knee */
    triode_build(&s->v1a, 330, 220e3, 1.0e6,  1.82e3, 1, 0.6);
    triode_build(&s->v1b, 330, 100e3, 1.47e6, 1.82e3, 1, 0.35);
    triode_build(&s->v2a, 330, 100e3, 330e3,  39e3,   0, 0.4);
    triode_build(&s->v2b, 300, 220e3, 1.1e6,  1.82e3, 1, 0.45);
    triode_build(&s->v5b, 300, 220e3, 2.0e6,  2.2e3,  0, 0.5);
    triode_build(&s->v5a, 300, 100e3, 1.0e6,  2.2e3,  0, 0.3);
    triode_build(&s->v3a, 300, 68e3,  1.1e6,  3.3e3,  1, 0.6);

    triode_set_cathode_cap(&s->v1a, 1e-6, fso);
    triode_set_cathode_cap(&s->v2b, 1e-6, fso);
    triode_set_cathode_cap(&s->v3a, 4.7e-9, fso);

    /* Grid stopper x Miller capacitance low-passes */
    triode_set_miller(&s->v1a, 22000.0, fso); /* R22 68k   */
    triode_set_miller(&s->v1b, 3000.0,  fso); /* R82 470k  */
    triode_set_miller(&s->v2b, 4700.0,  fso); /* R7 220k   */
    triode_set_miller(&s->v5b, 28000.0, fso); /* R101 node */
    triode_set_miller(&s->v5a, 2400.0,  fso); /* R88 1M    */

    /* Channel networks */
    op_set(&s->lead_shelf, 299.0, fso); /* zero 154 Hz, pole 299 Hz */
    s->rhy_lf = RHY_CLEAN_LF;
    s->rhy_hf = RHY_CLEAN_HF;
    op_set(&s->rhy_shelf, C51_ZERO_HZ / s->rhy_lf, fso);

    /* Interstage coupling */
    op_set(&s->c2_hp,  1.0 / (2.0 * M_PI * 1.47e6 * 22e-9), fso);
    op_set(&s->c1_hp,  1.0 / (2.0 * M_PI * 330e3 * 22e-9), fso);
    op_set(&s->c48_lp, 1.0 / (2.0 * M_PI * 77e3 * 1e-9), fso);
    op_set(&s->c57_hp, 1.0 / (2.0 * M_PI * 1.1e6 * 22e-9), fso);
    op_set(&s->c56_hp, 1.0 / (2.0 * M_PI * 2.0e6 * 22e-9), fso);
    op_set(&s->c58_hp, 1.0 / (2.0 * M_PI * 1.0e6 * 22e-9), fso);
    op_set(&s->c49_hp, 1.0 / (2.0 * M_PI * 1.1e6 * 22e-9), fso);
    op_set(&s->loop_hp, 1.0 / (2.0 * M_PI * 1.0e6 * 47e-9), fso); /* C11 */

    /* Power amp */
    op_set(&s->pa.ot_hp, 40.0, fso);
    op_set(&s->pa.ot_lp, 12000.0, fso);
    op_set(&s->pa.res_lp, 110.0, fso);
    op_set(&s->pa.pres_lp, 1500.0, fso);
    set_feedback(s, 0.5, 0.5);

    /* Cabinet (4x12 closed back, approximate) at base rate */
    bq_highpass(&s->cab[0], 75.0, 0.8, rate);
    bq_peak(&s->cab[1], 115.0, 1.4, 4.0, rate);
    bq_peak(&s->cab[2], 650.0, 1.0, -4.0, rate);
    bq_peak(&s->cab[3], 2600.0, 1.3, 4.5, rate);
    bq_lowpass(&s->cab[4], fmin(5200.0, 0.45 * rate), 0.6, rate);
    bq_lowpass(&s->cab[5], fmin(7000.0, 0.45 * rate), 0.707, rate);

    s->sm_coef = 1.0 - exp(-1.0 / (0.015 * rate));
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
    for (int k = 0; k < 6; ++k) s->cab[k].z1 = s->cab[k].z2 = 0.0;
    Triode* tr[] = { &s->v1a, &s->v1b, &s->v2a, &s->v2b, &s->v5b, &s->v5a, &s->v3a };
    for (int k = 0; k < 7; ++k) tr[k]->cath.s = tr[k]->miller.s = 0.0;
    OnePole* op[] = { &s->in_hp, &s->lead_shelf, &s->rhy_shelf, &s->rhy_bright,
                      &s->c2_hp, &s->c1_hp, &s->c48_lp, &s->c57_hp, &s->c56_hp,
                      &s->c58_hp, &s->c49_hp, &s->loop_hp, &s->pa.ot_hp,
                      &s->pa.ot_lp, &s->pa.res_lp, &s->pa.pres_lp };
    for (size_t k = 0; k < sizeof(op) / sizeof(op[0]); ++k) op[k]->s = 0.0;
    s->ts.z[0] = s->ts.z[1] = s->ts.z[2] = 0.0;
    s->pa.e = 0.0;
    s->sm_pre = s->sm_post = s->sm_attn = 0.0;
    s->sm_out = s->sm_in = 0.0;
    s->ts.valid = 0;
}

static void activate(LV2_Handle h)
{
    reset_state((Brownstone*)h);
}

#ifndef BS_PROBE
#  define BS_PROBE(i, v) ((void)0)
#endif

/* Process one oversampled sample through the amp. */
static inline double amp_tick(Brownstone* s, double x, double pre, double post,
                              double attn, int lead)
{
    /* Input jack + V1A */
    x = op_hp(&s->in_hp, x);
    x = triode_run(&s->v1a, x); BS_PROBE(0, x);

    /* Channel network after C6 */
    if (lead) {
        x = shelf_low(&s->lead_shelf, 500e3 / 970e3, x) * pre;
    } else {
        /* R19 || C51, R21, K4B shunt (clean) or S1C crunch */
        x = shelf_low(&s->rhy_shelf, s->rhy_lf, x) * attn;
        /* VR1 with C27 bright cap: HF bypasses the upper part of the pot */
        x = shelf_low(&s->rhy_bright, pre, x);
    }

    /* V1B */
    x = triode_run(&s->v1b, x); BS_PROBE(1, x);

    /* C2, R6/R9 divider (+R20 shunt on rhythm) -> V2A */
    x = op_hp(&s->c2_hp, x) * (lead ? 0.68 : 0.277);
    x = triode_run(&s->v2a, x); BS_PROBE(2, x);

    /* C1, R11 || C48 -> R7 -> V2B */
    x = op_lp(&s->c48_lp, op_hp(&s->c1_hp, x));
    x = triode_run(&s->v2b, x); BS_PROBE(3, x);

    /* R32, C57, R101 -> V5B */
    x = op_hp(&s->c57_hp, x) * 0.087;
    x = triode_run(&s->v5b, x); BS_PROBE(4, x);

    /* R87, C56, R91, R88 -> V5A */
    x = op_hp(&s->c56_hp, x) * 0.49;
    x = triode_run(&s->v5a, x); BS_PROBE(5, x);

    /* C58, R89 -> V3B cathode follower: grid current limits positive swing */
    x = op_hp(&s->c58_hp, x);
    if (x > 0.0) x = 60.0 * tanh(x / 60.0);
    else         x = 150.0 * tanh(x / 150.0);

    /* Tone stack -> post pot -> FX loop -> V3A */
    x = tonestack_run(&s->ts, x) * post; BS_PROBE(6, x);
    x = op_hp(&s->loop_hp, x);
    x = triode_run(&s->v3a, x); BS_PROBE(7, x);

    /* C49 -> phase inverter -> power amp with NFB */
    x = op_hp(&s->c49_hp, x);
    return power_run(&s->pa, x);
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
    if (ch != s->channel) {
        if (s->channel >= 0) {
            /* clear coupling states to avoid a thump when the relays switch */
            s->lead_shelf.s = s->rhy_shelf.s = s->rhy_bright.s = 0.0;
        }
        set_channel(s, ch);
    }
    int lead = ch == 2;

    double pre_knob  = port_val(s, lead ? P_LEAD_PRE : P_RHY_PRE, 5.0) / 10.0;
    double post_knob = port_val(s, lead ? P_LEAD_POST : P_RHY_POST, 5.0) / 10.0;
    double pre_t     = taper_pre(pre_knob);
    double post_t    = taper_pre(post_knob);
    double attn_t    = s->rhy_hf;
    double in_t      = pow(10.0, clampd(port_val(s, P_IN_TRIM, 0.0), -24.0, 24.0) / 20.0)
                     * (port_val(s, P_INPUT_LOW, 0.0) > 0.5 ? 0.5 : 1.0);
    double out_t     = pow(10.0, clampd(port_val(s, P_OUT_LEVEL, 0.0), -60.0, 12.0) / 20.0);
    int    cab       = port_val(s, P_CAB, 1.0) > 0.5;

    if (!s->ts.valid) {
        s->sm_pre = pre_t; s->sm_post = post_t; s->sm_attn = attn_t;
        s->sm_in = in_t; s->sm_out = out_t;
    }

    /* Rhythm bright cap corner follows the pot position (upper leg of VR1). */
    {
        /* H = Rl(1 + s Ru C)/(Rl + Ru + s Rl Ru C): DC gain = pre, pole at 1/((Rl||Ru) C) */
        double rl = fmax(s->sm_pre * 1e6, 1.0), ru = fmax((1.0 - s->sm_pre) * 1e6, 1.0);
        op_set(&s->rhy_bright, 1.0 / (2.0 * M_PI * (rl * ru / (rl + ru)) * 180e-12), s->fso);
    }

    /* Tone stack: glide toward targets once per block. */
    {
        double tt = clampd(port_val(s, P_HIGH, 5.0) / 10.0, 0.0, 1.0);
        double tm = clampd(port_val(s, P_MID, 5.0) / 10.0, 0.0, 1.0);
        double tl = clampd(port_val(s, P_LOW, 5.0) / 10.0, 0.0, 1.0);
        if (!s->ts.valid) {
            s->ts.cur_t = tt; s->ts.cur_m = tm; s->ts.cur_l = tl;
            tonestack_design(&s->ts, tt, tm, tl, s->fso);
            s->ts.valid = 1;
        } else if (fabs(tt - s->ts.cur_t) + fabs(tm - s->ts.cur_m) + fabs(tl - s->ts.cur_l) > 1e-5) {
            double k = fmin(1.0, n / (0.03 * s->fs));
            s->ts.cur_t += (tt - s->ts.cur_t) * k;
            s->ts.cur_m += (tm - s->ts.cur_m) * k;
            s->ts.cur_l += (tl - s->ts.cur_l) * k;
            tonestack_design(&s->ts, s->ts.cur_t, s->ts.cur_m, s->ts.cur_l, s->fso);
        }
    }

    {
        double pres = clampd(port_val(s, P_PRESENCE, 5.0) / 10.0, 0.0, 1.0);
        double res  = clampd(port_val(s, P_RESONANCE, 5.0) / 10.0, 0.0, 1.0);
        if (pres != s->last_pres || res != s->last_res) set_feedback(s, pres, res);
    }

    const double c = s->sm_coef;
    for (uint32_t i = 0; i < n; ++i) {
        s->sm_pre  += (pre_t - s->sm_pre) * c;
        s->sm_post += (post_t - s->sm_post) * c;
        s->sm_attn += (attn_t - s->sm_attn) * c;
        s->sm_in   += (in_t - s->sm_in) * c;
        s->sm_out  += (out_t - s->sm_out) * c;

        double x = (double)in[i] * s->sm_in;
        double acc = 0.0;
        for (int o = 0; o < OS; ++o) {
            double u = (o == 0) ? x * OS : 0.0;
            for (int k = 0; k < AA_STAGES; ++k) u = bq_run(&s->up[k], u);
            double y = amp_tick(s, u, s->sm_pre, s->sm_post, s->sm_attn, lead);
            for (int k = 0; k < AA_STAGES; ++k) y = bq_run(&s->down[k], y);
            if (o == OS - 1) acc = y;
        }

        double y = acc * 0.35;
        if (cab) {
            for (int k = 0; k < 6; ++k) y = bq_run(&s->cab[k], y);
        }
        y *= s->sm_out;
        if (!(y == y)) { reset_state(s); y = 0.0; } /* NaN guard */
        out[i] = (float)y;
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

LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t index)
{
    return index == 0 ? &descriptor : NULL;
}
