# IDGems — machine à glitch randomisable (VST3)

Plugin d'effets VST3 (JUCE / C++) : 27 effets en rack réordonnable, séquenceur par effet,
modulation façon Vital (10 LFO dessinables, 5 random, envelope follower, 8 macros),
système de random complet (dosage, drift, seed, historique, favoris, apprentissage, mutate),
snapshots + morph XY, pads momentanés, throws, capture, feedback global, gamme globale,
chaîne de sortie (passe-haut, grave mono, OTT, soft clip, limiteur), enregistreur avec glisser-déposer.

- **Installer / tester** : voir [docs/INSTALLATION.md](docs/INSTALLATION.md)
- **Compilation** : automatique sur GitHub (onglet Actions → artefact `IDGems-VST3-Windows`)

## Structure du code

```
Source/
  Core/        paramètres (ParamDefs), banque de paramètres modulés, gamme, utilitaires DSP
  Effects/     un fichier par module DSP + le Rack (ordre, série/parallèle, M/S, latence)
  Modulation/  LFO, random, envelope follower, macros, connexions
  Engine/      séquenceurs, chaîne de sortie, enregistreur
  State/       random, presets, sauvegarde de l'état
  UI/          interface (rack, effet, séquenceur, modulation, performance, entrée, sortie, enregistreur)
Tests/         tests de stabilité (NaN, plafond, feedback, latence, clics, CPU) + capture d'interface
```
