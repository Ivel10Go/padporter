# One JUCE target, two formats: the Standalone app and the VST3 plugin share all code.
juce_add_plugin(PadPorter
    PRODUCT_NAME "PadPorter"
    COMPANY_NAME "PadPorter"
    BUNDLE_ID "dev.padporter.padporter"
    VERSION ${PROJECT_VERSION}
    PLUGIN_MANUFACTURER_CODE Pdpt
    PLUGIN_CODE Pdp1
    FORMATS VST3 Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Drum Sampler
    DESCRIPTION "Drum sampler and MIDI exporter for .xpj projects")

target_sources(PadPorter PRIVATE
    Source/SampleKit.cpp
    Source/SamplerEngine.cpp
    Source/OfflineRenderer.cpp
    Source/TimeStretch.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ui/Theme.cpp
    Source/ui/PadGrid.cpp
    Source/ui/WaveformView.cpp
    Source/ui/SequenceBrowser.cpp
    Source/ui/PatternView.cpp
    Source/ui/ProjectsOverlay.cpp
    Source/ui/Hardware.cpp
    Source/ui/DisplayScreen.cpp)

target_include_directories(PadPorter PRIVATE Source)

target_compile_definitions(PadPorter PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_STANDALONE_FILTER_WINDOW_USE_KIOSK_MODE=0)

target_link_libraries(PadPorter
    PRIVATE
        padporter_core
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Host-independent tests for sample loading and the real-time engine.
if(PADPORTER_BUILD_TESTS)
    juce_add_console_app(padporter_engine_tests PRODUCT_NAME "PadPorterEngineTests")

    target_sources(padporter_engine_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/tests/engine/EngineTests.cpp
        ${PROJECT_SOURCE_DIR}/tests/Fixtures.cpp
        Source/SampleKit.cpp
        Source/SamplerEngine.cpp
        Source/OfflineRenderer.cpp
        Source/TimeStretch.cpp)

    target_include_directories(padporter_engine_tests PRIVATE
        Source
        ${PROJECT_SOURCE_DIR}/tests
        ${doctest_SOURCE_DIR})

    target_compile_definitions(padporter_engine_tests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)

    target_link_libraries(padporter_engine_tests PRIVATE
        padporter_core
        nlohmann_json::nlohmann_json
        juce::juce_audio_formats
        juce::juce_recommended_config_flags)

    add_test(NAME padporter_engine_tests COMMAND padporter_engine_tests)
endif()
