#pragma once
// Table unique de tous les paramètres sonores : sert au plugin (automation), au générateur, aux presets et aux tests.
#include <juce_core/juce_core.h>
#include <array>
#include <cmath>
#include <algorithm>

// kind : 0 = réel, 1 = choix, 2 = interrupteur
// X (id, nom, kind, min, max, défaut, centre (courbure, 0 = linéaire), pas)
#define TF_PARAMS(X) \
    X (wave1,   "Wave 1",      1, 0, 3, 0,      0,    1)     \
    X (o1,      "Osc 1",       0, 0, 1, 0.8f,   0,    0.001f) \
    X (wave2,   "Wave 2",      1, 0, 3, 2,      0,    1)     \
    X (o2,      "Osc 2",       0, 0, 1, 0,      0,    0.001f) \
    X (semi,    "Semitones",   0, -24, 24, 0,   0,    1)     \
    X (det,     "Detune",      0, -50, 50, 0,   0,    0.1f)  \
    X (sub,     "Sub",         0, 0, 1, 0,      0,    0.001f) \
    X (noise,   "Noise",       0, 0, 1, 0,      0,    0.001f) \
    X (fm,      "FM",          0, 0, 1, 0,      0,    0.001f) \
    X (fmratio, "FM Ratio",    0, 0.5f, 8, 2,   0,    0.01f) \
    X (ftype,   "Filter Type", 1, 0, 2, 0,      0,    1)     \
    X (cutoff,  "Cutoff",      0, 20, 20000, 8000, 1000, 1)  \
    X (res,     "Resonance",   0, 0, 1, 0.1f,   0,    0.001f) \
    X (fenv,    "Filter Env",  0, -1, 1, 0,     0,    0.001f) \
    X (fdec,    "Filter Decay",0, 0.01f, 2, 0.3f, 0.4f, 0.001f) \
    X (a,       "Attack",      0, 0.001f, 2, 0.002f, 0.1f, 0.001f) \
    X (d,       "Decay",       0, 0.01f, 4, 0.5f, 0.6f, 0.001f) \
    X (s,       "Sustain",     0, 0, 1, 0.6f,   0,    0.001f) \
    X (r,       "Release",     0, 0.01f, 4, 0.3f, 0.6f, 0.001f) \
    X (penv,    "Pitch Env",   0, 0, 48, 0,     0,    0.1f)  \
    X (pdec,    "Pitch Decay", 0, 0.005f, 0.5f, 0.05f, 0.08f, 0.001f) \
    X (glide,   "Glide",       0, 0, 0.4f, 0,   0.1f, 0.001f) \
    X (mono,    "Mono",        2, 0, 1, 0,      0,    1)     \
    X (lfoRate, "LFO Rate",    0, 0.1f, 12, 4,  3,    0.01f) \
    X (lfoPitch,"LFO Pitch",   0, 0, 1, 0,      0,    0.001f) \
    X (lfoFilter,"LFO Filter", 0, 0, 1, 0,      0,    0.001f) \
    X (lfoAmp,  "LFO Amp",     0, 0, 1, 0,      0,    0.001f) \
    X (lfoPan,  "LFO Pan",     0, 0, 1, 0,      0,    0.001f) \
    X (drive,   "Drive",       0, 0, 1, 0,      0,    0.001f) \
    X (dly,     "Delay",       0, 0, 1, 0.15f,  0,    0.001f) \
    X (cho,     "Chorus",      0, 0, 1, 0.25f,  0,    0.001f) \
    X (cmp,     "Compressor",  0, 0, 1, 0.3f,   0,    0.001f) \
    X (rvb,     "Reverb",      0, 0, 1, 0.2f,   0,    0.001f) \
    X (rsize,   "Room Size",   0, 0, 1, 0.5f,   0,    0.001f) \
    X (wid,     "Width",       0, 0, 1, 0.5f,   0,    0.001f) \
    X (eqLo,    "EQ Low",      0, -12, 12, 0,   0,    0.1f)  \
    X (eqMid,   "EQ Mid",      0, -12, 12, 0,   0,    0.1f)  \
    X (eqHi,    "EQ High",     0, -12, 12, 0,   0,    0.1f)  \
    X (master,  "Master",      0, -24, 6, 0,    0,    0.1f)  \
    X (voices,  "Voices",      0, 1, 16, 16,    0,    1)     \
    X (dlyOn,   "Delay On",    2, 0, 1, 1,      0,    1)     \
    X (choOn,   "Chorus On",   2, 0, 1, 1,      0,    1)     \
    X (cmpOn,   "Comp On",     2, 0, 1, 1,      0,    1)     \
    X (rvbOn,   "Reverb On",   2, 0, 1, 1,      0,    1)     \
    X (widOn,   "Width On",    2, 0, 1, 1,      0,    1)     \
    X (eqOn,    "EQ On",       2, 0, 1, 1,      0,    1)     \
    X (m1,      "Macro 1",     0, 0, 1, 0.5f,   0,    0.001f) \
    X (m2,      "Macro 2",     0, 0, 1, 0.5f,   0,    0.001f) \
    X (m3,      "Macro 3",     0, 0, 1, 0.5f,   0,    0.001f) \
    X (m4,      "Macro 4",     0, 0, 1, 0.5f,   0,    0.001f)

namespace TF
{
enum PIdx
{
#define X(id, ...) P_##id,
    TF_PARAMS (X)
#undef X
    P_count
};

struct PInfo { const char* id; const char* name; int kind; float lo, hi, def, centre, step; };

inline const PInfo* pinfo()
{
    static const PInfo t[] = {
#define X(id, name, kind, lo, hi, def, c, st) { #id, name, kind, (float) (lo), (float) (hi), (float) (def), (float) (c), (float) (st) },
        TF_PARAMS (X)
#undef X
    };
    return t;
}

using ParamVec = std::array<float, P_count>;

inline ParamVec defaultParams()
{
    ParamVec v {};
    for (int i = 0; i < P_count; ++i) v[(size_t) i] = pinfo()[i].def;
    return v;
}

inline float clampParam (int idx, float x) { return std::min (pinfo()[idx].hi, std::max (pinfo()[idx].lo, x)); }

inline int findParam (const juce::String& id)
{
    for (int i = 0; i < P_count; ++i)
        if (id == pinfo()[i].id) return i;
    return -1;
}

inline const char* waveName (int i) { static const char* n[] = { "Sine", "Tri", "Saw", "Square" }; return n[std::max (0, std::min (3, i))]; }
inline const char* filterName (int i) { static const char* n[] = { "LP", "HP", "BP" }; return n[std::max (0, std::min (2, i))]; }
}
