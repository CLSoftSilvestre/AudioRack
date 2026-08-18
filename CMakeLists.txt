cmake_minimum_required(VERSION 3.22)

project(AudioRack
    VERSION 0.1.0
    DESCRIPTION "AudioRack - a virtual 19-inch rack audio effects host"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version" FORCE)
endif()

option(AUDIORACK_BUILD_TESTS "Build Catch2 unit tests" ON)
option(AUDIORACK_BUILD_PLUGIN "Build the plugin/standalone targets" ON)

# --- Dependencies -----------------------------------------------------------

include(FetchContent)

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.15
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

if(AUDIORACK_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.7.1
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(Catch2)
endif()

# --- Strict warnings for first-party code only ------------------------------
# JUCE module sources are compiled into the plugin target, so -Werror is applied
# per-source to our files rather than target-wide.

function(audiorack_strict_sources)
    if(MSVC)
        set(flags "/W4;/WX")
    else()
        set(flags "-Wall;-Wextra;-Wpedantic;-Wshadow;-Wconversion;-Werror")
    endif()
    set_source_files_properties(${ARGV} PROPERTIES COMPILE_OPTIONS "${flags}")
endfunction()

# First-party engine/DSP sources, compiled into both the plugin and the test
# runner (kept as lists rather than a static lib so JUCE module sources are
# only ever compiled into final targets).
set(AUDIORACK_CORE_SOURCES
    ${CMAKE_SOURCE_DIR}/src/core/RackEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/core/ModuleRegistry.cpp)

set(AUDIORACK_DSP_SOURCES
    ${CMAKE_SOURCE_DIR}/src/dsp/gain/GainModule.cpp
    ${CMAKE_SOURCE_DIR}/src/dsp/compressor/CompressorModule.cpp
    ${CMAKE_SOURCE_DIR}/src/dsp/limiter/LimiterModule.cpp
    ${CMAKE_SOURCE_DIR}/src/dsp/gate/GateModule.cpp
    ${CMAKE_SOURCE_DIR}/src/dsp/eq/EqModule.cpp
    ${CMAKE_SOURCE_DIR}/src/dsp/delay/DelayModule.cpp
    ${CMAKE_SOURCE_DIR}/src/dsp/reverb/ReverbModule.cpp
    ${CMAKE_SOURCE_DIR}/src/dsp/saturator/SaturatorModule.cpp)

if(AUDIORACK_BUILD_PLUGIN)
    add_subdirectory(src)
endif()

if(AUDIORACK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
