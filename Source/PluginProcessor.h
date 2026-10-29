#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <deque>
#include <memory>
#include "SynthEngine.h"
#include "Effects.h"
#include "PresetGenerator.h"
#include "DrumGenerator.h"
#include "PresetManager.h"
#include "SampleExporter.h"

struct KitSpec
{
    int count[6] { 6, 4, 4, 4, 8, 4 };   // 808, claps, snares, kicks, hi-hats, percussions
    float saturation = 0.5f, coherence = 0.7f;
    int bits = 24;
    double sampleRate = 44100.0;
    juce::String name { "TrapForge_Kit" };
};

class TrapProcessor : public juce::AudioProcessor, private juce::AsyncUpdater
{
public:
    TrapProcessor();
    ~TrapProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "TrapForge"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---------------- paramètres ----------------
    juce::AudioProcessorValueTreeState apvts;
    juce::RangedAudioParameter* param (int idx) const { return params[(size_t) idx]; }
    float get (int idx) const { return raw[(size_t) idx]->load(); }

    // ---------------- générateur ----------------
    TF::GenControls gen;                         // Variation, Complexity, Brightness, Movement, Width, Character, Intensity
    int  getFamily() const { return family.load(); }
    int  getSub() const { return sub.load(); }          // -1 = au choix de la graine
    juce::int64 getSeed() const { return seed; }
    void setFamily (int f, int s);                      // change la famille/sous-catégorie visée (sans générer)
    void generate();                                    // nouveau son de la famille choisie (nouvelle graine)
    void generateWithSeed (juce::int64 s, const TF::GenControls* ctl = nullptr, const juce::String& origin = "generated");   // même son que pour cette graine
    juce::String currentName { "Init" };
    juce::String currentOrigin { "generated" };
    bool currentFavorite = false;
    juce::File currentFile;

    bool undo();  bool redo();
    TF::PresetData snapshot() const;
    void recall (const TF::PresetData&);

    // ---------------- presets ----------------
    TF::PresetManager presets;

    // ---------------- drums ----------------
    std::shared_ptr<TF::DrumResult> getDrum() const { return drum; }
    void regenerateDrum();                               // même graine, macros actuelles
    juce::File exportCurrentDrum (const juce::File& folder, const juce::String& fileName, double sr, int bits);
    void startKit (const KitSpec&, const juce::File& parent);
    bool kitRunning() const { return kitThread != nullptr && kitThread->isThreadRunning(); }
    float kitProgress() const { return kitProg.load(); }
    juce::String kitStatus() const { const juce::ScopedLock l (statusLock); return kitMsg; }
    std::deque<std::shared_ptr<TF::DrumResult>> drumHistory;    // précédents / suivants
    int drumHistIdx = -1;
    void drumStep (int dir);

    // ---------------- jeu ----------------
    juce::MidiKeyboardState keyboardState;
    std::atomic<float> uiBend { 0.f }, uiMod { 0.f }, level { 0.f };
    std::atomic<int> activeNotesHint { 0 };

private:
    void handleAsyncUpdate() override { regenerateDrum(); }
    void applyParams (const TF::ParamVec&);
    void pushHistory();
    void setDrum (std::shared_ptr<TF::DrumResult>);
    void triggerSample (int note, float vel);
    void releaseSample (int note);
    void renderSamples (float* L, float* R, int n);
    double hostRate() const { return getSampleRate() > 1000.0 ? getSampleRate() : 44100.0; }

    std::array<juce::RangedAudioParameter*, TF::P_count> params {};
    std::array<std::atomic<float>*, TF::P_count> raw {};

    TF::Engine engine;
    TF::FxChain fx;

    std::atomic<int> family { 0 }, sub { -1 };
    juce::int64 seed = 1;
    std::vector<TF::PresetData> history;
    int histIdx = -1;

    // échantillons joués au clavier (drums générés)
    struct SampleVoice { bool active = false, releasing = false; int note = 0; double pos = 0.0, ratio = 1.0; float gain = 1.f, fade = 1.f; const TF::DrumResult* res = nullptr; unsigned long long age = 0; };
    std::array<SampleVoice, 8> sv;
    std::atomic<const TF::DrumResult*> drumPtr { nullptr };
    std::atomic<bool> clearSamples { false };
    std::shared_ptr<TF::DrumResult> drum;
    std::deque<std::shared_ptr<TF::DrumResult>> keepAlive;
    unsigned long long svCounter = 0;

    // kit en arrière-plan
    struct KitThread : public juce::Thread
    {
        KitThread (TrapProcessor& o, KitSpec s, juce::File d) : juce::Thread ("TrapForge kit"), owner (o), spec (s), dir (d) {}
        void run() override;
        TrapProcessor& owner; KitSpec spec; juce::File dir;
    };
    std::unique_ptr<KitThread> kitThread;
    std::atomic<float> kitProg { 0.f };
    juce::CriticalSection statusLock;
    juce::String kitMsg;
    void setKitMsg (const juce::String& s) { const juce::ScopedLock l (statusLock); kitMsg = s; }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrapProcessor)
};
