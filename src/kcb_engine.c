#include "kcb_engine.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define KCB_PI     3.14159265358979323846
#define KCB_TWO_PI 6.28318530717958647692
#define BASS_FADE  64           /* Samples Ausblendung am Notenende */
#define KICK_FADE  16           /* Mikro-Fade am Kick-Ende (Cut-Modus) */

const char *const kcb_pattern_names[4] = {
    "KBBB Rolling", "K-BB Galopp", "K-B- Offbeat", "KBB Triole"
};
static const char *const note_names[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

const kcb_param_info kcb_params[KCB_NUM_PARAMS] = {
    { "root",        "Tonart",      "",    0,   11,   5,    12 },
    { "octave",      "Bass Oktave", "",    0,   1,    0,    2  },
    { "key_follow",  "Key Follow",  "",    0,   1,    0,    2  },
    { "bpm",         "Tempo",       "BPM", 100, 180,  145,  0  },
    { "pattern",     "Pattern",     "",    0,   3,    0,    4  },
    { "gap",         "Gap",         "ms",  0,   10,   0,    0  },
    { "kick_pitch",  "Kick Pitch",  "Hz",  60,  400,  220,  0  },
    { "kick_sweep",  "Kick Sweep",  "ms",  5,   80,   22,   0  },
    { "kick_decay",  "Kick Decay",  "ms",  50,  400,  180,  0  },
    { "kick_click",  "Click",       "",    0,   1,    0.5f, 0  },
    { "kick_drive",  "Kick Drive",  "",    0,   1,    0.3f, 0  },
    { "kick_level",  "Kick Level",  "",    0,   1,    0.9f, 0  },
    { "bass_cutoff", "Cutoff",      "",    0,   1,    0.35f,0  },
    { "bass_reso",   "Resonanz",    "",    0,   1,    0.3f, 0  },
    { "bass_envmod", "Env Mod",     "",    0,   1,    0.5f, 0  },
    { "bass_decay",  "Bass Decay",  "ms",  20,  200,  70,   0  },
    { "bass_level",  "Bass Level",  "",    0,   1,    0.7f, 0  },
    { "lock",        "Phase Lock",  "",    0,   1,    0,    2  },
    { "sync",        "Sync",        "",    0,   1,    0,    2  },
};

/* Bass-Einsätze in 16tel-Einheiten ab Kick */
static const float pat_starts[4][KCB_MAX_BASS] = {
    { 1.f, 2.f, 3.f, 0.f },           /* K B B B */
    { 2.f, 3.f, 0.f, 0.f },           /* K - B B */
    { 2.f, 0.f, 0.f, 0.f },           /* K - B - */
    { 4.f / 3.f, 8.f / 3.f, 0.f, 0.f } /* K B B Triole */
};
static const int pat_count[4] = { 3, 2, 1, 2 };

static double midi_hz(int note) { return 440.0 * pow(2.0, (note - 69) / 12.0); }

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

void kcb_init(kcb_engine *e, float sr)
{
    memset(e, 0, sizeof(*e));
    e->sr = sr;
    e->midi_root = -1;
    e->rng = 0x12345678u;
    for (int i = 0; i < KCB_NUM_PARAMS; i++) e->p[i] = kcb_params[i].def;
}

void kcb_set_param(kcb_engine *e, int idx, float v)
{
    if (idx < 0 || idx >= KCB_NUM_PARAMS) return;
    const kcb_param_info *pi = &kcb_params[idx];
    v = clampf(v, pi->min, pi->max);
    if (pi->steps > 0) v = floorf(v + 0.5f);
    e->p[idx] = v;
}

float kcb_get_param(const kcb_engine *e, int idx)
{
    return (idx >= 0 && idx < KCB_NUM_PARAMS) ? e->p[idx] : 0.f;
}

void kcb_set_param_norm(kcb_engine *e, int idx, float v01)
{
    if (idx < 0 || idx >= KCB_NUM_PARAMS) return;
    const kcb_param_info *pi = &kcb_params[idx];
    kcb_set_param(e, idx, pi->min + clampf(v01, 0.f, 1.f) * (pi->max - pi->min));
}

float kcb_get_param_norm(const kcb_engine *e, int idx)
{
    if (idx < 0 || idx >= KCB_NUM_PARAMS) return 0.f;
    const kcb_param_info *pi = &kcb_params[idx];
    return (e->p[idx] - pi->min) / (pi->max - pi->min);
}

void kcb_param_text(const kcb_engine *e, int idx, char *buf, int len)
{
    if (idx < 0 || idx >= KCB_NUM_PARAMS || len <= 0) return;
    int v = (int)e->p[idx];
    switch (idx) {
    case KCB_P_ROOT:       snprintf(buf, len, "%s", note_names[v]); break;
    case KCB_P_OCTAVE:     snprintf(buf, len, "%s", v ? "+1" : "0"); break;
    case KCB_P_KEY_FOLLOW: snprintf(buf, len, "%s", v ? "An" : "Aus"); break;
    case KCB_P_PATTERN:    snprintf(buf, len, "%s", kcb_pattern_names[v]); break;
    case KCB_P_LOCK:       snprintf(buf, len, "%s", v ? "Cut" : "Sweep-Lock"); break;
    case KCB_P_SYNC:       snprintf(buf, len, "%s", v ? "Intern" : "Host"); break;
    default:
        if (kcb_params[idx].unit[0])
            snprintf(buf, len, "%.1f %s", e->p[idx], kcb_params[idx].unit);
        else
            snprintf(buf, len, "%d%%", (int)(e->p[idx] * 100.f + 0.5f));
    }
}

/* Phase der Kick nach `window` Samples bei gegebener Startfrequenz.
 * Exakt dieselbe Rekursion wie im Renderer. */
static double kick_phase_at(const kcb_engine *e, double kstart, int window,
                            double *env_sum, int *last_cross)
{
    double ph = 0.0, env = 1.0, es = 0.0;
    long cyc = 0;
    int last = 0;
    for (int i = 0; i < window; i++) {
        double f = e->kroot + (kstart - e->kroot) * env;
        es += env;
        ph += f / e->sr;
        env *= e->kenv_coef;
        if ((long)ph > cyc) { cyc = (long)ph; last = i + 1; }
    }
    if (env_sum) *env_sum = es / e->sr;
    if (last_cross) *last_cross = last;
    return ph;
}

/* Sweep-Lock: Startfrequenz der Pitch-Hüllkurve minimal so nachstimmen, dass
 * die Kick nach GENAU `window` Samples eine ganze Zahl Zyklen vollendet hat.
 * Phase(W) = kroot*W/sr + (kstart-kroot)*S  ->  nach kstart auflösen.
 * Cut: Kick endet am letzten Nulldurchgang vor dem Fenster. */
static void plan_kick(kcb_engine *e, int window)
{
    double S;
    int last;
    double P = kick_phase_at(e, e->kstart, window, &S, &last);
    e->kstart_eff = e->kstart;
    e->kick_end = last > 0 ? last : window;

    if (e->p[KCB_P_LOCK] < 0.5f && S > 0.0) {
        double base = e->kroot * window / e->sr;
        double n = floor(P + 0.5);
        double ks = e->kroot + (n - base) / S;
        if (ks < e->kroot) { n = ceil(P); ks = e->kroot + (n - base) / S; }
        if (ks >= e->kroot && ks <= 2.0 * e->kstart + 50.0) {
            e->kstart_eff = ks;
            e->kick_end = window;
        }
    }
}

void kcb_set_host_bpm(kcb_engine *e, float bpm)
{
    e->host_bpm = (bpm >= 20.f && bpm <= 400.f) ? bpm : 0.f;
}

float kcb_effective_bpm(const kcb_engine *e)
{
    if (e->p[KCB_P_SYNC] < 0.5f && e->host_bpm > 0.f) return e->host_bpm;
    return e->p[KCB_P_BPM];
}

void kcb_trigger(kcb_engine *e, int note, int vel)
{
    const float sr = e->sr;
    if (e->p[KCB_P_KEY_FOLLOW] > 0.5f && note >= 0) e->midi_root = note % 12;
    int root = (e->p[KCB_P_KEY_FOLLOW] > 0.5f && e->midi_root >= 0)
                   ? e->midi_root : (int)e->p[KCB_P_ROOT];

    /* Timing */
    double t16 = sr * 60.0 / kcb_effective_bpm(e) / 4.0;
    int pat = (int)e->p[KCB_P_PATTERN];
    e->beat_len = (int)lround(4.0 * t16);
    e->n_bass = pat_count[pat];
    for (int i = 0; i < e->n_bass; i++)
        e->bass_start[i] = (int)lround(pat_starts[pat][i] * t16);
    for (int i = 0; i < e->n_bass; i++)
        e->bass_end[i] = (i + 1 < e->n_bass) ? e->bass_start[i + 1] : e->beat_len;

    /* Kick: Pitch-Hüllkurve endet auf dem Grundton (Oktave 1) */
    e->kroot = midi_hz(24 + root);
    e->kstart = e->p[KCB_P_KICK_PITCH] > e->kroot ? e->p[KCB_P_KICK_PITCH] : e->kroot;
    e->kenv_coef = exp(-1.0 / (e->p[KCB_P_KICK_SWEEP] * 0.001 * sr));
    e->kenv = 1.0;
    e->kphase = 0.0;
    e->kamp = 1.f;
    e->kamp_coef = expf(-1.f / (e->p[KCB_P_KICK_DECAY] * 0.001f * sr));

    int gap = (int)lroundf(e->p[KCB_P_GAP] * 0.001f * sr);
    int window = e->bass_start[0] - gap;
    if (window < 1) window = 1;
    plan_kick(e, window);

    /* Click */
    e->click_env = 1.f;
    e->click_coef = expf(-1.f / (0.0015f * sr));
    e->click_phase = 0.0;
    e->click_lp = 0.f;

    /* Bass */
    e->bfreq = midi_hz(24 + root + 12 * (int)e->p[KCB_P_OCTAVE]);
    e->benv_coef = expf(-1.f / (e->p[KCB_P_BASS_DECAY] * 0.001f * sr));
    e->fenv_coef = expf(-1.f / (e->p[KCB_P_BASS_DECAY] * 0.0005f * sr));
    e->bass_idx = 0;
    e->bass_active = 0;

    e->vel = (vel < 1 ? 1 : (vel > 127 ? 127 : vel)) / 127.f;
    e->pos = 0;
    e->running = 1;
}

static inline float polyblep(double t, double dt)
{
    if (t < dt) { t /= dt; return (float)(t + t - t * t - 1.0); }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt; return (float)(t * t + t + t + 1.0); }
    return 0.f;
}

static inline float noise(kcb_engine *e)
{
    uint32_t x = e->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    e->rng = x;
    return (float)(int32_t)x * (1.f / 2147483648.f);
}

void kcb_render(kcb_engine *e, float *out, int n)
{
    const float sr = e->sr;
    const float drive_g = 1.f + 9.f * e->p[KCB_P_KICK_DRIVE];
    const float drive_n = 1.f / tanhf(drive_g);
    const float click_amt = e->p[KCB_P_KICK_CLICK];
    const float k_lvl = e->p[KCB_P_KICK_LEVEL];
    const float b_lvl = e->p[KCB_P_BASS_LEVEL];
    const float cut_oct = e->p[KCB_P_BASS_CUTOFF] * 7.f;
    const float env_oct = e->p[KCB_P_BASS_ENVMOD] * 6.f;
    const float kres = 2.f - 1.9f * e->p[KCB_P_BASS_RESO];
    const float fc_max = 0.45f * sr;

    for (int i = 0; i < n; i++) {
        float y = 0.f;
        if (e->running) {
            /* ---- Kick ---- */
            if (e->pos < e->kick_end) {
                float s = (float)sin(KCB_TWO_PI * e->kphase) * e->kamp;
                int kr = e->kick_end - e->pos;
                float kf = kr < KICK_FADE ? (float)kr / KICK_FADE : 1.f;
                y += k_lvl * tanhf(drive_g * s) * drive_n * kf;
                double f = e->kroot + (e->kstart_eff - e->kroot) * e->kenv;
                e->kphase += f / sr;
                e->kenv *= e->kenv_coef;
                e->kamp *= e->kamp_coef;
            }
            /* ---- Click (Transient am Beatanfang) ---- */
            if (click_amt > 0.f && e->click_env > 1e-4f) {
                float nz = noise(e);
                e->click_lp += 0.25f * (nz - e->click_lp);
                float c = 0.5f * (nz - e->click_lp)
                        + 0.5f * (float)sin(KCB_TWO_PI * e->click_phase);
                e->click_phase += 2500.0 / sr;
                y += k_lvl * click_amt * e->click_env * c;
                e->click_env *= e->click_coef;
            }
            /* ---- Bass: Notenstart mit Phase-Reset ---- */
            if (e->bass_idx < e->n_bass && e->pos == e->bass_start[e->bass_idx]) {
                e->bphase = 0.0;
                e->benv = 1.f;
                e->fenv = 1.f;
                e->ic1 = e->ic2 = 0.f;
                e->bass_cur_end = e->bass_end[e->bass_idx];
                e->bass_active = 1;
                e->bass_idx++;
            }
            if (e->bass_active) {
                double dt = e->bfreq / sr;
                double t = e->bphase + 0.5;       /* Saw startet bei 0, steigend */
                t -= floor(t);
                float x = (float)(2.0 * t - 1.0) - polyblep(t, dt);
                e->bphase += dt;

                float fc = 40.f * exp2f(cut_oct + env_oct * e->fenv);
                if (fc > fc_max) fc = fc_max;
                float g = tanf((float)KCB_PI * fc / sr);
                float a1 = 1.f / (1.f + g * (g + kres));
                float a2 = g * a1, a3 = g * a2;
                float v3 = x - e->ic2;
                float v1 = a1 * e->ic1 + a2 * v3;
                float v2 = e->ic2 + a2 * e->ic1 + a3 * v3;
                e->ic1 = 2.f * v1 - e->ic1;
                e->ic2 = 2.f * v2 - e->ic2;

                int remain = e->bass_cur_end - e->pos;
                float fade = remain < BASS_FADE ? (float)remain / BASS_FADE : 1.f;
                y += b_lvl * v2 * e->benv * fade;
                e->benv *= e->benv_coef;
                e->fenv *= e->fenv_coef;
                if (remain <= 1) e->bass_active = 0;
            }
            if (++e->pos >= e->beat_len) e->running = 0;
        }
        out[i] = y * e->vel;
    }
}
