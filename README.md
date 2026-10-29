# TRAP FORGE (v0.2.0) — synthé VST3 + générateur de presets et de drums

Instrument pour Windows (FL Studio), fait avec JUCE 8 / C++17 / CMake. Tout fonctionne hors ligne.

## Installer (sans rien compiler)
1. Mets ce dossier dans un dépôt GitHub (le dossier caché `.github` doit y être).
2. Onglet **Actes** (Actions) → le build « Build VST3 » démarre tout seul.
3. En bas de la page du run, télécharge l'artefact **TrapForge-VST3**, dézippe-le.
4. Copie `TrapForge.vst3` dans `C:\Program Files\Common Files\VST3\`.
5. Dans FL Studio : Options → Manage plugins → Find plugins, puis ajoute **TrapForge** comme instrument.

## Compiler soi-même (Windows, Visual Studio 2022 + CMake)
```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```
Résultat : `build/TrapForge_artefacts/Release/VST3/TrapForge.vst3`
Tests hors DAW : `cmake -B build -DTRAPFORGE_BUILD_TESTS=ON` puis `cmake --build build --target TrapForgeTests --config Release`.

## Organisation du code (Source/)
| Fichier | Rôle |
|---|---|
| `ParamTable.h` | table unique de tous les paramètres (automation, presets, tests) |
| `SynthEngine.h` | voix polyphoniques : 2 oscillateurs, sub, bruit, FM, filtre LP/HP/BP, ADSR, LFO, glide, pitch env |
| `Effects.h` | EQ 3 bandes, compresseur, chorus, delay (noire pointée au tempo), width, reverb, limiteur doux |
| `PresetGenerator.*` | recettes de timbres, 16 familles, sous-catégories, graine, macros (module indépendant de l'interface) |
| `DrumGenerator.*` | 808, kicks, snares, claps, hi-hats, rims/toms/percs/impacts/risers/downlifters |
| `SampleExporter.h` | export WAV 16/24 bits avec vérification par relecture |
| `PresetManager.h` | sauvegarde/chargement/renommage/favoris/recherche/récents, presets d'usine |
| `ThemeManager.h` | palettes par famille + recoloration déterministe de l'illustration |
| `PluginProcessor.*`, `PluginEditor.*` | plugin VST3 et interface |
| `Tests/Tests.cpp` | tests hors DAW |

Polices : VT323 (SIL Open Font License). Illustration : `Resources/Images/background.png` (ta référence).

## ÉTAT HONNÊTE DES FONCTIONNALITÉS

### Implémenté ET testé hors DAW (Tests/Tests.cpp, 13 824 vérifications, 0 échec, sous Linux)
- Moteur de synthèse : rendu de 240 presets (10 familles × sous-catégories × graines) sans NaN, sans dépassement, non muets ; polyphonie 16 voix avec vol de voix ; glide ; mono.
- Générateur de presets : reproductible (même graine = mêmes paramètres), paramètres toujours dans leurs plages.
- Drums : 156 samples (6 familles × sous-catégories × graines), reproductibles, niveau -1 dBFS, durées cohérentes.
- Export WAV 16/24 bits, mono/stéréo, 44,1/48/96 kHz, relu et comparé.
- Presets : sauvegarde exacte, rappel, renommage, favoris, recherche, suppression.
- Thèmes : remappage des couleurs (gris, orange et sombres préservés).
- Des exemples audio produits par ce code sont dans `Examples/` (presets/ et drums/).

### Implémenté, vérifié seulement à la compilation (pas testé dans FL Studio)
- Le plugin VST3 lui-même, l'interface pixel-art, le clavier virtuel, les molettes, le navigateur, le Drum Forge, la génération de kit en arrière-plan, la sauvegarde d'état du projet.
- La disposition des éléments est calée sur l'image de référence mais n'a pas pu être vue à l'écran : des décalages sont possibles.

### Pas encore développé
- Sampler (chargement de WAV, boucle, reverse, début/fin) — le Drum Forge joue ses propres samples.
- ARP et CHORDS (les onglets l'indiquent clairement).
- Oscillateurs à wavetable (les oscillateurs actuels sont sinus/triangle/dent de scie/carré anti-aliasés), superposition de voix (unison), modulation PWM.
- MIDI learn, double-clic pour saisir une valeur, infobulles.
- Choix du « style général » du kit (cohérence et saturation sont réglables).
- Presets d'usine : générés à la demande par le moteur (pas de fichiers audio), 38 entrées.
