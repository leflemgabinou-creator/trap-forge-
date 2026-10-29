#include "PresetGenerator.h"

namespace TF
{
const FamilyInfo& familyInfo (int f)
{
    static const FamilyInfo t[F_count] = {
        { "Bells",    false, { "Soft", "Dark", "Metallic", "Glassy" } },
        { "Plucks",   false, { "Short", "Bright", "Warm", "Muted" } },
        { "Leads",    false, { "Nasal", "Smooth", "Aggressive", "Airy" } },
        { "Pads",     false, { "Ambient", "Dark", "Wide", "Dreamy" } },
        { "Keys",     false, { "Electric", "Soft", "Vintage", "Digital" } },
        { "Piano",    false, { "Soft", "Dark", "Bright", "Layered" } },
        { "Rhodes",   false, { "Warm", "Chorus", "Soft", "Mellow" } },
        { "Textures", false, { "Air", "Dust", "Glass", "Dark" } },
        { "Arps",     false, { "Pluck", "Bell", "Bright", "Soft" } },
        { "Synth Bass", false, { "Clean", "Distorted", "Deep", "Moving" } },
        { "808",      true,  { "Deep", "Short", "Long", "Saturated", "Aggressive", "Metallic", "Clean", "Pitch Drop" } },
        { "Claps",    true,  { "Dry", "Wide", "Bright", "Dark", "Metallic", "Saturated", "Layered" } },
        { "Snares",   true,  { "Short", "Long", "Metallic", "Dry", "Aggressive", "Electronic" } },
        { "Kicks",    true,  { "Deep", "Short", "Punchy", "Saturated", "Electronic" } },
        { "Hi-hats",  true,  { "Closed", "Open", "Short", "Long", "Metallic", "Bright", "Dark" } },
        { "Percs",    true,  { "Rim", "Tom", "Perc", "Impact", "Riser", "Downlifter" } },
    };
    return t[std::max (0, std::min ((int) F_count - 1, f))];
}

const char* macroLabel (int family, int m)
{
    static const char* melodic[4] = { "Timbre", "Movement", "Character", "Filter" };
    static const char* drum[4]    = { "Tone", "Length", "Drive", "Brightness" };
    return familyInfo (family).drum ? drum[m & 3] : melodic[m & 3];
}

namespace
{
struct Gen
{
    juce::Random r;
    ParamVec v = defaultParams();
    float var;

    Gen (juce::int64 seed, float variation) : r (seed), var (variation) {}
    float R (float a, float b) { const float mid = 0.5f * (a + b), half = 0.5f * (b - a) * (0.4f + 1.2f * var); return mid + (r.nextFloat() * 2.f - 1.f) * half; }
    void set (int idx, float x) { v[(size_t) idx] = clampParam (idx, x); }
    void setR (int idx, float a, float b) { set (idx, R (a, b)); }
    float pick (std::initializer_list<float> l) { return *(l.begin() + r.nextInt ((int) l.size())); }
    float& operator[] (int i) { return v[(size_t) i]; }
    void mul (int idx, float k) { set (idx, v[(size_t) idx] * k); }
    void add (int idx, float x) { set (idx, v[(size_t) idx] + x); }
};

void bells (Gen& g, int sub)
{
    g.set (P_wave1, 0); g.set (P_o1, 0.8f); g.set (P_wave2, g.pick ({ 0, 1 })); g.setR (P_o2, 0.25f, 0.5f);
    g.set (P_semi, g.pick ({ 12, 19, 24, 7 })); g.setR (P_fm, 0.25f, 0.7f); g.set (P_fmratio, g.pick ({ 2, 3, 3.5f, 4, 5, 7 }));
    g.setR (P_cutoff, 4000, 12000); g.set (P_res, 0.1f); g.set (P_a, 0.001f); g.setR (P_d, 0.6f, 1.8f); g.set (P_s, 0.f); g.setR (P_r, 0.5f, 1.5f);
    g.setR (P_dly, 0.15f, 0.35f); g.setR (P_rvb, 0.25f, 0.45f); g.setR (P_rsize, 0.5f, 0.9f);
    switch (sub)
    {
        case 0: g.mul (P_fm, 0.6f); g.mul (P_cutoff, 0.7f); g.mul (P_d, 0.8f); break;
        case 1: g.setR (P_cutoff, 1200, 3000); g.mul (P_d, 1.4f); g.add (P_rvb, 0.1f); g.set (P_fmratio, g.pick ({ 2, 3 })); break;
        case 2: g.setR (P_fm, 0.6f, 0.9f); g.set (P_fmratio, g.pick ({ 3.5f, 5, 7, 8 })); g.setR (P_cutoff, 9000, 16000); break;
        default: g.set (P_fmratio, g.pick ({ 5, 7, 8 })); g.setR (P_fm, 0.3f, 0.5f); g.set (P_wave2, 1); g.set (P_semi, 24); g.set (P_dly, 0.4f); g.set (P_cho, 0.35f); break;
    }
}
void plucks (Gen& g, int sub)
{
    g.set (P_wave1, 2); g.set (P_o1, 0.7f); g.set (P_wave2, g.pick ({ 2, 3 })); g.setR (P_o2, 0.25f, 0.5f); g.set (P_semi, g.pick ({ 0, 0, 12 })); g.setR (P_det, 5, 14);
    g.setR (P_cutoff, 500, 2200); g.setR (P_res, 0.15f, 0.4f); g.setR (P_fenv, 0.5f, 0.9f); g.setR (P_fdec, 0.08f, 0.3f);
    g.set (P_a, 0.001f); g.setR (P_d, 0.2f, 0.5f); g.set (P_s, 0.f); g.setR (P_r, 0.12f, 0.35f); g.setR (P_rvb, 0.12f, 0.3f); g.setR (P_drive, 0.f, 0.2f);
    switch (sub)
    {
        case 0: g.mul (P_d, 0.5f); g.mul (P_r, 0.6f); break;
        case 1: g.mul (P_cutoff, 2.2f); g.mul (P_fdec, 0.7f); break;
        case 2: g.mul (P_cutoff, 0.6f); g.set (P_wave2, 1); g.add (P_cho, 0.15f); break;
        default: g.mul (P_cutoff, 0.45f); g.mul (P_d, 0.7f); g.set (P_res, 0.1f); break;
    }
}
void leads (Gen& g, int sub)
{
    g.set (P_wave1, g.pick ({ 2, 3 })); g.set (P_o1, 0.65f); g.set (P_wave2, g.pick ({ 2, 3 })); g.set (P_o2, 0.45f); g.setR (P_det, 6, 14);
    g.setR (P_cutoff, 1800, 7000); g.setR (P_res, 0.15f, 0.4f); g.setR (P_fenv, 0.2f, 0.6f); g.setR (P_fdec, 0.15f, 0.5f);
    g.set (P_a, 0.003f); g.setR (P_d, 0.25f, 0.7f); g.setR (P_s, 0.5f, 0.8f); g.setR (P_r, 0.15f, 0.4f); g.setR (P_glide, 0.02f, 0.1f); g.set (P_mono, 1);
    g.setR (P_drive, 0.15f, 0.45f); g.setR (P_rvb, 0.1f, 0.25f); g.setR (P_lfoPitch, 0.1f, 0.25f); g.setR (P_lfoRate, 4.5f, 6.5f);
    switch (sub)
    {
        case 0: g.set (P_wave1, 3); g.set (P_res, 0.45f); g.setR (P_cutoff, 1200, 2600); g.set (P_o2, 0.2f); break;
        case 1: g.set (P_wave1, 1); g.set (P_wave2, 2); g.mul (P_drive, 0.3f); g.mul (P_cutoff, 0.7f); break;
        case 2: g.set (P_wave1, 2); g.add (P_drive, 0.3f); g.add (P_res, 0.15f); g.set (P_det, 14); break;
        default: g.set (P_wave1, 1); g.set (P_o2, 0.3f); g.mul (P_cutoff, 1.6f); g.set (P_noise, 0.04f); g.add (P_rvb, 0.2f); break;
    }
}
void pads (Gen& g, int sub)
{
    g.set (P_wave1, 2); g.set (P_o1, 0.6f); g.set (P_wave2, g.pick ({ 2, 1 })); g.set (P_o2, 0.55f); g.setR (P_det, 8, 22); g.setR (P_sub, 0.f, 0.25f);
    g.setR (P_cutoff, 500, 1800); g.setR (P_res, 0.05f, 0.2f); g.setR (P_fenv, 0.1f, 0.4f); g.setR (P_fdec, 0.8f, 1.8f);
    g.setR (P_a, 0.4f, 1.2f); g.set (P_d, 1.2f); g.setR (P_s, 0.7f, 0.95f); g.setR (P_r, 1.f, 2.8f);
    g.setR (P_rvb, 0.3f, 0.55f); g.setR (P_rsize, 0.7f, 0.95f); g.setR (P_lfoFilter, 0.1f, 0.3f); g.setR (P_lfoRate, 0.2f, 0.8f); g.setR (P_lfoPan, 0.1f, 0.3f); g.set (P_cho, 0.4f);
    switch (sub)
    {
        case 0: break;
        case 1: g.mul (P_cutoff, 0.6f); g.add (P_rvb, 0.1f); break;
        case 2: g.set (P_wid, 0.85f); g.set (P_cho, 0.6f); g.set (P_det, 20); break;
        default: g.set (P_wave2, 1); g.mul (P_cutoff, 1.4f); g.set (P_a, 1.4f); g.set (P_dly, 0.35f); break;
    }
}
void keys (Gen& g, int sub)
{
    g.set (P_wave1, g.pick ({ 0, 1 })); g.set (P_o1, 0.8f); g.set (P_wave2, 1); g.setR (P_o2, 0.2f, 0.4f); g.set (P_semi, g.pick ({ 12, 0 }));
    g.setR (P_fm, 0.1f, 0.35f); g.set (P_fmratio, g.pick ({ 1, 2, 3, 0.5f })); g.setR (P_cutoff, 2500, 6000); g.set (P_res, 0.1f); g.setR (P_fenv, 0.f, 0.3f); g.set (P_fdec, 0.4f);
    g.set (P_a, 0.002f); g.setR (P_d, 0.6f, 1.4f); g.setR (P_s, 0.1f, 0.3f); g.setR (P_r, 0.3f, 0.8f); g.setR (P_rvb, 0.18f, 0.38f); g.setR (P_drive, 0.f, 0.15f);
    switch (sub)
    {
        case 0: g.set (P_wave1, 0); g.mul (P_fm, 1.5f); break;
        case 1: g.mul (P_cutoff, 0.6f); g.mul (P_fm, 0.5f); break;
        case 2: g.set (P_wave1, 1); g.add (P_cho, 0.2f); g.add (P_drive, 0.1f); break;
        default: g.set (P_wave1, 3); g.mul (P_cutoff, 0.5f); g.set (P_o2, 0.3f); break;
    }
}
void piano (Gen& g, int sub)
{
    g.set (P_wave1, 1); g.set (P_o1, 0.8f); g.set (P_wave2, 0); g.set (P_o2, 0.3f); g.set (P_semi, 12); g.setR (P_fm, 0.1f, 0.2f); g.set (P_fmratio, g.pick ({ 1, 2 }));
    g.setR (P_cutoff, 3000, 6000); g.setR (P_fenv, 0.2f, 0.4f); g.setR (P_fdec, 0.3f, 0.6f); g.set (P_a, 0.002f); g.setR (P_d, 0.9f, 1.6f); g.setR (P_s, 0.02f, 0.1f); g.setR (P_r, 0.3f, 0.6f);
    g.setR (P_rvb, 0.15f, 0.3f); g.set (P_cho, 0.1f); g.setR (P_penv, 0.5f, 1.5f); g.set (P_pdec, 0.02f);
    switch (sub)
    {
        case 0: g.mul (P_cutoff, 0.7f); g.mul (P_fm, 0.6f); break;
        case 1: g.mul (P_cutoff, 0.45f); g.mul (P_d, 1.2f); break;
        case 2: g.mul (P_cutoff, 1.8f); g.mul (P_fm, 1.4f); break;
        default: g.set (P_o2, 0.5f); g.set (P_wave2, 2); g.set (P_det, 7); g.add (P_cho, 0.2f); break;
    }
}
void rhodes (Gen& g, int sub)
{
    g.set (P_wave1, 0); g.set (P_o1, 0.8f); g.set (P_wave2, 1); g.set (P_o2, 0.25f); g.set (P_fmratio, 1); g.setR (P_fm, 0.15f, 0.3f);
    g.setR (P_cutoff, 2500, 4500); g.set (P_a, 0.002f); g.setR (P_d, 1.2f, 2.2f); g.setR (P_s, 0.15f, 0.3f); g.setR (P_r, 0.4f, 0.9f);
    g.setR (P_cho, 0.35f, 0.6f); g.setR (P_rvb, 0.2f, 0.35f); g.setR (P_lfoAmp, 0.1f, 0.3f); g.setR (P_lfoRate, 3.5f, 5.5f); g.setR (P_lfoPan, 0.2f, 0.4f);
    switch (sub)
    {
        case 0: g.mul (P_cutoff, 0.8f); break;
        case 1: g.set (P_cho, 0.8f); break;
        case 2: g.mul (P_fm, 0.5f); g.mul (P_cutoff, 0.6f); break;
        default: g.mul (P_cutoff, 0.5f); g.set (P_lfoAmp, 0.4f); break;
    }
}
void textures (Gen& g, int sub)
{
    g.set (P_wave1, g.pick ({ 1, 2 })); g.set (P_o1, 0.4f); g.set (P_wave2, 2); g.set (P_o2, 0.3f); g.setR (P_det, 10, 30); g.setR (P_noise, 0.15f, 0.35f);
    g.setR (P_cutoff, 400, 1400); g.setR (P_res, 0.2f, 0.4f); g.setR (P_fenv, -0.3f, 0.3f); g.setR (P_fdec, 1.0f, 1.8f);
    g.setR (P_a, 1.0f, 2.0f); g.set (P_d, 2.0f); g.setR (P_s, 0.7f, 0.9f); g.setR (P_r, 2.f, 3.5f);
    g.setR (P_lfoFilter, 0.3f, 0.6f); g.setR (P_lfoRate, 0.15f, 0.6f); g.setR (P_lfoPan, 0.3f, 0.6f); g.setR (P_rvb, 0.5f, 0.75f); g.set (P_rsize, 0.9f); g.setR (P_cho, 0.4f, 0.6f);
    switch (sub)
    {
        case 0: g.mul (P_cutoff, 2.f); g.mul (P_noise, 1.5f); break;
        case 1: g.set (P_noise, 0.4f); g.mul (P_cutoff, 0.7f); break;
        case 2: g.set (P_wave1, 0); g.set (P_fm, 0.5f); g.set (P_fmratio, g.pick ({ 5, 7 })); g.mul (P_cutoff, 3.f); break;
        default: g.mul (P_cutoff, 0.5f); g.set (P_sub, 0.3f); break;
    }
}
void arps (Gen& g, int sub)
{
    plucks (g, 0);
    g.setR (P_d, 0.12f, 0.3f); g.setR (P_r, 0.08f, 0.2f); g.setR (P_dly, 0.3f, 0.5f); g.setR (P_rvb, 0.15f, 0.3f); g.set (P_cho, 0.3f);
    switch (sub)
    {
        case 1: g.set (P_wave1, 0); g.setR (P_fm, 0.3f, 0.6f); g.set (P_fmratio, g.pick ({ 3, 5, 7 })); g.set (P_fenv, 0.f); g.mul (P_cutoff, 3.f); break;
        case 2: g.mul (P_cutoff, 2.5f); break;
        case 3: g.set (P_wave1, 1); g.mul (P_cutoff, 0.7f); break;
        default: break;
    }
}
void bass (Gen& g, int sub)
{
    g.set (P_mono, 1); g.set (P_wave1, g.pick ({ 2, 3 })); g.set (P_o1, 0.7f); g.set (P_wave2, 2); g.setR (P_o2, 0.2f, 0.4f); g.set (P_semi, -12); g.setR (P_sub, 0.5f, 0.8f);
    g.setR (P_cutoff, 200, 900); g.setR (P_res, 0.2f, 0.4f); g.setR (P_fenv, 0.4f, 0.8f); g.setR (P_fdec, 0.15f, 0.4f);
    g.set (P_a, 0.002f); g.setR (P_d, 0.3f, 0.6f); g.setR (P_s, 0.5f, 0.8f); g.setR (P_r, 0.1f, 0.3f); g.setR (P_drive, 0.2f, 0.5f); g.setR (P_glide, 0.02f, 0.06f);
    g.set (P_rvb, 0.05f); g.set (P_dly, 0.f); g.set (P_cho, 0.f); g.set (P_wid, 0.3f);
    switch (sub)
    {
        case 0: g.mul (P_drive, 0.3f); g.set (P_wave1, 2); break;
        case 1: g.set (P_drive, 0.85f); g.add (P_res, 0.1f); break;
        case 2: g.set (P_wave1, 0); g.set (P_o2, 0.f); g.set (P_sub, 0.8f); g.mul (P_cutoff, 0.7f); break;
        default: g.set (P_lfoFilter, 0.4f); g.set (P_lfoRate, 2.5f); g.set (P_lfoPan, 0.f); break;
    }
}

const char* adj[] = { "Midnight", "Blue", "Dark", "Neon", "Frozen", "Red", "Velvet", "Glass", "Ghost", "Silver", "Cold", "Heavy", "Smoke", "Night", "Hazy", "Wicked", "Phantom", "Electric", "Crimson", "Lunar" };
const char* single[F_count] = { "Bell", "Pluck", "Lead", "Pad", "Keys", "Piano", "Rhodes", "Texture", "Arp", "Bass", "808", "Clap", "Snare", "Kick", "Hat", "Perc" };
}

void applyMacros (ParamVec& v, int family)
{
    if (familyInfo (family).drum) return;   // drums : les macros pilotent le générateur de samples
    const int grp = (family == F_Pads || family == F_Textures) ? 1 : (family == F_Leads || family == F_Bass) ? 2 : 0;
    auto M = [&] (int i) { return (v[(size_t) (P_m1 + i)] - 0.5f) * 2.f; };
    auto add = [&] (int idx, float amt) { v[(size_t) idx] = clampParam (idx, v[(size_t) idx] + amt); };

    const float m1 = M (0), m2 = M (1), m3 = M (2), m4 = M (3);
    if (grp == 0)      { add (P_fm, 0.4f * m1); add (P_o2, 0.25f * m1);  add (P_lfoPan, 0.35f * m2); add (P_lfoFilter, 0.3f * m2); add (P_lfoPitch, 0.1f * m2);  add (P_drive, 0.35f * m3); add (P_det, 8.f * m3); add (P_cho, 0.2f * m3); }
    else if (grp == 1) { add (P_o2, 0.3f * m1);  add (P_det, 15.f * m1); add (P_noise, 0.1f * m1); add (P_lfoFilter, 0.5f * m2); add (P_lfoPan, 0.4f * m2); add (P_drive, 0.2f * m3); add (P_cho, 0.3f * m3); add (P_wid, 0.2f * m3); }
    else               { add (P_o2, 0.3f * m1);  add (P_sub, 0.3f * m1);   add (P_fm, 0.2f * m1); add (P_lfoPitch, 0.15f * m2); add (P_glide, 0.05f * m2); add (P_lfoFilter, 0.3f * m2); add (P_drive, 0.5f * m3); add (P_det, 6.f * m3); }
    v[P_cutoff] = clampParam (P_cutoff, v[P_cutoff] * std::exp2 (3.f * m4));
    add (P_res, 0.25f * m4); add (P_fenv, 0.3f * m4);
}

Generated generatePreset (int family, int sub, juce::int64 seed, const GenControls& c)
{
    family = std::max (0, std::min ((int) F_count - 1, family));
    const auto& fi = familyInfo (family);
    Gen g (seed, c.variation);
    if (sub < 0 || sub >= (int) fi.subs.size()) sub = g.r.nextInt ((int) fi.subs.size());

    if (! fi.drum)
    {
        switch (family)
        {
            case F_Bells: bells (g, sub); break;    case F_Plucks: plucks (g, sub); break;  case F_Leads: leads (g, sub); break;
            case F_Pads: pads (g, sub); break;      case F_Keys: keys (g, sub); break;      case F_Piano: piano (g, sub); break;
            case F_Rhodes: rhodes (g, sub); break;  case F_Textures: textures (g, sub); break;
            case F_Arps: arps (g, sub); break;      default: bass (g, sub); break;
        }
        // contrôles globaux du générateur
        g.mul (P_cutoff, std::exp2 ((c.brightness - 0.5f) * 3.f));
        g.add (P_o2, (c.complexity - 0.5f) * 0.4f);  g.add (P_fm, (c.complexity - 0.5f) * 0.3f);  g.add (P_noise, (c.complexity - 0.5f) * 0.1f);
        g.add (P_lfoPan, (c.movement - 0.5f) * 0.6f); g.add (P_lfoFilter, (c.movement - 0.5f) * 0.5f); g.add (P_lfoPitch, std::max (0.f, c.movement - 0.5f) * 0.2f);
        g.set (P_wid, g[P_wid] == 0.5f ? 0.2f + 0.7f * c.width : 0.5f * (g[P_wid] + 0.2f + 0.7f * c.width));
        g.add (P_cho, (c.width - 0.5f) * 0.3f);
        g.add (P_drive, (c.character - 0.5f) * 0.6f); g.add (P_det, (c.character - 0.5f) * 10.f);
        g.add (P_fenv, (c.intensity - 0.5f) * 0.4f);  g.add (P_res, (c.intensity - 0.5f) * 0.2f);
    }

    for (int i = 0; i < 4; ++i) g[P_m1 + i] = 0.5f;

    Generated out;
    out.p = g.v; out.family = family; out.sub = sub; out.seed = seed;
    out.name = juce::String (adj[g.r.nextInt (20)]) + " " + single[family];
    return out;
}
}
