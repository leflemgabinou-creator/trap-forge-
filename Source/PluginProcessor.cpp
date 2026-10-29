#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace TF;
using APVTS = juce::AudioProcessorValueTreeState;

static APVTS::ParameterLayout createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> v;
    for (int i = 0; i < P_count; ++i)
    {
        const auto& q = pinfo()[i];
        if (q.kind == 1)
        {
            StringArray names;
            if (juce::String (q.id).startsWith ("wave")) for (int k = 0; k < 4; ++k) names.add (waveName (k));
            else for (int k = 0; k < 3; ++k) names.add (filterName (k));
            v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { q.id, 1 }, q.name, names, (int) q.def));
        }
        else if (q.kind == 2)
            v.push_back (std::make_unique<AudioParameterBool> (ParameterID { q.id, 1 }, q.name, q.def > 0.5f));
        else
        {
            NormalisableRange<float> r (q.lo, q.hi, q.step);
            if (q.centre > 0.f) r.setSkewForCentre (q.centre);
            v.push_back (std::make_unique<AudioParameterFloat> (ParameterID { q.id, 1 }, q.name, r, q.def));
        }
    }
    return { v.begin(), v.end() };
}

TrapProcessor::TrapProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "TRAPFORGE", createLayout())
{
    for (int i = 0; i < P_count; ++i)
    {
        params[(size_t) i] = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (pinfo()[i].id));
        raw[(size_t) i] = apvts.getRawParameterValue (pinfo()[i].id);
    }
}

TrapProcessor::~TrapProcessor()
{
    cancelPendingUpdate();
    if (kitThread != nullptr) kitThread->stopThread (4000);
}

bool TrapProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void TrapProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
    fx.prepare (sampleRate);
    clearSamples = true;
    if (drum != nullptr && std::abs (drum->sampleRate - sampleRate) > 1.0)
        triggerAsyncUpdate();   // le drum est regénéré à la bonne fréquence, hors du fil audio
}

// ---------------------------------------------------------------- audio
void TrapProcessor::triggerSample (int note, float vel)
{
    const auto* d = drumPtr.load();
    if (d == nullptr) return;
    SampleVoice* v = nullptr;
    for (auto& s : sv) if (! s.active) { v = &s; break; }
    if (v == nullptr) { v = &sv[0]; for (auto& s : sv) if (s.age < v->age) v = &s; }
    v->active = true; v->releasing = false; v->fade = 1.f; v->note = note; v->pos = 0.0; v->res = d; v->age = ++svCounter;
    v->ratio = (d->pitchFollow ? std::exp2 ((double) (note - d->rootNote) / 12.0) : 1.0) * d->sampleRate / hostRate();
    v->gain = vel;
}

void TrapProcessor::releaseSample (int note)
{
    for (auto& s : sv) if (s.active && s.note == note && s.res != nullptr && s.res->pitchFollow) s.releasing = true;
}

void TrapProcessor::renderSamples (float* L, float* R, int n)
{
    const float relStep = 1.f / (float) (0.03 * hostRate());
    for (auto& s : sv)
    {
        if (! s.active) continue;
        const auto& a = s.res->audio;
        const int len = a.getNumSamples(), nc = a.getNumChannels();
        const float* c0 = a.getReadPointer (0); const float* c1 = a.getReadPointer (nc > 1 ? 1 : 0);
        for (int i = 0; i < n; ++i)
        {
            const int i0 = (int) s.pos;
            if (i0 + 1 >= len) { s.active = false; break; }
            const float f = (float) (s.pos - i0);
            const float g = s.gain * 0.9f * s.fade;
            L[i] += (c0[i0] + (c0[i0 + 1] - c0[i0]) * f) * g;
            R[i] += (c1[i0] + (c1[i0 + 1] - c1[i0]) * f) * g;
            s.pos += s.ratio;
            if (s.releasing) { s.fade -= relStep; if (s.fade <= 0.f) { s.active = false; break; } }
        }
    }
}

void TrapProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    const int n = buffer.getNumSamples();
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : L;

    ParamVec base; for (int i = 0; i < P_count; ++i) base[(size_t) i] = raw[(size_t) i]->load();
    const int fam = family.load();
    const bool isDrum = familyInfo (fam).drum;
    ParamVec eff = base;
    if (! isDrum) applyMacros (eff, fam);
    engine.setParams (eff);
    engine.setPitchBend (uiBend.load());

    if (clearSamples.exchange (false)) for (auto& s : sv) s.active = false;

    double bpm = 120.0;
    if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition()) if (auto b = pos->getBpm()) bpm = *b;

    keyboardState.processNextMidiBuffer (midi, 0, n, true);   // notes du clavier virtuel + MIDI externe

    float mod = uiMod.load();
    int pos = 0;
    auto renderTo = [&] (int end)
    {
        if (end <= pos) return;
        engine.setModWheel (mod);
        engine.render (L + pos, R + pos, end - pos);
        renderSamples (L + pos, R + pos, end - pos);   // (vide hors drums)
        pos = end;
    };

    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        renderTo (juce::jlimit (0, n, meta.samplePosition));
        if (m.isNoteOn())            { if (isDrum) triggerSample (m.getNoteNumber(), m.getFloatVelocity()); else engine.noteOn (m.getNoteNumber(), m.getFloatVelocity()); }
        else if (m.isNoteOff())      { if (isDrum) releaseSample (m.getNoteNumber()); else engine.noteOff (m.getNoteNumber()); }
        else if (m.isPitchWheel())   uiBend.store ((float) (m.getPitchWheelValue() - 8192) / 8192.f);
        else if (m.isControllerOfType (1)) { mod = (float) m.getControllerValue() / 127.f; uiMod.store (mod); }
        else if (m.isAllNotesOff() || m.isAllSoundOff()) { engine.allNotesOff(); for (auto& s : sv) s.releasing = true; }
    }
    renderTo (n);

    fx.process (L, R, n, eff, bpm);
    buffer.applyGain (juce::Decibels::decibelsToGain (eff[P_master]));

    float peak = 0.f; for (int i = 0; i < n; ++i) peak = std::max ({ peak, std::abs (L[i]), std::abs (R[i]) });
    level.store (std::max (peak, level.load() * 0.92f));
}

// ---------------------------------------------------------------- générateur / presets
void TrapProcessor::applyParams (const ParamVec& v)
{
    for (int i = 0; i < P_count; ++i)
        if (auto* p = params[(size_t) i]) p->setValueNotifyingHost (p->convertTo0to1 (v[(size_t) i]));
}

void TrapProcessor::setFamily (int f, int s)
{
    family.store (juce::jlimit (0, (int) F_count - 1, f)); sub.store (s);
}

void TrapProcessor::generate()
{
    generateWithSeed ((juce::int64) (juce::Random::getSystemRandom().nextInt() & 0x7fffffff) + 1);
}

void TrapProcessor::generateWithSeed (juce::int64 s, const TF::GenControls* ctl, const juce::String& origin)
{
    const int f = family.load();
    auto g = generatePreset (f, sub.load(), s, ctl != nullptr ? *ctl : gen);
    seed = s;
    applyParams (g.p);
    currentName = g.name; currentOrigin = origin; currentFavorite = false; currentFile = juce::File();
    sub.store (g.sub);
    if (familyInfo (f).drum) { regenerateDrum(); }
    else { setDrum (nullptr); }
    pushHistory();
}

TF::PresetData TrapProcessor::snapshot() const
{
    PresetData d;
    d.name = currentName; d.family = family.load(); d.sub = juce::jmax (0, sub.load()); d.seed = seed; d.origin = currentOrigin; d.favorite = currentFavorite;
    for (int i = 0; i < P_count; ++i) d.p[(size_t) i] = raw[(size_t) i]->load();
    return d;
}

void TrapProcessor::recall (const TF::PresetData& d)
{
    family.store (juce::jlimit (0, (int) F_count - 1, d.family)); sub.store (d.sub); seed = d.seed;
    applyParams (d.p);
    currentName = d.name; currentOrigin = d.origin; currentFavorite = d.favorite;
    if (familyInfo (family.load()).drum) regenerateDrum(); else setDrum (nullptr);
}

void TrapProcessor::pushHistory()
{
    if (histIdx + 1 < (int) history.size()) history.erase (history.begin() + histIdx + 1, history.end());
    history.push_back (snapshot());
    if (history.size() > 80) history.erase (history.begin());
    histIdx = (int) history.size() - 1;
}

bool TrapProcessor::undo() { if (histIdx <= 0) return false; recall (history[(size_t) --histIdx]); return true; }
bool TrapProcessor::redo() { if (histIdx + 1 >= (int) history.size()) return false; recall (history[(size_t) ++histIdx]); return true; }

// ---------------------------------------------------------------- drums
void TrapProcessor::setDrum (std::shared_ptr<TF::DrumResult> d)
{
    if (d != nullptr) keepAlive.push_back (d);
    while (keepAlive.size() > 10) keepAlive.pop_front();
    drum = d;
    clearSamples = true;
    drumPtr.store (d.get());
}

void TrapProcessor::regenerateDrum()
{
    const int f = family.load();
    if (! familyInfo (f).drum) return;
    ParamVec v; for (int i = 0; i < P_count; ++i) v[(size_t) i] = raw[(size_t) i]->load();
    auto r = std::make_shared<DrumResult> (generateDrum (f, sub.load(), seed, hostRate(), drumParamsFromMacros (v)));
    setDrum (r);
    if (drumHistIdx + 1 < (int) drumHistory.size() && ! drumHistory.empty() && drumHistory[(size_t) drumHistIdx]->seed == r->seed && drumHistory[(size_t) drumHistIdx]->family == r->family)
        { drumHistory[(size_t) drumHistIdx] = r; return; }   // simple retouche de macro : remplace l'entrée courante
    if (! drumHistory.empty() && drumHistIdx >= 0 && drumHistory[(size_t) drumHistIdx]->seed == r->seed && drumHistory[(size_t) drumHistIdx]->family == r->family)
        { drumHistory[(size_t) drumHistIdx] = r; return; }
    if (drumHistIdx + 1 < (int) drumHistory.size()) drumHistory.erase (drumHistory.begin() + drumHistIdx + 1, drumHistory.end());
    drumHistory.push_back (r);
    if (drumHistory.size() > 30) drumHistory.pop_front();
    drumHistIdx = (int) drumHistory.size() - 1;
}

void TrapProcessor::drumStep (int dir)
{
    const int i = juce::jlimit (0, (int) drumHistory.size() - 1, drumHistIdx + dir);
    if (drumHistory.empty() || i == drumHistIdx) return;
    drumHistIdx = i;
    auto r = drumHistory[(size_t) i];
    family.store (r->family); sub.store (r->sub); seed = r->seed;
    setDrum (r);
}

juce::File TrapProcessor::exportCurrentDrum (const juce::File& folder, const juce::String& fileName, double sr, int bits)
{
    if (drum == nullptr) return {};
    ParamVec v; for (int i = 0; i < P_count; ++i) v[(size_t) i] = raw[(size_t) i]->load();
    const auto r = generateDrum (drum->family, drum->sub, drum->seed, sr, drumParamsFromMacros (v));
    const auto f = folder.getChildFile (safeFileName (fileName.isEmpty() ? r.name : fileName) + ".wav");
    return exportWav (f, r.audio, sr, bits) ? f : juce::File();
}

void TrapProcessor::startKit (const KitSpec& spec, const juce::File& parent)
{
    if (kitRunning()) return;
    kitProg = 0.f; setKitMsg ("Generation en cours...");
    kitThread = std::make_unique<KitThread> (*this, spec, parent);
    kitThread->startThread();
}

void TrapProcessor::KitThread::run()
{
    static const int fams[6] = { F_808, F_Claps, F_Snares, F_Kicks, F_HiHats, F_Percs };
    static const char* dirs[6] = { "808", "Claps", "Snares", "Kicks", "HiHats", "Percs" };
    juce::Random r ((juce::int64) juce::Time::currentTimeMillis());
    const juce::int64 kitSeed = r.nextInt64() & 0xffffff;
    const auto root = dir.getNonexistentChildFile (safeFileName (spec.name) + "_" + juce::String::toHexString (kitSeed).toUpperCase(), "", false);
    root.createDirectory();

    DrumParams base { 0.35f + r.nextFloat() * 0.3f, 0.35f + r.nextFloat() * 0.3f, 0.3f + 0.5f * spec.saturation, 0.35f + r.nextFloat() * 0.3f };   // identité sonore commune
    int total = 0; for (int c : spec.count) total += c;
    int done = 0, ok = 0, failed = 0;
    for (int cat = 0; cat < 6 && ! threadShouldExit(); ++cat)
        for (int i = 0; i < spec.count[cat] && ! threadShouldExit(); ++i)
        {
            const float dev = (1.f - spec.coherence) * 0.6f;
            DrumParams dp { juce::jlimit (0.f, 1.f, base.tone + (r.nextFloat() - 0.5f) * dev), juce::jlimit (0.f, 1.f, base.length + (r.nextFloat() - 0.5f) * dev),
                            juce::jlimit (0.f, 1.f, base.drive + (r.nextFloat() - 0.5f) * dev * 0.5f), juce::jlimit (0.f, 1.f, base.bright + (r.nextFloat() - 0.5f) * dev) };
            const int nSubs = (int) familyInfo (fams[cat]).subs.size();
            int sub = i % nSubs;
            if (fams[cat] == F_HiHats) { static const int closed[5] = { 0, 2, 4, 5, 6 }, open[2] = { 1, 3 }; sub = (i % 3 == 2) ? open[(i / 3) % 2] : closed[i % 5]; }
            const auto d = generateDrum (fams[cat], sub, kitSeed * 1000 + cat * 100 + i, spec.sampleRate, dp);
            const auto f = root.getChildFile (dirs[cat]).getChildFile (juce::String (i + 1).paddedLeft ('0', 2) + "_" + safeFileName (d.name) + ".wav");
            if (exportWav (f, d.audio, spec.sampleRate, spec.bits)) ++ok; else ++failed;
            owner.kitProg.store ((float) ++done / (float) std::max (1, total));
        }
    owner.setKitMsg (failed == 0 && ok == total ? "Kit termine : " + juce::String (ok) + " fichiers dans " + root.getFullPathName()
                                                : "Kit incomplet : " + juce::String (ok) + " fichiers ecrits, " + juce::String (failed) + " erreurs");
}

// ---------------------------------------------------------------- état
juce::AudioProcessorEditor* TrapProcessor::createEditor() { return new TrapEditor (*this); }

void TrapProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto st = apvts.copyState();
    st.setProperty ("family", family.load(), nullptr); st.setProperty ("sub", sub.load(), nullptr); st.setProperty ("seed", juce::String (seed), nullptr);
    st.setProperty ("name", currentName, nullptr); st.setProperty ("origin", currentOrigin, nullptr); st.setProperty ("fav", currentFavorite, nullptr);
    st.setProperty ("g0", gen.variation, nullptr); st.setProperty ("g1", gen.complexity, nullptr); st.setProperty ("g2", gen.brightness, nullptr); st.setProperty ("g3", gen.movement, nullptr);
    st.setProperty ("g4", gen.width, nullptr); st.setProperty ("g5", gen.character, nullptr); st.setProperty ("g6", gen.intensity, nullptr);
    if (auto xml = st.createXml()) copyXmlToBinary (*xml, dest);
}

void TrapProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto st = juce::ValueTree::fromXml (*xml);
    apvts.replaceState (st);
    family.store (juce::jlimit (0, (int) F_count - 1, (int) st.getProperty ("family", 0))); sub.store ((int) st.getProperty ("sub", -1));
    seed = st.getProperty ("seed", "1").toString().getLargeIntValue();
    currentName = st.getProperty ("name", "Init").toString(); currentOrigin = st.getProperty ("origin", "generated").toString(); currentFavorite = (bool) st.getProperty ("fav", false);
    gen.variation = st.getProperty ("g0", 0.5f); gen.complexity = st.getProperty ("g1", 0.5f); gen.brightness = st.getProperty ("g2", 0.5f); gen.movement = st.getProperty ("g3", 0.5f);
    gen.width = st.getProperty ("g4", 0.5f); gen.character = st.getProperty ("g5", 0.5f); gen.intensity = st.getProperty ("g6", 0.5f);
    if (familyInfo (family.load()).drum) triggerAsyncUpdate();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TrapProcessor(); }
