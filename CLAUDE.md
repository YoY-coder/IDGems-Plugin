# IDGems — contexte du projet (à lire au début de chaque conversation)

## Qui est l'utilisateur
- **Altered Brother** : producteur de bass music glitch, ingé son. Ce plugin est pour sa propre musique.
- **Pas développeur.** Explique chaque manipulation pas à pas, simplement, en français.
  Côté audio, tu peux parler technique sans simplifier.
- Il travaille souvent **depuis son téléphone, en déplacement**. On fait donc **tout via GitHub** :
  tu écris le code, tu pousses, GitHub Actions compile le VST3 Windows, il le télécharge.
  Il ne teste dans Ableton (Windows, Visual Studio dispo sur son fixe) qu'en rentrant.
- Il a parfois demandé un « caveman mode » (réponses très courtes, mots simples). Suis sa demande du moment.

## Le projet
- **IDGems** : plugin d'effets VST3 (JUCE 8 / C++ / CMake), « machine à glitch randomisable ».
  Inspiration de départ : Infiltrator (Devious Machines), mais la DA ne doit PAS ressembler à Infiltrator.
- Une suite est prévue : **IDGems Synth** (synthé basé sur le même rack d'effets) viendra plus tard.
  Ne pas le commencer sans qu'il le demande.
- Une autre session de l'utilisateur travaille sur un autre plugin (« Breath »). Les correctifs
  communs (ex. taille de fenêtre) doivent être appliqués à tous ses plugins quand il le demande.

## Plan actuel (décidé le 20/11/2026) : un effet à la fois
Constat de l'utilisateur après essai : IDGems « fait plein de trucs mais ce n'est pas très quali,
ça ne sonne pas comme je veux, la mise en place a été bâclée ».
Nouvelle méthode :
1. **Un effet = un petit VST3 séparé** (même dépôt, même socle `Source/Core`, `Source/Effects`),
   avec une interface simple centrée sur lui.
2. Pour chaque effet, il décrit le son visé + références. On fait des **allers-retours à l'oreille** :
   s'il fournit une boucle sèche, rendre des WAV (sec / traité, plusieurs réglages) et les lui envoyer
   pour écoute sur téléphone.
3. Effet validé → il réintègre le rack IDGems avec sa qualité finale.
4. Faire le tri : mieux vaut 10 effets excellents que 27 moyens. Lui demander lesquels comptent.
Garder le socle existant (paramètres, latence, sécurité anti-saturation, tests, CI) : c'est le son
des effets qu'il faut reprendre, pas l'architecture.

**Mets à jour ce fichier** à la fin de chaque session avec les décisions prises (effet traité,
réglages validés, ce qui reste à faire).

## Décisions et retours déjà actés
- **Mix des effets à 100 % par défaut et jamais randomisé** (« un effet à 20 %, c'est dégueulasse,
  le glitch doit s'affirmer »). Seule exception : famille Espace (delay, reverb, chorus, flanger, phaser).
  La macro Chaos ne dose que le mix des effets d'espace.
- **Pas de modes Stéréo/Mid/Side/L/R, pas de parallèle** : chaîne strictement en série.
- **Séquenceurs éteints par défaut** ; le random ne les allume que si l'option « SÉQ. » est activée
  (désactivée par défaut). Bouton « SÉQ ON/OFF » général en haut de la chaîne.
- Morph XY entre snapshots : pondération en 1/d² (il trouvait que ça ne morphait pas).
- La taille de la fenêtre doit survivre à la fermeture/réouverture et à la sauvegarde du projet.

## Effet n° 1 : Beat Repeat (session du 21/11/2026, en cours d'écoute)
- Modèle : le module **Loop d'Infiltrator** (capture d'écran fournie : Speed, Tempo, Smooth, Glide,
  sens → / ↔ / ←, Free/Sync, Gain, Mix). Remplacera **Stutter + Beat Repeat** du rack une fois validé.
- Analyse de sa boucle de référence (Loop, Tempo « 1.5 » = recapture toutes les demi-mesures, Speed
  jouée à la main) : boucles de 83 / 31,25 / 11,45 / 9,64 / 20,84 ms → sous ~30 ms la répétition
  devient une note (buzz granulaire). C'est LE son visé.
- Code : moteur `Source/Effects/LoopRepeat.h` (autonome, sans MParam), plugin séparé
  `Source/Plugins/BeatRepeat/` (VST3 « IDGems Beat Repeat », code Idbr), labo `Tests/BeatRepeatLab.cpp`
  (`BeatRepeatLab` = tests ; `--render dossier` = boucle sèche de synthèse + rendus WAV ;
  `--snapshot x.png`). CI : compilé, testé, pluginval, livré dans le même artefact.
- Réglages : Speed (1 mesure → ~1 ms, log ; libre ou aimanté « Snap » sur droites/triolets/pointées),
  Tempo (Hold, 1/32 … 4 mesures ; ou ms en mode libre), Smooth (fondu sin² à chaque tour), Glide,
  Rampe (±3 octaves de vitesse sur un pas de tempo), sens, REPEAT (on/off automatisable), Mix 100 %, Gain.
- Il n'a pas voulu fournir de boucle sèche : les rendus utilisent une boucle de synthèse (120 BPM).
- **À faire** : ses retours d'écoute ; les deux petits potards de gauche du Loop d'Infiltrator
  (modulation de la Speed : enveloppe + LFO/forme) ne sont pas encore reproduits, attendre sa capture.

## Direction artistique (DA)
- Basée sur l'univers **Altered Brother** (il autorise à la reprendre librement, c'est son projet).
  Références : artwork « Process Overload » (roche noire fissurée, fissures orange incandescentes,
  braises, poussière/bokeh, lumière blanche froide) et son logo (typo large, futuriste, lettres
  tranchées en biais et décalées, rendu métal blanc).
- Base noir texturé, accent **orange**, un peu de bleu, une couleur par famille d'effets.
- Typo des titres : Michroma (SIL OFL, `Resources/Fonts`) avec effet « tranché » maison
  (`Theme::cutText`). Sa police d'origine est un mix de plusieurs polices : il valide le rendu actuel.
- Potards : **arc néon plein** qui bloome et scintille plus la valeur est forte (il n'aimait pas le pointillé).
- Onglet Sortie en **boîtes par module avec retours graphiques** (courbe de soft clip + position du
  signal, OTT par bande, historique du limiteur…) + **spectre détaillé**. Un **logo vectoriel par effet**.
- Braises animées qui réagissent au son (réglable : Réglages → Glitch visuel).

## Technique
- Compilation : GitHub Actions (`.github/workflows/build.yml`) sur Windows → artefact
  `IDGems-VST3-Windows` (onglet Actions, dernière coche verte, section Artifacts). Lance aussi les
  tests de stabilité et pluginval.
- En local (Linux) : `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && ninja -C build IDGemsTests IDGems_VST3`
  puis `build/IDGemsTests_artefacts/Release/IDGemsTests` (tests) ou
  `... IDGemsTests --snapshot chemin.png <onglet 0-4>` (capture de l'interface, pour vérifier le rendu).
- Sources en UTF-8 ; les littéraux avec accents passent par `IDG_T ("...")` ; MSVC compile en `/utf-8`.
- Règles audio : aucune allocation ni verrou dans le thread audio, pas de NaN, pas de clics,
  limiteur toujours actif en sortie, latence déclarée exacte.

## Structure
```
Source/Core        paramètres (ParamDefs.cpp = liste des 27 effets et de leurs paramètres), banque modulée, gamme, DSP
Source/Effects     un fichier par effet + Rack (ordre, latence, suréchantillonnage destruction)
Source/Modulation  LFO (formes au crayon), random, envelope follower, macros
Source/Engine      séquenceurs, chaîne de sortie (HP, mono, OTT, clip, limiteur), enregistreur
Source/State       random (seed, historique, favoris, apprentissage), presets, état
Source/UI          interface (Theme, Knob, RackView, EffectPanel, Graphs, Icons, panneaux…)
Tests              tests de stabilité + capture d'interface
docs/INSTALLATION.md  installation pas à pas pour l'utilisateur
```
