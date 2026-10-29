#pragma once
// Générateur procédural de presets : recettes de timbres par famille et sous-catégorie, graine reproductible.
// Aucune dépendance à l'interface ni au plugin.
#include <juce_core/juce_core.h>
#include "ParamTable.h"

namespace TF
{
enum Family { F_Bells, F_Plucks, F_Leads, F_Pads, F_Keys, F_Piano, F_Rhodes, F_Textures, F_Arps, F_Bass,
              F_808, F_Claps, F_Snares, F_Kicks, F_HiHats, F_Percs, F_count };

struct FamilyInfo { const char* name; bool drum; std::vector<const char*> subs; };
const FamilyInfo& familyInfo (int family);

struct GenControls
{
    float variation = 0.5f, complexity = 0.5f, brightness = 0.5f, movement = 0.5f,
          width = 0.5f, character = 0.5f, intensity = 0.5f;
};

struct Generated
{
    juce::String name;
    ParamVec p = defaultParams();
    int family = 0, sub = 0;
    juce::int64 seed = 0;
};

// sub = -1 : sous-catégorie choisie par la graine
Generated generatePreset (int family, int sub, juce::int64 seed, const GenControls& c = {});

// Applique les 4 macros (Timbre, Mouvement, Caractère, Filtre) selon la famille. 0.5 = neutre.
void applyMacros (ParamVec& v, int family);
const char* macroLabel (int family, int macro);
}
