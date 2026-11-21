cmake_minimum_required(VERSION 3.22)

project(IDGems VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Sur Windows : runtime statique (le .vst3 ne dépend d'aucune DLL Visual C++)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ---------------------------------------------------------------------------
# JUCE est téléchargé automatiquement à la première configuration.
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.9
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

option(IDGEMS_BUILD_TESTS "Compiler les tests de stabilité" ON)
option(IDGEMS_COPY_AFTER_BUILD "Copier le VST3 dans le dossier système après compilation" OFF)

juce_add_plugin(IDGems
    COMPANY_NAME                "IDGems"
    COMPANY_WEBSITE             ""
    PLUGIN_MANUFACTURER_CODE    Idgm
    PLUGIN_CODE                 Idg1
    PRODUCT_NAME                "IDGems"
    FORMATS                     VST3 Standalone
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES             Fx Distortion Delay
    COPY_PLUGIN_AFTER_BUILD     ${IDGEMS_COPY_AFTER_BUILD})

set(IDGEMS_DSP_SOURCES
    Source/Core/ParamDefs.cpp
    Source/Core/ParamBank.cpp
    Source/Effects/Rack.cpp
    Source/Effects/EffectFactory.cpp
    Source/Modulation/ModEngine.cpp
    Source/Engine/Sequencer.cpp
    Source/Engine/Recorder.cpp
    Source/State/RandomEngine.cpp
    Source/State/StateHelpers.cpp
    Source/State/PresetManager.cpp
    Source/PluginProcessor.cpp)

set(IDGEMS_UI_SOURCES
    Source/PluginEditor.cpp
    Source/UI/LookAndFeel.cpp
    Source/UI/Knob.cpp
    Source/UI/RackView.cpp
    Source/UI/EffectPanel.cpp
    Source/UI/SeqEditor.cpp
    Source/UI/ModPanel.cpp
    Source/UI/LfoEditor.cpp
    Source/UI/PerfPanel.cpp
    Source/UI/IOPanels.cpp
    Source/UI/RightPanel.cpp
    Source/UI/TopBar.cpp
    Source/UI/PresetBrowser.cpp
    Source/UI/Visuals.cpp
    Source/UI/Theme.cpp
    Source/UI/Icons.cpp
    Source/UI/Graphs.cpp)

# Police des titres (Michroma, licence SIL OFL : voir Resources/Fonts/OFL.txt)
juce_add_binary_data(IDGemsData SOURCES Resources/Fonts/Michroma-Regular.ttf)
set_target_properties(IDGemsData PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_sources(IDGems PRIVATE ${IDGEMS_DSP_SOURCES} ${IDGEMS_UI_SOURCES})
target_include_directories(IDGems PRIVATE Source)

set(IDGEMS_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_compile_definitions(IDGems PUBLIC ${IDGEMS_DEFINITIONS})

# Les sources sont en UTF-8 (textes en français) : indispensable pour Visual Studio
if(MSVC)
    add_compile_options(/utf-8)
    target_compile_options(IDGems PRIVATE /utf-8)
endif()

set(IDGEMS_JUCE_MODULES
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_dsp
    juce::juce_gui_extra)

target_link_libraries(IDGems
    PRIVATE ${IDGEMS_JUCE_MODULES} IDGemsData
    PUBLIC juce::juce_recommended_config_flags
           juce::juce_recommended_lto_flags
           juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Plugins d'effet séparés (un effet = un petit VST3, pour les réglages à l'oreille)
# ---------------------------------------------------------------------------
juce_add_plugin(IDGemsBeatRepeat
    COMPANY_NAME                "IDGems"
    PLUGIN_MANUFACTURER_CODE    Idgm
    PLUGIN_CODE                 Idbr
    PRODUCT_NAME                "IDGems Beat Repeat"
    FORMATS                     VST3
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    VST3_CATEGORIES             Fx Delay
    COPY_PLUGIN_AFTER_BUILD     ${IDGEMS_COPY_AFTER_BUILD})

set(BEATREPEAT_SOURCES
    Source/Plugins/BeatRepeat/BeatRepeatProcessor.cpp
    Source/Plugins/BeatRepeat/BeatRepeatEditor.cpp
    Source/UI/LookAndFeel.cpp
    Source/UI/Theme.cpp)

target_sources(IDGemsBeatRepeat PRIVATE ${BEATREPEAT_SOURCES})
target_include_directories(IDGemsBeatRepeat PRIVATE Source)
target_compile_definitions(IDGemsBeatRepeat PUBLIC ${IDGEMS_DEFINITIONS})
if(MSVC)
    target_compile_options(IDGemsBeatRepeat PRIVATE /utf-8)
endif()
target_link_libraries(IDGemsBeatRepeat
    PRIVATE ${IDGEMS_JUCE_MODULES} IDGemsData
    PUBLIC juce::juce_recommended_config_flags
           juce::juce_recommended_lto_flags
           juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Tests de stabilité (exécutable console) : NaN, clics, plafond du limiteur...
# ---------------------------------------------------------------------------
if(IDGEMS_BUILD_TESTS)
    juce_add_console_app(IDGemsTests PRODUCT_NAME "IDGemsTests")
    target_sources(IDGemsTests PRIVATE Tests/StabilityTests.cpp ${IDGEMS_DSP_SOURCES} ${IDGEMS_UI_SOURCES})
    target_include_directories(IDGemsTests PRIVATE Source)
    target_compile_definitions(IDGemsTests PRIVATE ${IDGEMS_DEFINITIONS} IDGEMS_TESTS=1 JUCE_STANDALONE_APPLICATION=0)
    if(MSVC)
        target_compile_options(IDGemsTests PRIVATE /utf-8)
    endif()
    target_link_libraries(IDGemsTests
        PRIVATE ${IDGEMS_JUCE_MODULES} IDGemsData
        PUBLIC juce::juce_recommended_config_flags)
    # Labo du Beat Repeat : tests + rendus WAV + capture d'interface
    juce_add_console_app(BeatRepeatLab PRODUCT_NAME "BeatRepeatLab")
    target_sources(BeatRepeatLab PRIVATE Tests/BeatRepeatLab.cpp ${BEATREPEAT_SOURCES})
    target_include_directories(BeatRepeatLab PRIVATE Source)
    target_compile_definitions(BeatRepeatLab PRIVATE ${IDGEMS_DEFINITIONS} IDGEMS_TESTS=1 JUCE_STANDALONE_APPLICATION=0)
    if(MSVC)
        target_compile_options(BeatRepeatLab PRIVATE /utf-8)
    endif()
    target_link_libraries(BeatRepeatLab
        PRIVATE ${IDGEMS_JUCE_MODULES} IDGemsData
        PUBLIC juce::juce_recommended_config_flags)
endif()
