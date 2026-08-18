#pragma once

// Fractional delay line with 3rd-order (4-point) Lagrange interpolation
// (Laakso et al., "Splitting the Unit Delay", IEEE SP Mag 1996). Lagrange is
// chosen over allpass interpolation because the delay time is modulated
// (tape wow/flutter, reverb line modulation) and Lagrange has no recursive
// state to produce transients when the delay moves.

#include <cmath>
#include <cstddef>
#include <vector>

namespace audiorack::dsp
{

class FractionalDelay
{
public:
    void prepare (double maxDelaySamples)
    {
        size_t cap = 8;
        while (cap < static_cast<size_t> (maxDelaySamples) + 8)
            cap <<= 1;

        mask = cap - 1;
        buffer.assign (cap, 0.0f);
        writePos = 0;
    }

    void reset() noexcept
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
    }

    void write (float x) noexcept
    {
        buffer[writePos & mask] = x;
        ++writePos;
    }

    /// Read `delay` samples behind the last write (delay >= 2 recommended).
    float read (double delay) const noexcept
    {
        const double d      = delay < 2.0 ? 2.0 : delay;
        const auto   whole  = static_cast<size_t> (d);
        const float  frac   = static_cast<float> (d - static_cast<double> (whole));

        // x[n-(whole-1)] .. x[n-(whole+2)] around the fractional position.
        const float y0 = at (whole - 1);
        const float y1 = at (whole);
        const float y2 = at (whole + 1);
        const float y3 = at (whole + 2);

        // 4-point Lagrange basis, t in [0,1) between y1 and y2.
        const float t   = frac;
        const float tm1 = t - 1.0f, tm2 = t - 2.0f, tp1 = t + 1.0f;

        return y0 * (t * tm1 * tm2) / -6.0f
             + y1 * (tp1 * tm1 * tm2) / 2.0f
             + y2 * (tp1 * t * tm2) / -2.0f
             + y3 * (tp1 * t * tm1) / 6.0f;
    }

private:
    float at (size_t samplesBack) const noexcept
    {
        // read() runs before write() in a frame, so writePos already points
        // past the most recent write; a delay of d returns the sample written
        // exactly d frames ago (buffer[writePos - d]).
        return buffer[(writePos - samplesBack) & mask];
    }

    std::vector<float> buffer;
    size_t mask = 0, writePos = 0;
};

} // namespace audiorack::dsp
