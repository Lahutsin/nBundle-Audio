#pragma once

namespace hiir
{
template <int NumCoefs, typename SampleType>
class Downsampler2xFPU
{
public:
    void set_coefs(const double*) noexcept {}
    void clear_buffers() noexcept {}

    void process_block(SampleType* output, const SampleType* input, long numSamples) noexcept
    {
        if (output == nullptr || input == nullptr || numSamples <= 0)
            return;

        for (long i = 0; i < numSamples; ++i)
        {
            const auto even = input[i * 2];
            const auto odd = input[i * 2 + 1];
            output[i] = static_cast<SampleType>((even + odd) * static_cast<SampleType>(0.5));
        }
    }
};
} // namespace hiir

