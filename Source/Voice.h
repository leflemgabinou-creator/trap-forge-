#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <cmath>

struct VoiceParams
{
    int wave1 = 0, wave2 = 0;
    float o1 = 0.8f, o2 = 0.f, semi = 0.f, det = 0.f, sub = 0.f, noise = 0.f, fm = 0.f, fmRatio = 2.f;
    float cutoff = 8000.f, res = 0.1f, fenv = 0.f, fdec = 0.3f;
    float A = 0.002f, D = 0.5f, S = 0.6f, R = 0.3f;
    float penv = 0.f, pdec = 0.05f, glide = 0.f, drive = 0.f;
    bool mono = false;
};

struct Shared
{
    VoiceParams p;
    float lastFreq = 0.f;
    bool fastKill = false;
};

struct TrapSound : public juce::SynthesiserSound
{
    bool appliesToNote (int) override { return true; }
    bool appliesToChannel (int) override { return true; }
};

class TrapVoice : public juce::SynthesiserVoice
{
public:
    explicit TrapVoice (Shared& s) : shared (s) {}

    void prepareFilter (double sampleRate, int blockSize)
    {
        filter.prepare ({ sampleRate, (juce::uint32) blockSize, 1 });
        filter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        adsr.setSampleRate (sampleRate);
    }

    bool canPlaySound (juce::SynthesiserSound* s) override { return dynamic_cast<TrapSound*> (s) != nullptr; }

    void startNote (int midiNote, float velocity, juce::SynthesiserSound*, int) override
    {
        const auto& P = shared.p;
        vel = velocity;
        targetFreq = (float) juce::MidiMessage::getMidiNoteInHertz (midiNote);
        curFreq = (P.mono && P.glide > 0.0005f && shared.lastFreq > 0.f) ? shared.lastFreq : targetFreq;
        shared.lastFreq = targetFreq;
        pEnv = 1.f; fEnv = 1.f;
        ph1 = ph2 = phSub = phMod = 0.f;
        filter.reset();
        adsr.setSampleRate (getSampleRate());
        adsr.setParameters ({ P.A, P.D, P.S, P.R });
        adsr.noteOn();
    }

    void stopNote (float, bool allowTailOff) override
    {
        if (allowTailOff)
        {
            if (shared.fastKill)   // mono : l'ancienne note s'éteint vite pour laisser glisser la nouvelle
                adsr.setParameters ({ shared.p.A, shared.p.D, shared.p.S, 0.03f });
            adsr.noteOff();
        }
        else
        {
            adsr.reset();
            clearCurrentNote();
        }
    }

    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}

    void renderNextBlock (juce::AudioBuffer<float>& out, int startSample, int numSamples) override
    {
        if (! isVoiceActive())
            return;

        const auto& P = shared.p;
        const float sr = (float) getSampleRate();
        adsr.setParameters ({ P.A, P.D, P.S, adsr.getParameters().release });   // garde la release en cours si coupure rapide
        const float fCoef = std::exp (-1.f / (sr * juce::jmax (0.005f, P.fdec)));
        const float pCoef = std::exp (-1.f / (sr * juce::jmax (0.003f, P.pdec)));
        const float gCoef = P.glide > 0.0005f ? 1.f - std::exp (-1.f / (sr * P.glide)) : 1.f;
        const float q = 0.5f + P.res * 7.5f;
        filter.setResonance (q);
        const float semiMul = std::exp2 ((P.semi + P.det / 100.f) / 12.f);
        const float driveG = 1.f + P.drive * 9.f;
        const float driveNorm = 1.f / std::tanh (driveG);
        const float maxCut = juce::jmin (20000.f, 0.45f * sr);

        for (int i = 0; i < numSamples; ++i)
        {
            if (! adsr.isActive())
            {
                clearCurrentNote();
                break;
            }

            curFreq += (targetFreq - curFreq) * gCoef;
            const float f = curFreq * std::exp2 (P.penv * pEnv / 12.f);
            pEnv *= pCoef;
            fEnv *= fCoef;

            const float dt1 = f / sr;
            const float dt2 = f * semiMul / sr;

            phMod += f * P.fmRatio / sr; phMod -= std::floor (phMod);
            const float mod = std::sin (6.2831853f * phMod);
            float p1 = ph1 + P.fm * 0.6f * mod; p1 -= std::floor (p1);

            float s = osc (P.wave1, p1, dt1) * P.o1;
            if (P.o2 > 0.001f) s += osc (P.wave2, ph2, dt2) * P.o2;
            if (P.sub > 0.001f) s += std::sin (6.2831853f * phSub) * P.sub;
            if (P.noise > 0.001f) s += (rng.nextFloat() * 2.f - 1.f) * P.noise;

            if ((ctr++ & 7) == 0)
            {
                const float c = P.cutoff * std::exp2 (P.fenv * fEnv * 6.f);
                filter.setCutoffFrequency (juce::jlimit (20.f, maxCut, c));
            }
            float y = filter.processSample (0, s);
            if (P.drive > 0.001f)
                y = std::tanh (driveG * y) * driveNorm;

            y *= adsr.getNextSample() * vel * 0.5f;

            for (int ch = 0; ch < out.getNumChannels(); ++ch)
                out.addSample (ch, startSample + i, y);

            ph1 += dt1;   ph1 -= std::floor (ph1);
            ph2 += dt2;   ph2 -= std::floor (ph2);
            phSub += f * 0.5f / sr; phSub -= std::floor (phSub);
        }
    }

private:
    static float blep (float t, float dt)
    {
        if (t < dt)       { t /= dt; return t + t - t * t - 1.f; }
        if (t > 1.f - dt) { t = (t - 1.f) / dt; return t * t + t + t + 1.f; }
        return 0.f;
    }

    static float osc (int wave, float ph, float dt)
    {
        switch (wave)
        {
            case 0:  return std::sin (6.2831853f * ph);
            case 1:  return 4.f * std::abs (ph - 0.5f) - 1.f;
            case 2:  return 2.f * ph - 1.f - blep (ph, dt);
            default:
            {
                float p2 = ph + 0.5f; p2 -= std::floor (p2);
                return (ph < 0.5f ? 1.f : -1.f) + blep (ph, dt) - blep (p2, dt);
            }
        }
    }

    Shared& shared;
    juce::ADSR adsr;
    juce::dsp::StateVariableTPTFilter<float> filter;
    juce::Random rng;
    float vel = 1.f, targetFreq = 440.f, curFreq = 440.f, pEnv = 1.f, fEnv = 1.f;
    float ph1 = 0.f, ph2 = 0.f, phSub = 0.f, phMod = 0.f;
    int ctr = 0;
};
