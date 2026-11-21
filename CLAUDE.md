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

## Effets traités (méthode « un effet à la fois »)
### Stretch — en cours (session du 20/11/2026)
- Demande : inspiré de la section Stretch d'Infiltrator : un **rate** de gel (1/4 de temps, 1/2 temps,
  1 temps…) où la frame du son se fige, et un **grain** = taille de la frame figée.
- Code : DSP pur `Source/Effects/StretchEngine.h` ; VST3 seul `Plugins/Stretch/` (« IDGems Stretch »,
  code plugin `IdSt`) ; outil `Tools/StretchRender.cpp` (`IDGemsStretchRender <dossier> [boucle.wav] [bpm]`
  rend sec + 6 réglages ; `--snapshot img.png` capture l'interface).
- Comportement v1 : à chaque pas, le son **joue le temps d'un grain** (min(grain, ½ pas)) puis **reste coincé**
  sur cette frame (nuage de grains Hann x4, pré-vieillis, bascules en 3 ms à puissance constante).
  Réglages : Rate (1/32…2/1), Grain (2 ms–1 s), Texture (aléa : buzz net ↔ granuleux),
  Vitesse (0 = gel, >0 = étirement lent), Hold (part du pas figée, le reste en direct), Mix (100 %).
  Choix fait : figer le son *après* le pas (le gel du son *avant* le pas capturait des fins de notes, trop faible).
- Retour v1 (granulaire seul) : « pas ouf ». Le plus proche était le grain 12 ms / texture 0, mais trop aigu,
  pas assez texturé, et on ne reconnaissait plus les notes. Cause : des grains identiques répétés tous les
  grain/4 imposent leur propre hauteur (12 ms → ~330 Hz). Il veut **garder les notes d'origine, étirées, texturées**.
- v2 : moteur **Spectral** par défaut (vocodeur de phase, verrouillage de phase Laroche-Dolson, Grain = fenêtre
  d'analyse, FFT jusqu'à 65536 points). Texture = la frame analysée bouge dans la zone figée + phases et
  amplitudes bousculées. Vérifié : un 440 Hz figé ressort à 440 Hz, au même niveau. Le moteur Grains reste en option.
  Défauts : 1/8, Spectral, grain 120 ms, texture 30 %.
- Retour v2 (spectral) : « pas ouf » aussi. Il veut tester en conditions réelles (VST3), et fixe le cahier des charges :
  **vitesse toujours 0, mix toujours 100 %** (aucun son direct mélangé), Rate = période de répétition,
  Grain = taille du grain répété, + **filtre coupe-bas / coupe-haut graphique**.
- Référence fournie : rendu Infiltrator « speed 0, grain 40 Hz, 1/16 » (synthé Vital, 120 BPM). Analyse :
  le son se répète **exactement toutes les 1102,5 échantillons = 1/40 s** (boucle d'UN grain, forme d'onde
  intacte, pas de creux d'enveloppe au raccord : pas de nuage de grains superposés) ; nouvelle frame toutes
  les 125 ms (1/16) ; juste après chaque pas, ~1 à 2 grains non répétitifs (son direct + bascule) avant la boucle.
  => le « Grain » d'Infiltrator est une fréquence (Hz) = 1 / durée de la boucle.
- v3 (actuelle) : moteur réécrit en conséquence (le spectral et le nuage de grains sont abandonnés, voir
  l'historique git) : à chaque pas, son direct pendant 1/f (min ½ pas), puis boucle exacte de 1/f, raccord
  sans clic (les premiers ms de la boucle sont fondus avec ce qui suivait la fenêtre), boucle copiée hors du
  tampon circulaire. Paramètres : Rate (défaut 1/16), Grain 1–500 Hz (défaut 40 Hz), coupe-bas, coupe-haut
  (24 dB/oct, SVF x2), Résonance. Interface : Rate, gros potard Grain (+ durée en ms), filtre graphique
  (spectre de sortie + courbe, poignées : glisser = fréquence, haut/bas = résonance, double-clic = reset).
  Hold/Texture/Vitesse/Mix supprimés (Hold : question posée, pas de réponse ; à rajouter s'il le demande).
- CI : artefact séparé **IDGems-Stretch-VST3-Windows** (le Stretch seul).
- Retour v3 (21/11) : « il y a de l'idée, le son est cool, j'aime bien ». Demandes :
  grain modifiable PENDANT le gel (automation), afficher la note du grain (comme son « Bit Repeat »),
  retirer la légende « joué / boucle figée » (incomprise, pas manipulable).
- v4 : grain en direct (lissé ~10 ms en log, fenêtre ancrée sur sa fin, position gardée en proportion ;
  la boucle copie 1 s de passé pour pouvoir s'allonger) ; note + cents sous le potard (convention Ableton
  C3 = 60) ; frise réduite à un indicateur de pas avec un trait par répétition du grain ; légende supprimée.
- Reste à faire : ses retours sur la v4 (piste : accrocher le grain aux demi-tons ?), puis réintégration au rack.

## Décisions et retours déjà actés
- **Mix des effets à 100 % par défaut et jamais randomisé** (« un effet à 20 %, c'est dégueulasse,
  le glitch doit s'affirmer »). Seule exception : famille Espace (delay, reverb, chorus, flanger, phaser).
  La macro Chaos ne dose que le mix des effets d'espace.
- **Pas de modes Stéréo/Mid/Side/L/R, pas de parallèle** : chaîne strictement en série.
- **Séquenceurs éteints par défaut** ; le random ne les allume que si l'option « SÉQ. » est activée
  (désactivée par défaut). Bouton « SÉQ ON/OFF » général en haut de la chaîne.
- Morph XY entre snapshots : pondération en 1/d² (il trouvait que ça ne morphait pas).
- La taille de la fenêtre doit survivre à la fermeture/réouverture et à la sauvegarde du projet.

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
