/* Offline-Test für die KCB-Engine (x86 oder armhf unter QEMU).
 * 1. Prüft für alle Tonarten/Patterns/Tempi, dass Kick und Bass nie
 *    überlappen und beide an der Nahtstelle bei ~0 liegen.
 * 2. Rendert eine Demo-WAV (8 Beats, 145 BPM, F) plus CSV der Nahtstelle.
 * 3. Misst die CPU-Zeit für 60 s Audio. */
#include "kcb_engine.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SR 44100.f
#define BLOCK 128

static void render_beat(kcb_engine *e, float *buf, int len)
{
    kcb_trigger(e, 60, 127);
    for (int o = 0; o < len; o += BLOCK)
        kcb_render(e, buf + o, (len - o) < BLOCK ? (len - o) : BLOCK);
}

static int write_wav(const char *path, const float *x, int n)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    int bytes = n * 2;
    unsigned int sr = (unsigned int)SR, br = sr * 2, v32;
    unsigned short v16;
    fwrite("RIFF", 1, 4, f); v32 = 36 + bytes; fwrite(&v32, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); v32 = 16; fwrite(&v32, 4, 1, f);
    v16 = 1; fwrite(&v16, 2, 1, f); fwrite(&v16, 2, 1, f);
    fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f);
    v16 = 2; fwrite(&v16, 2, 1, f); v16 = 16; fwrite(&v16, 2, 1, f);
    fwrite("data", 1, 4, f); v32 = bytes; fwrite(&v32, 4, 1, f);
    for (int i = 0; i < n; i++) {
        float s = x[i] > 1.f ? 1.f : (x[i] < -1.f ? -1.f : x[i]);
        short q = (short)lrintf(s * 32767.f);
        fwrite(&q, 2, 1, f);
    }
    fclose(f);
    return 0;
}

int main(int argc, char **argv)
{
    const char *outdir = argc > 1 ? argv[1] : ".";
    static float kick[44100], bass[44100], mix[44100];
    kcb_engine e;
    int fails = 0, cases = 0;
    float worst_kick_edge = 0.f, worst_bass_edge = 0.f;
    double max_dev = 0.0, min_dev = 1e9;
    int locked = 0, lock_cases = 0;
    const float bpms[] = { 130, 138, 145, 150, 160, 180 };
    const float gaps[] = { 0, 3 };

    for (int b = 0; b < 6; b++)
    for (int pat = 0; pat < 4; pat++)
    for (int root = 0; root < 12; root++)
    for (int oct = 0; oct < 2; oct++)
    for (int g = 0; g < 2; g++)
    for (int lk = 0; lk < 2; lk++) {
        kcb_init(&e, SR);
        kcb_set_param(&e, KCB_P_LOCK, (float)lk);
        kcb_set_param(&e, KCB_P_BPM, bpms[b]);
        kcb_set_param(&e, KCB_P_PATTERN, (float)pat);
        kcb_set_param(&e, KCB_P_ROOT, (float)root);
        kcb_set_param(&e, KCB_P_OCTAVE, (float)oct);
        kcb_set_param(&e, KCB_P_GAP, gaps[g]);
        kcb_set_param(&e, KCB_P_KICK_CLICK, 0.f);  /* Click separat, liegt bei t=0 */

        kcb_set_param(&e, KCB_P_BASS_LEVEL, 0.f);
        render_beat(&e, kick, e.beat_len ? e.beat_len : 44100);
        int len = e.beat_len, kend = e.kick_end, bstart = e.bass_start[0];
        if (lk == 0) {
            double dev = e.kstart_eff / e.kstart;
            if (dev > max_dev) max_dev = dev;
            if (dev < min_dev) min_dev = dev;
            if (kend == bstart - (int)lroundf(gaps[g] * 0.001f * SR)) locked++;
            lock_cases++;
        }

        kcb_set_param(&e, KCB_P_BASS_LEVEL, 0.7f);
        kcb_set_param(&e, KCB_P_KICK_LEVEL, 0.f);
        render_beat(&e, bass, len);

        cases++;
        int ok = 1;
        double overlap = 0.0;
        for (int i = 0; i < len; i++) overlap += fabs(kick[i] * bass[i]);
        for (int i = kend; i < len; i++) if (kick[i] != 0.f) ok = 0;
        for (int i = 0; i < bstart; i++) if (bass[i] != 0.f) ok = 0;
        if (overlap != 0.0 || kend > bstart) ok = 0;
        float ke = fabsf(kick[kend - 1]);
        float be = fabsf(bass[bstart]);
        if (ke > worst_kick_edge) worst_kick_edge = ke;
        if (be > worst_bass_edge) worst_bass_edge = be;
        if (ke > 0.02f) ok = 0;
        if (!ok) {
            fails++;
            printf("FAIL bpm=%.0f pat=%d root=%d oct=%d gap=%.0f kend=%d bstart=%d edge=%.4f\n",
                   bpms[b], pat, root, oct, gaps[g], kend, bstart, ke);
        }
    }
    printf("Phasentest: %d Fälle, %d Fehler\n", cases, fails);
    printf("  max |Kick| am letzten Sample: %.5f  (%.1f dBFS)\n",
           worst_kick_edge, 20.0 * log10(worst_kick_edge + 1e-12));
    printf("  max |Bass| am ersten Sample:  %.5f\n", worst_bass_edge);
    printf("  Sweep-Lock: %d/%d Fälle enden exakt am Basseinsatz, Pitch-Start-Korrektur %.1f%% .. %+.1f%%\n",
           locked, lock_cases, (min_dev - 1.0) * 100.0, (max_dev - 1.0) * 100.0);

    /* Demo-Render: 8 Beats, 145 BPM, F, Pattern KBBB */
    kcb_init(&e, SR);
    int beat = (int)lround(SR * 60.0 / 145.0);
    int total = beat * 8;
    float *demo = calloc((size_t)total, sizeof(float));
    for (int k = 0; k < 8; k++) {
        if (k == 4) kcb_set_param(&e, KCB_P_PATTERN, 1.f);
        render_beat(&e, demo + k * beat, beat);
    }
    char path[512];
    snprintf(path, sizeof path, "%s/kcb_demo_145bpm_F.wav", outdir);
    write_wav(path, demo, total);
    printf("Demo: %s\n", path);

    /* Nahtstelle für Plot: Kick und Bass getrennt, erster Beat */
    kcb_init(&e, SR);
    kcb_set_param(&e, KCB_P_KICK_CLICK, 0.f);
    kcb_set_param(&e, KCB_P_BASS_LEVEL, 0.f);
    render_beat(&e, kick, beat);
    int kend = e.kick_end, bstart = e.bass_start[0];
    kcb_init(&e, SR);
    kcb_set_param(&e, KCB_P_KICK_LEVEL, 0.f);
    render_beat(&e, bass, beat);
    kcb_init(&e, SR);
    render_beat(&e, mix, beat);
    snprintf(path, sizeof path, "%s/seam.csv", outdir);
    FILE *f = fopen(path, "w");
    fprintf(f, "i,kick,bass,mix,kick_end,bass_start\n");
    for (int i = 0; i < beat; i++)
        fprintf(f, "%d,%f,%f,%f,%d,%d\n", i, kick[i], bass[i], mix[i], kend, bstart);
    fclose(f);
    printf("Kick-Ende: Sample %d (%.2f ms), Bass-Start: Sample %d (%.2f ms)\n",
           kend, kend * 1000.0 / SR, bstart, bstart * 1000.0 / SR);

    /* CPU: 60 s Audio in 128er-Blöcken */
    kcb_init(&e, SR);
    int n60 = (int)SR * 60, done = 0;
    float blk[BLOCK];
    clock_t t0 = clock();
    while (done < n60) {
        if (done % beat < BLOCK) kcb_trigger(&e, 60, 127);
        kcb_render(&e, blk, BLOCK);
        done += BLOCK;
    }
    double sec = (double)(clock() - t0) / CLOCKS_PER_SEC;
    printf("CPU: 60 s Audio in %.3f s gerechnet (%.2f %% Echtzeit auf diesem Rechner)\n",
           sec, sec / 60.0 * 100.0);

    free(demo);
    return fails ? 1 : 0;
}
