#pragma once

// Branching one-pole envelope follower (Zölzer, "Digital Audio Signal
// Processing" §7; Reiss & McPherson ch. 6): separate attack/release
// coefficients a = exp(-1 / (tau * fs)). Peak mode tracks |x|; RMS mode
// tracks x^2 through the same ballistics and returns sqrt.

#include <cmath>

namespace audiorack::dsp
{

class EnvelopeDetector
{
public:
    enum class Mode { peak, rms };

    void prepare (double sampleRate) noexcept
    {
        fs = sampleRate;
        setTimesMs (attackMs, releaseMs);
        reset();
    }

    void setMode (Mode m) noexcept { mode = m; }

    void setTimesMs (double newAttackMs, double newReleaseMs) noexcept
    {
        attackMs  = newAttackMs;
        releaseMs = newReleaseMs;
        attackCoef  = coefFor (attackMs);
        releaseCoef = coefFor (releaseMs);
    }

    void reset() noexcept { state = 0.0; }

    /// Feed one rectified-input sample, get the envelope (linear amplitude).
    float process (float rectified) noexcept
    {
        const double target = mode == Mode::rms
                                ? static_cast<double> (rectified) * static_cast<double> (rectified)
                                : static_cast<double> (rectified);

        const double coef = target > state ? attackCoef : releaseCoef;
        state = coef * state + (1.0 - coef) * target;

        return mode == Mode::rms ? static_cast<float> (std::sqrt (state))
                                 : static_cast<float> (state);
    }

private:
    double coefFor (double ms) const noexcept
    {
        if (ms <= 0.0 || fs <= 0.0)
            return 0.0;
        return std::exp (-1.0 / (ms * 0.001 * fs));
    }

    Mode   mode        = Mode::peak;
    double fs          = 44100.0;
    double attackMs    = 1.0;
    double releaseMs   = 100.0;
    double attackCoef  = 0.0;
    double releaseCoef = 0.0;
    double state       = 0.0;
};

} // namespace audiorack::dsp
