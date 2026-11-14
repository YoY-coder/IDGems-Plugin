# IDGems — Installer et tester le plugin (pas à pas)

## Option A — Sans rien installer : la compilation GitHub (recommandé)

À chaque modification, GitHub compile automatiquement le plugin sur une machine Windows,
lance les tests de stabilité et le valide avec pluginval (l'outil qui vérifie qu'un plugin
respecte la norme VST3).

1. Ouvre la page du dépôt sur GitHub, onglet **Actions**.
2. Clique sur la dernière ligne avec une **coche verte** (« Compilation Windows (VST3) »).
3. En bas de la page, section **Artifacts**, clique sur **IDGems-VST3-Windows** : un fichier `.zip` se télécharge.
4. Dézippe-le : tu obtiens un dossier **`IDGems.vst3`**.
5. Copie ce dossier `IDGems.vst3` (le dossier entier) dans :
   `C:\Program Files\Common Files\VST3`
   (Windows demande une confirmation administrateur : accepte.)
6. Dans Ableton Live : **Options → Préférences → Plug-ins**
   - « Utiliser le dossier système VST3 » : **Activé**
   - clique sur **Rescanner**.
7. Dans le navigateur d'Ableton : **Plug-ins → IDGems → IDGems**. Glisse-le sur une piste audio.

> Si une ancienne version est déjà installée, ferme Ableton avant de remplacer le dossier.

## Option B — Compiler toi-même avec Visual Studio

1. Installe **Visual Studio Community** (2022 ou plus récent) avec la charge de travail
   **« Développement Desktop en C++ »**.
2. Installe **GitHub Desktop**, connecte-toi, et clone le dépôt `IDGems-Plugin`.
3. Dans Visual Studio : **Fichier → Ouvrir → Dossier…** et choisis le dossier du dépôt.
   Visual Studio détecte le `CMakeLists.txt` et télécharge JUCE tout seul (quelques minutes la première fois).
4. En haut, choisis la configuration **x64-Release**, puis la cible **IDGems_VST3**.
5. Menu **Générer → Tout générer**.
6. Le plugin est créé dans `out\build\x64-Release\IDGems_artefacts\Release\VST3\IDGems.vst3`.
   Copie-le comme à l'étape 5 de l'option A.

## Sidechain dans Ableton

Sur la piste où est IDGems, dans l'appareil, clic sur le triangle ▸ en haut à gauche → **Sidechain**,
puis choisis la piste source. Dans l'onglet **ENTRÉE** d'IDGems : source des transitoires / de
l'envelope follower = **Sidechain**.

## Déclencher les glitchs au clavier MIDI

Crée une piste MIDI, sortie MIDI → la piste qui contient IDGems (« IDGems »).
Notes à partir de **C1** : C1 Stutter, C#1 Beat Repeat, D1 Reverse, D#1 Tape Stop, E1 Shuffle,
F1 Freeze, F#1 Scratch, G1 Bitcrush… (un effet par demi-ton, dans l'ordre de la liste des effets ;
l'ordre exact est rappelé dans le menu clic droit de chaque effet du rack).

## Fichiers

- Presets : `Documents\IDGems\Presets`
- Enregistrements : `Documents\IDGems\Recordings`
