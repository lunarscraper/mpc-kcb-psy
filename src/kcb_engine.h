/*
 * KCB Psy – Kick/Click/Bass-Generator mit phasengenauer Kopplung
 *
 * Host-unabhängige DSP-Engine (C99, keine Abhängigkeiten außer libm).
 * Gedacht zum Einbetten in einen VST2-/Schwung-plugin_api_v2-Wrapper
 * (mpc-vst-plugins) für die Akai Force.
 *
 * Prinzip:
 *   - Jeder MIDI-Note-On startet einen Beat (Kick auf 1, Bass laut Pattern).
 *   - Der Kick-Tail endet auf dem Grundton der Tonart.
 *   - Die Kick wird nach einer GANZEN Zahl von Schwingungen beendet, also im
 *     Nulldurchgang, bevor der erste Basston einsetzt.
 *   - Jeder Basston startet mit Oszillator-Phase-Reset (Wert 0, steigende Flanke).
 *   => Kick und Bass überlappen nie, der Übergang ist lückenlos phasenkohärent.
 */
#ifndef KCB_ENGINE_H
#define KCB_ENGINE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Reihenfolge = VST-Parameterindex. NUR ANHÄNGEN, NIE UMSORTIEREN
 * (Skins und gespeicherte Projekte binden an den Index). */
enum {
    KCB_P_ROOT = 0,     /* Tonart 0..11 (C..B), Default F               */
    KCB_P_OCTAVE,       /* Bass-Oktave 0 / +1                           */
    KCB_P_KEY_FOLLOW,   /* 0 = Tonart-Parameter, 1 = MIDI-Note setzt Ton */
    KCB_P_BPM,          /* 100..180                                     */
    KCB_P_PATTERN,      /* 0..3, siehe kcb_pattern_names                */
    KCB_P_GAP,          /* Luft vor dem ersten Bass, ms                 */
    KCB_P_KICK_PITCH,   /* Startfrequenz der Pitch-Hüllkurve, Hz        */
    KCB_P_KICK_SWEEP,   /* Zeitkonstante Pitch-Hüllkurve, ms            */
    KCB_P_KICK_DECAY,   /* Amp-Decay Kick, ms                           */
    KCB_P_KICK_CLICK,   /* Click-Anteil 0..1                            */
    KCB_P_KICK_DRIVE,   /* Sättigung 0..1                               */
    KCB_P_KICK_LEVEL,   /* 0..1                                         */
    KCB_P_BASS_CUTOFF,  /* 0..1                                         */
    KCB_P_BASS_RESO,    /* 0..1                                         */
    KCB_P_BASS_ENVMOD,  /* 0..1                                         */
    KCB_P_BASS_DECAY,   /* ms                                           */
    KCB_P_BASS_LEVEL,   /* 0..1                                         */
    KCB_P_LOCK,         /* 0 = Sweep-Lock, 1 = Cut (siehe README)       */
    KCB_P_SYNC,         /* 0 = Host-Tempo, 1 = BPM-Parameter            */
    KCB_NUM_PARAMS
};

typedef struct {
    const char *key;    /* stabiler Schlüssel (chain_params / Chunks) */
    const char *name;   /* Anzeigename                                */
    const char *unit;
    float min, max, def;
    int steps;          /* >0: diskrete Optionen, 0: kontinuierlich   */
} kcb_param_info;

extern const kcb_param_info kcb_params[KCB_NUM_PARAMS];
extern const char *const kcb_pattern_names[4];

#define KCB_MAX_BASS 4

typedef struct {
    float sr;
    float p[KCB_NUM_PARAMS];
    int midi_root;              /* -1 = keine Note empfangen */
    float host_bpm;             /* 0 = unbekannt */

    /* Beat-Ablauf */
    int running;
    int pos;
    int beat_len;
    int n_bass, bass_idx;
    int bass_start[KCB_MAX_BASS];
    int bass_end[KCB_MAX_BASS];
    int kick_end;               /* erstes Sample, an dem die Kick schweigt */
    float vel;

    /* Kick */
    double kphase, kenv, kenv_coef, kroot, kstart, kstart_eff;
    float kamp, kamp_coef;
    float click_env, click_coef, click_lp;
    double click_phase;
    uint32_t rng;

    /* Bass */
    int bass_active, bass_cur_end;
    double bphase, bfreq;
    float benv, benv_coef, fenv, fenv_coef;
    float ic1, ic2;
} kcb_engine;

void  kcb_init(kcb_engine *e, float sample_rate);
void  kcb_set_param(kcb_engine *e, int idx, float value);   /* reale Einheit */
float kcb_get_param(const kcb_engine *e, int idx);
void  kcb_set_param_norm(kcb_engine *e, int idx, float v01); /* 0..1 (VST) */
float kcb_get_param_norm(const kcb_engine *e, int idx);
void  kcb_param_text(const kcb_engine *e, int idx, char *buf, int len);

/* Tempo vom Host (MPC) melden; wirkt bei Sync = Host ab dem nächsten Beat. */
void  kcb_set_host_bpm(kcb_engine *e, float bpm);
float kcb_effective_bpm(const kcb_engine *e);

/* Beat starten. note: MIDI-Note (für Key Follow), vel: 1..127 */
void  kcb_trigger(kcb_engine *e, int note, int vel);

/* n Mono-Samples schreiben (überschreibt out). Für sampleakkurates MIDI
 * den Block am Event-Offset teilen: render(offset) -> trigger -> render(rest). */
void  kcb_render(kcb_engine *e, float *out, int n);

#ifdef __cplusplus
}
#endif
#endif
