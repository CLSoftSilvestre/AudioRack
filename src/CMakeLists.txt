# Plugin target. Core engine (src/core) and DSP (src/dsp) sources are compiled
# in directly; the web UI bundle is built by npm and embedded as BinaryData.

set(AUDIORACK_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND AUDIORACK_FORMATS AU)
endif()

# --- Web UI bundle -----------------------------------------------------------
# Built at configure time (deterministic file names, see ui/vite.config.ts).
# Re-run the configure step after UI changes, or set AUDIORACK_UI_DIR at
# runtime to serve straight from ui/dist during development.

option(AUDIORACK_BUILD_UI "Build the web UI bundle with npm at configure time" ON)

set(AUDIORACK_UI_DIST ${CMAKE_SOURCE_DIR}/ui/dist)

if(AUDIORACK_BUILD_UI)
    find_program(NPM_EXECUTABLE npm)

    if(NOT NPM_EXECUTABLE)
        message(FATAL_ERROR "npm not found — install Node >= 18 or configure with -DAUDIORACK_BUILD_UI=OFF")
    endif()

    message(STATUS "Building web UI bundle (npm install && npm run build)")

    execute_process(
        COMMAND ${NPM_EXECUTABLE} install --no-audit --no-fund
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/ui
        RESULT_VARIABLE AUDIORACK_NPM_INSTALL_RESULT)

    if(NOT AUDIORACK_NPM_INSTALL_RESULT EQUAL 0)
        message(FATAL_ERROR "npm install failed for ui/")
    endif()

    execute_process(
        COMMAND ${NPM_EXECUTABLE} run build
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/ui
        RESULT_VARIABLE AUDIORACK_NPM_BUILD_RESULT)

    if(NOT AUDIORACK_NPM_BUILD_RESULT EQUAL 0)
        message(FATAL_ERROR "npm run build failed for ui/")
    endif()
endif()

foreach(asset index.html app.js app.css)
    if(NOT EXISTS ${AUDIORACK_UI_DIST}/${asset})
        message(FATAL_ERROR "Missing UI asset ${AUDIORACK_UI_DIST}/${asset} — build ui/ first")
    endif()
endforeach()

juce_add_binary_data(AudioRackAssets
    HEADER_NAME UiAssets.h
    NAMESPACE uiassets
    SOURCES
        ${AUDIORACK_UI_DIST}/index.html
        ${AUDIORACK_UI_DIST}/app.js
        ${AUDIORACK_UI_DIST}/app.css)

# --- Plugin ---------------------------------------------------------------------

juce_add_plugin(AudioRack
    PRODUCT_NAME                "AudioRack"
    COMPANY_NAME                "AudioRack Project"
    COMPANY_WEBSITE             "https://github.com/celsosilvestre/audiorack"
    BUNDLE_ID                   com.audiorackproject.audiorack
    PLUGIN_MANUFACTURER_CODE    ARkP
    PLUGIN_CODE                 ARk1
    FORMATS                     ${AUDIORACK_FORMATS}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    NEEDS_WEBVIEW2              TRUE
    # Without this the standalone's Info.plist has no NSMicrophoneUsageDescription,
    # so macOS 10.14+ silently denies audio input (the app captures only silence).
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT  "AudioRack processes audio from the input device you select."
    VST3_CATEGORIES             Fx Dynamics EQ
    AU_MAIN_TYPE                kAudioUnitType_Effect)

juce_generate_juce_header(AudioRack)

set(AUDIORACK_PLUGIN_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/plugin/PluginProcessor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/plugin/PluginEditor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/state/RackState.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/RackWebView.cpp)

target_sources(AudioRack PRIVATE
    ${AUDIORACK_PLUGIN_SOURCES}
    ${AUDIORACK_CORE_SOURCES}
    ${AUDIORACK_DSP_SOURCES})
audiorack_strict_sources(${AUDIORACK_PLUGIN_SOURCES} ${AUDIORACK_CORE_SOURCES} ${AUDIORACK_DSP_SOURCES})

target_include_directories(AudioRack PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(AudioRack PUBLIC
    JUCE_WEB_BROWSER=1
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

if(WIN32)
    target_compile_definitions(AudioRack PUBLIC JUCE_USE_WIN_WEBVIEW2=1)
endif()

target_link_libraries(AudioRack
    PRIVATE
        AudioRackAssets
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
