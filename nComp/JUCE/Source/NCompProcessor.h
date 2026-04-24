#pragma once

#include <cassert>
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include "Parameters.h"

namespace NComp
{
class NCompProcessor
{
public:
    void prepare(const juce::dsp::ProcessSpec& spec);
    void reset() noexcept;
    int getLatencySamples() const noexcept { return lookaheadSamples + parallelDelaySamples; }
    void process(juce::AudioBuffer<float>& mainBuffer,
                 const juce::AudioBuffer<float>* sideChainBuffer,
                 juce::AudioProcessorValueTreeState& state,
                 std::atomic<float>& meterL,
                 std::atomic<float>& meterR,
                 std::atomic<float>& transferInputDb,
                 std::atomic<float>& transferOutputDb,
                 std::atomic<float>& transferReductionDb);

private:
    using SmoothedValue = juce::SmoothedValue<double, juce::ValueSmoothingTypes::Linear>;

    static double dbToLin(double db) noexcept;
    static double linToDb(double lin) noexcept;
    static double getParamValue(juce::AudioProcessorValueTreeState& state, const char* id, double fallback = 0.0) noexcept;
    void prepareLookahead(double sr);
    void resetLookahead() noexcept;
    void prepareParallelDelay(int latencySamples);
    void resetParallelDelay() noexcept;
    void processParallelDelaySample(double inL, double inR, double& outL, double& outR) noexcept;
    void ensureScratchCapacity(int numSamples);
    void resetParameterSmoothers() noexcept;
    void prepareSidechainListenMonitor(double sr) noexcept;
    void resetSidechainListenMonitor() noexcept;
    double applySidechainListenSafetyGain(double left, double right) noexcept;
    void prepareAutoGainTracking(double sr) noexcept;
    void updateAutoGainTiming(double attackL, double attackR, double releaseL, double releaseR, double mix) noexcept;
    void resetAutoGainTracking() noexcept;
    double updateAutoGainCompensation(double referenceL,
                                      double referenceR,
                                      double processedL,
                                      double processedR,
                                      double guideCompensationLin) noexcept;

    class EnvelopeDetector
    {
    public:
        EnvelopeDetector(double ms = 1.0, double sr = 44100.0);
        void setTc(double ms);
        void setSampleRate(double sr);
        double getTc() const noexcept { return ms; }
        double getSampleRate() const noexcept { return sampleRate; }
        void run(double in, double& state) const noexcept;

    private:
        void updateCoef() noexcept;
        double sampleRate { 44100.0 };
        double ms { 1.0 };
        double coef { 0.0 };
    };

    class DetectorEq
    {
    public:
        void setSampleRate(double sr) noexcept;
        void setHighPass(double hz) noexcept;
        void setTilt(double db) noexcept;
        void reset() noexcept;
        double processSample(double in) noexcept;

    private:
        void updateCoefficients() noexcept;

        static constexpr double tiltPivotHz = 1800.0;
        double sampleRate { 44100.0 };
        double highPassHz { 90.0 };
        double tiltDb { 0.0 };
        double highPassCoef { 0.0 };
        double tiltCoef { 0.0 };
        double lowState { 0.0 };
        double tiltState { 0.0 };
        double lowBandGain { 1.0 };
        double highBandGain { 1.0 };
    };

    class AttRelEnvelope
    {
    public:
        AttRelEnvelope(double attackMs = 10.0, double releaseMs = 100.0, double sr = 44100.0);
        void setAttack(double ms);
        void setRelease(double ms);
        void setSampleRate(double sr);
        double getAttack() const noexcept { return attack.getTc(); }
        double getRelease() const noexcept { return release.getTc(); }
        void run(double in, double& state) const noexcept;

    private:
        EnvelopeDetector attack;
        EnvelopeDetector release;
    };

    class SimpleComp : public AttRelEnvelope
    {
    public:
        SimpleComp();
        void setCharacterMode(int modeIndex);
        int getCharacterMode() const noexcept { return characterMode; }
        void setSampleRate(double sr);
        void setAttack(double ms);
        void setRelease(double ms);
        void setThresh(double db);
        void setRatio(double ratioIn);
        void setDetectorHighPass(double hz);
        void setDetectorTilt(double db);
        void initRuntime() noexcept;
        double detectKeyLevel(double sideIn) noexcept;
        double applyGainFromKeyLevel(double& in, double keyLin) noexcept;
        double sidechain(double& in, double sideIn);
        double compression(double& in);

    private:
        void updateStaticCurve() noexcept;
        void updateGainSmoothing() noexcept;
        double coefFromMs(double ms) const noexcept;

        static constexpr double dcOffset = 1.0E-25;
        static constexpr double rmsWindowMs = 8.0;
        static constexpr double minTwoStageRecoveryDb = 0.5;
        static constexpr double releaseHoldMinMs = 1.0;
        static constexpr double releaseHoldMaxMs = 8.0;
        static constexpr double minGainAttackMs = 0.03;
        static constexpr double minGainReleaseMs = 1.0;
        double threshDb { 0.0 };
        double ratio { 1.0 };
        double slope { 0.0 };
        double kneeWidthDb { 6.0 };
        double kneeHalfWidthDb { 3.0 };
        double kneeStartLin { 1.0 };
        double envDb { dcOffset };
        double sampleRateHz { 44100.0 };
        EnvelopeDetector rmsAverage { rmsWindowMs, 44100.0 };
        DetectorEq detectorEq;
        double detectorPeakWeight { 0.2 };
        double detectorRmsWeight { 0.8 };
        double ratioResponse { 1.0 };
        double releaseTargetSustainWeight { 0.58 };
        double releaseTargetCrestWeight { 0.24 };
        double releaseTargetRiseWeight { 0.18 };
        double releaseTrackingRate { 0.065 };
        double releaseFastMinMap { 0.62 };
        double releaseFastMaxMap { 0.98 };
        double releaseSlowMinMap { 0.74 };
        double releaseSlowMaxMap { 1.34 };
        double fastRecoveryBase { 0.72 };
        double fastRecoveryProgramInfluence { 0.24 };
        int characterMode { 0 };
        double rmsState { dcOffset };
        double previousKeyLin { dcOffset };
        double releaseProgramState { 0.5 };
        double gainLin { 1.0 };
        double gainAttackCoef { 0.0 };
        double gainAttackScale { 0.32 };
        double gainAttackMs { minGainAttackMs };
        double gainReleaseFastScale { 0.18 };
        double gainReleaseFastMs { minGainReleaseMs };
        double gainReleaseSlowScale { 1.28 };
        double gainReleaseSlowMs { minGainReleaseMs };
        double holdScale { 0.042 };
        int releaseHoldSamples { 0 };
        int releaseHoldCounter { 0 };
        bool releasePhasePending { false };
        double releaseBoundaryGainLin { 1.0 };
    };

    class TapeSaturator
    {
    public:
        void setSampleRate(double sr) noexcept;
        void reset() noexcept;
        double processSample(double in, int channel, double amount, int modeIndex) noexcept;

    private:
        double processBand(double in, int band, int channel, double amount, int modeIndex) noexcept;

        double oversampledSampleRate { 44100.0 * 16.0 };
        double splitCoef { 1.0 };
        double splitState[2] { 0.0, 0.0 };
        double previousIn[2][2] { { 0.0, 0.0 }, { 0.0, 0.0 } };
        double toneState[2][2] { { 0.0, 0.0 }, { 0.0, 0.0 } };
        double dcState[2][2] { { 0.0, 0.0 }, { 0.0, 0.0 } };
    };

    class SampleOversampler
    {
    public:
        SampleOversampler();
        void prepare(int maxBlockSize);
        void reset();
        int getLatencySamples() const noexcept;
        int getOversamplingFactor() const noexcept;
        void processBlock(double* output, const double* input, const double* amountEnvelope, int numSamples, TapeSaturator& saturator, int channel, int modeIndex);

    private:
        int maxBlockSize { 0 };
        juce::dsp::Oversampling<double> oversampler { 1,
                                                      4,
                                                      juce::dsp::Oversampling<double>::filterHalfBandPolyphaseIIR,
                                                      true,
                                                      true };
    };

    double sampleRate { 44100.0 };
    static constexpr double lookaheadMs = 0.3;
    static constexpr double automationSmoothingSeconds = 0.012;
    static constexpr double stereoLinkPeakBlend = 0.28;
    static constexpr double sidechainListenOutputTrimDb = -6.0;
    static constexpr double sidechainListenHeadroomLin = 0.89;
    static constexpr double sidechainListenAttackMs = 0.12;
    static constexpr double sidechainListenReleaseMs = 45.0;
    static constexpr double autoGainNeutralAttackMs = 24.0;
    static constexpr double autoGainNeutralReleaseMs = 180.0;
    static constexpr double autoGainMinTrackerAttackMs = 8.0;
    static constexpr double autoGainMaxTrackerAttackMs = 180.0;
    static constexpr double autoGainMinTrackerReleaseMs = 55.0;
    static constexpr double autoGainMaxTrackerReleaseMs = 1800.0;
    static constexpr double autoGainMinFollowAttackMs = 12.0;
    static constexpr double autoGainMaxFollowAttackMs = 260.0;
    static constexpr double autoGainMinFollowReleaseMs = 80.0;
    static constexpr double autoGainMaxFollowReleaseMs = 2600.0;
    static constexpr double autoGainMinCompensationDb = -24.0;
    static constexpr double autoGainMaxCompensationDb = 24.0;
    static constexpr double autoGainMinResidualCompensationDb = -6.0;
    static constexpr double autoGainMaxResidualCompensationDb = 6.0;
    static constexpr double autoGainFloorPower = 1.0e-12;
    static constexpr double autoGainSilencePower = 1.0e-10;
    int lookaheadSamples { 0 };
    int lookaheadReadPos { 0 };
    int lookaheadWritePos { 0 };
    std::vector<float> lookaheadL;
    std::vector<float> lookaheadR;
    int parallelDelaySamples { 0 };
    int parallelDelayReadPos { 0 };
    int parallelDelayWritePos { 0 };
    std::vector<double> parallelDelayL;
    std::vector<double> parallelDelayR;
    std::vector<double> delayedBlockL;
    std::vector<double> delayedBlockR;
    std::vector<double> dryBlendBlockL;
    std::vector<double> dryBlendBlockR;
    std::vector<double> alignedDryBlockL;
    std::vector<double> alignedDryBlockR;
    std::vector<double> wetBlockL;
    std::vector<double> wetBlockR;
    std::vector<double> saturatedBlockL;
    std::vector<double> saturatedBlockR;
    std::vector<double> tapeAmountBlock;
    std::vector<double> mixBlock;
    std::vector<double> outputGainBlockL;
    std::vector<double> outputGainBlockR;
    std::vector<double> compGainBlockL;
    std::vector<double> compGainBlockR;

    SimpleComp comp1;
    SimpleComp comp2;
    TapeSaturator tapeSaturator;
    SampleOversampler overSampler1;
    SampleOversampler overSampler2;
    DetectorEq detectorMonitorL;
    DetectorEq detectorMonitorR;

    SmoothedValue inputDbSmoothedL;
    SmoothedValue inputDbSmoothedR;
    SmoothedValue outputDbSmoothedL;
    SmoothedValue outputDbSmoothedR;
    SmoothedValue thresholdDbSmoothedL;
    SmoothedValue thresholdDbSmoothedR;
    SmoothedValue mixSmoothed;
    SmoothedValue autoGainBlendSmoothed;
    SmoothedValue linkAmountSmoothed;
    bool parameterSmoothersReady { false };
    bool modeStateInitialised { false };
    bool wasMsMode { false };
    bool wasPoweredOn { false };
    bool wasSidechainListening { false };
    double sidechainListenTrimLin { 1.0 };
    double sidechainListenGainLin { 1.0 };
    double sidechainListenAttackCoef { 0.0 };
    double sidechainListenReleaseCoef { 0.0 };
    AttRelEnvelope autoGainReferenceAverage { autoGainNeutralAttackMs, autoGainNeutralReleaseMs, 44100.0 };
    AttRelEnvelope autoGainProcessedAverage { autoGainNeutralAttackMs, autoGainNeutralReleaseMs, 44100.0 };
    double autoGainReferencePowerState { 0.0 };
    double autoGainProcessedPowerState { 0.0 };
    double autoGainCompensationLin { 1.0 };
    double autoGainResidualCompensationLin { 1.0 };
    double autoGainAttackCoef { 0.0 };
    double autoGainReleaseCoef { 0.0 };
    double autoGainMinCompensationLin { 1.0 };
    double autoGainMaxCompensationLin { 1.0 };
    double autoGainMinResidualCompensationLin { 1.0 };
    double autoGainMaxResidualCompensationLin { 1.0 };

    double prevMeterL { 0.0 };
    double prevMeterR { 0.0 };
};
} // namespace NComp
