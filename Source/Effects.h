#pragma once
// Chaîne d'effets : EQ 3 bandes -> compresseur -> chorus -> delay (noire pointée, calé sur le tempo) -> largeur -> reverb -> limiteur doux.
// Aucune allocation dans process().
#include <juce_audio_basics/juce_audio_basics.h>
#include "ParamTable.h"

namespace TF
{
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    float process (float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void set (float B0, float B1, float B2, float A0, float A1, float A2) { b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0; }
    void lowShelf (float sr, float f, float dB)
    {
        const float A = std::pow (10.f, dB / 40.f), w = 6.2831853f * f / sr, c = std::cos (w), al = std::sin (w) * 0.70710678f, sA = 2.f * std::sqrt (A) * al;
        set (A * ((A + 1) - (A - 1) * c + sA), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sA),
             (A + 1) + (A - 1) * c + sA, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sA);
    }
    void highShelf (float sr, float f, float dB)
    {
        const float A = std::pow (10.f, dB / 40.f), w = 6.2831853f * f / sr, c = std::cos (w), al = std::sin (w) * 0.70710678f, sA = 2.f * std::sqrt (A) * al;
        set (A * ((A + 1) + (A - 1) * c + sA), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sA),
             (A + 1) - (A - 1) * c + sA, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sA);
    }
    void peak (float sr, float f, float dB, float Q)
    {
        const float A = std::pow (10.f, dB / 40.f), w = 6.2831853f * f / sr, c = std::cos (w), al = std::sin (w) / (2.f * Q);
        set (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
};

struct DLine
{
    std::vector<float> b; int w = 0;
    void init (int n) { b.assign ((size_t) std::max (8, n), 0.f); w = 0; }
    void push (float x) { b[(size_t) w] = x; if (++w >= (int) b.size()) w = 0; }
    float read (float d) const
    {
        const int n = (int) b.size();
        float r = (float) w - d; while (r < 0.f) r += (float) n; while (r >= (float) n) r -= (float) n;
        const int i0 = (int) r; const int i1 = i0 + 1 >= n ? 0 : i0 + 1; const float f = r - (float) i0;
        return b[(size_t) i0] + (b[(size_t) i1] - b[(size_t) i0]) * f;
    }
};

class FxChain
{
public:
    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        dl[0].init ((int) (sr * 2.0f)); dl[1].init ((int) (sr * 2.0f));
        ch[0].init ((int) (sr * 0.06f)); ch[1].init ((int) (sr * 0.06f));
        reverb.setSampleRate (sampleRate); reverb.reset();
        env = 0.f; cph = 0.f; dTimeSm = 0.35f * sr; widSm = 1.f; lastLo = lastMid = lastHi = 999.f;
        for (auto& e : eq) for (auto& b : e) b = Biquad();
        dFilt[0] = dFilt[1] = 0.f; dcx[0] = dcx[1] = dcy[0] = dcy[1] = 0.f;
    }

    void process (float* L, float* R, int n, const ParamVec& p, double bpm)
    {
        const bool dlyOn = p[P_dlyOn] > 0.5f && p[P_dly] > 0.002f, choOn = p[P_choOn] > 0.5f && p[P_cho] > 0.002f;
        const bool cmpOn = p[P_cmpOn] > 0.5f && p[P_cmp] > 0.002f, rvbOn = p[P_rvbOn] > 0.5f && p[P_rvb] > 0.002f;
        const bool widOn = p[P_widOn] > 0.5f, eqOn = p[P_eqOn] > 0.5f;
        const float dly = p[P_dly], cho = p[P_cho], cmp = p[P_cmp];

        if (eqOn && (p[P_eqLo] != lastLo || p[P_eqMid] != lastMid || p[P_eqHi] != lastHi))
        {
            for (int c = 0; c < 2; ++c)
            { eq[0][c].lowShelf (sr, 150.f, p[P_eqLo]); eq[1][c].peak (sr, 1200.f, p[P_eqMid], 0.8f); eq[2][c].highShelf (sr, 6000.f, p[P_eqHi]); }
            lastLo = p[P_eqLo]; lastMid = p[P_eqMid]; lastHi = p[P_eqHi];
        }
        const bool eqActive = eqOn && (p[P_eqLo] != 0.f || p[P_eqMid] != 0.f || p[P_eqHi] != 0.f);

        const float atk = std::exp (-1.f / (sr * 0.005f)), rel = std::exp (-1.f / (sr * 0.09f));
        const float thr = -3.f - 21.f * cmp, makeup = std::pow (10.f, (cmp * 7.f) / 20.f);
        const float dTarget = (float) (0.75 * 60.0 / std::max (40.0, std::min (240.0, bpm))) * sr;
        const float widTarget = widOn ? std::min (1.6f, p[P_wid] * 2.f) : 1.f;
        const float cInc = 0.7f / sr;

        for (int i = 0; i < n; ++i)
        {
            float l = L[i], r = R[i];

            if (eqActive) { l = eq[2][0].process (eq[1][0].process (eq[0][0].process (l))); r = eq[2][1].process (eq[1][1].process (eq[0][1].process (r))); }

            if (cmpOn)
            {
                const float lvl = std::max (std::abs (l), std::abs (r));
                env = lvl > env ? atk * env + (1.f - atk) * lvl : rel * env + (1.f - rel) * lvl;
                const float over = 20.f * std::log10 (env + 1e-6f) - thr;
                const float g = (over > 0.f ? std::pow (10.f, -(over * 0.75f) / 20.f) : 1.f) * makeup;
                l *= g; r *= g;
            }

            if (choOn)
            {
                cph += cInc; cph -= std::floor (cph);
                ch[0].push (l); ch[1].push (r);
                const float d1 = (0.018f + 0.004f * std::sin (6.2831853f * cph)) * sr, d2 = (0.018f + 0.004f * std::sin (6.2831853f * (cph + 0.5f))) * sr;
                l += ch[0].read (d1) * cho * 0.6f; r += ch[1].read (d2) * cho * 0.6f;
            }

            if (dlyOn)
            {
                dTimeSm += (dTarget - dTimeSm) * 0.0005f;
                const float wl = dl[0].read (dTimeSm), wr = dl[1].read (dTimeSm);
                dFilt[0] += (wr - dFilt[0]) * 0.35f; dFilt[1] += (wl - dFilt[1]) * 0.35f;   // ping-pong, repasse adoucie
                dl[0].push (l + 0.4f * dFilt[0]); dl[1].push (r + 0.4f * dFilt[1]);
                l += wl * dly * 0.6f; r += wr * dly * 0.6f;
            }

            if (widOn)
            {
                widSm += (widTarget - widSm) * 0.002f;
                const float m = 0.5f * (l + r), s = 0.5f * (l - r) * widSm;
                l = m + s; r = m - s;
            }
            L[i] = l; R[i] = r;
        }

        if (rvbOn)
        {
            juce::Reverb::Parameters rp;
            rp.roomSize = 0.3f + 0.65f * p[P_rsize]; rp.damping = 0.55f; rp.wetLevel = p[P_rvb] * 0.5f; rp.dryLevel = 1.f; rp.width = 1.f;
            reverb.setParameters (rp);
            reverb.processStereo (L, R, n);
        }

        for (int i = 0; i < n; ++i)   // filtre anti-continu (3 Hz) puis limiteur doux : évite tout dépassement incontrôlé
        {
            const float xl = L[i], xr = R[i];
            const float yl = xl - dcx[0] + 0.9995f * dcy[0], yr = xr - dcx[1] + 0.9995f * dcy[1];
            dcx[0] = xl; dcy[0] = yl; dcx[1] = xr; dcy[1] = yr;
            L[i] = soft (yl); R[i] = soft (yr);
        }
    }

private:
    static float soft (float x)
    {
        const float a = std::abs (x);
        if (a <= 0.85f) return x;
        const float y = 0.85f + 0.15f * std::tanh ((a - 0.85f) / 0.15f);
        return x < 0.f ? -y : y;
    }

    float sr = 44100.f, env = 0.f, cph = 0.f, dTimeSm = 15000.f, widSm = 1.f, lastLo = 999.f, lastMid = 999.f, lastHi = 999.f;
    float dFilt[2] { 0.f, 0.f }, dcx[2] { 0.f, 0.f }, dcy[2] { 0.f, 0.f };
    DLine dl[2], ch[2];
    Biquad eq[3][2];
    juce::Reverb reverb;
};
}
