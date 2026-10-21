#include "Envelope.h"

namespace nsynth
{
float Envelope::coefficientFor (float seconds, float ratio, double sr) noexcept
{
    const double samples = (double) seconds * sr;
    if (samples <= 1.0)
        return 1.0f;
    return (float) (1.0 - std::pow ((double) ratio / (1.0 + (double) ratio), 1.0 / samples));
}

void Envelope::setSettings (const Settings& s) noexcept
{
    settings = s;
    settings.sustain = juce::jlimit (0.0f, 1.0f, s.sustain);
    attackCoef = coefficientFor (s.attack, kAttackRatio, sampleRate);
    decayCoef = coefficientFor (s.decay, kDecayRatio, sampleRate);
    releaseCoef = coefficientFor (s.release, kDecayRatio, sampleRate);
}

void Envelope::noteOn() noexcept
{
    if (settings.attackHold > 0.0f)
    {
        stage = Stage::attackHold;
        holdCounter = (int64_t) std::llround ((double) settings.attackHold * sampleRate);
        return;
    }
    enterAttack();
}

void Envelope::noteOff() noexcept
{
    if (stage != Stage::idle)
        stage = Stage::release;
}

void Envelope::enterAttack() noexcept
{
    stage = Stage::attack;
}

void Envelope::enterDecayHold() noexcept
{
    value = 1.0f;
    if (settings.decayHold > 0.0f)
    {
        stage = Stage::decayHold;
        holdCounter = (int64_t) std::llround ((double) settings.decayHold * sampleRate);
        return;
    }
    enterDecay();
}

void Envelope::enterDecay() noexcept
{
    stage = Stage::decay;
}

float Envelope::process() noexcept
{
    switch (stage)
    {
        case Stage::idle:
            value = 0.0f;
            break;

        case Stage::attackHold:
            if (--holdCounter <= 0)
                enterAttack();
            break;

        case Stage::attack:
            value += (1.0f + kAttackRatio - value) * attackCoef;
            if (value >= 1.0f)
                enterDecayHold();
            break;

        case Stage::decayHold:
            if (--holdCounter <= 0)
                enterDecay();
            break;

        case Stage::decay:
        {
            const float s = settings.sustain;
            value += (s - kDecayRatio - value) * decayCoef;
            if (value <= s)
            {
                value = s;
                // Loop mode: rise again from the sustain level. A sustain of
                // 1 would make the loop degenerate, so it then simply sustains.
                stage = (settings.loop && s < 0.999f) ? Stage::attack : Stage::sustain;
            }
            break;
        }

        case Stage::sustain:
            value += (settings.sustain - value) * 0.002f;
            break;

        case Stage::release:
            value += (-kDecayRatio - value) * releaseCoef;
            if (value <= 0.0f)
            {
                value = 0.0f;
                stage = Stage::idle;
            }
            break;
    }
    return value;
}
} // namespace nsynth
