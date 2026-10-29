#pragma once
#include "PluginProcessor.h"
#include "ThemeManager.h"
#include <functional>

// Interface pixel-art : l'illustration de référence sert de fond (recolorée selon la famille de sons),
// tout ce qui est dynamique (textes, knobs, clavier, pages) est dessiné en code par-dessus.
class TrapEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit TrapEditor (TrapProcessor&);
    ~TrapEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    using Pt = juce::Point<float>;
    using Rc = juce::Rectangle<float>;
    struct Hit { Rc r; std::function<void (Pt)> down; std::function<void (Pt, Pt)> drag; std::function<void()> up, dbl; };
    struct Row { juce::String name, info; bool fav = false, factory = false; juce::File file; TF::FactorySpec spec {}; };
    struct ThemeJob;

    void timerCallback() override;
    float refScale() const { return (float) getWidth() / 1536.f; }

    // thème
    void startTheme (int family);
    juce::Colour themed (juce::Colour c) const;
    void updateColours();

    // dessin
    juce::Font pix (float h) const;
    void text (juce::Graphics&, const juce::String&, Rc, juce::Colour, float size, juce::Justification j = juce::Justification::centred) const;
    void box (juce::Graphics&, Rc, juce::Colour fill, juce::Colour border, float w = 2.f) const;
    void button (juce::Graphics&, Rc, const juce::String&, bool on, std::function<void()> fn, juce::Colour accent = {});
    void knob (juce::Graphics&, Pt c, float rad, int pidx, const juce::String& valueText = {});
    void hslider (juce::Graphics&, Rc r, float& v, std::function<void()> changed = {});
    void addHit (Rc r, std::function<void (Pt)> down, std::function<void (Pt, Pt)> drag = {}, std::function<void()> up = {}, std::function<void()> dbl = {});
    juce::String fmt (int idx) const;

    void paintTabs (juce::Graphics&);
    void paintSoundPanels (juce::Graphics&);
    void paintKeyboard (juce::Graphics&);
    void paintBrowser (juce::Graphics&);
    void paintFxPage (juce::Graphics&);
    void paintNotImplemented (juce::Graphics&, const juce::String& title, const juce::StringArray& lines);
    void paintSceneInfo (juce::Graphics&);

    // actions
    void setPage (int p);
    void doGenerate();
    void audition();
    void openCategoryMenu();
    void refreshRows();
    void askName (const juce::String& title, const juce::String& def, std::function<void (juce::String)> cb);
    void savePresetAs (const juce::String& name);
    void toastMsg (const juce::String& s) { toast = s; toastTicks = 180; }
    void chooseFolderThen (std::function<void (juce::File)> cb);
    int  noteAt (Pt p, bool& isBlack) const;

    TrapProcessor& proc;
    juce::Typeface::Ptr typeface;
    juce::Image bgOrig, bgCur, bgPrev;
    std::unique_ptr<ThemeJob> job;
    int themeFamily = -1, pendingFamily = -1;
    float fade = 1.f;
    juce::Colour pri, sec, panelCol, barCol, keyBg;

    std::vector<Hit> hits;
    Hit active; bool hasActive = false; Pt dragStart; float dragVal = 0.f;

    int page = 2;                 // 0 BROWSER, 1 FX, 2 GENERATE, 3 ARP, 4 CHORDS
    int octave = 0, heldNote = -1, auditionNote = -1, auditionTicks = 0;
    bool regenDirty = false; juce::int64 regenAt = 0;
    juce::String toast; int toastTicks = 0;

    // navigateur
    juce::TextEditor searchEd, seedEd, nameEd;
    int filter = 0, selRow = -1, scroll = 0;
    std::vector<Row> rows;
    bool rowsDirty = true;
    KitSpec kit;
    int exportBits = 24, exportRateIdx = 0;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrapEditor)
};
