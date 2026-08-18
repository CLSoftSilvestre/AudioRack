#pragma once

// Allocation-free streaming helpers for the lookahead limiter:
//
//  - RunningMin: sliding-window minimum via a monotonic wedge (Lemire,
//    "Streaming maximum-minimum filter using no more than three comparisons
//    per element", 2006). O(1) amortised per sample.
//  - MovingAverage: boxcar with a running double-precision sum (double
//    keeps additive drift far below float epsilon over any session length).
//  - DelayLine: plain power-of-two integer delay.
//
// All capacities are fixed at prepare() time; process() never allocates.

#include <cmath>
#include <cstddef>
#include <vector>

namespace audiorack::dsp
{

class RunningMin
{
public:
    /// maxWindow: largest window ever used; allocates once (message thread).
    void prepare (int maxWindow)
    {
        capacity = static_cast<size_t> (maxWindow) + 1;
        values.assign (capacity, 0.0f);
        indices.assign (capacity, 0);
        reset();
    }

    void setWindow (int newWindow) noexcept { window = newWindow; }

    void reset() noexcept
    {
        head = tail = 0;
        sampleIndex = 0;
    }

    /// Push one sample, get min over the last `window` samples.
    float process (float x) noexcept
    {
        // Drop candidates >= x from the back (they can never be the minimum
        // while x is in the window).
        while (head != tail && values[(tail + capacity - 1) % capacity] >= x)
            tail = (tail + capacity - 1) % capacity;

        values[tail]  = x;
        indices[tail] = sampleIndex;
        tail = (tail + 1) % capacity;

        // Expire the front when it leaves the window.
        if (indices[head] <= sampleIndex - static_cast<long long> (window))
            head = (head + 1) % capacity;

        ++sampleIndex;
        return values[head];
    }

private:
    std::vector<float>     values;
    std::vector<long long> indices;
    size_t capacity = 1, head = 0, tail = 0;
    long long sampleIndex = 0;
    int window = 1;
};

/// NB: idle state is 1.0 (unity gain), because this smooths limiter gain
/// signals; it is not a general-purpose zero-initialised average.
class MovingAverage
{
public:
    void prepare (int maxLength)
    {
        buffer.assign (static_cast<size_t> (maxLength) + 1, 0.0f);
        reset();
    }

    void setLength (int newLength) noexcept
    {
        length = newLength < 1 ? 1 : newLength;
        reset();
    }

    void reset() noexcept
    {
        std::fill (buffer.begin(), buffer.end(), 1.0f);
        sum = static_cast<double> (length);
        pos = 0;
    }

    float process (float x) noexcept
    {
        sum += static_cast<double> (x) - static_cast<double> (buffer[pos]);
        buffer[pos] = x;
        pos = (pos + 1) % static_cast<size_t> (length);
        return static_cast<float> (sum / static_cast<double> (length));
    }

private:
    std::vector<float> buffer;
    double sum   = 0.0;
    size_t pos   = 0;
    int    length = 1;
};

class DelayLine
{
public:
    void prepare (int maxDelay, int numChannels)
    {
        size_t cap = 1;
        while (cap < static_cast<size_t> (maxDelay) + 1)
            cap <<= 1;

        mask = cap - 1;
        lines.assign (static_cast<size_t> (numChannels), std::vector<float> (cap, 0.0f));
        pos = 0;
    }

    void setDelay (int newDelay) noexcept { delay = static_cast<size_t> (newDelay); }

    void reset() noexcept
    {
        for (auto& line : lines)
            std::fill (line.begin(), line.end(), 0.0f);
        pos = 0;
    }

    /// Push x on `channel`, get the sample from `delay` samples ago.
    /// Call advance() once per frame after all channels are pushed.
    float process (int channel, float x) noexcept
    {
        auto& line = lines[static_cast<size_t> (channel)];
        line[pos & mask] = x;
        return line[(pos + mask + 1 - delay) & mask];
    }

    void advance() noexcept { ++pos; }

private:
    std::vector<std::vector<float>> lines;
    size_t mask = 0, pos = 0, delay = 0;
};

} // namespace audiorack::dsp
