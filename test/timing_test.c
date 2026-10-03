/* Sample-accuracy test for the patched wrapper (vst/wrap_fix.py): a note-on delivered with
 * deltaFrames = D must start sounding at frame D of that 128-frame host block, not at frame 0.
 * Built against the patched vst2_wrap.c by vst/test.sh; PASSED/FAILED. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct AEffect AEffect;
typedef intptr_t (*audioMasterCallback)(AEffect *, int32_t, int32_t, intptr_t, void *, float);
struct AEffect {
    int32_t magic;
    intptr_t (*d)(AEffect *, int32_t, int32_t, intptr_t, void *, float);
    void (*process)(AEffect *, float **, float **, int32_t);
    void (*setP)(AEffect *, int32_t, float);
    float (*getP)(AEffect *, int32_t);
    int32_t numPrograms, numParams, numInputs, numOutputs, flags;
    intptr_t resvd1, resvd2;
    int32_t initialDelay, realQualities, offQualities;
    float ioRatio;
    void *object, *user;
    int32_t uniqueID, version;
    void (*pr)(AEffect *, float **, float **, int32_t);
    void (*prd)(AEffect *, double **, double **, int32_t);
    char future[56];
};
typedef struct { int32_t type, byteSize, deltaFrames, flags, noteLength, noteOffset; unsigned char midiData[4]; char detune, noteOffVelocity, r1, r2; } VstMidiEvent;
typedef struct { int32_t numEvents; intptr_t reserved; void *events[2]; } VstEvents;
AEffect *VSTPluginMain(audioMasterCallback);
static intptr_t master(AEffect *e, int32_t op, int32_t i, intptr_t v, void *p, float o) { (void)e; (void)op; (void)i; (void)v; (void)p; (void)o; return 0; }

static int onset_after(int delta, int *ok) {
    AEffect *a = VSTPluginMain(master);
    float l[128], r[128], *out[2] = {l, r};
    for (int b = 0; b < 20; b++) a->pr(a, 0, out, 128);          /* settle: silence */
    VstMidiEvent m = {.type = 1, .byteSize = sizeof m, .deltaFrames = delta, .midiData = {0x90, 41, 120, 0}};
    VstEvents ev = {.numEvents = 1, .events = {&m, 0}};
    a->d(a, 25, 0, 0, &ev, 0);
    a->pr(a, 0, out, 128);
    int first = -1;
    for (int i = 0; i < 128; i++) if (fabsf(l[i]) > 1e-4f || fabsf(r[i]) > 1e-4f) { first = i; break; }
    *ok = first >= 0;
    a->d(a, 1, 0, 0, 0, 0);
    return first;
}

int main(void) {
    int fails = 0, ok;
    int deltas[] = {0, 17, 64, 100, 127};
    for (unsigned k = 0; k < sizeof deltas / sizeof *deltas; k++) {
        int f = onset_after(deltas[k], &ok);
        printf("deltaFrames %3d -> first audible frame %3d\n", deltas[k], f);
        /* the first sample AT the event can itself be ~0 (a sine starts at 0): allow 2 frames */
        if (!ok || f < deltas[k] || f > deltas[k] + 2) { fails++; printf("FAIL\n"); }
    }
    printf(fails ? "FAILED\n" : "PASSED\n");
    return fails != 0;
}
