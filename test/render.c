/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian */
/* Offline test harness: runs the plugin through its LV2 descriptor on synthetic
 * guitar signals, checks for NaN/instability, reports levels and speed, and
 * writes WAV files for listening. */
#include <stdio.h>
#include <time.h>
#include "../src/brownstone100.c"

#define SR 48000.0

static void write_wav(const char* path, const float* x, int n)
{
    FILE* f = fopen(path, "wb");
    if (!f) return;
    int sr = (int)SR, bytes = n * 2;
    fwrite("RIFF", 1, 4, f);
    int v = 36 + bytes; fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);
    v = 16; fwrite(&v, 4, 1, f);
    short s = 1; fwrite(&s, 2, 1, f); fwrite(&s, 2, 1, f);
    fwrite(&sr, 4, 1, f);
    v = sr * 2; fwrite(&v, 4, 1, f);
    s = 2; fwrite(&s, 2, 1, f);
    s = 16; fwrite(&s, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
    for (int i = 0; i < n; ++i) {
        double y = x[i] < -1 ? -1 : (x[i] > 1 ? 1 : x[i]);
        short q = (short)lrint(y * 32767.0);
        fwrite(&q, 2, 1, f);
    }
    fclose(f);
}

/* Karplus-Strong power chord (E2 + B2 + E3), repeated strums.
 * K. Karplus and A. Strong, "Digital synthesis of plucked-string and drum timbres",
 * Computer Music Journal 7(2), 1983. */
static void make_guitar(float* x, int n, double level)
{
    double freqs[3] = { 82.41, 123.47, 164.81 };
    unsigned seed = 1234;
    for (int i = 0; i < n; ++i) x[i] = 0.0f;
    for (int strum = 0; strum * (int)SR < n; ++strum) {
        int start = strum * (int)SR;
        for (int s = 0; s < 3; ++s) {
            int L = (int)(SR / freqs[s]);
            float* buf = (float*)malloc(sizeof(float) * L);
            for (int k = 0; k < L; ++k) {
                seed = seed * 1103515245u + 12345u;
                buf[k] = (float)(((seed >> 9) & 0xFFFF) / 32768.0 - 1.0);
            }
            int p = 0;
            for (int i = start + s * 300; i < n && i < start + (int)SR; ++i) {
                int q = (p + 1) % L;
                float out = buf[p];
                buf[p] = 0.4985f * (buf[p] + buf[q]);
                p = q;
                x[i] += (float)(out * level / 3.0);
            }
            free(buf);
        }
    }
}

int main(void)
{
    const LV2_Descriptor* d = lv2_descriptor(0);
    if (!d || lv2_descriptor(1) != NULL) { printf("descriptor FAIL\n"); return 1; }

    const int N = (int)SR * 4;
    float* in  = (float*)malloc(sizeof(float) * N);
    float* out = (float*)malloc(sizeof(float) * N);
    make_guitar(in, N, 0.3);

    struct { const char* name; float ch, pre, post; } cases[] = {
        { "rhythm_clean", 0, 3, 3 },
        { "rhythm_crunch", 1, 5, 3 },
        { "lead", 2, 7, 3 },
        { "lead_max", 2, 10, 10 },
    };

    int fail = 0;
    for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); ++c) {
        float ctl[P_COUNT] = { 0 };
        ctl[P_CHANNEL] = cases[c].ch;
        ctl[P_INPUT_LOW] = 0;
        ctl[P_RHY_PRE] = cases[c].pre;  ctl[P_RHY_POST] = cases[c].post;
        ctl[P_LEAD_PRE] = cases[c].pre; ctl[P_LEAD_POST] = cases[c].post;
        ctl[P_LOW] = 5; ctl[P_MID] = 5; ctl[P_HIGH] = 6;
        ctl[P_PRESENCE] = 5; ctl[P_RESONANCE] = 5;
        ctl[P_CAB] = 1; ctl[P_IN_TRIM] = 0; ctl[P_OUT_LEVEL] = 0;

        LV2_Handle h = d->instantiate(d, SR, "", NULL);
        d->connect_port(h, P_IN, in);
        d->connect_port(h, P_OUT, out);
        for (int p = 2; p < P_COUNT; ++p) d->connect_port(h, p, &ctl[p]);
        d->activate(h);

        clock_t t0 = clock();
        for (int i = 0; i < N; i += 256) {
            int m = N - i < 256 ? N - i : 256;
            d->connect_port(h, P_IN, in + i);
            d->connect_port(h, P_OUT, out + i);
            /* sweep the tone controls in the last second */
            if (i > 3 * SR) { ctl[P_HIGH] = (float)(10.0 * (i - 3 * SR) / SR); ctl[P_LOW] = 10 - ctl[P_HIGH]; }
            d->run(h, m);
        }
        double secs = (double)(clock() - t0) / CLOCKS_PER_SEC;

        double peak = 0, sum = 0; int bad = 0;
        for (int i = 0; i < N; ++i) {
            if (!isfinite(out[i])) bad = 1;
            peak = fmax(peak, fabs(out[i]));
            sum += (double)out[i] * out[i];
        }
        double rms = sqrt(sum / N);
        printf("%-14s peak %6.3f (%6.1f dBFS)  rms %6.1f dBFS  realtime x%.0f  %s\n",
               cases[c].name, peak, 20 * log10(peak + 1e-12), 20 * log10(rms + 1e-12),
               (N / SR) / secs, bad ? "NaN/INF!" : "ok");
        if (bad || peak < 1e-4 || peak > 4.0) fail = 1;

        char path[128];
        snprintf(path, sizeof path, "test_%s.wav", cases[c].name);
        write_wav(path, out, N);
        d->deactivate(h);
        d->cleanup(h);
    }
    write_wav("test_input_di.wav", in, N);

    /* Silence must stay silent (no self-oscillation in the NFB loop). */
    {
        float ctl[P_COUNT] = { 0 };
        ctl[P_CHANNEL] = 2; ctl[P_LEAD_PRE] = 10; ctl[P_LEAD_POST] = 10;
        ctl[P_LOW] = 10; ctl[P_MID] = 10; ctl[P_HIGH] = 10;
        ctl[P_PRESENCE] = 10; ctl[P_RESONANCE] = 10; ctl[P_CAB] = 0;
        for (int i = 0; i < N; ++i) in[i] = i < 10 ? 0.5f : 0.0f;
        LV2_Handle h = d->instantiate(d, SR, "", NULL);
        d->connect_port(h, P_IN, in); d->connect_port(h, P_OUT, out);
        for (int p = 2; p < P_COUNT; ++p) d->connect_port(h, p, &ctl[p]);
        d->activate(h);
        d->run(h, N);
        double tail = 0;
        for (int i = N - (int)SR; i < N; ++i) tail = fmax(tail, fabs(out[i]));
        printf("impulse tail after 3 s: %g  %s\n", tail, tail < 1e-4 ? "ok" : "UNSTABLE");
        if (!(tail < 1e-4)) fail = 1;
        d->cleanup(h);
    }

    printf(fail ? "FAIL\n" : "PASS\n");
    return fail;
}
