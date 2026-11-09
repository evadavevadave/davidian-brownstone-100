/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian */
/* Speaker cabinet simulation: four cabinet voicings built from biquad cascades
 * (Audio EQ Cookbook shapes), loudness-matched to each other, with a crossfade
 * whenever the model changes or the cabinet is switched in or out.
 *
 * The voicings are generic, not measurements of any particular product:
 *   0  4x12 Modern   closed back, tight low end, scooped low mids, forward upper mids
 *   1  4x12 Vintage  closed back, darker, smoother top, mid-forward lower-power speakers
 *   2  2x12 Open     open back, less low-end thump, a cancellation dip, more air
 *   3  1x12 Combo    small open-back box, tight bass, boxy low mids, bright upper mids
 *
 * Included by brownstone100.c after the Biquad helpers. */
#ifndef BROWNSTONE_CABINET_H
#define BROWNSTONE_CABINET_H

#define CAB_MODELS   4
#define CAB_OFF      (-1)
#define CAB_STAGES   7
#define CAB_FADE_SEC 0.02

enum { CS_NONE, CS_HP, CS_LP, CS_PEAK };

typedef struct {
    int    type;
    double f, q, db;
} CabStage;

static const CabStage CAB_SPEC[CAB_MODELS][CAB_STAGES] = {
    /* 0: 4x12 Modern (the voicing used before the cabinet selector existed) */
    { { CS_HP, 75, 0.8, 0 }, { CS_PEAK, 115, 1.4, 4.0 }, { CS_PEAK, 650, 1.0, -4.0 },
      { CS_PEAK, 2600, 1.3, 4.5 }, { CS_LP, 5200, 0.6, 0 }, { CS_LP, 7000, 0.707, 0 },
      { CS_NONE, 0, 0, 0 } },
    /* 1: 4x12 Vintage */
    { { CS_HP, 70, 0.9, 0 }, { CS_PEAK, 105, 1.6, 4.5 }, { CS_PEAK, 500, 1.0, -2.0 },
      { CS_PEAK, 1600, 1.2, 3.5 }, { CS_PEAK, 3200, 2.0, -3.0 }, { CS_LP, 4300, 0.7, 0 },
      { CS_LP, 5500, 0.6, 0 } },
    /* 2: 2x12 Open */
    { { CS_HP, 90, 0.6, 0 }, { CS_PEAK, 130, 1.2, 2.5 }, { CS_PEAK, 400, 0.9, -3.0 },
      { CS_PEAK, 2400, 1.1, 4.0 }, { CS_PEAK, 4200, 2.5, -2.5 }, { CS_LP, 6000, 0.7, 0 },
      { CS_LP, 8000, 0.6, 0 } },
    /* 3: 1x12 Combo */
    { { CS_HP, 105, 0.7, 0 }, { CS_PEAK, 160, 1.3, 3.0 }, { CS_PEAK, 900, 1.0, 2.0 },
      { CS_PEAK, 2000, 1.4, 3.0 }, { CS_PEAK, 3800, 2.0, -3.0 }, { CS_LP, 5600, 0.7, 0 },
      { CS_LP, 7500, 0.6, 0 } },
};

typedef struct {
    Biquad q[CAB_STAGES];
    double gain; /* loudness match */
    int    model; /* CAB_OFF = pass-through */
} CabBank;

typedef struct {
    CabBank bank[2];
    int     cur;       /* bank being heard */
    int     fade_left; /* samples left in a crossfade to the other bank */
    int     fade_len;
    double  fs;
    double  norm[CAB_MODELS];
} CabSim;

static void cab_identity(Biquad* q)
{
    q->b0 = 1.0;
    q->b1 = q->b2 = q->a1 = q->a2 = 0.0;
}

static void cab_design_stages(Biquad* q, int model, double fs)
{
    for (int k = 0; k < CAB_STAGES; ++k) {
        const CabStage* st = &CAB_SPEC[model][k];
        double f = st->f < 0.45 * fs ? st->f : 0.45 * fs;
        switch (st->type) {
        case CS_HP: bq_highpass(&q[k], f, st->q, fs); break;
        case CS_LP: bq_lowpass(&q[k], f, st->q, fs); break;
        case CS_PEAK: bq_peak(&q[k], f, st->q, st->db, fs); break;
        default: cab_identity(&q[k]); break;
        }
    }
}

/* Magnitude of one biquad at normalized angular frequency w. */
static double bq_mag(const Biquad* q, double w)
{
    double c1 = cos(w), s1 = sin(w), c2 = cos(2 * w), s2 = sin(2 * w);
    double nr = q->b0 + q->b1 * c1 + q->b2 * c2, ni = -(q->b1 * s1 + q->b2 * s2);
    double dr = 1.0 + q->a1 * c1 + q->a2 * c2, di = -(q->a1 * s1 + q->a2 * s2);
    return sqrt((nr * nr + ni * ni) / (dr * dr + di * di));
}

/* Mean level in dB over the guitar band, used to loudness-match the models. */
static double cab_band_db(const Biquad* q, double fs)
{
    double sum = 0.0;
    const int n = 48;
    for (int i = 0; i < n; ++i) {
        double f = 150.0 * pow(5000.0 / 150.0, i / (double)(n - 1));
        double m = 1.0;
        for (int k = 0; k < CAB_STAGES; ++k) m *= bq_mag(&q[k], 2.0 * M_PI * f / fs);
        sum += 20.0 * log10(m + 1e-12);
    }
    return sum / n;
}

static void cab_bank_set(CabSim* c, CabBank* b, int model)
{
    b->model = model;
    for (int k = 0; k < CAB_STAGES; ++k) b->q[k].z1 = b->q[k].z2 = 0.0;
    if (model == CAB_OFF) {
        for (int k = 0; k < CAB_STAGES; ++k) cab_identity(&b->q[k]);
        b->gain = 1.0;
    } else {
        cab_design_stages(b->q, model, c->fs);
        b->gain = c->norm[model];
    }
}

static void cab_init(CabSim* c, double fs, int model)
{
    Biquad tmp[CAB_STAGES];
    double ref;
    c->fs = fs;
    c->fade_len = (int)(CAB_FADE_SEC * fs);
    if (c->fade_len < 1) c->fade_len = 1;
    /* Match every model to the band level of model 0, which keeps the original level. */
    cab_design_stages(tmp, 0, fs);
    ref = cab_band_db(tmp, fs);
    for (int m = 0; m < CAB_MODELS; ++m) {
        cab_design_stages(tmp, m, fs);
        c->norm[m] = pow(10.0, (ref - cab_band_db(tmp, fs)) / 20.0);
    }
    c->cur = 0;
    c->fade_left = 0;
    cab_bank_set(c, &c->bank[0], model);
    cab_bank_set(c, &c->bank[1], model);
}

static void cab_reset(CabSim* c)
{
    for (int b = 0; b < 2; ++b)
        for (int k = 0; k < CAB_STAGES; ++k) c->bank[b].q[k].z1 = c->bank[b].q[k].z2 = 0.0;
    c->fade_left = 0;
}

/* Ask for a model (or CAB_OFF). Starts a crossfade if it differs from what is heard. */
static void cab_select(CabSim* c, int model)
{
    CabBank* heard = &c->bank[c->cur];
    if (c->fade_left > 0) {
        /* Already fading: retarget the incoming bank if needed. */
        CabBank* next = &c->bank[c->cur ^ 1];
        if (next->model != model && heard->model != model) cab_bank_set(c, next, model);
        return;
    }
    if (heard->model == model) return;
    cab_bank_set(c, &c->bank[c->cur ^ 1], model);
    c->fade_left = c->fade_len;
}

static inline double cab_bank_run(CabBank* b, double x)
{
    if (b->model == CAB_OFF) return x;
    for (int k = 0; k < CAB_STAGES; ++k) x = bq_run(&b->q[k], x);
    return x * b->gain;
}

static inline double cab_run(CabSim* c, double x)
{
    double y = cab_bank_run(&c->bank[c->cur], x);
    if (c->fade_left > 0) {
        double t = (double)c->fade_left / c->fade_len; /* 1 -> 0 */
        double z = cab_bank_run(&c->bank[c->cur ^ 1], x);
        /* equal-power crossfade */
        y = y * sin(0.5 * M_PI * t) + z * cos(0.5 * M_PI * t);
        if (--c->fade_left == 0) c->cur ^= 1;
    }
    return y;
}

#endif
