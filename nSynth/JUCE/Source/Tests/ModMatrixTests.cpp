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
            expectEquals (empty->activeSlotCount, 0);   // published snapshots never change
            expect (! empty->sourceUsed[(size_t) ModSource::lfo2]);
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
            invalid.setProperty ("schemaVersion", 2, nullptr);
            juce::ValueTree route ("ROUTE");
            route.setProperty ("source", (int) ModSource::dds2, nullptr);
            route.setProperty ("target", (int) ModTarget::lfo1Rate, nullptr);
            route.setProperty ("amount", 2.0f, nullptr);
            invalid.addChild (route, -1, nullptr);
            juce::String error;
            expect (! matrix.restoreFromValueTree (invalid, &error));
            expect (error.isNotEmpty());
            expectWithinAbsoluteError (matrix.getFixedAmount (ModSource::env1, ModTarget::env1Decay), 0.4f, 1.0e-7f);

            juce::ValueTree badSlot ("MOD_MATRIX");
            badSlot.setProperty ("schemaVersion", ModMatrix::schemaVersion, nullptr);
            juce::ValueTree slot ("SLOT");
            slot.setProperty ("source", 99, nullptr);
            slot.setProperty ("dest", pid::lpf, nullptr);
            slot.setProperty ("amount", 0.5f, nullptr);
            badSlot.addChild (slot, -1, nullptr);
            expect (! matrix.restoreFromValueTree (badSlot, &error));
            juce::ValueTree badShape ("MOD_MATRIX");
            badShape.setProperty ("schemaVersion", ModMatrix::schemaVersion, nullptr);
            juce::ValueTree mseg ("MSEG");
            mseg.setProperty ("index", 0, nullptr);
            mseg.setProperty ("points", "0.5,0.5,0", nullptr);   // must start at 0 and end at 1
            badShape.addChild (mseg, -1, nullptr);
            expect (! matrix.restoreFromValueTree (badShape, &error));
            expectWithinAbsoluteError (matrix.getFixedAmount (ModSource::env1, ModTarget::env1Decay), 0.4f, 1.0e-7f);
        }

        beginTest ("direct routes duplicated by panel paths are rejected");
        {
            ModMatrix matrix;
            expect (! matrix.setDirectAmount (ModSource::dds2, DirectModTarget::vcfDds2Amount, 0.5f));
            expect (! matrix.setDirectAmount (ModSource::lfo2, DirectModTarget::lfo2Rate, 0.5f));
            expect (matrix.setDirectAmount (ModSource::ribbon, DirectModTarget::vcfDds2Amount, 0.5f));
        }

        beginTest ("slots: any continuous parameter, curve and polarity, round trip with macros, MSEGs and FX order");
        {
            ModMatrix source, restored;
            const int f2 = findLayerSpecIndex (pid::filter2Cutoff), comp = findLayerSpecIndex (pid::compThreshold);
            expect (source.addRoute (ModSource::mseg1, f2, 0.7f, true, -0.3f) >= 0);
            expect (source.addRoute (ModSource::macro3, comp, -0.4f) >= 0);
            expectEquals (source.addRoute (ModSource::mseg1, f2, 0.2f), 0);   // same route: amount updated
            expectWithinAbsoluteError (source.getSlot (0).amount, 0.2f, 1.0e-7f);
            expectEquals (source.addRoute (ModSource::env1, findLayerSpecIndex (pid::vcfType), 0.5f), -1);   // a choice
            expectEquals (source.addRoute (ModSource::env1, findLayerSpecIndex (pid::macro1), 0.5f), -1);    // macros
            source.setMacroName (2, "Crunch");
            source.setMseg (1, MsegShape::make (MsegShape::Preset::square));
            source.moveFxModule (7, 0);   // reverb first
            const auto data = source.snapshot();
            expect (data->modulated[(size_t) f2] && data->modulated[(size_t) comp]);
            expect (data->anyVoiceDestination && data->anyLayerDestination);

            juce::String error;
            expect (restored.restoreFromValueTree (source.toValueTree (0), &error), error);
            expect (restored.snapshot()->sameContent (*data));
            expectEquals (restored.getMacroName (2), juce::String ("Crunch"));
            expectEquals (restored.getMacroName (3), juce::String ("Macro 4"));
            expect (restored.getFxOrder()[0] == FxModule::reverb && restored.getFxOrder()[1] == FxModule::distortion);
            expect (restored.getSlot (0).bipolar);
            expectWithinAbsoluteError (restored.getSlot (0).curve, -0.3f, 1.0e-4f);
        }

        beginTest ("schema 2 routes become slots of the same parameters");
        {
            ModMatrix restored;
            juce::ValueTree legacy ("MOD_MATRIX");
            legacy.setProperty ("schemaVersion", 2, nullptr);
            juce::ValueTree route ("ROUTE"), direct ("DIRECT");
            route.setProperty ("source", (int) ModSource::env1, nullptr);
            route.setProperty ("target", (int) ModTarget::delayTime, nullptr);
            route.setProperty ("amount", 0.3f, nullptr);
            direct.setProperty ("source", (int) ModSource::timbre, nullptr);
            direct.setProperty ("target", (int) DirectModTarget::lpfCutoff, nullptr);
            direct.setProperty ("amount", -0.6f, nullptr);
            legacy.addChild (route, -1, nullptr);
            legacy.addChild (direct, -1, nullptr);
            juce::String error;
            expect (restored.restoreFromValueTree (legacy, &error), error);
            expectEquals (restored.getSlot (0).destination, findLayerSpecIndex (pid::delayMs));
            expectEquals (restored.getSlot (1).destination, findLayerSpecIndex (pid::lpf));
            expect (! restored.getSlot (0).bipolar && juce::exactlyEqual (restored.getSlot (1).curve, 0.0f));
            expect (restored.getFxOrder() == ModMatrixData::defaultFxOrder());
        }

        beginTest ("slot shaping and MSEG evaluation");
        {
            ModSlot slot { 0, 0, 1.0f, 0.0f, false };
            expectWithinAbsoluteError (slot.shape (0.25f), 0.25f, 1.0e-6f);
            slot.bipolar = true;
            expectWithinAbsoluteError (slot.shape (0.5f), 0.0f, 1.0e-6f);
            expectWithinAbsoluteError (slot.shape (1.0f), 1.0f, 1.0e-6f);
            slot.bipolar = false;
            slot.curve = 0.8f;
            expectGreaterThan (slot.shape (0.5f), 0.5f);   // convex
            slot.curve = -0.8f;
            expectLessThan (slot.shape (0.5f), 0.5f);
            expectWithinAbsoluteError (slot.shape (1.0f), 1.0f, 1.0e-6f);

            const auto triangle = MsegShape::make (MsegShape::Preset::triangle);
            int hint = 0;
            expectWithinAbsoluteError (triangle.evaluate (0.25f, hint), 0.5f, 1.0e-6f);
            expectWithinAbsoluteError (triangle.evaluate (0.5f, hint), 1.0f, 1.0e-6f);
            expectWithinAbsoluteError (triangle.evaluate (0.75f, hint), 0.5f, 1.0e-6f);
            expectWithinAbsoluteError (triangle.evaluate (0.1f, hint), 0.2f, 1.0e-6f);   // hint moving backwards
            const auto square = MsegShape::make (MsegShape::Preset::square);
            expectWithinAbsoluteError (square.evaluate (0.49f, hint), 1.0f, 1.0e-6f);
            expectWithinAbsoluteError (square.evaluate (0.51f, hint), 0.0f, 1.0e-6f);
            for (int p = 0; p <= (int) MsegShape::Preset::steps; ++p)
            {
                const auto shape = MsegShape::make ((MsegShape::Preset) p);
                expect (shape.isValid());
                MsegShape parsed;
                expect (MsegShape::fromString (shape.toString(), parsed) && parsed.count == shape.count);
            }
        }

        beginTest ("FX order moves keep every module exactly once");
        {
            ModMatrix matrix;
            matrix.moveFxModule (0, 5);
            const auto order = matrix.getFxOrder();
            expect (order[5] == FxModule::distortion && order[0] == FxModule::eq);
            std::array<FxModule, ModMatrixData::kFxModules> duplicate = order;
            duplicate[1] = duplicate[0];
            expect (! matrix.setFxOrder (duplicate));
            expect (matrix.getFxOrder() == order);
        }
    }
};

static ModMatrixTests modMatrixTests;
