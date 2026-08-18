#pragma once

// Direct Form II transposed biquad with RBJ "Audio EQ Cookbook" coefficients
// (Robert Bristow-Johnson, "Cookbook formulae for audio EQ biquad filter
// coefficients"). Only the responses the dynamics sidechains need for now;
// the parametric EQ (M6) extends this same header.

#include <cmath>

namespace audiorack::dsp
{

class Biquad
{
public:
    void setHighpass (double fc, double q, double sampleRate) noexcept
    {
        const double w0    = 2.0 * pi * clampFreq (fc, sampleRate) / sampleRate;
        const double cosw0 = std::cos (w0);
        const double alpha = std::sin (w0) / (2.0 * q);
        const double a0    = 1.0 + alpha;

        b0 = ((1.0 + cosw0) / 2.0) / a0;
        b1 = (-(1.0 + cosw0)) / a0;
        b2 = b0;
        a1 = (-2.0 * cosw0) / a0;
        a2 = (1.0 - alpha) / a0;
    }

    void setLowpass (double fc, double q, double sampleRate) noexcept
    {
        const double w0    = 2.0 * pi * clampFreq (fc, sampleRate) / sampleRate;
        const double cosw0 = std::cos (w0);
        const double alpha = std::sin (w0) / (2.0 * q);
        const double a0    = 1.0 + alpha;

        b0 = ((1.0 - cosw0) / 2.0) / a0;
        b1 = (1.0 - cosw0) / a0;
        b2 = b0;
        a1 = (-2.0 * cosw0) / a0;
        a2 = (1.0 - alpha) / a0;
    }

    /// Bell/peaking EQ. A = 10^(dB/40) per the cookbook.
    void setPeak (double fc, double q, double gainDb, double sampleRate) noexcept
    {
        const double A     = std::pow (10.0, gainDb / 40.0);
        const double w0    = 2.0 * pi * clampFreq (fc, sampleRate) / sampleRate;
        const double cosw0 = std::cos (w0);
        const double alpha = std::sin (w0) / (2.0 * q);
        const double a0    = 1.0 + alpha / A;

        b0 = (1.0 + alpha * A) / a0;
        b1 = (-2.0 * cosw0) / a0;
        b2 = (1.0 - alpha * A) / a0;
        a1 = (-2.0 * cosw0) / a0;
        a2 = (1.0 - alpha / A) / a0;
    }

    void setLowShelf (double fc, double q, double gainDb, double sampleRate) noexcept
    {
        const double A     = std::pow (10.0, gainDb / 40.0);
        const double w0    = 2.0 * pi * clampFreq (fc, sampleRate) / sampleRate;
        const double cosw0 = std::cos (w0);
        const double alpha = std::sin (w0) / (2.0 * q);
        const double sqA2a = 2.0 * std::sqrt (A) * alpha;
        const double a0    = (A + 1.0) + (A - 1.0) * cosw0 + sqA2a;

        b0 = (A * ((A + 1.0) - (A - 1.0) * cosw0 + sqA2a)) / a0;
        b1 = (2.0 * A * ((A - 1.0) - (A + 1.0) * cosw0)) / a0;
        b2 = (A * ((A + 1.0) - (A - 1.0) * cosw0 - sqA2a)) / a0;
        a1 = (-2.0 * ((A - 1.0) + (A + 1.0) * cosw0)) / a0;
        a2 = ((A + 1.0) + (A - 1.0) * cosw0 - sqA2a) / a0;
    }

    void setHighShelf (double fc, double q, double gainDb, double sampleRate) noexcept
    {
        const double A     = std::pow (10.0, gainDb / 40.0);
        const double w0    = 2.0 * pi * clampFreq (fc, sampleRate) / sampleRate;
        const double cosw0 = std::cos (w0);
        const double alpha = std::sin (w0) / (2.0 * q);
        const double sqA2a = 2.0 * std::sqrt (A) * alpha;
        const double a0    = (A + 1.0) - (A - 1.0) * cosw0 + sqA2a;

        b0 = (A * ((A + 1.0) + (A - 1.0) * cosw0 + sqA2a)) / a0;
        b1 = (-2.0 * A * ((A - 1.0) + (A + 1.0) * cosw0)) / a0;
        b2 = (A * ((A + 1.0) + (A - 1.0) * cosw0 - sqA2a)) / a0;
        a1 = (2.0 * ((A - 1.0) - (A + 1.0) * cosw0)) / a0;
        a2 = ((A + 1.0) - (A - 1.0) * cosw0 - sqA2a) / a0;
    }

    void setIdentity() noexcept
    {
        b0 = 1.0; b1 = b2 = a1 = a2 = 0.0;
    }

    void reset() noexcept { z1 = z2 = 0.0; }

    float process (float x) noexcept
    {
        const double in  = static_cast<double> (x);
        const double out = b0 * in + z1;

        z1 = b1 * in - a1 * out + z2;
        z2 = b2 * in - a2 * out;

        return static_cast<float> (out);
    }

private:
    static constexpr double pi = 3.14159265358979323846;

    static double clampFreq (double fc, double sampleRate) noexcept
    {
        const double nyquistGuard = 0.49 * sampleRate;
        return fc < 1.0 ? 1.0 : (fc > nyquistGuard ? nyquistGuard : fc);
    }

    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double z1 = 0.0, z2 = 0.0;
};

} // namespace audiorack::dsp
