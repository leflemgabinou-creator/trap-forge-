#include "PluginEditor.h"
#include <BinaryData.h>

using namespace juce;
using namespace TF;

// ----------------------------------------------------------------------------- thème (recoloration en arrière-plan)
struct TrapEditor::ThemeJob : public Thread
{
    ThemeJob (Image s, int f) : Thread ("TrapForge theme"), src (s), family (f) {}
    void run() override
    {
        const auto& pal = paletteFor (family);
        Image out (Image::ARGB, src.getWidth(), src.getHeight(), false);
        { Graphics g (out); g.drawImageAt (src, 0, 0); }
        Image::BitmapData bd (out, Image::BitmapData::readWrite);
        for (int y = 0; y < bd.height && ! threadShouldExit(); ++y)
        {
            auto* line = bd.getLinePointer (y);
            for (int x = 0; x < bd.width; ++x)
            {
                auto* px = (PixelARGB*) (line + x * bd.pixelStride);
                float r = px->getRed() / 255.f, g = px->getGreen() / 255.f, b = px->getBlue() / 255.f;
                Theme::remap (r, g, b, pal);
                px->setARGB (255, (uint8) (r * 255.f + 0.5f), (uint8) (g * 255.f + 0.5f), (uint8) (b * 255.f + 0.5f));
            }
        }
        result = out;
        ready = true;
    }
    Image src, result; int family; std::atomic<bool> ready { false };
};

static const Colour kWhite (0xffdce8ff), kDim (0xff7088b8), kDark (0xff03091c);

TrapEditor::TrapEditor (TrapProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    typeface = Typeface::createSystemTypefaceFor (BinaryData::VT323Regular_ttf, (size_t) BinaryData::VT323Regular_ttfSize);
    bgOrig = ImageCache::getFromMemory (BinaryData::background_png, BinaryData::background_pngSize);
    bgCur = bgOrig;

    for (auto* ed : { &searchEd, &seedEd, &nameEd })
    {
        ed->setFont (pix (22.f)); ed->setColour (TextEditor::backgroundColourId, kDark); ed->setColour (TextEditor::textColourId, kWhite);
        ed->setColour (TextEditor::outlineColourId, Colour (0xff1a4a9a)); ed->setColour (TextEditor::focusedOutlineColourId, Colour (0xffff3a4a));
        ed->setMultiLine (false); ed->setScrollbarsShown (false); addChildComponent (ed);
    }
    searchEd.setTextToShowWhenEmpty ("recherche...", kDim); searchEd.onTextChange = [this] { rowsDirty = true; scroll = 0; };
    seedEd.setInputRestrictions (12, "0123456789");
    nameEd.setTextToShowWhenEmpty ("nom du fichier / du kit", kDim);

    themeFamily = -1;
    startTheme (proc.getFamily());
    setResizeLimits (768, 512, 2304, 1536);
    getConstrainer()->setFixedAspectRatio (1.5);
    setSize (1152, 768);
    setResizable (true, true);
    startTimerHz (30);
}

TrapEditor::~TrapEditor()
{
    stopTimer();
    if (job != nullptr) job->stopThread (3000);
}

Font TrapEditor::pix (float h) const { return Font (FontOptions (typeface).withHeight (h)); }

// ----------------------------------------------------------------------------- thème
Colour TrapEditor::themed (Colour c) const
{
    float r = c.getFloatRed(), g = c.getFloatGreen(), b = c.getFloatBlue();
    Theme::remap (r, g, b, paletteFor (themeFamily < 0 ? 0 : themeFamily));
    return Colour::fromFloatRGBA (r, g, b, c.getFloatAlpha());
}

void TrapEditor::updateColours()
{
    const auto& pal = paletteFor (themeFamily < 0 ? 0 : themeFamily);
    float r, g, b;
    Theme::primary (pal, r, g, b);   pri = Colour::fromFloatRGBA (r, g, b, 1.f);
    Theme::secondary (pal, r, g, b); sec = Colour::fromFloatRGBA (r, g, b, 1.f);
    panelCol = themed (Colour (1, 9, 28)); barCol = themed (Colour (0, 4, 20)); keyBg = themed (Colour (1, 6, 20));
}

void TrapEditor::startTheme (int fam)
{
    if (fam == themeFamily || fam == pendingFamily) return;
    if (job != nullptr) { if (! job->ready) { pendingFamily = fam; return; } job->stopThread (1000); job.reset(); }
    pendingFamily = fam;
    job = std::make_unique<ThemeJob> (bgOrig, fam);
    job->startThread();
}

void TrapEditor::timerCallback()
{
    if (job != nullptr && job->ready)
    {
        bgPrev = bgCur; bgCur = job->result; fade = 0.f;
        themeFamily = job->family; job->stopThread (1000); job.reset(); pendingFamily = -1;
        updateColours();
    }
    if (job == nullptr && proc.getFamily() != themeFamily && proc.getFamily() != pendingFamily) startTheme (proc.getFamily());
    if (fade < 1.f) fade = std::min (1.f, fade + 0.08f);

    if (regenDirty && Time::currentTimeMillis() - regenAt > 90) { regenDirty = false; proc.regenerateDrum(); if (proc.getDrum() != nullptr) { auditionNote = proc.getDrum()->rootNote; proc.keyboardState.noteOn (1, auditionNote, 0.8f); auditionTicks = 12; } }
    if (auditionTicks > 0 && --auditionTicks == 0 && auditionNote >= 0) { proc.keyboardState.noteOff (1, auditionNote, 0.f); auditionNote = -1; }
    if (toastTicks > 0) --toastTicks;
    if (page == 0 && ! searchEd.isVisible()) setPage (0);
    repaint();
}

// ----------------------------------------------------------------------------- outils de dessin
void TrapEditor::text (Graphics& g, const String& s, Rc r, Colour c, float size, Justification j) const
{
    g.setColour (c); g.setFont (pix (size));
    g.drawText (s, r.toNearestInt(), j, false);
}

void TrapEditor::box (Graphics& g, Rc r, Colour fill, Colour border, float w) const
{
    g.setColour (fill); g.fillRect (r);
    g.setColour (border); g.drawRect (r, w);
}

void TrapEditor::addHit (Rc r, std::function<void (Pt)> down, std::function<void (Pt, Pt)> drag, std::function<void()> up, std::function<void()> dbl)
{
    hits.push_back ({ r, std::move (down), std::move (drag), std::move (up), std::move (dbl) });
}

void TrapEditor::button (Graphics& g, Rc r, const String& label, bool on, std::function<void()> fn, Colour accent)
{
    const auto a = accent == Colour() ? sec : accent;
    box (g, r, on ? a.withAlpha (0.35f) : kDark, on ? a : a.withAlpha (0.55f), 2.f);
    text (g, label, r, on ? Colours::white : kWhite.withAlpha (0.85f), std::min (28.f, r.getHeight() * 0.8f));
    addHit (r, [fn] (Pt) { if (fn) fn(); });
}

String TrapEditor::fmt (int idx) const
{
    const auto& q = pinfo()[idx]; const float v = proc.get (idx);
    switch (idx)
    {
        case P_cutoff: return v >= 1000.f ? String (v / 1000.f, 1) + "k" : String (roundToInt (v)) + "Hz";
        case P_a: case P_d: case P_r: case P_fdec: case P_pdec: case P_glide: return v < 1.f ? String (roundToInt (v * 1000.f)) + "ms" : String (v, 2) + "s";
        case P_semi: return (v > 0 ? "+" : "") + String (roundToInt (v));
        case P_det: return String (v, 1) + "c";
        case P_eqLo: case P_eqMid: case P_eqHi: case P_master: return String (v, 1) + "dB";
        case P_fmratio: return String (v, 2);
        case P_penv: return String (v, 1) + "st";
        case P_lfoRate: return String (v, 1) + "Hz";
        case P_voices: return String (roundToInt (v));
        default: return String (roundToInt ((v - q.lo) / (q.hi - q.lo) * 100.f)) + "%";
    }
}

void TrapEditor::knob (Graphics& g, Pt c, float rad, int pidx, const String& valueText)
{
    auto* prm = proc.param (pidx);
    const float norm = prm->getValue();
    const float a0 = -2.356f, a1 = 2.356f, av = a0 + norm * (a1 - a0);

    for (int i = 0; i <= 10; ++i)   // petites graduations
    {
        const float a = a0 + (a1 - a0) * (float) i / 10.f, s = std::sin (a), co = std::cos (a);
        g.setColour (sec.withAlpha (0.55f));
        g.drawLine (c.x + s * (rad + 5.f), c.y - co * (rad + 5.f), c.x + s * (rad + 9.f), c.y - co * (rad + 9.f), 1.5f);
    }
    Path track, val;
    track.addCentredArc (c.x, c.y, rad, rad, 0.f, a0, a1, true);
    val.addCentredArc (c.x, c.y, rad, rad, 0.f, a0, av, true);
    g.setColour (sec.withAlpha (0.35f));  g.strokePath (track, PathStrokeType (4.f));
    g.setColour (pri.withAlpha (0.28f));  g.strokePath (val, PathStrokeType (9.f));
    g.setColour (pri);                    g.strokePath (val, PathStrokeType (4.f, PathStrokeType::curved, PathStrokeType::rounded));

    const float br = rad * 0.78f;
    g.setGradientFill (ColourGradient (Colour (0xff1c2848), c.x - br * 0.4f, c.y - br * 0.5f, Colour (0xff04081a), c.x + br, c.y + br, true));
    g.fillEllipse (c.x - br, c.y - br, br * 2.f, br * 2.f);
    g.setColour (sec.withAlpha (0.85f)); g.drawEllipse (c.x - br, c.y - br, br * 2.f, br * 2.f, 2.f);
    g.setColour (Colours::white);
    g.drawLine (c.x + std::sin (av) * br * 0.25f, c.y - std::cos (av) * br * 0.25f, c.x + std::sin (av) * br * 0.85f, c.y - std::cos (av) * br * 0.85f, 3.f);
    (void) valueText;

    addHit (Rc (c.x - rad - 8.f, c.y - rad - 8.f, (rad + 8.f) * 2.f, (rad + 8.f) * 2.f),
        [this, prm] (Pt) { dragVal = prm->getValue(); prm->beginChangeGesture(); },
        [this, prm, pidx] (Pt q, Pt s0)
        {
            prm->setValueNotifyingHost (jlimit (0.f, 1.f, dragVal - (q.y - s0.y) / 170.f));
            if (pidx >= P_m1 && familyInfo (proc.getFamily()).drum) { regenDirty = true; regenAt = Time::currentTimeMillis(); }
        },
        [prm] { prm->endChangeGesture(); },
        [prm] { prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->getDefaultValue()); prm->endChangeGesture(); });
}

void TrapEditor::hslider (Graphics& g, Rc r, float& v, std::function<void()> changed)
{
    g.setColour (sec.withAlpha (0.35f)); g.fillRect (r.withSizeKeepingCentre (r.getWidth(), 6.f));
    g.setColour (pri); g.fillRect (r.withSizeKeepingCentre (r.getWidth() * v, 6.f).withX (r.getX()));
    const float x = r.getX() + v * r.getWidth();
    box (g, Rc (x - 7.f, r.getCentreY() - 12.f, 14.f, 24.f), kDark, kWhite, 2.f);
    auto setFrom = [this, r, &v, changed] (Pt q) { v = jlimit (0.f, 1.f, (q.x - r.getX()) / r.getWidth()); if (changed) changed(); };
    addHit (r.expanded (8.f, 10.f), setFrom, [setFrom] (Pt q, Pt) { setFrom (q); });
}

// ----------------------------------------------------------------------------- actions
void TrapEditor::setPage (int p)
{
    page = p;
    const bool b = (p == 0);
    searchEd.setVisible (b); seedEd.setVisible (b); nameEd.setVisible (b);
    if (b) { rowsDirty = true; seedEd.setText (String (proc.getSeed()), dontSendNotification); resized(); }
}

void TrapEditor::audition()
{
    if (auditionNote >= 0) proc.keyboardState.noteOff (1, auditionNote, 0.f);
    if (familyInfo (proc.getFamily()).drum)
    {
        if (proc.getDrum() == nullptr) return;
        auditionNote = proc.getDrum()->rootNote; auditionTicks = 12;
    }
    else { auditionNote = 60 + 12 * octave - 12 * 0; auditionTicks = 24; }
    proc.keyboardState.noteOn (1, auditionNote, 0.8f);
}

void TrapEditor::doGenerate() { proc.generate(); audition(); rowsDirty = true; }

void TrapEditor::openCategoryMenu()
{
    PopupMenu menu;
    for (int f = 0; f < F_count; ++f)
    {
        PopupMenu subs;
        subs.addItem (1000 + f * 20, "Any");
        const auto& fi = familyInfo (f);
        for (int k = 0; k < (int) fi.subs.size(); ++k) subs.addItem (1000 + f * 20 + 1 + k, fi.subs[(size_t) k]);
        menu.addSubMenu (String (fi.name) + (fi.drum ? "  [drums]" : ""), subs, true, nullptr, f == proc.getFamily());
    }
    const auto area = localAreaToGlobal (Rectangle<float> (47.f, 660.f, 287.f, 33.f) .transformedBy (AffineTransform::scale (refScale())).toNearestInt());
    menu.showMenuAsync (PopupMenu::Options().withTargetScreenArea (area).withMinimumWidth (area.getWidth()), [this] (int r)
    {
        if (r < 1000) return;
        const int f = (r - 1000) / 20, s = (r - 1000) % 20 - 1;
        proc.setFamily (f, s); doGenerate();
    });
}

void TrapEditor::chooseFolderThen (std::function<void (File)> cb)
{
    chooser = std::make_unique<FileChooser> ("Choisir le dossier de destination", proc.presets.samplesDir());
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories, [cb] (const FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f != File()) cb (f);
    });
}

void TrapEditor::askName (const String& title, const String& def, std::function<void (String)> cb)
{
    auto* w = new AlertWindow (title, "", MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("n", def, "Nom");
    w->addButton ("OK", 1, KeyPress (KeyPress::returnKey));
    w->addButton ("Annuler", 0, KeyPress (KeyPress::escapeKey));
    w->enterModalState (true, ModalCallbackFunction::create ([w, cb] (int r) { if (r == 1) cb (w->getTextEditorContents ("n")); }), true);
}

void TrapEditor::savePresetAs (const String& name)
{
    auto d = proc.snapshot();
    d.name = name.trim().isEmpty() ? String ("Preset") : name.trim(); d.origin = "user";
    const auto f = proc.presets.presetFile (d.name, "user");
    auto doSave = [this, d, f]() mutable
    {
        if (proc.presets.save (d, f))
        {
            proc.currentName = d.name; proc.currentOrigin = "user"; proc.currentFile = f; proc.presets.addRecent (f);
            toastMsg ("Preset sauvegarde : " + f.getFileName()); rowsDirty = true;
        }
        else toastMsg ("Echec de la sauvegarde");
    };
    if (f.existsAsFile())
        AlertWindow::showOkCancelBox (MessageBoxIconType::WarningIcon, "Ecraser ?", "Le preset \"" + d.name + "\" existe deja. L'ecraser ?", "Ecraser", "Annuler", this,
                                      ModalCallbackFunction::create ([doSave] (int r) mutable { if (r == 1) doSave(); }));
    else doSave();
}

void TrapEditor::refreshRows()
{
    rows.clear();
    const auto search = searchEd.getText().trim();
    const int fam = -1;
    if (filter == 4)
    {
        for (const auto& fs : PresetManager::factory())
        {
            const auto g = generatePreset (fs.family, fs.sub, fs.seed);
            Row r; r.name = g.name; r.info = String (familyInfo (fs.family).name) + " / " + familyInfo (fs.family).subs[(size_t) fs.sub]; r.factory = true; r.spec = fs;
            if (search.isEmpty() || r.name.containsIgnoreCase (search) || r.info.containsIgnoreCase (search)) rows.push_back (r);
        }
    }
    else
        for (const auto& e : proc.presets.list())
        {
            if (filter == 1 && ! e.favorite) continue;
            if (filter == 2 && e.origin == "generated") continue;
            if (filter == 3 && e.origin != "generated") continue;
            if (! PresetManager::matches (e, search, fam, false)) continue;
            Row r; r.name = e.name; r.fav = e.favorite; r.file = e.file;
            r.info = String (familyInfo (e.family).name) + (e.origin == "generated" ? "  [gen]" : "");
            rows.push_back (r);
        }
    rowsDirty = false;
    selRow = -1;
}

int TrapEditor::noteAt (Pt q, bool& isBlack) const
{
    const Rc k (175.f, 790.f, 1223.f, 175.f);
    if (! k.contains (q)) return -1;
    const int base = 36 + 12 * octave;
    const float w = k.getWidth() / 36.f;
    isBlack = false;
    if (q.y < k.getY() + k.getHeight() * 0.58f)
    {
        const int wi = (int) std::round ((q.x - k.getX()) / w);   // frontière la plus proche
        static const int blackAfter[7] = { 1, 1, 0, 1, 1, 1, 0 };
        if (wi >= 1 && wi < 36 && blackAfter[(wi - 1) % 7] && std::abs (q.x - (k.getX() + (float) wi * w)) < w * 0.3f)
        {
            static const int semis[7] = { 0, 2, 4, 5, 7, 9, 11 };
            isBlack = true;
            return base + ((wi - 1) / 7) * 12 + semis[(wi - 1) % 7] + 1;
        }
    }
    const int wi = jlimit (0, 35, (int) ((q.x - k.getX()) / w));
    static const int semis[7] = { 0, 2, 4, 5, 7, 9, 11 };
    return base + (wi / 7) * 12 + semis[wi % 7];
}

// ----------------------------------------------------------------------------- dessin principal
void TrapEditor::paint (Graphics& g)
{
    hits.clear();
    g.fillAll (Colour (0xff000510));
    g.addTransform (AffineTransform::scale (refScale()));
    const Rectangle<float> full (0.f, 0.f, 1536.f, 1024.f);
    if (bgPrev.isValid() && fade < 1.f) g.drawImage (bgPrev, full);
    g.setOpacity (fade < 1.f ? fade : 1.f);
    g.drawImage (bgCur, full);
    g.setOpacity (1.f);
    if (themeFamily < 0) updateColours();

    paintTabs (g);
    if (page == 2) paintSceneInfo (g);
    else if (page == 0) paintBrowser (g);
    else if (page == 1) paintFxPage (g);
    else if (page == 3) paintNotImplemented (g, "ARP", { "Arpegiateur : PAS ENCORE IMPLEMENTE dans cette version.", "Prevu : vitesse synchronisee au tempo, octaves, gate, direction, swing.", "Les sons et le clavier fonctionnent normalement." });
    else paintNotImplemented (g, "CHORDS", { "Generateur d'accords : PAS ENCORE IMPLEMENTE dans cette version.", "Prevu : intervalles, majeur/mineur, extensions, renversements, voicings.", "Les sons et le clavier fonctionnent normalement." });

    paintSoundPanels (g);
    paintKeyboard (g);

    if (toastTicks > 0)
    {
        const Rc t (380.f, 484.f, 780.f, 34.f);
        box (g, t, kDark.withAlpha (0.92f), pri, 2.f);
        text (g, toast, t, Colours::white, 24.f);
    }
}

void TrapEditor::paintTabs (Graphics& g)
{
    g.setColour (barCol); g.fillRect (598.f, 14.f, 800.f, 60.f);
    static const char* names[5] = { "BROWSER", "FX", "GENERATE", "ARP", "CHORDS" };
    static const Rc rects[5] = { { 606, 22, 146, 46 }, { 765, 22, 135, 46 }, { 908, 20, 172, 50 }, { 1090, 22, 138, 46 }, { 1238, 22, 145, 46 } };
    for (int i = 0; i < 5; ++i)
    {
        const bool on = (page == i);
        box (g, rects[i], on ? pri.withAlpha (0.35f) : kDark, on ? pri : sec, on ? 3.f : 2.f);
        text (g, names[i], rects[i], on ? Colours::white : sec.brighter (0.3f), 32.f);
        addHit (rects[i], [this, i] (Pt) { setPage (i); });
    }
}

void TrapEditor::paintSceneInfo (Graphics& g)
{
    const auto& fi = familyInfo (proc.getFamily());
    const int s = proc.getSub();
    const String info = proc.currentName + "   |   " + fi.name + (s >= 0 ? String (" / ") + fi.subs[(size_t) s] : String (" / Any")) + "   |   seed " + String (proc.getSeed());
    const Rc r (22.f, 96.f, 640.f, 30.f);
    g.setColour (Colours::black.withAlpha (0.55f)); g.fillRect (r);
    text (g, info, r.reduced (8.f, 0.f), pri.brighter (0.4f), 24.f, Justification::centredLeft);
}

void TrapEditor::paintNotImplemented (Graphics& g, const String& title, const StringArray& lines)
{
    const Rc area (14.f, 90.f, 1508.f, 432.f);
    box (g, area, panelCol, sec, 3.f);
    text (g, title, Rc (40.f, 110.f, 600.f, 50.f), pri, 44.f, Justification::centredLeft);
    for (int i = 0; i < lines.size(); ++i) text (g, lines[i], Rc (40.f, 190.f + (float) i * 44.f, 1400.f, 40.f), i == 0 ? Colours::white : kDim, 30.f, Justification::centredLeft);
}

void TrapEditor::paintSoundPanels (Graphics& g)
{
    // ---- SOUND
    g.setColour (panelCol); g.fillRect (18.f, 544.f, 322.f, 210.f);
    text (g, "SOUND", Rc (50.f, 548.f, 200.f, 32.f), pri, 32.f, Justification::centredLeft);
    const Rc nameBox (47.f, 585.f, 287.f, 38.f);
    box (g, nameBox, kDark, sec, 2.f);
    text (g, proc.currentName + (proc.currentFavorite ? " *" : ""), Rc (57.f, 585.f, 215.f, 38.f), Colours::white, 28.f, Justification::centredLeft);
    const Rc undoB (276.f, 590.f, 24.f, 28.f), redoB (306.f, 590.f, 24.f, 28.f);
    Path t1, t2; t1.addTriangle (296.f, 595.f, 296.f, 613.f, 281.f, 604.f); t2.addTriangle (310.f, 595.f, 310.f, 613.f, 325.f, 604.f);
    g.setColour (sec.brighter (0.4f)); g.fillPath (t1); g.fillPath (t2);
    addHit (undoB, [this] (Pt) { if (proc.undo()) audition(); });
    addHit (redoB, [this] (Pt) { if (proc.redo()) audition(); });

    text (g, "CATEGORY", Rc (50.f, 632.f, 200.f, 28.f), pri.brighter (0.2f), 26.f, Justification::centredLeft);
    const Rc cat (47.f, 660.f, 287.f, 33.f);
    box (g, cat, kDark, sec, 2.f);
    const auto& fi = familyInfo (proc.getFamily()); const int sb = proc.getSub();
    text (g, String (fi.name) + (sb >= 0 && sb < (int) fi.subs.size() ? String (" - ") + fi.subs[(size_t) sb] : String()), cat.reduced (10.f, 0.f).withTrimmedRight (36.f), Colours::white, 26.f, Justification::centredLeft);
    Path dd; dd.addTriangle (306.f, 671.f, 324.f, 671.f, 315.f, 683.f); g.setColour (sec.brighter (0.4f)); g.fillPath (dd);
    addHit (cat, [this] (Pt) { openCategoryMenu(); });

    const Rc gen (47.f, 703.f, 287.f, 47.f);
    box (g, gen, pri.darker (0.8f), pri, 3.f);
    box (g, Rc (96.f, 716.f, 24.f, 24.f), kWhite, kWhite.darker (0.4f), 1.f);   // dé
    g.setColour (kDark);
    for (auto p : { Pt (103.f, 723.f), Pt (113.f, 723.f), Pt (108.f, 728.f), Pt (103.f, 733.f), Pt (113.f, 733.f) }) g.fillEllipse (p.x - 2.f, p.y - 2.f, 4.f, 4.f);
    text (g, "GENERATE", Rc (130.f, 703.f, 190.f, 47.f), pri.brighter (0.5f), 36.f);
    addHit (gen, [this] (Pt) { doGenerate(); });

    // ---- MACROS
    g.setColour (panelCol); g.fillRect (354.f, 545.f, 516.f, 208.f);
    static const float mx[4] = { 412.f, 540.f, 675.f, 805.f };
    for (int i = 0; i < 4; ++i)
    {
        text (g, "MACRO " + String (i + 1), Rc (mx[i] - 60.f, 552.f, 120.f, 26.f), sec.brighter (0.3f), 22.f);
        text (g, macroLabel (proc.getFamily(), i), Rc (mx[i] - 60.f, 576.f, 120.f, 28.f), Colours::white, 26.f);
        knob (g, { mx[i], 663.f }, 34.f, P_m1 + i);
        text (g, fmt (P_m1 + i), Rc (mx[i] - 50.f, 706.f, 100.f, 30.f), Colours::white, 26.f);
        if (i > 0) { g.setColour (sec.withAlpha (0.35f)); g.fillRect (mx[i] - 66.f, 560.f, 2.f, 180.f); }
    }

    // ---- FX
    g.setColour (panelCol); g.fillRect (885.f, 545.f, 638.f, 208.f);
    text (g, "FX", Rc (910.f, 548.f, 100.f, 32.f), pri, 32.f, Justification::centredLeft);
    static const float fx[6] = { 945.f, 1060.f, 1172.f, 1283.f, 1388.f, 1472.f };
    static const int fi6[6] = { P_cutoff, P_res, P_drive, P_rvb, P_dly, P_wid };
    static const char* fl[6] = { "Cutoff", "Resonance", "Drive", "Reverb", "Delay", "Width" };
    for (int i = 0; i < 6; ++i)
    {
        text (g, fl[i], Rc (fx[i] - 55.f, 596.f, 110.f, 28.f), Colours::white, 26.f);
        knob (g, { fx[i], 665.f }, 31.f, fi6[i]);
        text (g, fmt (fi6[i]), Rc (fx[i] - 50.f, 706.f, 100.f, 30.f), Colours::white, 26.f);
    }
}

void TrapEditor::paintKeyboard (Graphics& g)
{
    // ---- OCTAVE / molettes
    g.setColour (panelCol); g.fillRect (19.f, 779.f, 137.f, 202.f);
    text (g, "OCTAVE", Rc (20.f, 780.f, 135.f, 26.f), kWhite, 26.f);
    const Rc ob1 (38.f, 806.f, 28.f, 28.f), ob2 (110.f, 806.f, 28.f, 28.f);
    box (g, ob1, kDark, sec, 2.f); box (g, ob2, kDark, sec, 2.f);
    text (g, String (octave), Rc (68.f, 806.f, 40.f, 28.f), Colours::white, 28.f);
    Path l, r; l.addTriangle (58.f, 812.f, 58.f, 828.f, 46.f, 820.f); r.addTriangle (118.f, 812.f, 118.f, 828.f, 130.f, 820.f);
    g.setColour (sec.brighter (0.4f)); g.fillPath (l); g.fillPath (r);
    addHit (ob1, [this] (Pt) { octave = jmax (-2, octave - 1); });
    addHit (ob2, [this] (Pt) { octave = jmin (2, octave + 1); });

    text (g, "PITCH", Rc (36.f, 842.f, 56.f, 22.f), kDim, 22.f); text (g, "MOD", Rc (96.f, 842.f, 44.f, 22.f), kDim, 22.f);
    const Rc pw (48.f, 868.f, 30.f, 92.f), mw (104.f, 868.f, 30.f, 92.f);
    box (g, pw, kDark, sec, 2.f); box (g, mw, kDark, sec, 2.f);
    const float bend = proc.uiBend.load(), mod = proc.uiMod.load();
    box (g, Rc (pw.getX() + 3.f, pw.getCentreY() - 8.f - bend * 38.f, pw.getWidth() - 6.f, 16.f), pri.withAlpha (0.8f), Colours::white, 1.f);
    box (g, Rc (mw.getX() + 3.f, mw.getBottom() - 16.f - mod * 74.f, mw.getWidth() - 6.f, 16.f), sec.brighter (0.2f), Colours::white, 1.f);
    addHit (pw.expanded (6.f), [this, pw] (Pt q) { proc.uiBend.store (jlimit (-1.f, 1.f, (pw.getCentreY() - q.y) / 38.f)); },
            [this, pw] (Pt q, Pt) { proc.uiBend.store (jlimit (-1.f, 1.f, (pw.getCentreY() - q.y) / 38.f)); }, [this] { proc.uiBend.store (0.f); });
    addHit (mw.expanded (6.f), [this, mw] (Pt q) { proc.uiMod.store (jlimit (0.f, 1.f, (mw.getBottom() - 8.f - q.y) / 74.f)); },
            [this, mw] (Pt q, Pt) { proc.uiMod.store (jlimit (0.f, 1.f, (mw.getBottom() - 8.f - q.y) / 74.f)); });

    // ---- VOICES / GLIDE
    g.setColour (panelCol); g.fillRect (1414.f, 779.f, 110.f, 202.f);
    text (g, "VOICES", Rc (1410.f, 780.f, 110.f, 26.f), kWhite, 26.f);
    const Rc vb1 (1424.f, 806.f, 28.f, 28.f), vb2 (1482.f, 806.f, 28.f, 28.f);
    box (g, vb1, kDark, sec, 2.f); box (g, vb2, kDark, sec, 2.f);
    text (g, fmt (P_voices), Rc (1452.f, 806.f, 30.f, 28.f), Colours::white, 28.f);
    Path l2, r2; l2.addTriangle (1444.f, 812.f, 1444.f, 828.f, 1432.f, 820.f); r2.addTriangle (1490.f, 812.f, 1490.f, 828.f, 1502.f, 820.f);
    g.setColour (sec.brighter (0.4f)); g.fillPath (l2); g.fillPath (r2);
    auto* vp = proc.param (P_voices);
    addHit (vb1, [vp] (Pt) { vp->beginChangeGesture(); vp->setValueNotifyingHost (vp->convertTo0to1 (jmax (1.f, vp->convertFrom0to1 (vp->getValue()) - 1.f))); vp->endChangeGesture(); });
    addHit (vb2, [vp] (Pt) { vp->beginChangeGesture(); vp->setValueNotifyingHost (vp->convertTo0to1 (jmin (16.f, vp->convertFrom0to1 (vp->getValue()) + 1.f))); vp->endChangeGesture(); });
    text (g, "GLIDE", Rc (1410.f, 848.f, 110.f, 26.f), kWhite, 26.f);
    knob (g, { 1465.f, 908.f }, 28.f, P_glide);
    text (g, String (roundToInt (proc.get (P_glide) / 0.4f * 100.f)) + " %", Rc (1415.f, 948.f, 100.f, 28.f), Colours::white, 26.f);

    // ---- clavier
    const Rc kb (175.f, 790.f, 1223.f, 175.f);
    g.setColour (keyBg); g.fillRect (kb.expanded (3.f));
    const float w = kb.getWidth() / 36.f, bh = kb.getHeight() * 0.58f;
    const int base = 36 + 12 * octave;
    static const int semis[7] = { 0, 2, 4, 5, 7, 9, 11 };
    for (int i = 0; i < 36; ++i)
    {
        const int note = base + (i / 7) * 12 + semis[i % 7];
        const bool down = proc.keyboardState.isNoteOn (1, note);
        const Rc k (kb.getX() + (float) i * w + 1.f, kb.getY(), w - 2.f, kb.getHeight());
        g.setColour (down ? pri.brighter (0.2f) : Colour (0xfff2f6ff)); g.fillRect (k);
        g.setColour (Colour (0xff9db0d8)); g.drawRect (k, 1.f);
        if (i % 7 == 0) text (g, "C" + String (note / 12 - 1), Rc (k.getX() + 2.f, k.getBottom() - 30.f, w, 26.f), kDark, 26.f, Justification::centredLeft);
    }
    static const int blackAfter[7] = { 1, 1, 0, 1, 1, 1, 0 };
    for (int i = 1; i < 36; ++i)
        if (blackAfter[(i - 1) % 7])
        {
            const int note = base + ((i - 1) / 7) * 12 + semis[(i - 1) % 7] + 1;
            const bool down = proc.keyboardState.isNoteOn (1, note);
            const Rc k (kb.getX() + (float) i * w - w * 0.3f, kb.getY(), w * 0.6f, bh);
            g.setColour (down ? pri : Colour (0xff070b1e)); g.fillRect (k);
            g.setColour (sec.withAlpha (0.8f)); g.drawRect (k, 1.5f);
        }

    addHit (kb,
        [this] (Pt q) { bool b; const int n = noteAt (q, b); if (n >= 0) { heldNote = n; proc.keyboardState.noteOn (1, n, jlimit (0.3f, 1.f, 0.35f + (q.y - 790.f) / 175.f * 0.65f)); } },
        [this] (Pt q, Pt) { bool b; const int n = noteAt (q, b); if (n >= 0 && n != heldNote) { if (heldNote >= 0) proc.keyboardState.noteOff (1, heldNote, 0.f); heldNote = n; proc.keyboardState.noteOn (1, n, 0.8f); } },
        [this] { if (heldNote >= 0) proc.keyboardState.noteOff (1, heldNote, 0.f); heldNote = -1; });
}

// ----------------------------------------------------------------------------- page FX / moteur
void TrapEditor::paintFxPage (Graphics& g)
{
    const Rc area (14.f, 90.f, 1508.f, 432.f);
    box (g, area, panelCol, sec, 3.f);
    text (g, "EFFECTS", Rc (34.f, 98.f, 300.f, 30.f), pri, 32.f, Justification::centredLeft);
    static const int fxk[10] = { P_dly, P_cho, P_cmp, P_rvb, P_rsize, P_wid, P_eqLo, P_eqMid, P_eqHi, P_master };
    static const int fxOn[10] = { P_dlyOn, P_choOn, P_cmpOn, P_rvbOn, -1, P_widOn, -1, P_eqOn, -1, -1 };
    for (int i = 0; i < 10; ++i)
    {
        const float x = 100.f + (float) i * 148.f;
        text (g, pinfo()[fxk[i]].name, Rc (x - 66.f, 128.f, 132.f, 24.f), Colours::white, 24.f);
        knob (g, { x, 188.f }, 24.f, fxk[i]);
        text (g, fmt (fxk[i]), Rc (x - 50.f, 220.f, 100.f, 24.f), kWhite, 24.f);
        if (fxOn[i] >= 0)
        {
            auto* pp = proc.param (fxOn[i]); const bool on = proc.get (fxOn[i]) > 0.5f;
            button (g, Rc (x - 30.f, 246.f, 60.f, 24.f), on ? "ON" : "OFF", on, [pp, on] { pp->beginChangeGesture(); pp->setValueNotifyingHost (on ? 0.f : 1.f); pp->endChangeGesture(); });
        }
    }

    text (g, "OSCILLATORS / FILTER", Rc (34.f, 280.f, 500.f, 30.f), pri, 32.f, Justification::centredLeft);
    static const int osk[8] = { P_o1, P_o2, P_semi, P_det, P_sub, P_noise, P_fm, P_fmratio };
    for (int i = 0; i < 8; ++i)
    {
        const float x = 90.f + (float) i * 112.f;
        text (g, pinfo()[osk[i]].name, Rc (x - 52.f, 310.f, 104.f, 24.f), Colours::white, 24.f);
        knob (g, { x, 362.f }, 22.f, osk[i]);
        text (g, fmt (osk[i]), Rc (x - 50.f, 392.f, 100.f, 24.f), kWhite, 24.f);
    }
    auto choice = [&] (float y, const String& label, int pidx, int n, std::function<String (int)> nm)
    {
        text (g, label, Rc (1010.f, y, 120.f, 30.f), Colours::white, 26.f, Justification::centredLeft);
        auto* pp = proc.param (pidx); const int cur = (int) std::round (proc.get (pidx));
        for (int k = 0; k < n; ++k)
            button (g, Rc (1130.f + (float) k * 86.f, y, 80.f, 30.f), nm (k), cur == k, [pp, k, pidx] { pp->beginChangeGesture(); pp->setValueNotifyingHost (pp->convertTo0to1 ((float) k)); pp->endChangeGesture(); });
    };
    choice (306.f, "WAVE 1", P_wave1, 4, [] (int k) { return String (waveName (k)); });
    choice (346.f, "WAVE 2", P_wave2, 4, [] (int k) { return String (waveName (k)); });
    choice (386.f, "FILTER", P_ftype, 3, [] (int k) { return String (filterName (k)); });
    {
        auto* pp = proc.param (P_mono); const bool on = proc.get (P_mono) > 0.5f;
        button (g, Rc (1130.f, 426.f, 166.f, 30.f), on ? "MONO" : "POLY", on, [pp, on] { pp->beginChangeGesture(); pp->setValueNotifyingHost (on ? 0.f : 1.f); pp->endChangeGesture(); });
    }

    text (g, "ENVELOPES / MODULATION", Rc (34.f, 424.f, 600.f, 30.f), pri, 32.f, Justification::centredLeft);
    static const int evk[13] = { P_a, P_d, P_s, P_r, P_fdec, P_fenv, P_penv, P_pdec, P_lfoRate, P_lfoPitch, P_lfoFilter, P_lfoAmp, P_lfoPan };
    for (int i = 0; i < 13; ++i)
    {
        const float x = 66.f + (float) i * 76.f;
        text (g, pinfo()[evk[i]].name, Rc (x - 36.f, 452.f, 72.f, 20.f), Colours::white, 19.f);
        knob (g, { x, 486.f }, 16.f, evk[i]);
        (void) 0;
    }
}

// ----------------------------------------------------------------------------- navigateur + drum forge
void TrapEditor::paintBrowser (Graphics& g)
{
    const Rc area (14.f, 90.f, 1508.f, 432.f);
    box (g, area, panelCol, sec, 3.f);
    if (rowsDirty) refreshRows();

    // ---- colonne A : presets
    text (g, "BROWSER", Rc (30.f, 94.f, 300.f, 28.f), pri, 30.f, Justification::centredLeft);
    button (g, Rc (290.f, 120.f, 90.f, 28.f), "SAVE", false, [this]
    {
        if (proc.currentFile.existsAsFile() && proc.currentOrigin == "user") savePresetAs (proc.currentName); else askName ("Sauvegarder le preset", proc.currentName, [this] (String n) { savePresetAs (n); });
    });
    button (g, Rc (384.f, 120.f, 100.f, 28.f), "SAVE AS", false, [this] { askName ("Sauvegarder sous...", proc.currentName, [this] (String n) { savePresetAs (n); }); });
    static const char* fl[5] = { "ALL", "FAV", "USER", "GEN", "FACTORY" };
    for (int i = 0; i < 5; ++i) button (g, Rc (30.f + (float) i * 92.f, 154.f, 88.f, 26.f), fl[i], filter == i, [this, i] { filter = i; rowsDirty = true; scroll = 0; });
    const int visible = 11;
    const int maxScroll = jmax (0, (int) rows.size() - visible);
    scroll = jlimit (0, maxScroll, scroll);
    for (int i = 0; i < visible; ++i)
    {
        const int ri = i + scroll; if (ri >= (int) rows.size()) break;
        const auto& r = rows[(size_t) ri];
        const Rc rr (30.f, 186.f + (float) i * 24.f, 458.f, 23.f);
        const bool sel = (ri == selRow);
        g.setColour (sel ? pri.withAlpha (0.3f) : (i % 2 ? kDark : panelCol.brighter (0.05f))); g.fillRect (rr);
        text (g, String (r.fav ? "* " : "  ") + r.name, rr.withTrimmedRight (180.f).reduced (6.f, 0.f), sel ? Colours::white : kWhite.withAlpha (0.9f), 23.f, Justification::centredLeft);
        text (g, r.info, rr.withTrimmedLeft (280.f).reduced (6.f, 0.f), kDim, 20.f, Justification::centredRight);
        addHit (rr, [this, ri, r] (Pt)
        {
            selRow = ri;
            if (r.factory)
            {
                proc.setFamily (r.spec.family, r.spec.sub); const GenControls neutral; proc.generateWithSeed (r.spec.seed, &neutral, "factory"); audition();
            }
            else
            {
                PresetData d;
                if (PresetManager::load (r.file, d)) { proc.recall (d); proc.currentFile = r.file; proc.presets.addRecent (r.file); audition(); }
                else toastMsg ("Preset illisible");
            }
        });
    }
    if (rows.empty()) text (g, "Aucun preset. Genere un son puis SAVE.", Rc (30.f, 200.f, 450.f, 30.f), kDim, 24.f, Justification::centredLeft);
    const bool hasSel = selRow >= 0 && selRow < (int) rows.size() && ! rows[(size_t) selRow].factory;
    const File selFile = hasSel ? rows[(size_t) selRow].file : File();
    button (g, Rc (30.f, 478.f, 100.f, 28.f), "RENAME", false, [this, hasSel, selFile]
    {
        if (! hasSel) { toastMsg ("Selectionne d'abord un preset de la liste"); return; }
        askName ("Renommer", selFile.getFileNameWithoutExtension(), [this, selFile] (String n) { auto nf = proc.presets.rename (selFile, n); toastMsg (nf.existsAsFile() ? "Renomme" : "Echec (nom deja pris ?)"); rowsDirty = true; });
    });
    button (g, Rc (136.f, 478.f, 100.f, 28.f), "DELETE", false, [this, hasSel, selFile]
    {
        if (! hasSel) { toastMsg ("Selectionne d'abord un preset de la liste"); return; }
        AlertWindow::showOkCancelBox (MessageBoxIconType::WarningIcon, "Supprimer ?", "Supprimer definitivement \"" + selFile.getFileNameWithoutExtension() + "\" ?", "Supprimer", "Annuler", this,
            ModalCallbackFunction::create ([this, selFile] (int r) { if (r == 1) { selFile.deleteFile(); toastMsg ("Preset supprime"); rowsDirty = true; } }));
    });
    button (g, Rc (242.f, 478.f, 100.f, 28.f), "FAV", false, [this, hasSel, selFile]
    {
        if (! hasSel) { toastMsg ("Selectionne d'abord un preset de la liste"); return; }
        PresetData d; if (PresetManager::load (selFile, d)) { proc.presets.setFavorite (selFile, ! d.favorite); rowsDirty = true; }
    });
    text (g, "(" + String ((int) rows.size()) + ")", Rc (440.f, 94.f, 50.f, 28.f), kDim, 24.f);

    // ---- colonne B : réglages du générateur
    text (g, "GENERATOR", Rc (520.f, 94.f, 300.f, 28.f), pri, 30.f, Justification::centredLeft);
    static const char* gl[7] = { "Variation", "Complexity", "Brightness", "Movement", "Stereo Width", "Character", "Intensity" };
    float* gv[7] = { &proc.gen.variation, &proc.gen.complexity, &proc.gen.brightness, &proc.gen.movement, &proc.gen.width, &proc.gen.character, &proc.gen.intensity };
    for (int i = 0; i < 7; ++i)
    {
        const float y = 138.f + (float) i * 38.f;
        text (g, gl[i], Rc (520.f, y - 12.f, 150.f, 28.f), Colours::white, 24.f, Justification::centredLeft);
        hslider (g, Rc (680.f, y, 220.f, 8.f), *gv[i]);
        text (g, String (roundToInt (*gv[i] * 100.f)) + "%", Rc (910.f, y - 12.f, 60.f, 28.f), kWhite, 24.f);
    }
    text (g, "SEED", Rc (520.f, 410.f, 80.f, 28.f), Colours::white, 24.f, Justification::centredLeft);
    button (g, Rc (730.f, 408.f, 120.f, 30.f), "FROM SEED", false, [this] { proc.generateWithSeed (seedEd.getText().getLargeIntValue()); audition(); });
    button (g, Rc (856.f, 408.f, 114.f, 30.f), "RE-ROLL", false, [this] { doGenerate(); seedEd.setText (String (proc.getSeed()), dontSendNotification); });
    text (g, "Chaque son = famille + sous-categorie + graine + reglages (sauvegardes dans le preset).", Rc (520.f, 446.f, 470.f, 44.f), kDim, 20.f, Justification::topLeft);
    seedEd.setText (seedEd.hasKeyboardFocus (true) ? seedEd.getText() : String (proc.getSeed()), dontSendNotification);

    // ---- colonne C : drum forge
    text (g, "DRUM FORGE", Rc (1010.f, 94.f, 300.f, 28.f), pri, 30.f, Justification::centredLeft);
    const auto drum = proc.getDrum();
    if (drum == nullptr)
        text (g, "Choisis une famille de drums (808, Claps, Snares, Kicks, Hi-hats, Percs) dans CATEGORY.", Rc (1010.f, 124.f, 500.f, 48.f), kDim, 22.f, Justification::topLeft);
    else
    {
        text (g, drum->name + "   " + String (drum->audio.getNumSamples() / drum->sampleRate, 2) + "s  " + (drum->audio.getNumChannels() > 1 ? "stereo" : "mono"), Rc (1010.f, 122.f, 500.f, 26.f), Colours::white, 24.f, Justification::centredLeft);
        // forme d'onde
        const Rc wv (1010.f, 152.f, 500.f, 54.f);
        box (g, wv, kDark, sec.withAlpha (0.6f), 1.f);
        const int n = drum->audio.getNumSamples(); const auto* d0 = drum->audio.getReadPointer (0);
        g.setColour (pri);
        for (int x = 0; x < 500; x += 2)
        {
            const int s0 = (int) ((int64) x * n / 500), s1 = jmax (s0 + 1, (int) ((int64) (x + 2) * n / 500));
            float mn = 0.f, mxv = 0.f; for (int k = s0; k < s1 && k < n; ++k) { mn = jmin (mn, d0[k]); mxv = jmax (mxv, d0[k]); }
            g.fillRect (wv.getX() + (float) x, wv.getCentreY() - mxv * 25.f, 2.f, jmax (1.f, (mxv - mn) * 25.f));
        }
        button (g, Rc (1010.f, 214.f, 76.f, 28.f), "PREV", false, [this] { proc.drumStep (-1); audition(); });
        button (g, Rc (1090.f, 214.f, 76.f, 28.f), "PLAY", false, [this] { audition(); });
        button (g, Rc (1170.f, 214.f, 76.f, 28.f), "NEXT", false, [this] { proc.drumStep (1); audition(); });
        button (g, Rc (1250.f, 214.f, 110.f, 28.f), "REGENERATE", false, [this] { doGenerate(); });
        button (g, Rc (1364.f, 214.f, 70.f, 28.f), "SAVE", false, [this] { askName ("Sauvegarder le preset drum", proc.currentName, [this] (String n) { savePresetAs (n); }); });

        text (g, "EXPORT", Rc (1010.f, 252.f, 70.f, 26.f), Colours::white, 24.f, Justification::centredLeft);
        for (int i = 0; i < 2; ++i) button (g, Rc (1080.f + (float) i * 56.f, 252.f, 52.f, 26.f), i == 0 ? "16" : "24", exportBits == (i == 0 ? 16 : 24), [this, i] { exportBits = i == 0 ? 16 : 24; });
        static const char* rn[3] = { "44.1k", "48k", "96k" };
        for (int i = 0; i < 3; ++i) button (g, Rc (1200.f + (float) i * 62.f, 252.f, 58.f, 26.f), rn[i], exportRateIdx == i, [this, i] { exportRateIdx = i; });
        button (g, Rc (1392.f, 252.f, 118.f, 26.f), "EXPORT WAV", false, [this]
        {
            chooseFolderThen ([this] (File dir)
            {
                static const double rates[3] = { 44100.0, 48000.0, 96000.0 };
                const auto f = proc.exportCurrentDrum (dir, nameEd.getText().trim(), rates[exportRateIdx], exportBits);
                toastMsg (f.existsAsFile() ? "Exporte : " + f.getFullPathName() : "Echec de l'export");
            });
        });
    }

    text (g, "DRUM KIT", Rc (1010.f, 290.f, 300.f, 28.f), pri, 30.f, Justification::centredLeft);
    static const char* kn[6] = { "808", "Claps", "Snares", "Kicks", "Hi-hats", "Percs" };
    for (int i = 0; i < 6; ++i)
    {
        const float y = 322.f + (float) i * 26.f;
        text (g, kn[i], Rc (1010.f, y, 90.f, 24.f), Colours::white, 23.f, Justification::centredLeft);
        button (g, Rc (1100.f, y, 24.f, 24.f), "-", false, [this, i] { kit.count[i] = jmax (0, kit.count[i] - 1); });
        text (g, String (kit.count[i]), Rc (1126.f, y, 36.f, 24.f), Colours::white, 24.f);
        button (g, Rc (1164.f, y, 24.f, 24.f), "+", false, [this, i] { kit.count[i] = jmin (24, kit.count[i] + 1); });
    }
    text (g, "Saturation", Rc (1210.f, 316.f, 120.f, 24.f), Colours::white, 22.f, Justification::centredLeft);
    hslider (g, Rc (1330.f, 326.f, 140.f, 8.f), kit.saturation);
    text (g, "Coherence", Rc (1210.f, 350.f, 120.f, 24.f), Colours::white, 22.f, Justification::centredLeft);
    hslider (g, Rc (1330.f, 360.f, 140.f, 8.f), kit.coherence);
    nameEd.setVisible (true);
    const bool running = proc.kitRunning();
    button (g, Rc (1210.f, 392.f, 262.f, 32.f), running ? "GENERATING..." : "GENERATE DRUM KIT", false, [this, running]
    {
        if (running) return;
        chooseFolderThen ([this] (File dir)
        {
            static const double rates[3] = { 44100.0, 48000.0, 96000.0 };
            kit.bits = exportBits; kit.sampleRate = rates[exportRateIdx];
            kit.name = nameEd.getText().trim().isEmpty() ? String ("TrapForge_Kit") : nameEd.getText().trim();
            proc.startKit (kit, dir);
        });
    });
    const Rc pb (1010.f, 440.f, 500.f, 14.f);
    box (g, pb, kDark, sec, 1.f);
    g.setColour (pri); g.fillRect (pb.withWidth (pb.getWidth() * proc.kitProgress()));
    text (g, proc.kitStatus(), Rc (1010.f, 456.f, 500.f, 60.f), kWhite, 20.f, Justification::topLeft);
}

// ----------------------------------------------------------------------------- souris
void TrapEditor::resized()
{
    const float s = refScale();
    auto place = [s] (TextEditor& e, float x, float y, float w, float h) { e.setBounds (Rectangle<float> (x * s, y * s, w * s, h * s).toNearestInt()); e.setFont (Font (FontOptions (22.f * s * 1.0f))); };
    place (searchEd, 30.f, 120.f, 250.f, 28.f);
    place (seedEd, 640.f, 408.f, 80.f, 30.f);
    seedEd.setBounds (Rectangle<float> (580.f * s, 408.f * s, 140.f * s, 30.f * s).toNearestInt());
    place (nameEd, 1010.f, 290.f, 190.f, 26.f);
    nameEd.setBounds (Rectangle<float> (1330.f * s, 284.f * s, 180.f * s, 26.f * s).toNearestInt());
}

void TrapEditor::mouseDown (const MouseEvent& e)
{
    const Pt q = e.position / refScale();
    hasActive = false;
    for (int i = (int) hits.size() - 1; i >= 0; --i)
        if (hits[(size_t) i].r.contains (q))
        {
            active = hits[(size_t) i]; hasActive = true; dragStart = q;
            if (active.down) active.down (q);
            break;
        }
}

void TrapEditor::mouseDrag (const MouseEvent& e)
{
    if (hasActive && active.drag) active.drag (e.position / refScale(), dragStart);
}

void TrapEditor::mouseUp (const MouseEvent&)
{
    if (hasActive && active.up) active.up();
    hasActive = false;
}

void TrapEditor::mouseDoubleClick (const MouseEvent& e)
{
    const Pt q = e.position / refScale();
    for (int i = (int) hits.size() - 1; i >= 0; --i)
        if (hits[(size_t) i].r.contains (q)) { if (hits[(size_t) i].dbl) hits[(size_t) i].dbl(); break; }
}

void TrapEditor::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    const Pt q = e.position / refScale();
    if (page == 0 && Rc (30.f, 186.f, 458.f, 264.f).contains (q)) { scroll -= (int) std::round (w.deltaY * 4.f); return; }
    for (int i = (int) hits.size() - 1; i >= 0; --i)   // molette sur un knob
        if (hits[(size_t) i].r.contains (q) && hits[(size_t) i].dbl) { return; }
}
