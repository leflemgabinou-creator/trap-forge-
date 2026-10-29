#pragma once
// Gestionnaire de presets : sauvegarde exacte (tous les paramètres + graine + famille), chargement, renommage,
// suppression, favoris, récents, recherche. Fichiers XML .tfpreset dans Documents/TrapForge.
#include <juce_core/juce_core.h>
#include "PresetGenerator.h"

namespace TF
{
struct PresetData
{
    juce::String name { "Init" };
    int family = 0, sub = 0;
    juce::int64 seed = 0;
    ParamVec p = defaultParams();
    bool favorite = false;
    juce::String origin { "user" };     // user | generated | factory
    int version = 1;
};

struct PresetEntry
{
    juce::File file;
    juce::String name, origin;
    int family = 0, sub = 0;
    bool favorite = false;
    juce::int64 modified = 0;
};

struct FactorySpec { int family, sub; juce::int64 seed; };

class PresetManager
{
public:
    explicit PresetManager (juce::File rootDir = defaultRoot()) : root (rootDir)
    {
        for (auto* d : { "Presets/User", "Presets/Generated", "Samples", "Kits" }) root.getChildFile (d).createDirectory();
    }

    static juce::File defaultRoot() { return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("TrapForge"); }
    juce::File getRoot() const { return root; }
    juce::File samplesDir() const { return root.getChildFile ("Samples"); }
    juce::File kitsDir() const { return root.getChildFile ("Kits"); }
    juce::File presetFile (const juce::String& name, const juce::String& origin) const
    {
        return root.getChildFile (origin == "generated" ? "Presets/Generated" : "Presets/User").getChildFile (safe (name) + ".tfpreset");
    }

    bool save (const PresetData& d, const juce::File& dest) const
    {
        juce::XmlElement x ("TrapForgePreset");
        x.setAttribute ("version", d.version); x.setAttribute ("name", d.name); x.setAttribute ("family", d.family);
        x.setAttribute ("sub", d.sub); x.setAttribute ("seed", juce::String (d.seed)); x.setAttribute ("favorite", d.favorite ? 1 : 0);
        x.setAttribute ("origin", d.origin);
        for (int i = 0; i < P_count; ++i)
        {
            auto* e = x.createNewChildElement ("P");
            e->setAttribute ("id", pinfo()[i].id); e->setAttribute ("v", (double) d.p[(size_t) i]);
        }
        dest.getParentDirectory().createDirectory();
        return x.writeTo (dest);
    }

    static bool load (const juce::File& f, PresetData& d)
    {
        auto x = juce::XmlDocument::parse (f);
        if (x == nullptr || ! x->hasTagName ("TrapForgePreset")) return false;
        d = PresetData();
        d.version = x->getIntAttribute ("version", 1); d.name = x->getStringAttribute ("name", f.getFileNameWithoutExtension());
        d.family = x->getIntAttribute ("family", 0); d.sub = x->getIntAttribute ("sub", 0);
        d.seed = x->getStringAttribute ("seed", "0").getLargeIntValue(); d.favorite = x->getIntAttribute ("favorite", 0) != 0;
        d.origin = x->getStringAttribute ("origin", "user");
        for (auto* e : x->getChildWithTagNameIterator ("P"))   // paramètres inconnus ignorés, manquants = valeur par défaut
        {
            const int idx = findParam (e->getStringAttribute ("id"));
            if (idx >= 0) d.p[(size_t) idx] = clampParam (idx, (float) e->getDoubleAttribute ("v", pinfo()[idx].def));
        }
        return true;
    }

    std::vector<PresetEntry> list() const
    {
        std::vector<PresetEntry> out;
        for (auto& f : root.getChildFile ("Presets").findChildFiles (juce::File::findFiles, true, "*.tfpreset"))
            if (auto x = juce::XmlDocument::parse (f))
            {
                PresetEntry e;
                e.file = f; e.name = x->getStringAttribute ("name", f.getFileNameWithoutExtension());
                e.origin = x->getStringAttribute ("origin", "user"); e.family = x->getIntAttribute ("family");
                e.sub = x->getIntAttribute ("sub"); e.favorite = x->getIntAttribute ("favorite") != 0;
                e.modified = f.getLastModificationTime().toMilliseconds();
                out.push_back (e);
            }
        std::sort (out.begin(), out.end(), [] (const PresetEntry& a, const PresetEntry& b) { return a.name.compareNatural (b.name) < 0; });
        return out;
    }

    static bool matches (const PresetEntry& e, const juce::String& search, int familyFilter, bool favOnly)
    {
        if (favOnly && ! e.favorite) return false;
        if (familyFilter >= 0 && e.family != familyFilter) return false;
        return search.isEmpty() || e.name.containsIgnoreCase (search) || juce::String (familyInfo (e.family).name).containsIgnoreCase (search);
    }

    bool setFavorite (const juce::File& f, bool fav) const
    {
        PresetData d; if (! load (f, d)) return false;
        d.favorite = fav; return save (d, f);
    }

    // renvoie le nouveau fichier (ou File() en cas d'échec / de nom déjà pris)
    juce::File rename (const juce::File& f, const juce::String& newName) const
    {
        PresetData d; if (! load (f, d)) return {};
        auto dest = f.getParentDirectory().getChildFile (safe (newName) + ".tfpreset");
        if (dest.existsAsFile() && dest != f) return {};
        d.name = newName;
        if (! save (d, dest)) return {};
        if (dest != f) f.deleteFile();
        return dest;
    }

    void addRecent (const juce::File& f) const
    {
        auto lines = recent();
        lines.removeString (f.getFullPathName());
        lines.insert (0, f.getFullPathName());
        while (lines.size() > 12) lines.remove (lines.size() - 1);
        root.getChildFile ("recent.txt").replaceWithText (lines.joinIntoString ("\n"));
    }
    juce::StringArray recent() const
    {
        juce::StringArray l; l.addLines (root.getChildFile ("recent.txt").loadFileAsString());
        l.removeEmptyStrings(); return l;
    }

    // presets d'usine : définis par (famille, sous-catégorie, graine), recréés à l'identique par le moteur
    static std::vector<FactorySpec> factory()
    {
        std::vector<FactorySpec> v;
        for (int f = 0; f < F_count; ++f)
        {
            const int n = familyInfo (f).drum ? 2 : 3;
            for (int k = 0; k < n; ++k) v.push_back ({ f, k, (juce::int64) (7000 + f * 31 + k * 7) });
        }
        return v;
    }

    static juce::String safe (juce::String s)
    {
        s = s.retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 _-()");
        s = s.trim(); return s.isEmpty() ? juce::String ("Preset") : s;
    }

private:
    juce::File root;
};
}
