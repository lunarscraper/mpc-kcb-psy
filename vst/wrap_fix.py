#!/usr/bin/env python3
"""Build-time patch for a COPY of mpc-vst-plugins' wrapper/vst2_wrap.c (never the checkout itself):
SAMPLE-ACCURATE NOTE STARTS for instruments.

Upstream hands every MIDI event to the engine as soon as effProcessEvents delivers it and renders
whole 128-frame blocks, so a note always starts at the block start: up to 127 frames (~2.9 ms) early or
late against the grid. MPC passes each event's position inside the block (VstMidiEvent.deltaFrames);
this patch keeps it, renders the block up to that frame, applies the event, renders the rest.
No added latency; effects (PLUG_EFFECT) are untouched.

Diagnostics: the first 300 note-ons are logged to /tmp/kcb_delta.log as "delta=<frames> note=<n>".
If every delta is 0, MPC itself quantises to blocks and the patch changes nothing (but costs nothing).

Usage: wrap_fix.py <mpc-vst-plugins copy>. Fails loudly if upstream changed the patched lines."""
import os, sys
p = os.path.join(sys.argv[1], "wrapper", "vst2_wrap.c")
s = open(p).read()
fixes = [
    # 1. per-instance event queue
    ("    char chunk[8192];\n} wrap_t;\n",
     "    char chunk[8192];\n"
     "    /* kcb wrap_fix.py: MIDI queued with its in-block position, applied mid-block */\n"
     "    struct { uint8_t m[3]; int32_t delta; } evq[128];\n"
     "    int evn;\n"
     "} wrap_t;\n"),
    # 2. log + sub-block renderer, before render_frames()
    ("static void render_frames(wrap_t *w, float **out, int32_t n, int accumulate) {\n",
     "static void delta_log(int32_t delta, const unsigned char *m) {\n"
     "    static FILE *f; static int count;\n"
     "    if ((m[0] & 0xF0) != 0x90 || !m[2] || count >= 300) return;\n"
     "    if (!f && !(f = fopen(\"/tmp/kcb_delta.log\", \"a\"))) return;\n"
     "    fprintf(f, \"delta=%d note=%d\\n\", (int)delta, m[1]);\n"
     "    if (++count % 8 == 0 || count == 300) fflush(f);\n"
     "}\n"
     "/* Render one engine block starting at host frame `base` of this processReplacing() call, applying\n"
     " * queued events at their own frame: render up to the event, apply it, continue. */\n"
     "static void render_block_events(wrap_t *w, int32_t base) {\n"
     "    int cur = 0;\n"
     "    for (;;) {\n"
     "        int best = -1;\n"
     "        for (int k = 0; k < w->evn; k++)   /* earliest event due in this block (stable: first wins) */\n"
     "            if (w->evq[k].delta - base < DSP_BLOCK && (best < 0 || w->evq[k].delta < w->evq[best].delta)) best = k;\n"
     "        if (best < 0) break;\n"
     "        int off = w->evq[best].delta - base;\n"
     "        if (off < cur) off = cur;\n"
     "        if (off > cur) { g_api->render(w->dsp, w->block + cur * 2, off - cur); cur = off; }\n"
     "        g_api->midi(w->dsp, w->evq[best].m, 3);\n"
     "        w->evq[best] = w->evq[--w->evn];\n"
     "    }\n"
     "    if (cur < DSP_BLOCK) g_api->render(w->dsp, w->block + cur * 2, DSP_BLOCK - cur);\n"
     "}\n\n"
     "static void render_frames(wrap_t *w, float **out, int32_t n, int accumulate) {\n"),
    # 3. render_frames: use it
    ("        if (w->pos >= DSP_BLOCK) {\n            g_api->render(w->dsp, w->block, DSP_BLOCK);\n            w->pos = 0;\n        }\n",
     "        if (w->pos >= DSP_BLOCK) {\n            render_block_events(w, i);\n            w->pos = 0;\n        }\n"),
    # 4. instrument run_block: events not yet due carry over into the next call
    ("    render_frames(w, out, n, accumulate);\n}\n",
     "    render_frames(w, out, n, accumulate);\n"
     "    for (int k = 0; k < w->evn; k++) { w->evq[k].delta -= n; if (w->evq[k].delta < 0) w->evq[k].delta = 0; }\n"
     "}\n"),
    # 5. effProcessEvents: queue instead of applying at once (instruments only)
    ("                VstMidiEvent *m = (VstMidiEvent *)ev->events[i];\n                g_api->midi(w->dsp, m->midiData, 3);\n",
     "                VstMidiEvent *m = (VstMidiEvent *)ev->events[i];\n"
     "#ifndef PLUG_EFFECT\n"
     "                delta_log(m->deltaFrames, m->midiData);\n"
     "                if (w->evn < 128) {\n"
     "                    memcpy(w->evq[w->evn].m, m->midiData, 3);\n"
     "                    w->evq[w->evn].delta = m->deltaFrames < 0 ? 0 : m->deltaFrames;\n"
     "                    w->evn++;\n"
     "                } else g_api->midi(w->dsp, m->midiData, 3);\n"
     "#else\n"
     "                g_api->midi(w->dsp, m->midiData, 3);\n"
     "#endif\n"),
]
missing = [i + 1 for i, (a, _) in enumerate(fixes) if a not in s]
if missing:
    sys.exit("wrap_fix: upstream vst2_wrap.c changed (fix %s not applicable) -- update vst/wrap_fix.py" % missing)
for a, b in fixes:
    s = s.replace(a, b, 1)
open(p, "w").write(s)
print("wrap_fix: sample-accurate MIDI patched into", p)
