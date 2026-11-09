cmake_minimum_required(VERSION 3.22)

# Must precede project(), which otherwise caches empty values and targets the build machine's OS.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")

project(AUDIO2MIDI VERSION 0.4.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(FetchContent)
FetchContent_Declare(JUCE
    URL https://github.com/juce-framework/JUCE/archive/refs/tags/9.0.5.tar.gz
    URL_HASH SHA256=d92207f51ba728d3d195b6d5e93ecd049717a915fc250ff6844c32ea7272315a
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(Audio2Midi
    COMPANY_NAME "Audio2MIDI"
    BUNDLE_ID org.audio2midi.plugin
    PLUGIN_MANUFACTURER_CODE A2Mo
    PLUGIN_CODE A2md
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "Audio2MIDI"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Fx Analyzer
    AU_MAIN_TYPE kAudioUnitType_Effect
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Audio2MIDI records audio input to transcribe it to MIDI."
    COPY_PLUGIN_AFTER_BUILD ${APPLE} # the Windows VST3 folder needs admin rights
    NEEDS_CURL TRUE)

set(CORE_DEFINITIONS JUCE_WEB_BROWSER=0 JUCE_VST3_CAN_REPLACE_VST2=0)

# The test runner compiles the same Transcription.cpp, so both exercise one engine implementation.
target_sources(Audio2Midi PRIVATE src/PluginProcessor.cpp src/PluginEditor.cpp src/Transcription.cpp)
# AU hosting loads Apple's built-in General MIDI synth for the preview.
target_compile_definitions(Audio2Midi PUBLIC ${CORE_DEFINITIONS} JUCE_PLUGINHOST_AU=1 JUCE_USE_MP3AUDIOFORMAT=1)
target_link_libraries(Audio2Midi
    PRIVATE juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

juce_add_console_app(Audio2MidiTests PRODUCT_NAME "Audio2MidiTests" NEEDS_CURL TRUE)
target_sources(Audio2MidiTests PRIVATE tests/Tests.cpp src/Transcription.cpp)
target_include_directories(Audio2MidiTests PRIVATE src)
target_compile_definitions(Audio2MidiTests PRIVATE ${CORE_DEFINITIONS})
target_link_libraries(Audio2MidiTests
    PRIVATE juce::juce_core juce::juce_events juce::juce_audio_basics
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

enable_testing()
add_test(NAME unit COMMAND Audio2MidiTests)
