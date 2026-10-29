#include "DrumGenerator.h"
#include <juce_dsp/juce_dsp.h>

namespace TF
{
namespace
{
constexpr float kTwoPi = 6.2831853f;

struct Svf   // filtre à variable d'état (mono) basé sur JUCE
{
    juce::dsp::StateVariableTPTFilter<float> f;
    Svf (double sr, juce::dsp::StateVariableTPTFilterType t, float cutoff, float q = 0.7f)
    { f.prepare ({ sr, 512, 1 }); f.setType (t); f.setCutoffFrequency (std::min (cutoff, (float) (0.45 * sr))); f.setResonance (q); }
    float operator() (float x) { return f.processSample (0, x); }
};
using T = juce::dsp::StateVariableTPTFilterType;

float sat (float x, float drive) { const float g = 1.f + drive; return std::tanh (g * x) / std::tanh (g); }

void finish (juce::AudioBuffer<float>& b, double sr, float fadeMs)
{
    float peak = 0.f;
    for (int c = 0; c < b.getNumChannels(); ++c) peak = std::max (peak, b.getMagnitude (c, 0, b.getNumSamples()));
    if (peak > 1e-6f) b.applyGain (0.89f / peak);   // -1 dBFS
    const int fade = std::min (b.getNumSamples(), (int) (sr * fadeMs * 0.001));
    for (int c = 0; c < b.getNumChannels(); ++c)
    {
        b.applyGainRamp (c, b.getNumSamples() - fade, fade, 1.f, 0.f);
        b.applyGainRamp (c, 0, std::min (b.getNumSamples(), 16), 0.f, 1.f);   // évite le clic de départ
    }
}

struct Noise { juce::Random r; explicit Noise (juce::int64 s) : r (s) {} float operator()() { return r.nextFloat() * 2.f - 1.f; } };

// ------------------------------------------------------------------ 808 / kick (sinus + chute de hauteur)
void kickLike (juce::AudioBuffer<float>& out, double sr, juce::Random& r, bool is808, int sub, const DrumParams& dp, float& f0Out)
{
    float f0, mult, tauP, decay, drive, harm, click, metal = 0.f;
    if (is808)
    {
        f0 = 38.f + r.nextFloat() * 17.f; mult = 1.5f + r.nextFloat() * 2.5f; tauP = 0.015f + r.nextFloat() * 0.045f;
        decay = 0.9f + r.nextFloat() * 0.8f; drive = 1.5f + r.nextFloat(); harm = 0.1f + r.nextFloat() * 0.3f; click = 0.15f + r.nextFloat() * 0.3f;
        switch (sub)
        {
            case 0: f0 = 36.f + r.nextFloat() * 6.f; decay = 1.4f + r.nextFloat() * 0.5f; mult = 1.3f + r.nextFloat() * 0.7f; break;   // deep
            case 1: decay = 0.25f + r.nextFloat() * 0.2f; break;                                                                       // short
            case 2: decay = 1.9f + r.nextFloat() * 0.8f; break;                                                                        // long
            case 3: drive = 6.f + r.nextFloat() * 3.f; break;                                                                          // saturated
            case 4: drive = 4.f + r.nextFloat() * 3.f; click = 0.5f; mult = 3.f + r.nextFloat() * 2.f; break;                          // aggressive
            case 5: metal = 0.12f + r.nextFloat() * 0.1f; drive = 3.f; break;                                                          // metallic
            case 6: drive = 0.4f; harm = 0.05f; break;                                                                                 // clean
            default: mult = 4.f + r.nextFloat() * 2.f; tauP = 0.05f + r.nextFloat() * 0.04f; break;                                    // pitch drop
        }
    }
    else
    {
        f0 = 46.f + r.nextFloat() * 16.f; mult = 2.f + r.nextFloat() * 2.f; tauP = 0.025f + r.nextFloat() * 0.025f;
        decay = 0.25f + r.nextFloat() * 0.3f; drive = 1.f + r.nextFloat() * 2.f; harm = 0.1f; click = 0.3f + r.nextFloat() * 0.3f;
        switch (sub)
        {
            case 0: f0 = 40.f + r.nextFloat() * 6.f; decay = 0.55f + r.nextFloat() * 0.3f; break;     // deep
            case 1: decay = 0.12f + r.nextFloat() * 0.08f; break;                                    // short
            case 2: click = 0.7f; mult = 3.f + r.nextFloat() * 2.f; break;                           // punchy
            case 3: drive = 5.f + r.nextFloat() * 3.f; break;                                        // saturated
            default: mult = 4.f; tauP = 0.02f; harm = 0.25f; break;                                  // electronic
        }
    }
    f0 *= std::exp2 ((dp.tone - 0.5f) * 0.6f);
    decay *= std::exp2 ((dp.length - 0.5f) * 2.f);
    drive *= std::exp2 ((dp.drive - 0.5f) * 2.5f);
    f0Out = f0;

    const float total = std::min (3.2f, decay * 2.2f + 0.05f);
    const int n = (int) (total * (float) sr);
    out.setSize (1, n); out.clear();
    auto* o = out.getWritePointer (0);
    Noise nz (r.nextInt64());
    Svf clickHp (sr, T::highpass, 1800.f);
    const float tau = decay / 6.9f;
    float ph = 0.f;
    for (int i = 0; i < n; ++i)
    {
        const float t = (float) i / (float) sr;
        const float fi = f0 * (1.f + (mult - 1.f) * std::exp (-t / tauP));
        ph += fi / (float) sr; ph -= std::floor (ph);
        const float amp = std::exp (-t / tau) * (t < 0.0015f ? t / 0.0015f : 1.f);
        float body = std::sin (kTwoPi * ph) + harm * std::sin (kTwoPi * 2.f * ph) * std::exp (-t / (tau * 0.5f));
        if (metal > 0.f) body += metal * std::sin (kTwoPi * ph * 2.41f) * std::exp (-t / (tau * 0.3f));
        float y = sat (body * amp, drive);
        y += clickHp (nz()) * std::exp (-t / 0.003f) * click * 0.5f;
        o[i] = y;
    }
}

void snare (juce::AudioBuffer<float>& out, double sr, juce::Random& r, int sub, const DrumParams& dp)
{
    float body = 170.f + r.nextFloat() * 60.f, bodyDec = 0.08f + r.nextFloat() * 0.06f, noiseDec = 0.12f + r.nextFloat() * 0.15f;
    float hp = 1500.f + r.nextFloat() * 2000.f, mix = 0.6f + r.nextFloat() * 0.3f, drive = 1.f + r.nextFloat() * 2.f, metal = 0.f;
    switch (sub)
    {
        case 0: noiseDec *= 0.5f; bodyDec *= 0.6f; break;                       // short
        case 1: noiseDec = 0.3f + r.nextFloat() * 0.2f; break;                  // long
        case 2: metal = 0.35f; hp *= 1.5f; break;                               // metallic
        case 3: mix *= 0.7f; noiseDec *= 0.7f; break;                           // dry
        case 4: drive = 4.f + r.nextFloat() * 3.f; break;                       // aggressive
        default: body = 200.f + r.nextFloat() * 80.f; metal = 0.2f; drive = 2.f; break;   // electronic
    }
    body *= std::exp2 ((dp.tone - 0.5f) * 0.6f);
    noiseDec *= std::exp2 ((dp.length - 0.5f) * 1.5f); bodyDec *= std::exp2 ((dp.length - 0.5f) * 1.2f);
    drive *= std::exp2 ((dp.drive - 0.5f) * 2.5f);
    hp *= std::exp2 ((dp.bright - 0.5f) * 1.5f);

    const int n = (int) ((noiseDec * 3.f + 0.05f) * (float) sr);
    out.setSize (1, n); out.clear();
    auto* o = out.getWritePointer (0);
    Noise nz (r.nextInt64());
    Svf f (sr, T::highpass, hp, 0.9f), lp (sr, T::lowpass, 9000.f);
    float p1 = 0.f, p2 = 0.f;
    for (int i = 0; i < n; ++i)
    {
        const float t = (float) i / (float) sr;
        const float fb = body * (1.f + 0.6f * std::exp (-t / 0.012f));
        p1 += fb / (float) sr; p2 += fb * 1.62f / (float) sr;
        float s = (std::sin (kTwoPi * p1) + 0.5f * std::sin (kTwoPi * p2)) * std::exp (-t / bodyDec) * 0.7f;
        float x = nz();
        if (metal > 0.f) x = x * (1.f - metal) + metal * std::sin (kTwoPi * 5200.f * t) * x;
        float nse = lp (f (x)) * std::exp (-t / noiseDec) * mix;
        float y = s + nse + nz() * std::exp (-t / 0.002f) * 0.4f;
        o[i] = sat (y, drive);
    }
}

void clap (juce::AudioBuffer<float>& out, double sr, juce::Random& r, int sub, const DrumParams& dp)
{
    float centre = 1100.f + r.nextFloat() * 1100.f, tail = 0.12f + r.nextFloat() * 0.18f, drive = 1.f + r.nextFloat() * 2.f, gap = 0.009f + r.nextFloat() * 0.005f;
    int bursts = 3 + r.nextInt (3); bool stereo = false; bool layered = false;
    switch (sub)
    {
        case 0: tail *= 0.5f; bursts = 3; break;                                  // dry
        case 1: stereo = true; tail *= 1.3f; break;                               // wide
        case 2: centre *= 1.6f; break;                                            // bright
        case 3: centre *= 0.6f; tail *= 1.2f; break;                              // dark
        case 4: centre *= 1.3f; drive = 3.f; break;                               // metallic
        case 5: drive = 5.f + r.nextFloat() * 3.f; break;                         // saturated
        default: layered = true; bursts = 5; stereo = true; break;                // layered
    }
    centre *= std::exp2 ((dp.tone - 0.5f) * 0.8f + (dp.bright - 0.5f) * 0.8f);
    tail *= std::exp2 ((dp.length - 0.5f) * 1.5f);
    drive *= std::exp2 ((dp.drive - 0.5f) * 2.5f);

    const int ch = stereo ? 2 : 1;
    const int n = (int) ((bursts * gap + tail * 3.f + 0.03f) * (float) sr);
    out.setSize (ch, n); out.clear();
    for (int c = 0; c < ch; ++c)
    {
        Noise nz (r.nextInt64());
        Svf bp (sr, T::bandpass, centre * (c == 0 ? 1.f : 1.08f), 1.1f), hp (sr, T::highpass, 600.f);
        auto* o = out.getWritePointer (c);
        for (int i = 0; i < n; ++i)
        {
            const float t = (float) i / (float) sr;
            float env = 0.f;
            for (int b = 0; b < bursts; ++b) { const float tb = t - (float) b * gap; if (tb >= 0.f) env = std::max (env, std::exp (-tb / 0.004f) * (b == bursts - 1 ? 0.f : 1.f)); }
            const float tt = t - (float) (bursts - 1) * gap;
            if (tt >= 0.f) env = std::max (env, std::exp (-tt / tail) * 0.8f);
            float x = hp (bp (nz())) * env * 3.f;
            if (layered) x += nz() * std::exp (-t / 0.003f) * 0.3f;
            o[i] = sat (x, drive);
        }
    }
}

void hat (juce::AudioBuffer<float>& out, double sr, juce::Random& r, int sub, const DrumParams& dp)
{
    // sub : 0 closed, 1 open, 2 short, 3 long, 4 metallic, 5 bright, 6 dark
    float decay = (sub == 1) ? 0.3f + r.nextFloat() * 0.3f : (sub == 3) ? 0.2f + r.nextFloat() * 0.2f : (sub == 2) ? 0.02f + r.nextFloat() * 0.02f : 0.035f + r.nextFloat() * 0.05f;
    float base = 280.f + r.nextFloat() * 140.f, hp = 6500.f + r.nextFloat() * 2500.f, metal = 0.6f, noiseMix = 0.35f;
    if (sub == 4) { metal = 0.9f; noiseMix = 0.15f; }
    if (sub == 5) { hp *= 1.3f; }
    if (sub == 6) { hp *= 0.55f; base *= 0.8f; }
    base *= std::exp2 ((dp.tone - 0.5f) * 0.6f);
    decay *= std::exp2 ((dp.length - 0.5f) * 1.8f);
    hp *= std::exp2 ((dp.bright - 0.5f) * 1.2f);
    const float drive = 0.5f * std::exp2 ((dp.drive - 0.5f) * 2.5f);

    static const float ratios[6] = { 1.f, 1.4471f, 1.6170f, 1.9265f, 2.5028f, 2.6637f };
    const int n = (int) ((decay * 3.f + 0.02f) * (float) sr);
    out.setSize (1, n); out.clear();
    auto* o = out.getWritePointer (0);
    Noise nz (r.nextInt64());
    Svf h1 (sr, T::highpass, hp), h2 (sr, T::highpass, hp * 0.8f);
    float ph[6] = { 0, 0, 0, 0, 0, 0 };
    for (int i = 0; i < n; ++i)
    {
        const float t = (float) i / (float) sr;
        float s = 0.f;
        for (int k = 0; k < 6; ++k) { ph[k] += base * ratios[k] / (float) sr; ph[k] -= std::floor (ph[k]); s += ph[k] < 0.5f ? 1.f : -1.f; }
        s = (s / 6.f) * metal + nz() * noiseMix;
        const float env = std::exp (-t / decay) * (t < 0.0008f ? t / 0.0008f : 1.f);
        o[i] = sat (h2 (h1 (s)) * env, drive);
    }
}

void perc (juce::AudioBuffer<float>& out, double sr, juce::Random& r, int sub, const DrumParams& dp)
{
    // sub : 0 rim, 1 tom, 2 perc, 3 impact, 4 riser, 5 downlifter
    Noise nz (r.nextInt64());
    const float tone = std::exp2 ((dp.tone - 0.5f) * 0.8f), len = std::exp2 ((dp.length - 0.5f) * 1.6f);
    const float drive = 0.8f * std::exp2 ((dp.drive - 0.5f) * 2.5f);
    if (sub == 4 || sub == 5)
    {
        const float dur = (1.2f + r.nextFloat() * 1.3f) * len;
        const int n = (int) (dur * (float) sr);
        out.setSize (2, n); out.clear();
        for (int c = 0; c < 2; ++c)
        {
            Noise nn (r.nextInt64());
            Svf f (sr, T::bandpass, 300.f, 2.f);
            auto* o = out.getWritePointer (c);
            for (int i = 0; i < n; ++i)
            {
                float u = (float) i / (float) n; if (sub == 5) u = 1.f - u;
                f.f.setCutoffFrequency (std::min (0.45f * (float) sr, (300.f + 9000.f * u * u) * tone * std::exp2 ((dp.bright - 0.5f) * 1.5f)));
                o[i] = sat (f (nn()) * (0.15f + 0.85f * u) * 2.f, drive);
            }
        }
        return;
    }
    float f0 = (sub == 0 ? 900.f : sub == 1 ? 110.f : sub == 2 ? 420.f : 42.f) * tone * (0.9f + 0.2f * r.nextFloat());
    const float dec = (sub == 0 ? 0.05f : sub == 1 ? 0.35f : sub == 2 ? 0.18f : 1.1f) * len;
    const int n = (int) ((dec * 3.f + 0.02f) * (float) sr);
    out.setSize (1, n); out.clear();
    auto* o = out.getWritePointer (0);
    Svf hp (sr, T::highpass, 1500.f);
    float ph = 0.f, ph2 = 0.f;
    for (int i = 0; i < n; ++i)
    {
        const float t = (float) i / (float) sr;
        const float fi = f0 * (1.f + (sub == 1 ? 1.4f : sub == 3 ? 2.f : 0.3f) * std::exp (-t / 0.03f));
        ph += fi / (float) sr; ph2 += fi * (sub == 2 ? 1.5f : 2.7f) / (float) sr;
        float y = std::sin (kTwoPi * ph) + (sub == 2 || sub == 0 ? 0.6f * std::sin (kTwoPi * ph2) : 0.f);
        y *= std::exp (-t / dec);
        if (sub == 3) y += nz() * std::exp (-t / (dec * 0.5f)) * 0.5f;
        y += hp (nz()) * std::exp (-t / 0.002f) * 0.4f;
        o[i] = sat (y, drive);
    }
}
}

DrumParams drumParamsFromMacros (const ParamVec& v) { return { v[P_m1], v[P_m2], v[P_m3], v[P_m4] }; }

DrumResult generateDrum (int family, int sub, juce::int64 seed, double sr, const DrumParams& dp)
{
    DrumResult res;
    res.family = family; res.seed = seed; res.sampleRate = sr;
    const auto& fi = familyInfo (family);
    juce::Random r (seed);
    if (sub < 0 || sub >= (int) fi.subs.size()) sub = r.nextInt ((int) fi.subs.size());
    res.sub = sub;

    float f0 = 50.f;
    switch (family)
    {
        case F_808:    kickLike (res.audio, sr, r, true, sub, dp, f0); res.pitchFollow = true; break;
        case F_Kicks:  kickLike (res.audio, sr, r, false, sub, dp, f0); break;
        case F_Snares: snare (res.audio, sr, r, sub, dp); break;
        case F_Claps:  clap (res.audio, sr, r, sub, dp); break;
        case F_HiHats: hat (res.audio, sr, r, sub, dp); break;
        default:       perc (res.audio, sr, r, sub, dp); break;
    }
    res.rootNote = (int) std::round (69.f + 12.f * std::log2 (f0 / 440.f));
    finish (res.audio, sr, family == F_Percs && (sub == 4) ? 2.f : 6.f);
    if (family == F_Percs && sub == 5) res.audio.applyGainRamp (0, 0, 0, 1.f, 1.f);

    res.name = juce::String (fi.name).replaceCharacter (' ', '_') + "_" + fi.subs[(size_t) sub] + "_" + juce::String::toHexString (seed & 0xffff).toUpperCase();
    res.name = res.name.replace ("-", "");
    return res;
}
}
