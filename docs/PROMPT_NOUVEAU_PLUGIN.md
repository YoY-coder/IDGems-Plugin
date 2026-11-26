# Prompt à donner à Claude pour démarrer un projet de plugin audio

> **Mode d'emploi (pour l'humain)**
> 1. Sur GitHub, crée un dépôt vide (bouton **New repository**, coche « Add a README »).
> 2. Ouvre Claude Code (claude.ai/code, l'appli ou le terminal) sur ce dépôt.
> 3. Copie **tout ce qui est sous la ligne** ci-dessous, colle-le comme premier message,
>    et remplace les `<...>` (nom du plugin, effet ou synthé, ce que tu veux comme son).
> 4. Ensuite tu discutes normalement avec Claude : il code, il pousse sur GitHub, GitHub compile,
>    et tu télécharges ton plugin dans l'onglet **Actions** de ton dépôt.

---

Salut Claude. On démarre un projet de **plugin audio VST3** et je veux que tout soit installé proprement
dès le départ : le code, la compilation automatique sur GitHub, les tests. Voici le cahier des charges
et les leçons tirées d'un projet similaire qui marche déjà. Suis-les.

## Qui je suis / comment on travaille
- Nom du plugin : **<NomDuPlugin>** — type : **<effet / synthé / instrument>**.
- Ce que je veux comme son / comme fonction : **<décris ici, avec des références de plugins si tu en as>**.
- Mon niveau en code : **<pas développeur / un peu / développeur>**. Si je ne suis pas développeur,
  explique chaque manipulation pas à pas, simplement, en français. Côté audio, tu peux parler technique.
- Mon DAW : **<Ableton / FL / Reaper / …>** sur **<Windows / Mac>**.
- Je ne compile pas moi-même : **tout passe par GitHub**. Tu écris le code, tu le testes dans ton
  environnement (Linux), tu pousses, GitHub Actions compile le plugin pour mon système, et je le
  télécharge dans l'onglet Actions (dernière coche verte → section *Artifacts*).
- Je peux travailler depuis mon téléphone : fais des réponses lisibles et envoie-moi des captures de
  l'interface quand tu la modifies.

## 1. Mise en place (à faire au premier message, avant toute fonctionnalité)
Crée ce squelette, compile-le chez toi, fais passer les tests, puis pousse :

```
CMakeLists.txt
.github/workflows/build.yml
.gitignore                 (build/, *.user, .vs/, .DS_Store)
CLAUDE.md                  mémoire du projet (voir §5)
Source/PluginProcessor.h/.cpp
Source/PluginEditor.h/.cpp
Tests/Tests.cpp            tests console + capture de l'interface
Resources/                 polices / images éventuelles (juce_add_binary_data)
docs/INSTALLATION.md       installation pas à pas pour moi
```

### CMakeLists.txt (base éprouvée)
```cmake
cmake_minimum_required(VERSION 3.22)
project(<NomDuPlugin> VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Windows : runtime statique (le .vst3 ne dépend d'aucune DLL Visual C++)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
# Mac : binaire universel Intel + Apple Silicon
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.9          # version figée : ne jamais suivre une branche
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(<NomDuPlugin>
    COMPANY_NAME                "<Marque>"
    PLUGIN_MANUFACTURER_CODE    <Abcd>   # 4 caractères, au moins une majuscule, UNIQUE à la personne
    PLUGIN_CODE                 <Efgh>   # 4 caractères, UNIQUE pour chaque plugin
    PRODUCT_NAME                "<NomDuPlugin>"
    FORMATS                     VST3 AU Standalone
    IS_SYNTH                    FALSE    # TRUE pour un synthé
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES             Fx       # « Instrument Synth » pour un synthé
    COPY_PLUGIN_AFTER_BUILD     FALSE)

set(PLUGIN_SOURCES Source/PluginProcessor.cpp Source/PluginEditor.cpp)
set(PLUGIN_DEFS JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
set(PLUGIN_MODULES juce::juce_audio_utils juce::juce_audio_processors juce::juce_dsp juce::juce_gui_extra)

target_sources(<NomDuPlugin> PRIVATE ${PLUGIN_SOURCES})
target_include_directories(<NomDuPlugin> PRIVATE Source)
target_compile_definitions(<NomDuPlugin> PUBLIC ${PLUGIN_DEFS})
if(MSVC)
    target_compile_options(<NomDuPlugin> PRIVATE /utf-8)   # textes accentués
endif()
target_link_libraries(<NomDuPlugin>
    PRIVATE ${PLUGIN_MODULES}
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# Tests (exécutable console qui embarque le processeur et l'éditeur)
juce_add_console_app(<NomDuPlugin>Tests PRODUCT_NAME "<NomDuPlugin>Tests")
target_sources(<NomDuPlugin>Tests PRIVATE Tests/Tests.cpp ${PLUGIN_SOURCES})
target_include_directories(<NomDuPlugin>Tests PRIVATE Source)
target_compile_definitions(<NomDuPlugin>Tests PRIVATE ${PLUGIN_DEFS} PLUGIN_TESTS=1)
if(MSVC)
    target_compile_options(<NomDuPlugin>Tests PRIVATE /utf-8)
endif()
target_link_libraries(<NomDuPlugin>Tests PRIVATE ${PLUGIN_MODULES} PUBLIC juce::juce_recommended_config_flags)
```
Dans `PluginProcessor.cpp`, entoure `createPluginFilter()` de `#if ! PLUGIN_TESTS … #endif` pour que
l'exécutable de tests compile. (Ne redéfinis pas `JUCE_STANDALONE_APPLICATION` : JUCE le gère.)

### .github/workflows/build.yml (Windows + Mac, tests, pluginval, téléchargement)
```yaml
name: Compilation du plugin
on: [push, pull_request, workflow_dispatch]
jobs:
  build:
    strategy:
      fail-fast: false
      matrix:
        os: [windows-latest, macos-latest]
    runs-on: ${{ matrix.os }}
    timeout-minutes: 60
    steps:
      - uses: actions/checkout@v4
      - name: Configurer
        run: cmake -B build -DCMAKE_BUILD_TYPE=Release
      - name: Compiler
        run: cmake --build build --config Release --parallel
      - name: Tests
        shell: bash
        run: |
          if [ "$RUNNER_OS" = "Windows" ]; then build/<NomDuPlugin>Tests_artefacts/Release/<NomDuPlugin>Tests.exe
          else build/<NomDuPlugin>Tests_artefacts/Release/<NomDuPlugin>Tests; fi
      - name: pluginval (norme VST3)
        shell: bash
        run: |
          if [ "$RUNNER_OS" = "Windows" ]; then
            curl -sSL -o pv.zip https://github.com/Tracktion/pluginval/releases/latest/download/pluginval_Windows.zip
            unzip -q pv.zip -d pv && PV=pv/pluginval.exe
          else
            curl -sSL -o pv.zip https://github.com/Tracktion/pluginval/releases/latest/download/pluginval_macOS.zip
            unzip -q pv.zip -d pv && PV=pv/pluginval.app/Contents/MacOS/pluginval
          fi
          "$PV" --strictness-level 5 --validate-in-process --skip-gui-tests --timeout-ms 600000 \
                --validate "build/<NomDuPlugin>_artefacts/Release/VST3/<NomDuPlugin>.vst3"
      - name: Publier le plugin
        uses: actions/upload-artifact@v4
        with:
          name: <NomDuPlugin>-${{ runner.os }}
          path: |
            build/<NomDuPlugin>_artefacts/Release/VST3
            build/<NomDuPlugin>_artefacts/Release/AU
            docs/INSTALLATION.md
          retention-days: 30
```
Si je n'ai pas de Mac, retire `macos-latest` de la matrice et `AU` des formats (ça divise le temps de
compilation). Après chaque push, vérifie que l'action est verte ; si elle est rouge, lis le log et corrige
toi-même avant de me dire que c'est prêt.

### Compiler chez toi (environnement cloud Linux)
JUCE a besoin de ces paquets sous Linux (installe-les avant le premier `cmake`) :
```bash
apt-get update && apt-get install -y libx11-dev libxrandr-dev libxinerama-dev libxext-dev libxcursor-dev libfreetype6-dev libfontconfig1-dev libasound2-dev libgl1-mesa-dev ninja-build
```
Puis :
```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && ninja -C build <NomDuPlugin>Tests <NomDuPlugin>_VST3
```
Tu peux aussi lancer pluginval version Linux sur le `.vst3` compilé, avant de pousser.
Si cette installation est longue, propose-moi de l'enregistrer comme script de démarrage de
l'environnement cloud pour les prochaines sessions.

## 2. Tests (dans Tests/Tests.cpp)
- `juce::ScopedJuceInitialiser_GUI` au début du `main`.
- Une fausse `AudioPlayHead` (bpm, position, lecture) pour tester la synchro au tempo.
- Au minimum : pas de NaN/Inf avec des réglages aléatoires (toutes les valeurs de paramètres),
  pas de clic (saut max entre deux échantillons sur un sinus), niveau de sortie raisonnable,
  sauvegarde/rechargement de l'état identique, CPU (temps de calcul ≪ durée audio), latence déclarée
  exacte si le plugin en introduit.
- Option `--snapshot fichier.png` : crée l'éditeur, le dimensionne, et sauve
  `createComponentSnapshot()` en PNG. **Regarde toujours la capture** après avoir touché l'interface
  et envoie-la-moi.
- Option `--render dossier boucle.wav bpm` : traite un WAV que je te fournis avec plusieurs réglages
  et écrit les WAV résultants (sec + traités), pour que j'écoute sur mon téléphone. (API JUCE 8 :
  `createWriterFor (std::unique_ptr<OutputStream>&, AudioFormatWriterOptions{}.withSampleRate(...)…)`.)
- Le code de retour vaut 1 si un test échoue (la CI devient rouge).

## 3. Règles audio (non négociables)
- Dans `processBlock` : **aucune allocation, aucun verrou bloquant, aucun accès fichier, aucun log**.
- Données partagées interface → audio (formes dessinées, tables…) : copie de taille fixe protégée par
  un `juce::SpinLock`, le thread audio fait seulement `ScopedTryLockType` et garde l'ancienne version
  si le verrou est pris. Interface → audio pour les valeurs simples : `std::atomic`.
- Paramètres via `AudioProcessorValueTreeState`, lus avec `getRawParameterValue` ; lisse ceux qui
  feraient clic (`juce::SmoothedValue`).
- Pas de NaN (filtre `std::isfinite` en sortie), pas de clic aux changements, `ScopedNoDenormals`.
- Si le plugin peut monter en niveau (distorsion, feedback, résonance…) : limiteur de sécurité en sortie.
- Latence déclarée exacte avec `setLatencySamples`.
- Ajoute `using AudioProcessor::processBlock;` dans le processeur (sinon avertissement « hidden »).

## 4. Interface (pièges déjà rencontrés)
- Éditeur redimensionnable à ratio fixe : un composant « contenu » dessiné à une taille de base,
  mis à l'échelle avec `setTransform (AffineTransform::scale (s))`.
- **La taille de fenêtre doit survivre** à la fermeture/réouverture et à la sauvegarde du projet :
  stocke l'échelle dans le processeur (`std::atomic<float>`), sauve-la dans
  `getStateInformation`, et **lis-la AVANT** `setResizable` / `setSizeLimits` dans le constructeur
  de l'éditeur (ces appels déclenchent `resized()` qui écraserait la valeur). N'enregistre la taille
  dans `resized()` qu'une fois la construction terminée (drapeau `sizeReady`).
- Ne donne une taille au composant contenu (`setSize`) **qu'après** avoir créé tous ses enfants :
  sinon `resized()` touche des pointeurs nuls → plantage.
- Textes accentués : sources en UTF-8, `juce::String (juce::CharPointer_UTF8 ("…"))` (fais une macro),
  `/utf-8` sous MSVC.
- Polices / images : `juce_add_binary_data`, polices libres de droits seulement (SIL OFL, etc.).
- Info-bulles sur chaque contrôle, double-clic = valeur par défaut sur les potards.

## 5. CLAUDE.md (mémoire du projet)
Crée `CLAUDE.md` à la racine avec : qui je suis et comment on travaille (§ ci-dessus), le but du plugin,
la direction artistique quand on l'aura définie, les commandes de compilation/tests, les règles audio,
la structure des dossiers, et une section **« Décisions et retours actés »**. **Mets-le à jour à la fin
de chaque session** (ce qui a été fait, ce que j'ai validé à l'oreille, ce qui reste à faire), pour que
la prochaine conversation reparte sans rien perdre.

## 6. Méthode de travail
- Une fonctionnalité à la fois, bien finie, plutôt que plein de trucs moyens.
- Pour le son : je décris ce que je veux + références ; tu proposes, je teste dans mon DAW,
  on ajuste à l'oreille. Si je t'envoie une boucle sèche, rends-moi des WAV avec plusieurs réglages.
- Commits clairs en français, poussés sur la branche de travail ; pas de pull request sans que je la demande.
- À chaque livraison, dis-moi simplement : ce qui a changé, si la CI est verte, et comment
  télécharger/installer (Windows : copier le `.vst3` dans `C:\Program Files\Common Files\VST3` ;
  Mac : `~/Library/Audio/Plug-Ins/VST3` et `/Components` pour l'AU, puis dans le Terminal
  `xattr -dr com.apple.quarantine <chemin du plugin>` car il n'est pas signé).

Commence par la mise en place (§1), montre-moi la CI verte et une capture de l'interface vide,
puis on attaque le son.
