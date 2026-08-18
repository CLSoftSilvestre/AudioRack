set(AUDIORACK_TEST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/SanityTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SpscQueueTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/RackEngineTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/DynamicsTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TimeToneTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SessionRecoveryTests.cpp)

# SessionRecovery lives in src/state (outside AUDIORACK_CORE_SOURCES, which the
# plugin embeds). It depends only on juce_core, so it links into the test runner
# cleanly without pulling in the APVTS-bound processor.
set(AUDIORACK_TESTED_STATE_SOURCES
    ${CMAKE_SOURCE_DIR}/src/state/SessionRecovery.cpp)

add_executable(audiorack_tests
    ${AUDIORACK_TEST_SOURCES}
    ${AUDIORACK_TESTED_STATE_SOURCES}
    ${AUDIORACK_CORE_SOURCES}
    ${AUDIORACK_DSP_SOURCES})

# Strict flags are per-directory, so first-party sources need re-marking here.
audiorack_strict_sources(${AUDIORACK_TEST_SOURCES} ${AUDIORACK_TESTED_STATE_SOURCES} ${AUDIORACK_CORE_SOURCES} ${AUDIORACK_DSP_SOURCES})

target_include_directories(audiorack_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_compile_definitions(audiorack_tests PRIVATE
    JUCE_STANDALONE_APPLICATION=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=0)

target_link_libraries(audiorack_tests PRIVATE
    Catch2::Catch2WithMain
    juce::juce_dsp
    juce::juce_recommended_config_flags)

include(Catch)
# PRE_TEST: discovery at ctest time, not build time — the TSan build must not
# execute the instrumented binary on machines whose TSan runtime is broken.
catch_discover_tests(audiorack_tests DISCOVERY_MODE PRE_TEST)

# --- Performance benchmark (not a ctest — absolute timings are machine-bound) ---
# Times RackEngine::process on a full 12-slot rack. Build + run manually to
# reproduce the numbers in docs/PROGRESS.md:
#   cmake --build --preset macos --target audiorack_bench
#   ./build/macos/tests/audiorack_bench
add_executable(audiorack_bench
    ${CMAKE_CURRENT_SOURCE_DIR}/RackBench.cpp
    ${AUDIORACK_CORE_SOURCES}
    ${AUDIORACK_DSP_SOURCES})

audiorack_strict_sources(${CMAKE_CURRENT_SOURCE_DIR}/RackBench.cpp)

target_include_directories(audiorack_bench PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_compile_definitions(audiorack_bench PRIVATE
    JUCE_STANDALONE_APPLICATION=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

# Optimisation comes from the Release preset (CMAKE_BUILD_TYPE=Release); the
# bench is only meaningful built that way.
target_link_libraries(audiorack_bench PRIVATE
    juce::juce_dsp
    juce::juce_recommended_config_flags)
