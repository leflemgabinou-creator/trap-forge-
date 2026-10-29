#pragma once
// Générateur de drums : synthèse de vrais échantillons (808, kicks, snares, claps, hi-hats, percussions/FX).
#include <juce_audio_basics/juce_audio_basics.h>
#include "PresetGenerator.h"

namespace TF
{
struct DrumParams { float tone = 0.5f, length = 0.5f, drive = 0.5f, bright = 0.5f; };   // 0.5 = neutre (macros 1..4)

struct DrumResult
{
    juce::String name;
    juce::AudioBuffer<float> audio;   // 1 ou 2 canaux
    double sampleRate = 44100.0;
    int family = 0, sub = 0;
    juce::int64 seed = 0;
    int rootNote = 36;                // pour les 808 : note jouée à la hauteur d'origine
    bool pitchFollow = false;         // true : la hauteur suit les notes MIDI
};

// Génère un échantillon. Même (famille, sous-catégorie, graine, paramètres) = même son.
DrumResult generateDrum (int family, int sub, juce::int64 seed, double sampleRate, const DrumParams& dp = {});

DrumParams drumParamsFromMacros (const ParamVec& v);
}
