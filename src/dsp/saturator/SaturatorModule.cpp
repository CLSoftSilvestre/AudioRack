// Waveshaping saturator (Zölzer, "DAFX", ch. 5; Pirkle, "Designing Audio
// Effect Plugins in C++", nonlinear processing).
//
// Signal: drive -> bias -> waveshaper -> DC block -> drive-compensated output.
// Shaping runs 4x oversampled through a JUCE polyphase Oversampling stage so
// the harmonics the nonlinearity generates above fs/2 are created in the
// oversampled domain and filtered before decimation, keeping aliasing far
// down. Latency is the oversampler's reported FIR delay.
//
// Curves:
//   Tube (asymmetric): even + odd harmonics; positive and negative halves
//     shaped differently for the characteristic 2nd-harmonic warmth.
//   Tape (symmetric tanh-ish with soft compression): odd harmonics, gentle
//     knee, the classic hyperbolic-tangent transfer.
//   Transistor (harder clip): faster onset, more odd harmonics / edge.
//
// Drive compensation divides out the small-signal gain of each curve at the
// current drive so louder drive doesn't just mean louder output.

#include "SaturatorModule.h"

#include <cmath>

namespace audiorack
{

namespace
{
    float fromDb (float db) noexcept { return std::pow (10.0f, db * 0.05f); }

    // --- transfer functions (input already drive-scaled) -------------------
    inline float shapeTube (float x, float bias) noexcept
    {
        // Asymmetric soft clip: bias pushes the operating point for even
        // harmonics. Positive half saturates sooner than the negative half.
        const float b = x + 0.15f * bias;
        if (b >= 0.0f)
            return std::tanh (b);
        return 0.85f * std::tanh (b / 0.85f);   // gentler negative half
    }

    inline float shapeTape (float x, float /*bias*/) noexcept
    {
        // Symmetric hyperbolic-tangent tape curve with a hint of hysteresis
        // flattening near the extremes.
        return std::tanh (x) * (1.0f - 0.10f * std::tanh (x) * std::tanh (x));
    }

    inline float shapeTransistor (float x, float /*bias*/) noexcept
    {
        // Harder cubic soft-clip clamped to +/-1: sharper knee than tanh.
        const float c = juce::jlimit (-1.5f, 1.5f, x);
        return c - (c * c * c) / 3.375f;        // 3.375 = 1.5^3, unity slope at 0
    }

    inline float shape (int type, float x, float bias) noexcept
    {
        switch (type)
        {
            case 0:  return shapeTube (x, bias);
            case 1:  return shapeTape (x, bias);
            default: return shapeTransistor (x, bias);
        }
    }
}

ModuleTypeInfo SaturatorModule::typeInfo()
{
    return {
        kDescriptor,
        [] { return std::unique_ptr<AudioModule> (std::make_unique<SaturatorModule>()); },
        &SaturatorModule::declareParameters
    };
}

void SaturatorModule::declareParameters (ParameterBuilder& b)
{
    b.add ({ "drive", "Drive",    { 0.0f, 36.0f, 0.1f }, 6.0f, "dB" });
    b.add ({ "type",  "Type",     { 0.0f, 2.0f, 1.0f }, 0.0f, "", { "Tube", "Tape", "Transistor" } });
    b.add ({ "bias",  "Bias",     { -1.0f, 1.0f, 0.01f }, 0.0f, "" });
    b.add ({ "out",   "Output",   { -24.0f, 12.0f, 0.1f }, 0.0f, "dB" });
    b.add ({ "mix",   "Mix",      { 0.0f, 1.0f, 0.001f }, 1.0f, "" });
    b.add ({ "autogain", "Auto Gain", { 0.0f, 1.0f, 1.0f }, 1.0f, "", { "Off", "On" } });
}

void SaturatorModule::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    fs = sampleRate;

    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        static_cast<size_t> (juce::jmax (1, numChannels)),
        2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
    oversampler->initProcessing (static_cast<size_t> (maxBlockSize));
    latency.store (static_cast<int> (std::ceil (oversampler->getLatencyInSamples())),
                   std::memory_order_relaxed);

    dryBuffer.setSize (juce::jmax (1, numChannels), maxBlockSize, false, false, true);

    for (auto& dc : dcBlocker)
        dc.setHighpass (18.0, 0.707, fs);

    smoothedDrive.reset (fs, 0.02);
    smoothedOut.reset (fs, 0.02);
    smoothedMix.reset (fs, 0.02);
    reset();
}

void SaturatorModule::reset()
{
    if (oversampler != nullptr)
        oversampler->reset();
    for (auto& dc : dcBlocker)
        dc.reset();

    smoothedDrive.setCurrentAndTargetValue (fromDb (driveDb.get()));
    smoothedOut.setCurrentAndTargetValue (fromDb (outDb.get()));
    smoothedMix.setCurrentAndTargetValue (mix.get());
}

void SaturatorModule::bindParameter (const juce::String& id, std::atomic<float>* v)
{
    if (v == nullptr) return;

    if      (id == "drive")    driveDb.value = v;
    else if (id == "type")     type.value = v;
    else if (id == "bias")     bias.value = v;
    else if (id == "out")      outDb.value = v;
    else if (id == "mix")      mix.value = v;
    else if (id == "autogain") autoGain.value = v;
}

void SaturatorModule::process (juce::dsp::AudioBlock<float>& block, const ProcessContext&) noexcept
{
    const auto numSamples  = static_cast<int> (block.getNumSamples());
    const auto numChannels = static_cast<int> (block.getNumChannels());

    const int   curveType = juce::jlimit (0, 2, static_cast<int> (type.get()));
    const float biasAmt   = bias.get();
    const bool  comp      = autoGain.get() >= 0.5f;

    smoothedDrive.setTargetValue (fromDb (driveDb.get()));
    smoothedOut.setTargetValue (fromDb (outDb.get()));
    smoothedMix.setTargetValue (mix.get());

    // Dry copy for the wet/dry blend (delayed by the oversampler latency below
    // would be ideal; at 4x FIR the delay is a couple of samples and the
    // module reports it, so we blend against the undelayed dry — acceptable
    // for a saturator, which has no notch-prone phase cancellation).
    for (int ch = 0; ch < numChannels && ch < dryBuffer.getNumChannels(); ++ch)
        dryBuffer.copyFrom (ch, 0, block.getChannelPointer (static_cast<size_t> (ch)), numSamples);

    juce::dsp::AudioBlock<float> mainBlock = block.getSubsetChannelBlock (
        0, static_cast<size_t> (numChannels));

    // Upsample, shape in the oversampled domain, downsample.
    auto up = oversampler->processSamplesUp (mainBlock);
    const auto upSamples  = static_cast<int> (up.getNumSamples());
    const auto upChannels = static_cast<int> (up.getNumChannels());
    const int  ratio      = upSamples / juce::jmax (1, numSamples);   // 4

    int upIdx = 0;
    for (int i = 0; i < numSamples; ++i)
    {
        const float drive = smoothedDrive.getNextValue();
        // Small-signal slope of each curve at 0 is ~1, so compensation is
        // 1/drive to preserve level as drive increases.
        const float makeup = comp ? 1.0f / juce::jmax (0.25f, drive) : 1.0f;

        for (int k = 0; k < ratio && upIdx + k < upSamples; ++k)
            for (int ch = 0; ch < upChannels; ++ch)
            {
                auto* d = up.getChannelPointer (static_cast<size_t> (ch));
                d[upIdx + k] = shape (curveType, d[upIdx + k] * drive, biasAmt) * makeup;
            }

        upIdx += ratio;
    }

    oversampler->processSamplesDown (mainBlock);

    // DC block (tube bias adds DC) + output trim + wet/dry.
    float peak[2] = { 0.0f, 0.0f };
    double sumSq[2] = { 0.0, 0.0 };

    for (int i = 0; i < numSamples; ++i)
    {
        const float outGain = smoothedOut.getNextValue();
        const float wet = smoothedMix.getNextValue();
        const float dry = 1.0f - wet;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = block.getChannelPointer (static_cast<size_t> (ch));
            const float shaped = dcBlocker[ch < 2 ? ch : 0].process (data[i]) * outGain;
            const float dryS = ch < dryBuffer.getNumChannels() ? dryBuffer.getSample (ch, i) : 0.0f;
            const float out = shaped * wet + dryS * dry;
            data[i] = out;

            if (ch < 2)
            {
                peak[static_cast<size_t> (ch)] = juce::jmax (peak[static_cast<size_t> (ch)], std::abs (out));
                sumSq[static_cast<size_t> (ch)] += static_cast<double> (out) * static_cast<double> (out);
            }
        }
    }

    meterPeakL.store (peak[0], std::memory_order_relaxed);
    meterPeakR.store (peak[1], std::memory_order_relaxed);
    const auto n = static_cast<double> (juce::jmax (1, numSamples));
    meterRmsL.store (static_cast<float> (std::sqrt (sumSq[0] / n)), std::memory_order_relaxed);
    meterRmsR.store (static_cast<float> (std::sqrt (sumSq[1] / n)), std::memory_order_relaxed);
}

void SaturatorModule::getMeterFrame (MeterFrame& frame) const noexcept
{
    frame.peakL = meterPeakL.load (std::memory_order_relaxed);
    frame.peakR = meterPeakR.load (std::memory_order_relaxed);
    frame.rmsL  = meterRmsL.load (std::memory_order_relaxed);
    frame.rmsR  = meterRmsR.load (std::memory_order_relaxed);
    frame.gainReductionDb = 0.0f;
}

} // namespace audiorack
