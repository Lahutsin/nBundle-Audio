#include "TestHelpers.h"
#include "Core/Engine/ModMatrix.h"

using namespace nsynth;

class ModMatrixTests : public juce::UnitTest
{
public:
    ModMatrixTests() : juce::UnitTest ("ModMatrix", "nSynth") {}

    void runTest() override
    {
        beginTest ("fixed and direct routes clamp amount and publish immutable snapshots");
        {
            ModMatrix matrix;
            auto empty = matrix.snapshot();
            matrix.setFixedAmount (ModSource::lfo2, ModTarget::crossMod, 1.5f);
            expectWithinAbsoluteError (matrix.getFixedAmount (ModSource::lfo2, ModTarget::crossMod), 1.0f, 1.0e-7f);
            expectWithinAbsoluteError (empty->fixed[(int) ModSource::lfo2][(int) ModTarget::crossMod], 0.0f, 1.0e-7f);
            expect (matrix.hasSource (ModSource::lfo2));
            expect (matrix.setDirectAmount (ModSource::velocity, DirectModTarget::delayMix, -1.4f));
            expectWithinAbsoluteError (matrix.getDirectAmount (ModSource::velocity, DirectModTarget::delayMix), -1.0f, 1.0e-7f);
            expect (! matrix.setDirectAmount (ModSource::velocity, DirectModTarget::delayMix, std::numeric_limits<float>::quiet_NaN()));
        }

        beginTest ("source and target clear operations affect exactly their routes");
        {
            ModMatrix matrix;
            matrix.setFixedAmount (ModSource::lfo2, ModTarget::crossMod, 0.5f);
            matrix.setFixedAmount (ModSource::velocity, ModTarget::crossMod, -0.25f);
            expect (matrix.setDirectAmount (ModSource::lfo2, DirectModTarget::delayFeedback, 0.6f));
            matrix.clearTarget (ModTarget::crossMod);
            expectWithinAbsoluteError (matrix.getFixedAmount (ModSource::lfo2, ModTarget::crossMod), 0.0f, 1.0e-7f);
            expectWithinAbsoluteError (matrix.getFixedAmount (ModSource::velocity, ModTarget::crossMod), 0.0f, 1.0e-7f);
            expectWithinAbsoluteError (matrix.getDirectAmount (ModSource::lfo2, DirectModTarget::delayFeedback), 0.6f, 1.0e-7f);
            matrix.clearSource (ModSource::lfo2);
            expect (! matrix.hasSource (ModSource::lfo2));
            expect (matrix.setDirectAmount (ModSource::velocity, DirectModTarget::delayFeedback, 0.2f));
            matrix.clearDirectTarget (DirectModTarget::delayFeedback);
            expectWithinAbsoluteError (matrix.getDirectAmount (ModSource::velocity, DirectModTarget::delayFeedback), 0.0f, 1.0e-7f);
        }

        beginTest ("matrix ValueTree round-trip preserves both route grids");
        {
            ModMatrix source, restored;
            source.setFixedAmount (ModSource::ribbon, ModTarget::lpfResonance, -0.8f);
            expect (source.setDirectAmount (ModSource::noteNumber, DirectModTarget::portamento, 0.35f));
            juce::String error;
            auto tree = source.toValueTree (1);
            expectEquals ((int) tree.getProperty ("layer"), 1);
            expect (restored.restoreFromValueTree (tree, &error), error);
            expectWithinAbsoluteError (restored.getFixedAmount (ModSource::ribbon, ModTarget::lpfResonance), -0.8f, 1.0e-7f);
            expectWithinAbsoluteError (restored.getDirectAmount (ModSource::noteNumber, DirectModTarget::portamento), 0.35f, 1.0e-7f);
        }

        beginTest ("schema 1 modulation snapshots remain readable after adding MPE sources");
        {
            ModMatrix restored;
            juce::ValueTree legacy ("MOD_MATRIX");
            legacy.setProperty ("schemaVersion", 1, nullptr);
            juce::ValueTree route ("ROUTE");
            route.setProperty ("source", (int) ModSource::noteNumber, nullptr);
            route.setProperty ("target", (int) ModTarget::crossMod, nullptr);
            route.setProperty ("amount", 0.5f, nullptr);
            legacy.addChild (route, -1, nullptr);
            juce::String error;
            expect (restored.restoreFromValueTree (legacy, &error), error);
            expectWithinAbsoluteError (restored.getFixedAmount (ModSource::noteNumber, ModTarget::crossMod),
                                       0.5f, 1.0e-7f);
        }

        beginTest ("invalid matrix restores fail without replacing the active snapshot");
        {
            ModMatrix matrix;
            matrix.setFixedAmount (ModSource::env1, ModTarget::env1Decay, 0.4f);
            juce::ValueTree invalid ("MOD_MATRIX");
            invalid.setProperty ("schemaVersion", ModMatrix::schemaVersion, nullptr);
            juce::ValueTree route ("ROUTE");
            route.setProperty ("source", (int) ModSource::dds2, nullptr);
            route.setProperty ("target", (int) ModTarget::lfo1Rate, nullptr);
            route.setProperty ("amount", 2.0f, nullptr);
            invalid.addChild (route, -1, nullptr);
            juce::String error;
            expect (! matrix.restoreFromValueTree (invalid, &error));
            expect (error.isNotEmpty());
            expectWithinAbsoluteError (matrix.getFixedAmount (ModSource::env1, ModTarget::env1Decay), 0.4f, 1.0e-7f);
        }

        beginTest ("direct routes duplicated by panel paths are rejected");
        {
            ModMatrix matrix;
            expect (! matrix.setDirectAmount (ModSource::dds2, DirectModTarget::vcfDds2Amount, 0.5f));
            expect (! matrix.setDirectAmount (ModSource::lfo2, DirectModTarget::lfo2Rate, 0.5f));
            expect (matrix.setDirectAmount (ModSource::ribbon, DirectModTarget::vcfDds2Amount, 0.5f));
        }
    }
};

static ModMatrixTests modMatrixTests;
