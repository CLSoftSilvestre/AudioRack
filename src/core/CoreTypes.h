#pragma once

namespace audiorack
{

/// Fixed number of rack slots in v1. Parameter IDs are declared per slot at
/// processor construction (APVTS requires the full set up front), so this is
/// part of the state schema — raise only with a schema migration.
inline constexpr int kMaxSlots = 12;

/// Identity of a module type. All strings are static storage; instances are
/// cheap to copy on any thread.
struct ModuleDescriptor
{
    const char* id;        ///< stable, lowercase, used in param IDs and presets, e.g. "gain"
    const char* name;      ///< display name, e.g. "Gain"
    const char* category;  ///< "Dynamics", "EQ", "Time", "Utility", ...
    int rackUnits;         ///< faceplate height: 1, 2 or 3
};

/// Host transport snapshot passed to every process() call. Plain data, no locks.
struct TransportInfo
{
    double sampleRate    = 44100.0;
    double bpm           = 120.0;
    double ppqPosition   = 0.0;
    int    timeSigNumerator   = 4;
    int    timeSigDenominator = 4;
    bool   isPlaying     = false;
};

/// One meter snapshot produced by a module. Values are linear gains except
/// gainReductionDb. A negative peak/rms means "channel not present".
struct MeterFrame
{
    float peakL = 0.0f, peakR = 0.0f;
    float rmsL  = 0.0f, rmsR  = 0.0f;
    float gainReductionDb = 0.0f;
};

} // namespace audiorack
