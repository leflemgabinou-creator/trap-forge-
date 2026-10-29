#pragma once
// Thèmes : une palette par famille de sons. Les teintes de l'illustration sont remappées pixel par pixel
// (rouge -> couleur principale, bleu -> couleur secondaire) sans toucher aux orange / jaune / blanc.
#include <algorithm>
#include <cmath>
#include "PresetGenerator.h"

namespace TF
{
struct Palette { const char* name; float primaryHue, secondaryHue, satP, satS; };

// Palettes modifiables ici (teintes en degrés, saturations relatives)
inline const Palette& paletteFor (int family)
{
    static const Palette t[F_count] = {
        { "Bells",    215.f, 185.f, 1.00f, 1.00f },   // bleu électrique, cyan, reflets violets
        { "Plucks",   172.f, 205.f, 0.95f, 0.90f },   // turquoise, bleu clair
        { "Leads",    335.f, 220.f, 1.00f, 1.00f },   // rouge néon, magenta, bleu électrique
        { "Pads",     275.f, 235.f, 0.95f, 1.00f },   // violet, indigo
        { "Keys",      28.f, 225.f, 1.00f, 0.95f },   // ambre, orange, bleu nuit
        { "Piano",     42.f, 212.f, 0.45f, 0.60f },   // ivoire, bleu acier
        { "Rhodes",   315.f, 190.f, 0.75f, 0.85f },   // violet doux, rose, cyan
        { "Textures", 165.f, 255.f, 0.90f, 0.95f },   // vert turquoise, violet
        { "Arps",     190.f, 220.f, 1.00f, 1.00f },   // cyan lumineux, bleu électrique
        { "Synth Bass", 348.f, 250.f, 0.95f, 1.00f }, // rouge sombre, violet, bleu profond
        { "808",        8.f, 215.f, 1.00f, 1.00f },   // rouge/orange agressif, bleu électrique
        { "Claps",    355.f, 215.f, 1.00f, 1.00f },   // rouge vif, blanc, bleu (teintes d'origine)
        { "Snares",   355.f, 215.f, 1.00f, 1.00f },
        { "Kicks",     20.f, 235.f, 1.00f, 0.60f },   // orange, rouge sombre
        { "Hi-hats",  195.f, 205.f, 0.40f, 0.70f },   // argent, cyan
        { "Percs",     35.f, 170.f, 0.95f, 0.90f },
    };
    return t[std::max (0, std::min ((int) F_count - 1, family))];
}

namespace Theme
{
constexpr float baseRedHue = 355.f, baseBlueHue = 218.f;

inline void rgb2hsv (float r, float g, float b, float& h, float& s, float& v)
{
    const float mx = std::max ({ r, g, b }), mn = std::min ({ r, g, b }), d = mx - mn;
    v = mx; s = mx <= 0.f ? 0.f : d / mx;
    if (d <= 1e-6f) { h = 0.f; return; }
    if (mx == r) h = 60.f * std::fmod ((g - b) / d, 6.f);
    else if (mx == g) h = 60.f * ((b - r) / d + 2.f);
    else h = 60.f * ((r - g) / d + 4.f);
    if (h < 0.f) h += 360.f;
}
inline void hsv2rgb (float h, float s, float v, float& r, float& g, float& b)
{
    h = std::fmod (std::fmod (h, 360.f) + 360.f, 360.f);
    const float c = v * s, x = c * (1.f - std::abs (std::fmod (h / 60.f, 2.f) - 1.f)), m = v - c;
    float R = 0, G = 0, B = 0;
    if (h < 60) { R = c; G = x; } else if (h < 120) { R = x; G = c; } else if (h < 180) { G = c; B = x; }
    else if (h < 240) { G = x; B = c; } else if (h < 300) { R = x; B = c; } else { R = c; B = x; }
    r = R + m; g = G + m; b = B + m;
}

// Transformation déterministe d'une couleur : les pixels peu saturés, sombres, orange/jaune/vert restent identiques.
inline void remap (float& r, float& g, float& b, const Palette& pal)
{
    float h, s, v; rgb2hsv (r, g, b, h, s, v);
    if (s < 0.22f || v < 0.10f) return;
    const float dRed = pal.primaryHue - baseRedHue, dBlue = pal.secondaryHue - baseBlueHue;
    float shift, sat;
    if (h >= 330.f || h < 25.f)  { shift = dRed;  sat = pal.satP; }
    else if (h >= 180.f && h < 255.f) { shift = dBlue; sat = pal.satS; }
    else if (h >= 255.f && h < 330.f) { const float t = (h - 255.f) / 75.f; shift = dBlue + (dRed - dBlue) * t; sat = pal.satS + (pal.satP - pal.satS) * t; }
    else return;
    // l'orange/jaune (25..180) n'est pas touché : accents conservés
    hsv2rgb (h + shift, std::min (1.f, s * sat), v, r, g, b);
}

inline void primary (const Palette& p, float& r, float& g, float& b)   { hsv2rgb (p.primaryHue, std::min (1.f, 0.88f * p.satP + 0.05f), 1.f, r, g, b); }
inline void secondary (const Palette& p, float& r, float& g, float& b) { hsv2rgb (p.secondaryHue, std::min (1.f, 0.85f * p.satS + 0.1f), 1.f, r, g, b); }
}
}
