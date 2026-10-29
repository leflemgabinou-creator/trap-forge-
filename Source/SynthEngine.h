#pragma once
// Moteur de synthèse : voix polyphoniques (2 oscillateurs, sub, bruit, FM, filtre LP/HP/BP, ADSR, LFO, glide).
// Indépendant de l'interface et de JUCE-plugin : testable hors DAW.
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "ParamTable.h"

namespace TF
{
inline float midiHz (float note) { return 440.f * std::exp2 ((note - 69.f) / 12.f); }

class Voice
{
public:
    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        adsr.setSampleRate (sampleRate);
        f1.prepare ({ sampleRate, 512, 1 });
        f2.prepare ({ sampleRate, 512, 1 });
        kill();
    }

    bool isActive() const { return active; }
    bool isReleased() const { return released; }
    int  getNote() const { return note; }
    unsigned long long getAge() const { return age; }

    void kill() { active = false; released = false; adsr.reset(); }

    void start (int n, float velocity, const ParamVec& p, float fromFreq, unsigned long long a)
    {
        note = n; vel = velocity; age = a; active = true; released = false; fastRel = false;
        target = midiHz ((float) n);
        cur = fromFreq > 0.f ? fromFreq : target;
        pEnv = fEnv = 1.f; ph1 = ph2 = phSub = phMod = lfoPh = 0.f; fadeIn = 0.f;
        f1.reset(); f2.reset();
        cutSm = p[P_cutoff];
        adsr.setParameters ({ p[P_a], p[P_d], p[P_s], p[P_r] });
        adsr.noteOn();
    }

    void release (bool fast, const ParamVec& p)
    {
        if (! active || released) return;
        released = true;
        if (fast) { fastRel = true; adsr.setParameters ({ p[P_a], p[P_d], p[P_s], 0.025f }); }
        adsr.noteOff();
    }

    void render (const ParamVec& p, float bend, float mod, float* L, float* R, int n)
    {
        if (! active) return;

        if (! fastRel && ! released) adsr.setParameters ({ p[P_a], p[P_d], p[P_s], p[P_r] });

        const float fCoef = std::exp (-1.f / (sr * std::max (0.005f, p[P_fdec])));
        const float pCoef = std::exp (-1.f / (sr * std::max (0.003f, p[P_pdec])));
        const bool  glideOn = p[P_mono] > 0.5f && p[P_glide] > 0.0005f;
        const float gCoef = glideOn ? 1.f - std::exp (-1.f / (sr * p[P_glide])) : 1.f;
        const float q = 0.5f + p[P_res] * 7.5f;
        const int   ft = (int) std::round (p[P_ftype]);
        f1.setResonance (q);
        f1.setType (ft == 1 ? juce::dsp::StateVariableTPTFilterType::highpass
                  : ft == 2 ? juce::dsp::StateVariableTPTFilterType::bandpass
                            : juce::dsp::StateVariableTPTFilterType::lowpass);
        const float semiMul = std::exp2 ((p[P_semi] + p[P_det] / 100.f) / 12.f);
        const float driveG = 1.f + p[P_drive] * 9.f;
        const float driveNorm = 1.f / std::tanh (driveG);
        const float lfoInc = p[P_lfoRate] / sr;
        const float vib = (p[P_lfoPitch] + mod * 0.6f) * 0.5f;
        const float bendSemi = bend * 2.f;
        const float maxCut = std::min (20000.f, 0.45f * sr);
        const float o1 = p[P_o1], o2 = p[P_o2], sub = p[P_sub], nz = p[P_noise], fmA = p[P_fm] * 0.6f;
        const int w1 = (int) std::round (p[P_wave1]), w2 = (int) std::round (p[P_wave2]);

        for (int i = 0; i < n; ++i)
        {
            if (! adsr.isActive()) { active = false; break; }

            lfoPh += lfoInc; lfoPh -= std::floor (lfoPh);
            const float lfo = std::sin (6.2831853f * lfoPh);

            cur += (target - cur) * gCoef;
            const float f = cur * std::exp2 ((p[P_penv] * pEnv + vib * lfo + bendSemi) / 12.f);
            pEnv *= pCoef; fEnv *= fCoef;

            const float dt1 = std::min (0.45f, f / sr);
            const float dt2 = std::min (0.45f, f * semiMul / sr);

            phMod += f * p[P_fmratio] / sr; phMod -= std::floor (phMod);
            float pm = ph1 + fmA * std::sin (6.2831853f * phMod); pm -= std::floor (pm);

            float s = osc (w1, pm, dt1) * o1;
            if (o2 > 0.001f) s += osc (w2, ph2, dt2) * o2;
            if (sub > 0.001f) s += std::sin (6.2831853f * phSub) * sub;
            if (nz > 0.001f) s += (nextNoise() * 2.f - 1.f) * nz;

            if ((ctr++ & 7) == 0)
            {
                cutSm += (p[P_cutoff] - cutSm) * 0.2f;
                const float c = cutSm * std::exp2 (p[P_fenv] * fEnv * 6.f + p[P_lfoFilter] * lfo * 2.f);
                f1.setCutoffFrequency (std::min (maxCut, std::max (20.f, c)));
            }
            float y = f1.processSample (0, s);
            if (p[P_drive] > 0.001f) y = std::tanh (driveG * y) * driveNorm;

            fadeIn = std::min (1.f, fadeIn + 1.f / (sr * 0.0015f));
            const float trem = 1.f - p[P_lfoAmp] * 0.5f * (0.5f + 0.5f * lfo);
            y *= adsr.getNextSample() * vel * trem * fadeIn * 0.2f;

            const float pan = 0.5f + 0.5f * p[P_lfoPan] * lfo;
            L[i] += y * std::cos (pan * 1.5707963f) * 1.4142136f;
            R[i] += y * std::sin (pan * 1.5707963f) * 1.4142136f;

            ph1 += dt1; ph1 -= std::floor (ph1);
            ph2 += dt2; ph2 -= std::floor (ph2);
            phSub += f * 0.5f / sr; phSub -= std::floor (phSub);
        }
    }

private:
    static float blep (float t, float dt)
    {
        if (t < dt)        { t /= dt; return t + t - t * t - 1.f; }
        if (t > 1.f - dt)  { t = (t - 1.f) / dt; return t * t + t + t + 1.f; }
        return 0.f;
    }
    static float osc (int wave, float ph, float dt)
    {
        switch (wave)
        {
            case 0:  return std::sin (6.2831853f * ph);
            case 1:  return 4.f * std::abs (ph - 0.5f) - 1.f;
            case 2:  return 2.f * ph - 1.f - blep (ph, dt);
            default: { float p2 = ph + 0.5f; p2 -= std::floor (p2); return (ph < 0.5f ? 1.f : -1.f) + blep (ph, dt) - blep (p2, dt); }
        }
    }
    float nextNoise() { rng = rng * 1664525u + 1013904223u; return (float) (rng >> 8) / 16777216.f; }

    float sr = 44100.f;
    juce::ADSR adsr;
    juce::dsp::StateVariableTPTFilter<float> f1, f2;
    bool active = false, released = false, fastRel = false;
    int note = 0, ctr = 0;
    unsigned long long age = 0;
    unsigned int rng = 12345u;
    float vel = 1.f, target = 440.f, cur = 440.f, pEnv = 1.f, fEnv = 1.f, cutSm = 8000.f, fadeIn = 1.f;
    float ph1 = 0.f, ph2 = 0.f, phSub = 0.f, phMod = 0.f, lfoPh = 0.f;
};

class Engine
{
public:
    static constexpr int kVoices = 16;

    void prepare (double sampleRate)
    {
        for (auto& v : voices) v.prepare (sampleRate);
        held = 0; lastFreq = 0.f; counter = 0;
    }

    void setParams (const ParamVec& p) { P = p; }
    void setPitchBend (float b) { bend = b; }     // -1..1 (± 2 demi-tons)
    void setModWheel (float m) { mod = m; }       // 0..1

    void noteOn (int note, float velocity)
    {
        const int lim = std::max (1, std::min (kVoices, (int) std::round (P[P_voices])));
        const bool mono = P[P_mono] > 0.5f;
        if (held == 0) lastFreq = 0.f;
        const float from = (mono && P[P_glide] > 0.0005f) ? lastFreq : 0.f;

        if (mono)
            for (auto& v : voices) v.release (true, P);

        int idx = -1;
        for (int i = 0; i < lim; ++i)
            if (! voices[(size_t) i].isActive()) { idx = i; break; }
        if (idx < 0)   // vol de voix : d'abord une voix déjà relâchée, sinon la plus ancienne
        {
            unsigned long long best = ~0ull; bool bestReleased = false;
            for (int i = 0; i < lim; ++i)
            {
                auto& v = voices[(size_t) i];
                const bool rel = v.isReleased();
                if (idx < 0 || (rel && ! bestReleased) || (rel == bestReleased && v.getAge() < best))
                { idx = i; best = v.getAge(); bestReleased = rel; }
            }
        }
        voices[(size_t) idx].start (note, velocity, P, from, ++counter);
        lastFreq = midiHz ((float) note);
        ++held;
    }

    void noteOff (int note)
    {
        held = std::max (0, held - 1);
        for (auto& v : voices)
            if (v.isActive() && ! v.isReleased() && v.getNote() == note) v.release (false, P);
    }

    void allNotesOff() { held = 0; for (auto& v : voices) v.release (false, P); }

    void render (float* L, float* R, int n)
    {
        for (auto& v : voices) v.render (P, bend, mod, L, R, n);
    }

    int activeVoices() const { int c = 0; for (auto& v : voices) c += v.isActive() ? 1 : 0; return c; }

private:
    std::array<Voice, kVoices> voices;
    ParamVec P = defaultParams();
    float bend = 0.f, mod = 0.f, lastFreq = 0.f;
    int held = 0;
    unsigned long long counter = 0;
};
}
