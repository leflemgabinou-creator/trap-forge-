// Tests hors DAW : moteur, générateur de presets, drums, export WAV, presets, thèmes.
// Compilation : voir README (option TRAPFORGE_BUILD_TESTS). Écrit aussi des exemples audio dans le dossier donné en argument.
#include <juce_core/juce_core.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "../Source/SynthEngine.h"
#include "../Source/Effects.h"
#include "../Source/PresetGenerator.h"
#include "../Source/DrumGenerator.h"
#include "../Source/SampleExporter.h"
#include "../Source/PresetManager.h"
#include "../Source/ThemeManager.h"
#include <cstdio>

using namespace TF;
static int failures = 0, checks = 0;
#define CHECK(cond, msg) do { ++checks; if (! (cond)) { ++failures; std::printf ("  ECHEC : %s\n", juce::String (msg).toRawUTF8()); } } while (0)

static juce::AudioBuffer<float> renderNote (const ParamVec& base, int family, int note, double sr, float seconds, float noteLen, bool fx = true)
{
    ParamVec p = base; applyMacros (p, family);
    Engine e; e.prepare (sr); e.setParams (p);
    FxChain f; f.prepare (sr);
    const int n = (int) (seconds * sr);
    juce::AudioBuffer<float> b (2, n); b.clear();
    const int blk = 256;
    for (int pos = 0; pos < n; pos += blk)
    {
        const int m = std::min (blk, n - pos);
        if (pos == 0) e.noteOn (note, 0.9f);
        if (pos <= (int) (noteLen * sr) && (int) (noteLen * sr) < pos + blk) e.noteOff (note);
        e.render (b.getWritePointer (0) + pos, b.getWritePointer (1) + pos, m);
        if (fx) f.process (b.getWritePointer (0) + pos, b.getWritePointer (1) + pos, m, p, 120.0);
    }
    b.applyGain (juce::Decibels::decibelsToGain (p[P_master]));
    return b;
}

static bool sane (const juce::AudioBuffer<float>& b, float& peak, float& rms)
{
    peak = 0.f; double sum = 0.0; long cnt = 0;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float x = b.getSample (c, i);
            if (! std::isfinite (x)) return false;
            peak = std::max (peak, std::abs (x)); sum += (double) x * x; ++cnt;
        }
    rms = (float) std::sqrt (sum / std::max (1L, cnt));
    return true;
}

int main (int argc, char** argv)
{
    const juce::File out = argc > 1 ? juce::File (argv[1]) : juce::File::getCurrentWorkingDirectory().getChildFile ("Examples");
    out.getChildFile ("presets").createDirectory(); out.getChildFile ("drums").createDirectory();
    const double sr = 44100.0;

    std::printf ("[1] Presets melodiques : 10 familles x sous-categories x graines\n");
    int rendered = 0;
    for (int f = 0; f < F_count; ++f)
    {
        if (familyInfo (f).drum) continue;
        for (int sub = 0; sub < (int) familyInfo (f).subs.size(); ++sub)
            for (int seed = 1; seed <= 6; ++seed)
            {
                const auto g = generatePreset (f, sub, seed * 977 + f, {});
                const auto g2 = generatePreset (f, sub, seed * 977 + f, {});
                CHECK (g.p == g2.p && g.name == g2.name, "generation non reproductible " + g.name);
                for (int i = 0; i < P_count; ++i)
                    CHECK (std::isfinite (g.p[(size_t) i]) && g.p[(size_t) i] >= pinfo()[i].lo - 1e-4f && g.p[(size_t) i] <= pinfo()[i].hi + 1e-4f, juce::String ("parametre hors plage ") + pinfo()[i].id + " dans " + g.name);
                const auto b = renderNote (g.p, f, 48, sr, 1.6f, 1.0f);
                float peak, rms;
                const bool ok = sane (b, peak, rms);
                CHECK (ok, "NaN/Inf dans " + g.name);
                CHECK (peak <= 1.0f, "depassement " + juce::String (peak) + " dans " + g.name);
                CHECK (rms > 0.0005f, "son quasi muet " + g.name + " rms=" + juce::String (rms));
                ++rendered;
                if (seed == 1 && sub == 0)
                    exportWav (out.getChildFile ("presets").getChildFile (safeFileName (g.name + "_" + familyInfo (f).name) + ".wav"), b, sr, 24);
            }
    }
    std::printf ("    %d sons rendus et verifies\n", rendered);

    std::printf ("[2] Drums : toutes les familles et sous-categories\n");
    int drums = 0;
    for (int f = 0; f < F_count; ++f)
    {
        if (! familyInfo (f).drum) continue;
        for (int sub = 0; sub < (int) familyInfo (f).subs.size(); ++sub)
            for (int seed = 1; seed <= 4; ++seed)
            {
                const auto d = generateDrum (f, sub, seed * 131, sr);
                const auto d2 = generateDrum (f, sub, seed * 131, sr);
                float peak, rms; const bool ok = sane (d.audio, peak, rms);
                CHECK (ok, "NaN/Inf dans " + d.name);
                CHECK (peak > 0.8f && peak <= 0.9f, "niveau inattendu " + juce::String (peak) + " " + d.name);
                CHECK (rms > 0.005f, "drum quasi muet " + d.name);
                CHECK (d.audio.getNumSamples() > 100 && d.audio.getNumSamples() < (int) (6 * sr), "duree anormale " + d.name);
                bool same = d.audio.getNumSamples() == d2.audio.getNumSamples() && d.audio.getNumChannels() == d2.audio.getNumChannels();
                for (int c = 0; same && c < d.audio.getNumChannels(); ++c)
                    for (int i = 0; i < d.audio.getNumSamples(); ++i) if (d.audio.getSample (c, i) != d2.audio.getSample (c, i)) { same = false; break; }
                CHECK (same, "drum non reproductible " + d.name);
                ++drums;
                if (seed == 1) exportWav (out.getChildFile ("drums").getChildFile (safeFileName (d.name) + ".wav"), d.audio, sr, 24);
            }
    }
    std::printf ("    %d drums generes et verifies\n", drums);

    std::printf ("[3] Export WAV 16/24 bits mono/stereo + relecture\n");
    {
        auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("tf_test");
        tmp.createDirectory();
        for (int bits : { 16, 24 })
            for (double rate : { 44100.0, 48000.0, 96000.0 })
            {
                const auto d = generateDrum (F_Claps, 1, 42, rate);   // clap large = stéréo
                const auto k = generateDrum (F_Kicks, 0, 42, rate);   // mono
                CHECK (exportWav (tmp.getChildFile ("a.wav"), d.audio, rate, bits), "export stereo " + juce::String (bits));
                CHECK (exportWav (tmp.getChildFile ("b.wav"), k.audio, rate, bits), "export mono " + juce::String (bits));
                juce::AudioFormatManager fm; fm.registerBasicFormats();
                std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (tmp.getChildFile ("b.wav")));
                CHECK (r != nullptr && std::abs (r->sampleRate - rate) < 1.0 && (int) r->bitsPerSample == bits, "relecture " + juce::String (bits) + "/" + juce::String (rate));
                if (r != nullptr)
                {
                    juce::AudioBuffer<float> back (1, (int) r->lengthInSamples);
                    r->read (&back, 0, (int) r->lengthInSamples, 0, true, false);
                    float maxErr = 0.f; for (int i = 0; i < back.getNumSamples(); ++i) maxErr = std::max (maxErr, std::abs (back.getSample (0, i) - k.audio.getSample (0, i)));
                    CHECK (maxErr < (bits == 16 ? 1e-3f : 1e-5f), "erreur de quantification trop grande " + juce::String (maxErr));
                }
            }
        tmp.deleteRecursively();
    }

    std::printf ("[4] Gestionnaire de presets (sauvegarde exacte, renommage, favoris, suppression)\n");
    {
        auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("tf_presets");
        tmp.deleteRecursively();
        PresetManager pm (tmp);
        PresetData d; auto g = generatePreset (F_Pads, 2, 123456789012345LL); d.p = g.p; d.name = g.name; d.family = g.family; d.sub = g.sub; d.seed = g.seed;
        auto f = pm.presetFile (d.name, "user");
        CHECK (pm.save (d, f), "save");
        PresetData back; CHECK (PresetManager::load (f, back), "load");
        bool same = true; for (int i = 0; i < P_count; ++i) if (std::abs (back.p[(size_t) i] - d.p[(size_t) i]) > 1e-4f * std::max (1.f, std::abs (d.p[(size_t) i]))) same = false;
        CHECK (same && back.seed == d.seed && back.family == d.family && back.name == d.name, "rappel exact des reglages");
        CHECK (pm.setFavorite (f, true), "favori"); auto l = pm.list(); CHECK (l.size() == 1 && l[0].favorite, "liste/favori");
        auto nf = pm.rename (f, "Mon Preset"); CHECK (nf.existsAsFile() && ! f.existsAsFile(), "renommage");
        CHECK (PresetManager::matches (pm.list()[0], "mon", -1, true) && ! PresetManager::matches (pm.list()[0], "zzz", -1, false), "recherche");
        CHECK (nf.deleteFile() && pm.list().empty(), "suppression");
        tmp.deleteRecursively();
    }

    std::printf ("[5] Themes : remappage des couleurs\n");
    {
        for (int f = 0; f < F_count; ++f)
        {
            const auto& pal = paletteFor (f);
            float r = 0.9f, g = 0.1f, b = 0.15f; Theme::remap (r, g, b, pal);
            CHECK (r >= 0.f && r <= 1.f && g >= 0.f && g <= 1.f && b >= 0.f && b <= 1.f, "couleur hors plage");
            float gr = 0.5f, gg = 0.5f, gb = 0.5f; Theme::remap (gr, gg, gb, pal);
            CHECK (gr == 0.5f && gg == 0.5f && gb == 0.5f, "gris modifie");
            float orr = 1.f, og = 0.6f, ob = 0.05f; Theme::remap (orr, og, ob, pal);
            CHECK (orr == 1.f && og == 0.6f && ob == 0.05f, "orange modifie");
        }
        float r = 0.9f, g = 0.1f, b = 0.15f; Theme::remap (r, g, b, paletteFor (F_Pads));
        float h, s, v; Theme::rgb2hsv (r, g, b, h, s, v);
        CHECK (std::abs (h - 275.f) < 10.f, "teinte principale Pads");
    }

    std::printf ("[6] Polyphonie 16 voix, vol de voix, glide, mono (10 s)\n");
    {
        ParamVec p = generatePreset (F_Pads, 0, 5).p; applyMacros (p, F_Pads);
        Engine e; e.prepare (sr); e.setParams (p); FxChain fx; fx.prepare (sr);
        juce::Random r (77); float peak = 0.f; bool fin = true;
        std::vector<float> L (256), R (256);
        for (int blk = 0; blk < (int) (10 * sr / 256); ++blk)
        {
            for (int k = 0; k < 3; ++k) { const int n = 30 + r.nextInt (50); if (r.nextBool()) e.noteOn (n, 0.5f + 0.5f * r.nextFloat()); else e.noteOff (n); }
            std::fill (L.begin(), L.end(), 0.f); std::fill (R.begin(), R.end(), 0.f);
            e.render (L.data(), R.data(), 256); fx.process (L.data(), R.data(), 256, p, 120.0);
            for (int i = 0; i < 256; ++i) { if (! std::isfinite (L[(size_t) i]) || ! std::isfinite (R[(size_t) i])) fin = false; peak = std::max ({ peak, std::abs (L[(size_t) i]), std::abs (R[(size_t) i]) }); }
        }
        CHECK (fin, "valeurs non finies en polyphonie"); CHECK (peak <= 1.0f, "pic " + juce::String (peak));
        CHECK (e.activeVoices() <= 16, "trop de voix");
    }

    std::printf ("\n%d verifications, %d echec(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
