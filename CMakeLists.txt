cmake_minimum_required(VERSION 3.22)
project(TrapForge VERSION 0.2.0)

set(CMAKE_CXX_STANDARD 17)
option(TRAPFORGE_BUILD_TESTS "Compiler aussi les tests hors DAW (TrapForgeTests)" OFF)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# Illustration de référence + police pixel (VT323, licence OFL) intégrées au plugin
juce_add_binary_data(TrapAssets SOURCES
    Resources/Images/background.png
    Resources/Fonts/VT323-Regular.ttf)

juce_add_plugin(TrapForge
    COMPANY_NAME "MonsterAudio"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    PLUGIN_MANUFACTURER_CODE Mnaw
    PLUGIN_CODE Trpf
    FORMATS VST3 Standalone
    VST3_CATEGORIES Instrument Synth
    PRODUCT_NAME "TrapForge")

target_sources(TrapForge PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/PresetGenerator.cpp
    Source/DrumGenerator.cpp)

target_compile_definitions(TrapForge PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(TrapForge
    PRIVATE TrapAssets juce::juce_audio_utils juce::juce_dsp juce::juce_audio_formats
    PUBLIC juce::juce_recommended_config_flags
           juce::juce_recommended_lto_flags
           juce::juce_recommended_warning_flags)

if(TRAPFORGE_BUILD_TESTS)
    juce_add_console_app(TrapForgeTests PRODUCT_NAME "TrapForgeTests")
    target_sources(TrapForgeTests PRIVATE Tests/Tests.cpp Source/PresetGenerator.cpp Source/DrumGenerator.cpp)
    target_compile_definitions(TrapForgeTests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
    target_link_libraries(TrapForgeTests PRIVATE juce::juce_dsp juce::juce_audio_formats juce::juce_recommended_config_flags)
endif()
