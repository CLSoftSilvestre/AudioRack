set(AUDIORACK_TEST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/SanityTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SpscQueueTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/RackEngineTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/DynamicsTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TimeToneTests.cpp)

add_executable(audiorack_tests
    ${AUDIORACK_TEST_SOURCES}
    ${AUDIORACK_CORE_SOURCES}
    ${AUDIORACK_DSP_SOURCES})

# Strict flags are per-directory, so first-party sources need re-marking here.
audiorack_strict_sources(${AUDIORACK_TEST_SOURCES} ${AUDIORACK_CORE_SOURCES} ${AUDIORACK_DSP_SOURCES})

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
