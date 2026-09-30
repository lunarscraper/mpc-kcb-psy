/* KCB Psy -> mpc-vst-plugins engine interface (wrapper/engine.h).
 * Vertrag: 44,1 kHz, int16 stereo interleaved, 128-Frame-Blöcke, Parameter als Key/Value-Strings.
 * Die Keys müssen zu vst/params.json passen; die VST-Reihenfolge bestimmt allein params.json. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "engine.h"
#include "kcb_engine.h"

#define MAX_FRAMES 512

/* Key -> Engine-Parameter. scale: Faktor vom Wrapper-Wert zur Engine-Einheit
 * (Prozent-Regler 0..100 -> 0..1). Optionen kommen als Index-String. */
typedef struct { const char *key; int idx; float scale; } map_t;
static const map_t MAP[] = {
    { "pattern",     KCB_P_PATTERN,     1.f   },
    { "root",        KCB_P_ROOT,        1.f   },
    { "octave",      KCB_P_OCTAVE,      1.f   },
    { "key_follow",  KCB_P_KEY_FOLLOW,  1.f   },
    { "lock",        KCB_P_LOCK,        1.f   },
    { "gap",         KCB_P_GAP,         1.f   },
    { "sync",        KCB_P_SYNC,        1.f   },
    { "bpm",         KCB_P_BPM,         1.f   },
    { "kick_pitch",  KCB_P_KICK_PITCH,  1.f   },
    { "kick_sweep",  KCB_P_KICK_SWEEP,  1.f   },
    { "kick_decay",  KCB_P_KICK_DECAY,  1.f   },
    { "kick_click",  KCB_P_KICK_CLICK,  0.01f },
    { "kick_drive",  KCB_P_KICK_DRIVE,  0.01f },
    { "kick_level",  KCB_P_KICK_LEVEL,  0.01f },
    { "bass_cutoff", KCB_P_BASS_CUTOFF, 0.01f },
    { "bass_reso",   KCB_P_BASS_RESO,   0.01f },
    { "bass_envmod", KCB_P_BASS_ENVMOD, 0.01f },
    { "bass_decay",  KCB_P_BASS_DECAY,  1.f   },
    { "bass_level",  KCB_P_BASS_LEVEL,  0.01f },
};
#define NMAP ((int)(sizeof MAP / sizeof MAP[0]))

typedef struct {
    kcb_engine e;
    float buf[MAX_FRAMES];
} inst_t;

static const map_t *find(const char *key)
{
    for (int i = 0; i < NMAP; i++)
        if (!strcmp(MAP[i].key, key)) return &MAP[i];
    return NULL;
}

static void *create(const char *data_dir)
{
    (void)data_dir;
    inst_t *s = calloc(1, sizeof *s);
    if (s) kcb_init(&s->e, 44100.f);
    return s;
}

static void destroy(void *inst) { free(inst); }

static void midi(void *inst, const uint8_t *msg, int len)
{
    inst_t *s = inst;
    if (len < 3) return;
    if ((msg[0] & 0xF0) == 0x90 && msg[2] > 0) kcb_trigger(&s->e, msg[1], msg[2]);
    /* Note-Off wird ignoriert: ein Beat läuft immer vollständig aus. */
}

static void set_one(inst_t *s, const char *key, const char *val)
{
    if (!strcmp(key, "lfo_bpm")) { kcb_set_host_bpm(&s->e, (float)atof(val)); return; }
    const map_t *m = find(key);
    if (m) kcb_set_param(&s->e, m->idx, (float)atof(val) * m->scale);
}

/* state = "key=value;key=value;..." (alle Parameter, Werte in Wrapper-Einheiten) */
static void set_state(inst_t *s, const char *str)
{
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s", str);
    for (char *tok = strtok(tmp, ";"); tok; tok = strtok(NULL, ";")) {
        char *eq = strchr(tok, '=');
        if (!eq) continue;
        *eq = 0;
        if (strcmp(tok, "lfo_bpm")) set_one(s, tok, eq + 1);
    }
}

static void set_param(void *inst, const char *key, const char *val)
{
    inst_t *s = inst;
    if (!strcmp(key, "state")) set_state(s, val);
    else set_one(s, key, val);
}

static int get_param(void *inst, const char *key, char *buf, int buf_len)
{
    inst_t *s = inst;
    if (!strcmp(key, "state")) {
        int n = 0;
        buf[0] = 0;
        for (int i = 0; i < NMAP && n < buf_len - 1; i++) {
            float v = kcb_get_param(&s->e, MAP[i].idx) / MAP[i].scale;
            n += snprintf(buf + n, buf_len - n, "%s=%g;", MAP[i].key, (double)v);
        }
        return n < buf_len ? n : buf_len - 1;
    }
    const map_t *m = find(key);
    if (!m) return 0;
    float v = kcb_get_param(&s->e, m->idx) / m->scale;
    if (kcb_params[m->idx].steps > 0) return snprintf(buf, buf_len, "%d", (int)lroundf(v));
    return snprintf(buf, buf_len, "%g", (double)v);
}

static inline int16_t f2s(float f)
{
    f *= 32767.f;
    return f >= 32767.f ? 32767 : (f <= -32768.f ? -32768 : (int16_t)lrintf(f));
}

static void render(void *inst, int16_t *out_lr, int frames)
{
    inst_t *s = inst;
    while (frames > 0) {
        int n = frames > MAX_FRAMES ? MAX_FRAMES : frames;
        kcb_render(&s->e, s->buf, n);
        for (int i = 0; i < n; i++) {
            int16_t v = f2s(s->buf[i] * 0.8f);   /* 2 dB Headroom für Click + Drive */
            out_lr[2 * i] = v;
            out_lr[2 * i + 1] = v;
        }
        out_lr += 2 * n;
        frames -= n;
    }
}

static const mpc_engine_t ENGINE = { create, destroy, midi, set_param, get_param, render, NULL };
const mpc_engine_t *mpc_engine(void) { return &ENGINE; }
