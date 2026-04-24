#pragma once

namespace hiir
{
template <int NumCoefs, typename SampleType>
class Upsampler2xFPU
{
public:
    void set_coefs(const double*) noexcept {}

    void clear_buffers() noexcept
    {
        previous = SampleType{};
        hasPrevious = false;
    }

    void process_block(SampleType* output, const SampleType* input, long numSamples) noexcept
    {
        if (output == nullptr || input == nullptr || numSamples <= 0)
            return;

        auto last = hasPrevious ? previous : input[0];

        for (long i = 0; i < numSamples; ++i)
        {
            const auto current = input[i];
            const auto midpoint = static_cast<SampleType>((last + current) * static_cast<SampleType>(0.5));
            output[i * 2] = midpoint;
            output[i * 2 + 1] = current;
            last = current;
        }

        previous = last;
        hasPrevious = true;
    }

private:
    SampleType previous {};
    bool hasPrevious { false };
};
} // namespace hiir
