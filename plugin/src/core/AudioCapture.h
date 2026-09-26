#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <vector>

namespace nova
{

// Single-producer (audio thread) / single-consumer (analysis thread) FIFO carrying the chain
// input ("dry") and chain output ("processed") as 4 interleaved floats per frame:
//   [dryL, dryR, wetL, wetR]
// The audio thread never blocks and never allocates: if the consumer falls behind, frames are
// dropped and counted.
class AudioCapture
{
public:
    static constexpr int kStride = 4;

    void prepare (int capacityFrames)
    {
        fifo.setTotalSize (capacityFrames);
        data.assign ((size_t) capacityFrames * kStride, 0.f);
        fifo.reset();
        dropped.store (0);
    }

    // Audio thread. dryR / wetR may be nullptr for mono (the left channel is duplicated).
    void push (const float* dryL, const float* dryR, const float* wetL, const float* wetR, int n) noexcept
    {
        if (data.empty()) return;
        int s1, n1, s2, n2;
        fifo.prepareToWrite (n, s1, n1, s2, n2);
        auto write = [&] (int start, int count, int srcOffset)
        {
            float* d = data.data() + (size_t) start * kStride;
            for (int i = 0; i < count; ++i)
            {
                const int k = srcOffset + i;
                d[0] = dryL[k];
                d[1] = dryR != nullptr ? dryR[k] : dryL[k];
                d[2] = wetL[k];
                d[3] = wetR != nullptr ? wetR[k] : wetL[k];
                d += kStride;
            }
        };
        if (n1 > 0) write (s1, n1, 0);
        if (n2 > 0) write (s2, n2, n1);
        fifo.finishedWrite (n1 + n2);
        if (n1 + n2 < n)
            dropped.fetch_add (n - (n1 + n2), std::memory_order_relaxed);
    }

    // Consumer thread. Returns number of frames copied into dst (kStride floats per frame).
    int pull (float* dst, int maxFrames) noexcept
    {
        int s1, n1, s2, n2;
        fifo.prepareToRead (maxFrames, s1, n1, s2, n2);
        if (n1 > 0) std::memcpy (dst, data.data() + (size_t) s1 * kStride, sizeof (float) * (size_t) (n1 * kStride));
        if (n2 > 0) std::memcpy (dst + (size_t) n1 * kStride, data.data() + (size_t) s2 * kStride, sizeof (float) * (size_t) (n2 * kStride));
        fifo.finishedRead (n1 + n2);
        return n1 + n2;
    }

    int available() const noexcept { return fifo.getNumReady(); }
    int64_t droppedFrames() const noexcept { return dropped.load (std::memory_order_relaxed); }

private:
    juce::AbstractFifo fifo { 1 };
    std::vector<float> data;
    std::atomic<int64_t> dropped { 0 };
};

} // namespace nova
