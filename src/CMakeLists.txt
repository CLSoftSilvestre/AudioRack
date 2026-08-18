# Plugin target. Core engine (src/core) and DSP (src/dsp) libraries are added
# in later milestones; M0 is a bare passthrough.

set(AUDIORACK_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND AUDIORACK_FORMATS AU)
endif()

juce_add_plugin(AudioRack
    PRODUCT_NAME                "AudioRack"
    COMPANY_NAME                "AudioRack Project"
    COMPANY_WEBSITE             "https://github.com/celsosilvestre/audiorack"
    BUNDLE_ID                   com.audiorackproject.audiorack
    PLUGIN_MANUFACTURER_CODE    ARkP
    PLUGIN_CODE                 ARk1
    FORMATS                     ${AUDIORACK_FORMATS}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Fx Dynamics EQ
    AU_MAIN_TYPE                kAudioUnitType_Effect)

juce_generate_juce_header(AudioRack)

set(AUDIORACK_PLUGIN_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/plugin/PluginProcessor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/plugin/PluginEditor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/state/RackState.cpp)

target_sources(AudioRack PRIVATE
    ${AUDIORACK_PLUGIN_SOURCES}
    ${AUDIORACK_CORE_SOURCES}
    ${AUDIORACK_DSP_SOURCES})
audiorack_strict_sources(${AUDIORACK_PLUGIN_SOURCES} ${AUDIORACK_CORE_SOURCES} ${AUDIORACK_DSP_SOURCES})

target_include_directories(AudioRack PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(AudioRack PUBLIC
    JUCE_WEB_BROWSER=0          # flips to 1 at M3 (WebView UI shell)
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(AudioRack
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
