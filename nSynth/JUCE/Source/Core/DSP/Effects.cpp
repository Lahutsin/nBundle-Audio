#include "Effects.h"
#include <complex>

namespace nsynth
{
namespace
{
// Topology-preserving one-pole coefficient G = g / (1 + g).
float onePoleCoefficient (float hz, double sampleRate) noexcept
{
    const double f = juce::jlimit (5.0, 0.45 * sampleRate, (double) hz);
    const double g = std::tan (juce::MathConstants<double>::pi * f / sampleRate);
    return (float) (g / (1.0 + g));
}

float onePoleLowpass (float x, float& state, float G) noexcept
{
    const float v = (x - state) * G;
    const float y = v + state;
    state = y + v;
    return y;
}

float flushDenormal (float x) noexcept
{
    return std::abs (x) < 1.0e-15f ? 0.0f : x;
}
} // namespace

//==============================================================================
int StereoChorus::voicesFor (ChorusMode mode) noexcept
{
    switch (mode)
    {
        case ChorusMode::one:    return 1;
        case ChorusMode::two:    return 2;
        case ChorusMode::oneTwo: return 3;
        case ChorusMode::off:    break;
    }
    return 0;
}

void StereoChorus::prepare (double sampleRate, int)
{
    sr = sampleRate;
    line.setSize (2, (int) std::ceil (sr * kMaxDelayMs * 0.001) + 4);
    // Wet anti-imaging low-pass kept high so the chorus stays bright.
    lpCoef = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * juce::jmin (14000.0, 0.4 * sr) / sr));
    reset();
}

void StereoChorus::reset()
{
    line.clear();
    writePos = 0;
    phase = 0.0;
    active = 0.0f;
    smoothedCentre = smoothedDepth = -1.0f;
    lpState[0] = lpState[1] = 0.0f;
    feedbackState[0] = feedbackState[1] = 0.0f;
}

double StereoChorus::lfoShape (double p) noexcept
{
    // Smooth BBD-style LFO (75 % triangle + 25 % sine): organic, without turnaround corners.
    const double tri = p < 0.5 ? 4.0 * p - 1.0 : 3.0 - 4.0 * p;
    return 0.75 * tri + 0.25 * std::sin (juce::MathConstants<double>::twoPi * p);
}

float StereoChorus::readDelay (int channel, double delaySamples) const noexcept
{
    const int size = line.getNumSamples();
    double readPos = (double) writePos - delaySamples;
    while (readPos < 0.0)
        readPos += size;
    const int i0 = (int) readPos;
    const float frac = (float) (readPos - i0);
    const float* d = line.getReadPointer (channel);
    const float a = d[i0 % size], b = d[(i0 + 1) % size];
    return a + (b - a) * frac;
}

void StereoChorus::process (juce::AudioBuffer<float>& buffer, int start, int num, const Settings& s) noexcept
{
    const int voices = voicesFor (s.mode);
    if (voices == 0 && active < 1.0e-5f)
    {
        active = 0.0f;
        return;
    }

    const int n = juce::jmax (1, voices);
    const float targetActive = voices > 0 ? 1.0f : 0.0f;
    const float centreTarget = (float) (juce::jlimit (1.0f, 20.0f, s.delayMs) * 0.001 * sr);
    const float depthTarget = juce::jlimit (0.0f, 1.0f, s.depth) * 0.9f * centreTarget;
    if (smoothedCentre < 0.0f)
    {
        smoothedCentre = centreTarget;
        smoothedDepth = depthTarget;
    }

    const double increment = (double) juce::jlimit (0.01f, 20.0f, s.rateHz) / sr;
    const double widthOffset = 0.5 * (double) juce::jlimit (0.0f, 1.0f, s.width);
    const float feedback = juce::jlimit (0.0f, 0.9f, s.feedback);
    const float mix = juce::jlimit (0.0f, 1.0f, s.mix);
    const int size = line.getNumSamples();
    const double maxDelay = (double) size - 3.0;
    // Between a plain average (coherent lows) and power-preserving (decorrelated highs),
    // so II and I + II are not quieter than I.
    const float voiceScale = std::pow ((float) n, -0.75f);

    for (int i = start; i < start + num; ++i)
    {
        smoothedCentre += (centreTarget - smoothedCentre) * 0.001f;
        smoothedDepth += (depthTarget - smoothedDepth) * 0.001f;

        const float inL = buffer.getSample (0, i), inR = buffer.getSample (1, i);
        line.setSample (0, writePos, inL + feedback * feedbackState[0]);
        line.setSample (1, writePos, inR + feedback * feedbackState[1]);

        float wetL = 0.0f, wetR = 0.0f;
        for (int v = 0; v < n; ++v)
        {
            double pl = phase + (double) v / (double) n;
            double pr = pl + widthOffset;
            pl -= std::floor (pl);
            pr -= std::floor (pr);
            wetL += readDelay (0, juce::jlimit (1.0, maxDelay, (double) smoothedCentre + smoothedDepth * lfoShape (pl)));
            wetR += readDelay (1, juce::jlimit (1.0, maxDelay, (double) smoothedCentre + smoothedDepth * lfoShape (pr)));
        }
        wetL *= voiceScale;
        wetR *= voiceScale;
        phase += increment;
        phase -= std::floor (phase);

        // Subtle bucket-brigade soft saturation before the anti-imaging low-pass.
        wetL = wetL * (1.0f - 0.03f * wetL * wetL / (1.0f + wetL * wetL));
        wetR = wetR * (1.0f - 0.03f * wetR * wetR / (1.0f + wetR * wetR));
        lpState[0] += (wetL - lpState[0]) * lpCoef;
        lpState[1] += (wetR - lpState[1]) * lpCoef;
        feedbackState[0] = flushDenormal (lpState[0]);
        feedbackState[1] = flushDenormal (lpState[1]);

        active += (targetActive - active) * 0.001f;
        const float m = mix * active;
        buffer.setSample (0, i, inL * (1.0f - m) + lpState[0] * m);
        buffer.setSample (1, i, inR * (1.0f - m) + lpState[1] * m);

        writePos = (writePos + 1) % size;
    }
}

//==============================================================================
void StereoDelay::prepare (double sampleRate, int)
{
    sr = sampleRate;
    line.setSize (2, (int) std::ceil (sr * kMaxSeconds) + 4);
    reset();
}

void StereoDelay::reset()
{
    line.clear();
    writePos = 0;
    currentDelaySamples = -1.0;
    smoothedMix = -1.0f; // initialised to the first target
    lowState[0] = lowState[1] = highState[0] = highState[1] = 0.0f;
    duckEnvelope = 0.0f;
}

void StereoDelay::process (juce::AudioBuffer<float>& buffer, int start, int num, const Settings& s) noexcept
{
    const int size = line.getNumSamples();
    const double target = juce::jlimit (1.0, (double) size - 3.0, s.timeSeconds * sr);
    if (currentDelaySamples < 0.0)
        currentDelaySamples = target;

    // Infinite feedback must not degrade: the echo-path filters and the interpolation would
    // colour every pass, so both are bypassed (integer read position) in that case.
    const bool infinite = s.freeze || s.feedback >= 0.999f;
    const float fb = s.freeze ? 1.0f : juce::jlimit (0.0f, 1.0f, s.feedback);
    const float inputGain = s.freeze ? 0.0f : 1.0f; // freeze stops recording immediately
    const float mixTarget = juce::jlimit (0.0f, 1.0f, s.mix);
    if (smoothedMix < 0.0f)
        smoothedMix = mixTarget;
    const double slew = 1.0 - std::exp (-1.0 / (0.05 * sr)); // ~50 ms tape-like time glide
    const float lowG = onePoleCoefficient (s.lowCutHz, sr);
    const float highG = onePoleCoefficient (s.highCutHz, sr);

    for (int i = start; i < start + num; ++i)
    {
        currentDelaySamples += (target - currentDelaySamples) * slew;
        smoothedMix += (mixTarget - smoothedMix) * 0.002f;

        double readPos = (double) writePos - currentDelaySamples;
        while (readPos < 0.0)
            readPos += size;
        int i0 = (int) readPos;
        float frac = (float) (readPos - i0);
        if (infinite)
        {
            i0 = (int) std::lround (readPos);
            frac = 0.0f;
        }

        float delayed[2];
        for (int ch = 0; ch < 2; ++ch)
        {
            const float* d = line.getReadPointer (ch);
            float echo = d[i0 % size] + (d[(i0 + 1) % size] - d[i0 % size]) * frac;
            if (! infinite)
            {
                echo = onePoleLowpass (echo, highState[ch], highG);           // HIGH CUT
                echo -= onePoleLowpass (echo, lowState[ch], lowG);            // LOW CUT
                highState[ch] = flushDenormal (highState[ch]);
                lowState[ch] = flushDenormal (lowState[ch]);
            }
            delayed[ch] = echo;
        }

        const float dryL = buffer.getSample (0, i), dryR = buffer.getSample (1, i);
        if (s.pingPong)
        {
            // Mono input enters the left line; the lines feed each other crosswise.
            line.setSample (0, writePos, 0.5f * (dryL + dryR) * inputGain + fb * delayed[1]);
            line.setSample (1, writePos, fb * delayed[0]);
        }
        else
        {
            line.setSample (0, writePos, dryL * inputGain + fb * delayed[0]);
            line.setSample (1, writePos, dryR * inputGain + fb * delayed[1]);
        }

        const float dryGain = dryGainForMix (smoothedMix);
        const float wetGain = wetGainForMix (smoothedMix) * (s.duck > 0.0f ? duckGain (duckEnvelope, dryL, dryR, s.duck, sr) : 1.0f);
        buffer.setSample (0, i, dryL * dryGain + delayed[0] * wetGain);
        buffer.setSample (1, i, dryR * dryGain + delayed[1] * wetGain);
        writePos = (writePos + 1) % size;
    }
}

//==============================================================================
double StereoReverb::lineSeconds (int index, float size) noexcept
{
    // Mutually prime-ish lengths; SIZE scales the whole network 0.35x .. 1.6x.
    static constexpr double baseMs[kLines] = { 29.7, 37.1, 41.1, 43.7, 53.3, 59.9, 67.7, 73.1 };
    return baseMs[juce::jlimit (0, kLines - 1, index)] * 0.001 * (0.35 + 1.25 * (double) juce::jlimit (0.0f, 1.0f, size));
}

void StereoReverb::prepare (double sampleRate, int)
{
    sr = sampleRate;
    for (auto& channel : preDelay)
        channel.assign ((size_t) std::ceil (kMaxPreDelayMs * 0.001 * sr) + 4, 0.0f);

    static constexpr double diffuserMs[2][kDiffusers] = { { 4.77, 3.59, 12.73, 9.30 }, { 4.97, 3.39, 12.13, 9.77 } };
    for (int ch = 0; ch < 2; ++ch)
        for (int d = 0; d < kDiffusers; ++d)
            diffusers[(size_t) ch][(size_t) d].buffer.assign ((size_t) juce::jmax (1, (int) std::lround (diffuserMs[ch][d] * 0.001 * sr)), 0.0f);

    // One shared write index: every line gets the length of the longest one (+ modulation headroom).
    lineSize = (int) std::ceil ((lineSeconds (kLines - 1, 1.0f) + 0.003) * sr) + 4;
    for (int i = 0; i < kLines; ++i)
    {
        lines[(size_t) i].assign ((size_t) lineSize, 0.0f);
        lineBaseSamples[(size_t) i] = lineSeconds (i, 0.0f) / 0.35 * sr; // unscaled length in samples
    }
    reset();
}

void StereoReverb::reset()
{
    for (auto& channel : preDelay)
        std::fill (channel.begin(), channel.end(), 0.0f);
    for (auto& channel : diffusers)
        for (auto& allpass : channel)
        {
            std::fill (allpass.buffer.begin(), allpass.buffer.end(), 0.0f);
            allpass.pos = 0;
        }
    for (auto& l : lines)
        std::fill (l.begin(), l.end(), 0.0f);
    preDelayWrite = lineWrite = 0;
    dampState.fill (0.0f);
    modCurrent.fill (0.0f);
    modStep.fill (0.0f);
    for (int i = 0; i < kLines; ++i)
        modPhase[(size_t) i] = (double) i / (double) kLines;
    lowCutState[0] = lowCutState[1] = 0.0f;
    modCounter = 0;
    smoothedMix = smoothedSize = smoothedPreDelay = -1.0f;
    duckEnvelope = 0.0f;
    idle = true;
}

float StereoReverb::readLine (int index, double delaySamples) const noexcept
{
    const auto& l = lines[(size_t) index];
    double readPos = (double) lineWrite - delaySamples;
    if (readPos < 0.0)
        readPos += lineSize;
    const int i0 = (int) readPos;
    const int i1 = i0 + 1 < lineSize ? i0 + 1 : 0;
    const float frac = (float) (readPos - i0);
    return l[(size_t) i0] + (l[(size_t) i1] - l[(size_t) i0]) * frac;
}

void StereoReverb::process (juce::AudioBuffer<float>& buffer, int start, int num, const Settings& s) noexcept
{
    const float mixTarget = juce::jlimit (0.0f, 1.0f, s.mix);
    if (mixTarget <= 0.0f && smoothedMix <= 1.0e-5f)
    {
        // Fully dry: skip the network and start the next use from silence.
        if (! idle)
            reset();
        return;
    }
    idle = false;

    const float sizeTarget = juce::jlimit (0.0f, 1.0f, s.size);
    const float preDelayTarget = (float) (juce::jlimit (kMinPreDelayMs, kMaxPreDelayMs - 5.0, (double) s.preDelayMs) * 0.001 * sr);
    if (smoothedMix < 0.0f)
    {
        smoothedMix = mixTarget;
        smoothedSize = sizeTarget;
        smoothedPreDelay = preDelayTarget;
    }

    // Per-line loop gains for the requested RT60, evaluated at the block's size.
    const float decay = juce::jmax (0.05f, s.decaySeconds);
    std::array<float, kLines> loopGain {};
    for (int i = 0; i < kLines; ++i)
        loopGain[(size_t) i] = (float) std::pow (10.0, -3.0 * lineSeconds (i, smoothedSize) / (double) decay);

    const float dampG = onePoleCoefficient (s.dampingHz, sr);
    const float lowG = onePoleCoefficient (s.lowCutHz, sr);
    const float modDepth = (float) (juce::jlimit (0.0f, 1.0f, s.modulation) * 0.0012 * sr);
    const float widthAmount = juce::jlimit (0.0f, 1.0f, s.width);
    constexpr float diffusion = 0.62f;
    constexpr float inputGain = 0.3f, outputGain = 1.0f;
    constexpr int modInterval = 16;
    static constexpr double modRatios[kLines] = { 1.0, 1.13, 0.87, 1.29, 0.71, 1.41, 0.93, 1.19 };
    static constexpr float injectSign[kLines] = { 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f };
    const int preSize = (int) preDelay[0].size();

    for (int i = start; i < start + num; ++i)
    {
        smoothedMix += (mixTarget - smoothedMix) * 0.002f;
        smoothedSize += (sizeTarget - smoothedSize) * 0.0005f;
        smoothedPreDelay += (preDelayTarget - smoothedPreDelay) * 0.001f;

        const float dryL = buffer.getSample (0, i), dryR = buffer.getSample (1, i);

        // ---- pre-delay ------------------------------------------------------
        preDelay[0][(size_t) preDelayWrite] = dryL;
        preDelay[1][(size_t) preDelayWrite] = dryR;
        double readPos = (double) preDelayWrite - (double) smoothedPreDelay;
        if (readPos < 0.0)
            readPos += preSize;
        const int p0 = (int) readPos;
        const int p1 = p0 + 1 < preSize ? p0 + 1 : 0;
        const float pf = (float) (readPos - p0);
        float inL = preDelay[0][(size_t) p0] + (preDelay[0][(size_t) p1] - preDelay[0][(size_t) p0]) * pf;
        float inR = preDelay[1][(size_t) p0] + (preDelay[1][(size_t) p1] - preDelay[1][(size_t) p0]) * pf;
        preDelayWrite = preDelayWrite + 1 < preSize ? preDelayWrite + 1 : 0;

        // ---- low cut + input diffusion ---------------------------------------
        inL -= onePoleLowpass (inL, lowCutState[0], lowG);
        inR -= onePoleLowpass (inR, lowCutState[1], lowG);
        for (int d = 0; d < kDiffusers; ++d)
        {
            inL = diffusers[0][(size_t) d].process (inL, diffusion);
            inR = diffusers[1][(size_t) d].process (inR, diffusion);
        }
        inL *= inputGain;
        inR *= inputGain;

        // ---- slow modulation, interpolated between control points ------------
        if (--modCounter <= 0)
        {
            modCounter = modInterval;
            for (int l = 0; l < kLines; ++l)
            {
                auto& ph = modPhase[(size_t) l];
                ph += modInterval * 0.5 * modRatios[l] / sr;
                ph -= std::floor (ph);
                const float targetMod = modDepth * (float) std::sin (juce::MathConstants<double>::twoPi * ph);
                modStep[(size_t) l] = (targetMod - modCurrent[(size_t) l]) / (float) modInterval;
            }
        }

        // ---- feedback delay network -----------------------------------------
        const double scale = 0.35 + 1.25 * (double) smoothedSize;
        std::array<float, kLines> y {};
        float sum = 0.0f;
        for (int l = 0; l < kLines; ++l)
        {
            modCurrent[(size_t) l] += modStep[(size_t) l];
            const double delaySamples = juce::jlimit (2.0, (double) lineSize - 3.0,
                                                      lineBaseSamples[(size_t) l] * scale + (double) modCurrent[(size_t) l]);
            float v = readLine (l, delaySamples);
            v = onePoleLowpass (v, dampState[(size_t) l], dampG);                 // HIGH CUT damping
            dampState[(size_t) l] = flushDenormal (dampState[(size_t) l]);
            y[(size_t) l] = v * loopGain[(size_t) l];
            sum += y[(size_t) l];
        }

        const float householder = sum * (2.0f / (float) kLines);
        for (int l = 0; l < kLines; ++l)
        {
            const float input = (l & 1) == 0 ? inL : inR;
            lines[(size_t) l][(size_t) lineWrite] = flushDenormal (y[(size_t) l] - householder + injectSign[l] * input);
        }
        lineWrite = lineWrite + 1 < lineSize ? lineWrite + 1 : 0;

        // ---- output taps, width, mix --------------------------------------------
        float wetL = (y[0] + y[2] + y[4] + y[6]) * outputGain;
        float wetR = (y[1] + y[3] + y[5] + y[7]) * outputGain;
        const float mid = 0.5f * (wetL + wetR), side = 0.5f * (wetL - wetR) * widthAmount;
        wetL = mid + side;
        wetR = mid - side;

        const float dryGain = dryGainForMix (smoothedMix);
        const float wetGain = wetGainForMix (smoothedMix) * (s.duck > 0.0f ? duckGain (duckEnvelope, dryL, dryR, s.duck, sr) : 1.0f);
        buffer.setSample (0, i, dryL * dryGain + wetL * wetGain);
        buffer.setSample (1, i, dryR * dryGain + wetR * wetGain);
    }
}

//==============================================================================
void OutputLimiter::prepare (double sampleRate)
{
    releaseCoef = (float) (1.0 - std::exp (-1.0 / (kReleaseSeconds * sampleRate)));
    reset();
}

void OutputLimiter::process (float* left, float* right, int num) noexcept
{
    for (int i = 0; i < num; ++i)
    {
        if (! std::isfinite (left[i]) || ! std::isfinite (right[i]))
            left[i] = right[i] = 0.0f;
        const float level = juce::jmax (std::abs (left[i]), std::abs (right[i]));
        gain += (1.0f - gain) * releaseCoef;
        if (level * gain > kCeiling)
            gain = kCeiling / level;
        left[i] *= gain;
        right[i] *= gain;
    }
}

//==============================================================================
void AirShelf::prepare (double sampleRate)
{
    // RBJ high shelf, slope S = 1.
    const double f0 = juce::jmin ((double) kFrequencyHz, 0.4 * sampleRate);
    const double A = std::pow (10.0, (double) kGainDb / 40.0);
    const double w0 = juce::MathConstants<double>::twoPi * f0 / sampleRate;
    const double cosw = std::cos (w0), alpha = std::sin (w0) / 2.0 * std::sqrt (2.0);
    const double sqrtA2alpha = 2.0 * std::sqrt (A) * alpha;
    const double a0 = (A + 1.0) - (A - 1.0) * cosw + sqrtA2alpha;
    b0 = (float) (A * ((A + 1.0) + (A - 1.0) * cosw + sqrtA2alpha) / a0);
    b1 = (float) (-2.0 * A * ((A - 1.0) + (A + 1.0) * cosw) / a0);
    b2 = (float) (A * ((A + 1.0) + (A - 1.0) * cosw - sqrtA2alpha) / a0);
    a1 = (float) (2.0 * ((A - 1.0) - (A + 1.0) * cosw) / a0);
    a2 = (float) (((A + 1.0) - (A - 1.0) * cosw - sqrtA2alpha) / a0);
    reset();
}

void AirShelf::reset()
{
    z1[0] = z1[1] = z2[0] = z2[1] = 0.0f;
}

void AirShelf::process (juce::AudioBuffer<float>& buffer, int start, int num) noexcept
{
    for (int ch = 0; ch < 2; ++ch)
    {
        float* d = buffer.getWritePointer (ch);
        float s1 = z1[ch], s2 = z2[ch];
        for (int i = start; i < start + num; ++i)
        {
            const float x = d[i];
            const float y = b0 * x + s1;
            s1 = b1 * x - a1 * y + s2;
            s2 = b2 * x - a2 * y;
            d[i] = y;
        }
        z1[ch] = flushDenormal (s1);
        z2[ch] = flushDenormal (s2);
    }
}

//==============================================================================
float duckGain (float& envelope, float dryL, float dryR, float duck, double sampleRate) noexcept
{
    const float level = juce::jmax (std::abs (dryL), std::abs (dryR));
    const float attack = (float) (1.0 - std::exp (-1.0 / (0.002 * sampleRate)));
    const float release = (float) (1.0 - std::exp (-1.0 / (0.25 * sampleRate)));
    envelope += (level - envelope) * (level > envelope ? attack : release);
    envelope = flushDenormal (envelope);
    // Full ducking from about -12 dBFS of dry signal.
    return 1.0f - juce::jlimit (0.0f, 1.0f, duck) * juce::jmin (1.0f, envelope * 4.0f);
}

//==============================================================================
namespace
{
// Anti-derivatives of the shaping curves (first-order ADAA).
float logCosh (float x) noexcept
{
    const float a = std::abs (x);
    return a + std::log1p (std::exp (-2.0f * a)) - 0.69314718f;
}

float hardClipIntegral (float x) noexcept
{
    const float a = std::abs (x);
    return a <= 1.0f ? 0.5f * x * x : a - 0.5f;
}

constexpr float kFoldScale = juce::MathConstants<float>::halfPi;

float foldIntegral (float x) noexcept
{
    return -std::cos (kFoldScale * x) / kFoldScale;
}

template <typename Curve, typename Integral>
float antiAliased (float x, float& previous, Curve curve, Integral integral) noexcept
{
    const float dx = x - previous;
    const float y = std::abs (dx) > 1.0e-4f ? (integral (x) - integral (previous)) / dx
                                             : curve (0.5f * (x + previous));
    previous = x;
    return y;
}
} // namespace

void Distortion::prepare (double sampleRate)
{
    sr = sampleRate;
    reset();
}

void Distortion::reset()
{
    for (int ch = 0; ch < 2; ++ch)
        lastInput[ch] = toneState[ch] = dcIn[ch] = dcOut[ch] = held[ch] = 0.0f;
    holdCounter = 0;
}

void Distortion::process (juce::AudioBuffer<float>& buffer, int start, int num, const Settings& s) noexcept
{
    const float drive = juce::jlimit (0.0f, 1.0f, s.drive);
    const float gain = juce::Decibels::decibelsToGain (36.0f * drive);
    const float makeup = 1.0f / (1.0f + 1.5f * drive);
    const float mix = juce::jlimit (0.0f, 1.0f, s.mix);
    const float toneG = onePoleCoefficient (800.0f * std::exp2 (4.6f * juce::jlimit (0.0f, 1.0f, s.tone)), sr);
    const float dcR = 1.0f - (float) (juce::MathConstants<double>::twoPi * 10.0 / sr);
    // CRUSH: 16 -> 3 bits and a sample-and-hold of up to 32 samples.
    const float steps = std::exp2 (15.0f - 13.0f * drive);
    const int holdLength = 1 + (int) std::lround (31.0f * drive * drive);

    for (int i = start; i < start + num; ++i)
    {
        const bool sampleNow = --holdCounter <= 0;
        if (sampleNow)
            holdCounter = holdLength;
        for (int ch = 0; ch < 2; ++ch)
        {
            const float dry = buffer.getSample (ch, i);
            float y = 0.0f;
            switch (s.type)
            {
                case DistortionType::soft:
                    y = antiAliased (dry * gain, lastInput[ch], [] (float v) { return std::tanh (v); }, logCosh);
                    break;
                case DistortionType::hard:
                    y = antiAliased (dry * gain, lastInput[ch], [] (float v) { return juce::jlimit (-1.0f, 1.0f, v); }, hardClipIntegral);
                    break;
                case DistortionType::fold:
                    y = antiAliased (dry * (1.0f + 0.25f * (gain - 1.0f)), lastInput[ch],
                                     [] (float v) { return std::sin (kFoldScale * v); }, foldIntegral);
                    break;
                case DistortionType::tube:
                {
                    // Biased (asymmetric, even harmonics) tanh, DC removed afterwards.
                    const float v = std::tanh (dry * gain + 0.35f) - 0.33637554f;
                    dcOut[ch] = v - dcIn[ch] + dcR * dcOut[ch];
                    dcIn[ch] = v;
                    y = dcOut[ch] = flushDenormal (dcOut[ch]);
                    break;
                }
                case DistortionType::crush:
                    if (sampleNow)
                        held[ch] = std::round (juce::jlimit (-1.0f, 1.0f, dry * (1.0f + drive)) * steps) / steps;
                    y = held[ch];
                    break;
            }
            if (s.type != DistortionType::crush)
                y *= makeup;
            y = onePoleLowpass (y, toneState[ch], toneG);
            toneState[ch] = flushDenormal (toneState[ch]);
            buffer.setSample (ch, i, dry + (y - dry) * mix);
        }
    }
}

//==============================================================================
void ThreeBandEq::prepare (double sampleRate)
{
    sr = sampleRate;
    designed = false;
    reset();
}

void ThreeBandEq::reset()
{
    for (auto& b : bands)
        b.z1[0] = b.z1[1] = b.z2[0] = b.z2[1] = 0.0f;
}

ThreeBandEq::Biquad ThreeBandEq::design (int band, const Settings& s, double sampleRate) noexcept
{
    // RBJ cookbook; the shelves' Q sets their slope (1/sqrt(2) = slope S = 1, higher overshoots).
    const double hz = band == 0 ? s.lowHz : band == 1 ? s.midHz : s.highHz;
    const double db = band == 0 ? s.lowDb : band == 1 ? s.midDb : s.highDb;
    const double w0 = juce::MathConstants<double>::twoPi * juce::jlimit (10.0, 0.45 * sampleRate, hz) / sampleRate;
    const double A = std::pow (10.0, db / 40.0);
    const double cosw = std::cos (w0), sinw = std::sin (w0);
    double b0, b1, b2, a0, a1, a2;
    if (band == 1)
    {
        const double alpha = sinw / (2.0 * juce::jmax (0.1, (double) s.midQ));
        b0 = 1.0 + alpha * A; b1 = -2.0 * cosw; b2 = 1.0 - alpha * A;
        a0 = 1.0 + alpha / A; a1 = -2.0 * cosw; a2 = 1.0 - alpha / A;
    }
    else
    {
        const double alpha = sinw / (2.0 * juce::jmax (0.1, (double) (band == 0 ? s.lowQ : s.highQ)));
        const double k = 2.0 * std::sqrt (A) * alpha;
        const double sign = band == 0 ? -1.0 : 1.0;   // low shelf / high shelf
        b0 = A * ((A + 1.0) + sign * (A - 1.0) * cosw + k);
        b1 = -2.0 * sign * A * ((A - 1.0) + sign * (A + 1.0) * cosw);
        b2 = A * ((A + 1.0) + sign * (A - 1.0) * cosw - k);
        a0 = (A + 1.0) - sign * (A - 1.0) * cosw + k;
        a1 = 2.0 * sign * ((A - 1.0) - sign * (A + 1.0) * cosw);
        a2 = (A + 1.0) - sign * (A - 1.0) * cosw - k;
    }
    Biquad q;
    q.b0 = (float) (b0 / a0); q.b1 = (float) (b1 / a0); q.b2 = (float) (b2 / a0);
    q.a1 = (float) (a1 / a0); q.a2 = (float) (a2 / a0);
    return q;
}

float ThreeBandEq::responseDb (const Settings& s, double hz, double sampleRate) noexcept
{
    const double w = juce::MathConstants<double>::twoPi * hz / sampleRate;
    const std::complex<double> z1 = std::polar (1.0, -w), z2 = std::polar (1.0, -2.0 * w);
    double db = 0.0;
    for (int band = 0; band < 3; ++band)
    {
        const auto q = design (band, s, sampleRate);
        const auto h = ((double) q.b0 + (double) q.b1 * z1 + (double) q.b2 * z2) / (1.0 + (double) q.a1 * z1 + (double) q.a2 * z2);
        db += 20.0 * std::log10 (juce::jmax (1.0e-9, std::abs (h)));
    }
    return (float) db;
}

void ThreeBandEq::process (juce::AudioBuffer<float>& buffer, int start, int num, const Settings& s) noexcept
{
    const auto same = [] (float a, float b) { return juce::exactlyEqual (a, b); };
    const bool changed = ! designed || ! same (s.lowHz, last.lowHz) || ! same (s.lowDb, last.lowDb) || ! same (s.midHz, last.midHz)
                      || ! same (s.midDb, last.midDb) || ! same (s.midQ, last.midQ) || ! same (s.highHz, last.highHz)
                      || ! same (s.highDb, last.highDb) || ! same (s.lowQ, last.lowQ) || ! same (s.highQ, last.highQ);
    if (changed)
    {
        for (int band = 0; band < 3; ++band)
        {
            const auto q = design (band, s, sr);
            auto& b = bands[(size_t) band];
            b.b0 = q.b0; b.b1 = q.b1; b.b2 = q.b2; b.a1 = q.a1; b.a2 = q.a2;
        }
        last = s;
        designed = true;
    }
    for (auto& b : bands)
        for (int ch = 0; ch < 2; ++ch)
        {
            float* d = buffer.getWritePointer (ch);
            float s1 = b.z1[ch], s2 = b.z2[ch];
            for (int i = start; i < start + num; ++i)
            {
                const float x = d[i];
                const float y = b.b0 * x + s1;
                s1 = b.b1 * x - b.a1 * y + s2;
                s2 = b.b2 * x - b.a2 * y;
                d[i] = y;
            }
            b.z1[ch] = flushDenormal (s1);
            b.z2[ch] = flushDenormal (s2);
        }
}

//==============================================================================
void Compressor::prepare (double sampleRate)
{
    sr = sampleRate;
    reset();
}

void Compressor::reset()
{
    reductionDb = 0.0f;
}

float Compressor::staticCurveDb (float inputDb, const Settings& s) noexcept
{
    constexpr float knee = 6.0f;
    const float over = inputDb - s.thresholdDb;
    const float slope = 1.0f / juce::jmax (1.0f, s.ratio) - 1.0f;
    if (over <= -0.5f * knee)
        return 0.0f;
    if (over >= 0.5f * knee)
        return slope * over;
    const float x = over + 0.5f * knee;
    return slope * x * x / (2.0f * knee);
}

void Compressor::process (juce::AudioBuffer<float>& buffer, int start, int num, const Settings& s,
                          const juce::AudioBuffer<float>* key) noexcept
{
    const float attack = (float) (1.0 - std::exp (-1.0 / (juce::jmax (0.05, (double) s.attackMs) * 0.001 * sr)));
    const float release = (float) (1.0 - std::exp (-1.0 / (juce::jmax (1.0, (double) s.releaseMs) * 0.001 * sr)));
    const float makeup = juce::Decibels::decibelsToGain (s.makeupDb);
    const float mix = juce::jlimit (0.0f, 1.0f, s.mix);
    const auto& detector = key != nullptr ? *key : buffer;

    for (int i = start; i < start + num; ++i)
    {
        const float level = juce::jmax (std::abs (detector.getSample (0, i)), std::abs (detector.getSample (1, i)));
        const float targetDb = staticCurveDb (juce::Decibels::gainToDecibels (level, -120.0f), s);   // <= 0
        // More reduction = attack, less = release.
        reductionDb += (targetDb - reductionDb) * (targetDb < reductionDb ? attack : release);
        reductionDb = flushDenormal (reductionDb);
        const float gain = juce::Decibels::decibelsToGain (reductionDb) * makeup;
        for (int ch = 0; ch < 2; ++ch)
        {
            const float dry = buffer.getSample (ch, i);
            buffer.setSample (ch, i, dry + (dry * gain - dry) * mix);
        }
    }
}

//==============================================================================
void Phaser::prepare (double sampleRate)
{
    sr = sampleRate;
    reset();
}

void Phaser::reset()
{
    for (int ch = 0; ch < 2; ++ch)
    {
        for (auto& v : state[ch])
            v = 0.0f;
        feedbackState[ch] = 0.0f;
        coef[ch] = 0.0f;
    }
    coefCounter = 0;
}

void Phaser::process (juce::AudioBuffer<float>& buffer, int start, int num, const Settings& s) noexcept
{
    const double increment = (double) juce::jlimit (0.01f, 20.0f, s.rateHz) / sr;
    const float depth = juce::jlimit (0.0f, 1.0f, s.depth);
    const float feedback = juce::jlimit (-0.95f, 0.95f, s.feedback);
    const float mix = juce::jlimit (0.0f, 1.0f, s.mix);
    constexpr int kCoefInterval = 16;

    for (int i = start; i < start + num; ++i)
    {
        if (--coefCounter <= 0)
        {
            coefCounter = kCoefInterval;
            for (int ch = 0; ch < 2; ++ch)
            {
                // Right channel a quarter cycle later; exponential sweep 200 Hz .. 200 Hz x 2^(4.3 x depth).
                const double lfo = 0.5 + 0.5 * std::sin (juce::MathConstants<double>::twoPi * (phase + 0.25 * ch));
                const double hz = juce::jmin (0.45 * sr, 200.0 * std::exp2 (4.3 * depth * lfo));
                const double t = std::tan (juce::MathConstants<double>::pi * hz / sr);
                coef[ch] = (float) ((t - 1.0) / (t + 1.0));
            }
        }
        phase += increment;
        phase -= std::floor (phase);

        for (int ch = 0; ch < 2; ++ch)
        {
            const float dry = buffer.getSample (ch, i);
            float x = dry + feedback * feedbackState[ch];
            for (int k = 0; k < kStages; ++k)
            {
                const float y = coef[ch] * x + state[ch][k];
                state[ch][k] = flushDenormal (x - coef[ch] * y);
                x = y;
            }
            feedbackState[ch] = flushDenormal (x);
            // MIX 50 %: dry + all-passed in equal parts, the deepest notches.
            buffer.setSample (ch, i, dry * (1.0f - mix) + x * mix);
        }
    }
}

//==============================================================================
void Flanger::prepare (double sampleRate, int)
{
    sr = sampleRate;
    line.setSize (2, (int) std::ceil (sr * 0.008) + 4);
    reset();
}

void Flanger::reset()
{
    line.clear();
    writePos = 0;
    phase = 0.0;
    feedbackState[0] = feedbackState[1] = 0.0f;
}

void Flanger::process (juce::AudioBuffer<float>& buffer, int start, int num, const Settings& s) noexcept
{
    const double increment = (double) juce::jlimit (0.01f, 20.0f, s.rateHz) / sr;
    const double minDelay = 0.0003 * sr;
    const double sweep = 0.005 * sr * (double) juce::jlimit (0.0f, 1.0f, s.depth);
    const float feedback = juce::jlimit (-0.95f, 0.95f, s.feedback);
    const float mix = juce::jlimit (0.0f, 1.0f, s.mix);
    const int size = line.getNumSamples();

    for (int i = start; i < start + num; ++i)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            double p = phase + 0.25 * ch;
            p -= std::floor (p);
            const double tri = p < 0.5 ? 2.0 * p : 2.0 - 2.0 * p;   // linear sweep sounds most "jet"-like
            double readPos = (double) writePos - (minDelay + sweep * tri);
            if (readPos < 0.0)
                readPos += size;
            const int i0 = (int) readPos;
            const int i1 = i0 + 1 < size ? i0 + 1 : 0;
            const float frac = (float) (readPos - i0);
            const float* d = line.getReadPointer (ch);
            const float wet = d[i0] + (d[i1] - d[i0]) * frac;

            const float dry = buffer.getSample (ch, i);
            line.setSample (ch, writePos, dry + feedback * feedbackState[ch]);
            feedbackState[ch] = flushDenormal (wet);
            buffer.setSample (ch, i, dry * (1.0f - mix) + wet * mix);
        }
        writePos = writePos + 1 < size ? writePos + 1 : 0;
        phase += increment;
        phase -= std::floor (phase);
    }
}
} // namespace nsynth
