#include "NCompProcessor.h"

#include <algorithm>
#include <cmath>

namespace NComp
{
namespace
{
constexpr double kMidSideMatrixScale = 0.7071067811865475244;

double gainToReductionDb(double gainLin) noexcept
{
    static constexpr double logToDb = 8.6858896380650365530225783783321;
    return juce::jmax(0.0, -std::log(std::max(gainLin, 1.0e-25)) * logToDb);
}

double characterAttackMultiplier(int modeIndex) noexcept
{
    switch (juce::jlimit(0, 5, modeIndex))
    {
        case 1: return 0.72;
        case 2: return 0.2;
        case 3: return 1.5;
        case 4: return 1.12;
        case 5: return 0.82;
        default: return 1.0;
    }
}

double characterReleaseMultiplier(int modeIndex) noexcept
{
    switch (juce::jlimit(0, 5, modeIndex))
    {
        case 1: return 0.72;
        case 2: return 0.42;
        case 3: return 1.3;
        case 4: return 1.78;
        case 5: return 1.26;
        default: return 1.0;
    }
}

double characterDetectorHpfOffset(int modeIndex) noexcept
{
    switch (juce::jlimit(0, 5, modeIndex))
    {
        case 1: return 12.0;
        case 2: return 18.0;
        case 3: return -18.0;
        case 4: return -10.0;
        case 5: return -4.0;
        default: return 0.0;
    }
}

double characterDetectorTiltOffset(int modeIndex) noexcept
{
    switch (juce::jlimit(0, 5, modeIndex))
    {
        case 1: return 0.4;
        case 2: return 1.85;
        case 3: return -0.75;
        case 4: return -1.1;
        case 5: return -0.2;
        default: return 0.0;
    }
}

double tapeAmountFromReductionDb(double reductionDb, int modeIndex) noexcept
{
    static constexpr double baseTapeAmount = 0.16;
    static constexpr double fullDriveReductionDb = 12.0;
    const double normalised = juce::jlimit(0.0, 1.0, reductionDb / fullDriveReductionDb);

    double saturationMultiplier = 1.0;
    double saturationFloor = 0.0;

    switch (juce::jlimit(0, 5, modeIndex))
    {
        case 1:
            saturationMultiplier = 0.7;
            saturationFloor = 0.008;
            break;
        case 2:
            saturationMultiplier = 1.95;
            saturationFloor = 0.06;
            break;
        case 3:
            saturationMultiplier = 0.42;
            saturationFloor = 0.012;
            break;
        case 4:
            saturationMultiplier = 1.28;
            saturationFloor = 0.028;
            break;
        case 5:
            saturationMultiplier = 1.42;
            saturationFloor = 0.035;
            break;
        default:
            break;
    }

    return juce::jlimit(0.0, 1.0, saturationFloor + saturationMultiplier * baseTapeAmount * std::pow(normalised, 0.72));
}

double lerp(double start, double end, double amount) noexcept
{
    return start + (end - start) * amount;
}

double encodeMid(double left, double right) noexcept
{
    return (left + right) * kMidSideMatrixScale;
}

double encodeSide(double left, double right) noexcept
{
    return (left - right) * kMidSideMatrixScale;
}

double decodeLeft(double mid, double side) noexcept
{
    return (mid + side) * kMidSideMatrixScale;
}

double decodeRight(double mid, double side) noexcept
{
    return (mid - side) * kMidSideMatrixScale;
}
}

void NCompProcessor::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;

    overSampler1.prepare(static_cast<int>(spec.maximumBlockSize));
    overSampler2.prepare(static_cast<int>(spec.maximumBlockSize));
    prepareParallelDelay(overSampler1.getLatencySamples());
    prepareLookahead(sampleRate);
    ensureScratchCapacity(static_cast<int>(spec.maximumBlockSize));

    comp1.initRuntime();
    comp2.initRuntime();
    comp1.setSampleRate(sampleRate);
    comp2.setSampleRate(sampleRate);
    comp1.setDetectorHighPass(90.0);
    comp2.setDetectorHighPass(90.0);
    comp1.setDetectorTilt(0.0);
    comp2.setDetectorTilt(0.0);
    detectorMonitorL.setSampleRate(sampleRate);
    detectorMonitorR.setSampleRate(sampleRate);
    detectorMonitorL.setHighPass(90.0);
    detectorMonitorR.setHighPass(90.0);
    detectorMonitorL.setTilt(0.0);
    detectorMonitorR.setTilt(0.0);
    detectorMonitorL.reset();
    detectorMonitorR.reset();
    prepareSidechainListenMonitor(sampleRate);
    prepareAutoGainTracking(sampleRate);
    tapeSaturator.setSampleRate(sampleRate * static_cast<double>(overSampler1.getOversamplingFactor()));
    tapeSaturator.reset();
    overSampler1.reset();
    overSampler2.reset();
    resetParameterSmoothers();
    modeStateInitialised = false;
    wasMsMode = false;
    wasPoweredOn = false;
    wasSidechainListening = false;

    prevMeterL = 0.0;
    prevMeterR = 0.0;
}

void NCompProcessor::reset() noexcept
{
    resetLookahead();
    resetParallelDelay();
    comp1.initRuntime();
    comp2.initRuntime();
    tapeSaturator.reset();
    overSampler1.reset();
    overSampler2.reset();
    detectorMonitorL.reset();
    detectorMonitorR.reset();
    resetSidechainListenMonitor();
    resetAutoGainTracking();
    modeStateInitialised = false;
    wasMsMode = false;
    wasPoweredOn = false;
    wasSidechainListening = false;
    prevMeterL = 0.0;
    prevMeterR = 0.0;
}

double NCompProcessor::dbToLin(double db) noexcept
{
    static constexpr double dbToLog = 0.11512925464970228420089957273422;
    return std::exp(db * dbToLog);
}

double NCompProcessor::linToDb(double lin) noexcept
{
    static constexpr double logToDb = 8.6858896380650365530225783783321;
    return std::log(lin) * logToDb;
}

double NCompProcessor::getParamValue(juce::AudioProcessorValueTreeState& state, const char* id, double fallback) noexcept
{
    if (auto* ptr = state.getRawParameterValue(id))
        return static_cast<double>(ptr->load());
    return fallback;
}

void NCompProcessor::prepareLookahead(double sr)
{
    const double safeSr = std::max(sr, 1.0);
    lookaheadSamples = std::max(1, static_cast<int>(std::ceil(safeSr * lookaheadMs * 0.001)));

    const int size = lookaheadSamples + 1;
    lookaheadL.assign(static_cast<size_t>(size), 0.0f);
    lookaheadR.assign(static_cast<size_t>(size), 0.0f);
    lookaheadReadPos = 0;
    lookaheadWritePos = lookaheadSamples;
}

void NCompProcessor::resetLookahead() noexcept
{
    std::fill(lookaheadL.begin(), lookaheadL.end(), 0.0f);
    std::fill(lookaheadR.begin(), lookaheadR.end(), 0.0f);
    lookaheadReadPos = 0;
    lookaheadWritePos = lookaheadSamples;
}

void NCompProcessor::prepareParallelDelay(int latencySamples)
{
    parallelDelaySamples = juce::jmax(0, latencySamples);

    const int size = parallelDelaySamples + 1;
    parallelDelayL.assign(static_cast<size_t>(size), 0.0);
    parallelDelayR.assign(static_cast<size_t>(size), 0.0);
    parallelDelayReadPos = 0;
    parallelDelayWritePos = parallelDelaySamples;
}

void NCompProcessor::resetParallelDelay() noexcept
{
    std::fill(parallelDelayL.begin(), parallelDelayL.end(), 0.0);
    std::fill(parallelDelayR.begin(), parallelDelayR.end(), 0.0);
    parallelDelayReadPos = 0;
    parallelDelayWritePos = parallelDelaySamples;
}

void NCompProcessor::processParallelDelaySample(double inL, double inR, double& outL, double& outR) noexcept
{
    parallelDelayL[static_cast<size_t>(parallelDelayWritePos)] = inL;
    parallelDelayR[static_cast<size_t>(parallelDelayWritePos)] = inR;

    outL = parallelDelayL[static_cast<size_t>(parallelDelayReadPos)];
    outR = parallelDelayR[static_cast<size_t>(parallelDelayReadPos)];

    ++parallelDelayReadPos;
    if (parallelDelayReadPos > parallelDelaySamples)
        parallelDelayReadPos = 0;

    ++parallelDelayWritePos;
    if (parallelDelayWritePos > parallelDelaySamples)
        parallelDelayWritePos = 0;
}

void NCompProcessor::ensureScratchCapacity(int numSamples)
{
    const auto required = static_cast<size_t>(juce::jmax(0, numSamples));
    if (delayedBlockL.size() < required)
        delayedBlockL.resize(required);
    if (delayedBlockR.size() < required)
        delayedBlockR.resize(required);
    if (dryBlendBlockL.size() < required)
        dryBlendBlockL.resize(required);
    if (dryBlendBlockR.size() < required)
        dryBlendBlockR.resize(required);
    if (alignedDryBlockL.size() < required)
        alignedDryBlockL.resize(required);
    if (alignedDryBlockR.size() < required)
        alignedDryBlockR.resize(required);
    if (wetBlockL.size() < required)
        wetBlockL.resize(required);
    if (wetBlockR.size() < required)
        wetBlockR.resize(required);
    if (saturatedBlockL.size() < required)
        saturatedBlockL.resize(required);
    if (saturatedBlockR.size() < required)
        saturatedBlockR.resize(required);
    if (tapeAmountBlock.size() < required)
        tapeAmountBlock.resize(required);
    if (mixBlock.size() < required)
        mixBlock.resize(required);
    if (outputGainBlockL.size() < required)
        outputGainBlockL.resize(required);
    if (outputGainBlockR.size() < required)
        outputGainBlockR.resize(required);
    if (compGainBlockL.size() < required)
        compGainBlockL.resize(required);
    if (compGainBlockR.size() < required)
        compGainBlockR.resize(required);
}

void NCompProcessor::resetParameterSmoothers() noexcept
{
    auto resetSmoother = [this](SmoothedValue& smoother)
    {
        smoother.reset(sampleRate, automationSmoothingSeconds);
    };

    resetSmoother(inputDbSmoothedL);
    resetSmoother(inputDbSmoothedR);
    resetSmoother(outputDbSmoothedL);
    resetSmoother(outputDbSmoothedR);
    resetSmoother(thresholdDbSmoothedL);
    resetSmoother(thresholdDbSmoothedR);
    resetSmoother(mixSmoothed);
    resetSmoother(autoGainBlendSmoothed);
    resetSmoother(linkAmountSmoothed);
    parameterSmoothersReady = false;
}

void NCompProcessor::prepareSidechainListenMonitor(double sr) noexcept
{
    const double safeSr = std::max(sr, 1.0);
    sidechainListenTrimLin = dbToLin(sidechainListenOutputTrimDb);
    sidechainListenAttackCoef = std::exp(-1000.0 / (sidechainListenAttackMs * safeSr));
    sidechainListenReleaseCoef = std::exp(-1000.0 / (sidechainListenReleaseMs * safeSr));
    resetSidechainListenMonitor();
}

void NCompProcessor::resetSidechainListenMonitor() noexcept
{
    sidechainListenGainLin = 1.0;
}

double NCompProcessor::applySidechainListenSafetyGain(double left, double right) noexcept
{
    const double trimmedPeak = std::max(std::fabs(left), std::fabs(right)) * sidechainListenTrimLin;
    double targetGain = 1.0;

    if (trimmedPeak > sidechainListenHeadroomLin)
        targetGain = sidechainListenHeadroomLin / trimmedPeak;

    const double followCoef = targetGain < sidechainListenGainLin ? sidechainListenAttackCoef
                                                                  : sidechainListenReleaseCoef;
    sidechainListenGainLin = targetGain + followCoef * (sidechainListenGainLin - targetGain);
    return sidechainListenTrimLin * sidechainListenGainLin;
}

void NCompProcessor::prepareAutoGainTracking(double sr) noexcept
{
    const double safeSr = std::max(sr, 1.0);
    autoGainReferenceAverage.setSampleRate(safeSr);
    autoGainProcessedAverage.setSampleRate(safeSr);
    autoGainMinCompensationLin = dbToLin(autoGainMinCompensationDb);
    autoGainMaxCompensationLin = dbToLin(autoGainMaxCompensationDb);
    autoGainMinResidualCompensationLin = dbToLin(autoGainMinResidualCompensationDb);
    autoGainMaxResidualCompensationLin = dbToLin(autoGainMaxResidualCompensationDb);
    updateAutoGainTiming(autoGainNeutralAttackMs,
                         autoGainNeutralAttackMs,
                         autoGainNeutralReleaseMs,
                         autoGainNeutralReleaseMs,
                         1.0);
    resetAutoGainTracking();
}

void NCompProcessor::updateAutoGainTiming(double attackL, double attackR, double releaseL, double releaseR, double mix) noexcept
{
    const double dynamicsWeight = juce::jlimit(0.35, 1.0, 0.35 + 0.65 * mix);
    const double fastestAttackMs = std::max(0.1, juce::jmin(attackL, attackR));
    const double slowestReleaseMs = std::max(1.0, juce::jmax(releaseL, releaseR));
    const double effectiveAttackMs = lerp(autoGainNeutralAttackMs, fastestAttackMs, dynamicsWeight);
    const double effectiveReleaseMs = lerp(autoGainNeutralReleaseMs, slowestReleaseMs, dynamicsWeight);
        const double trackerAttackMs = juce::jlimit(autoGainMinTrackerAttackMs,
                                                                                                autoGainMaxTrackerAttackMs,
                                                                                                effectiveAttackMs * 0.4);
        const double trackerReleaseMs = juce::jlimit(autoGainMinTrackerReleaseMs,
                                                                                                 autoGainMaxTrackerReleaseMs,
                                                                                                 effectiveReleaseMs * 0.32 + effectiveAttackMs * 0.18);
    const double followAttackMs = juce::jlimit(autoGainMinFollowAttackMs,
                                               autoGainMaxFollowAttackMs,
                                                                                             effectiveAttackMs * 0.9);
    const double followReleaseMs = juce::jlimit(autoGainMinFollowReleaseMs,
                                                autoGainMaxFollowReleaseMs,
                                                                                                effectiveReleaseMs * 0.48 + effectiveAttackMs * 0.28);
    const double safeSr = std::max(sampleRate, 1.0);

        autoGainReferenceAverage.setAttack(trackerAttackMs);
        autoGainReferenceAverage.setRelease(trackerReleaseMs);
        autoGainProcessedAverage.setAttack(trackerAttackMs);
        autoGainProcessedAverage.setRelease(trackerReleaseMs);
    autoGainAttackCoef = std::exp(-1000.0 / (followAttackMs * safeSr));
    autoGainReleaseCoef = std::exp(-1000.0 / (followReleaseMs * safeSr));
}

void NCompProcessor::resetAutoGainTracking() noexcept
{
    autoGainReferencePowerState = 0.0;
    autoGainProcessedPowerState = 0.0;
    autoGainCompensationLin = 1.0;
    autoGainResidualCompensationLin = 1.0;
}

double NCompProcessor::updateAutoGainCompensation(double referenceL,
                                                  double referenceR,
                                                  double processedL,
                                                  double processedR,
                                                  double guideCompensationLin) noexcept
{
    const double referencePower = 0.5 * (referenceL * referenceL + referenceR * referenceR);
    const double processedPower = 0.5 * (processedL * processedL + processedR * processedR);
    const double guidedProcessedPower = processedPower * guideCompensationLin * guideCompensationLin;

    autoGainReferenceAverage.run(referencePower, autoGainReferencePowerState);
    autoGainProcessedAverage.run(guidedProcessedPower, autoGainProcessedPowerState);

    double residualTargetLin = 1.0;
    if (autoGainReferencePowerState > autoGainSilencePower || autoGainProcessedPowerState > autoGainSilencePower)
    {
        residualTargetLin = std::sqrt((autoGainReferencePowerState + autoGainFloorPower)
                                    / std::max(autoGainProcessedPowerState, autoGainFloorPower));
    }

    residualTargetLin = juce::jlimit(autoGainMinResidualCompensationLin,
                                     autoGainMaxResidualCompensationLin,
                                     residualTargetLin);

    const double followCoef = residualTargetLin > autoGainResidualCompensationLin ? autoGainAttackCoef
                                                                                   : autoGainReleaseCoef;
    autoGainResidualCompensationLin = residualTargetLin
                                    + followCoef * (autoGainResidualCompensationLin - residualTargetLin);
    autoGainCompensationLin = juce::jlimit(autoGainMinCompensationLin,
                                           autoGainMaxCompensationLin,
                                           guideCompensationLin * autoGainResidualCompensationLin);
    return autoGainCompensationLin;
}

NCompProcessor::EnvelopeDetector::EnvelopeDetector(double msIn, double sr)
    : sampleRate(sr), ms(msIn)
{
    updateCoef();
}

void NCompProcessor::EnvelopeDetector::setTc(double msIn)
{
    const double clamped = std::max(msIn, 0.0001);
    if (std::abs(clamped - ms) < 1.0e-12)
        return;

    ms = clamped;
    updateCoef();
}

void NCompProcessor::EnvelopeDetector::setSampleRate(double sr)
{
    const double clamped = std::max(sr, 1.0);
    if (std::abs(clamped - sampleRate) < 1.0e-12)
        return;

    sampleRate = clamped;
    updateCoef();
}

void NCompProcessor::EnvelopeDetector::run(double in, double& state) const noexcept
{
    state = in + coef * (state - in);
}

void NCompProcessor::EnvelopeDetector::updateCoef() noexcept
{
    coef = std::exp(-1000.0 / (ms * sampleRate));
}

void NCompProcessor::DetectorEq::setSampleRate(double sr) noexcept
{
    const double clamped = std::max(sr, 1.0);
    if (std::abs(clamped - sampleRate) < 1.0e-12)
        return;

    sampleRate = clamped;
    updateCoefficients();
}

void NCompProcessor::DetectorEq::setHighPass(double hz) noexcept
{
    const double clamped = juce::jlimit(20.0, 400.0, hz);
    if (std::abs(clamped - highPassHz) < 1.0e-12)
        return;

    highPassHz = clamped;
    updateCoefficients();
}

void NCompProcessor::DetectorEq::setTilt(double db) noexcept
{
    const double clamped = juce::jlimit(-6.0, 6.0, db);
    if (std::abs(clamped - tiltDb) < 1.0e-12)
        return;

    tiltDb = clamped;
    updateCoefficients();
}

void NCompProcessor::DetectorEq::reset() noexcept
{
    lowState = 0.0;
    tiltState = 0.0;
    updateCoefficients();
}

double NCompProcessor::DetectorEq::processSample(double in) noexcept
{
    lowState += highPassCoef * (in - lowState);
    const double highPassed = in - lowState;

    tiltState += tiltCoef * (highPassed - tiltState);
    const double lowBand = tiltState;
    const double highBand = highPassed - lowBand;

    return lowBand * lowBandGain + highBand * highBandGain;
}

void NCompProcessor::DetectorEq::updateCoefficients() noexcept
{
    const double safeSampleRate = std::max(sampleRate, 1.0);
    const double highPass = juce::jlimit(20.0, safeSampleRate * 0.45, highPassHz);
    const double tiltPivot = juce::jlimit(200.0, safeSampleRate * 0.45, tiltPivotHz);

    highPassCoef = 1.0 - std::exp((-2.0 * juce::MathConstants<double>::pi * highPass) / safeSampleRate);
    tiltCoef = 1.0 - std::exp((-2.0 * juce::MathConstants<double>::pi * tiltPivot) / safeSampleRate);

    const double halfTiltDb = tiltDb * 0.5;
    lowBandGain = NCompProcessor::dbToLin(-halfTiltDb);
    highBandGain = NCompProcessor::dbToLin(halfTiltDb);
}

NCompProcessor::AttRelEnvelope::AttRelEnvelope(double attackMs, double releaseMs, double sr)
    : attack(attackMs, sr), release(releaseMs, sr)
{
}

void NCompProcessor::AttRelEnvelope::setAttack(double ms)
{
    attack.setTc(ms);
}

void NCompProcessor::AttRelEnvelope::setRelease(double ms)
{
    release.setTc(ms);
}

void NCompProcessor::AttRelEnvelope::setSampleRate(double sr)
{
    attack.setSampleRate(sr);
    release.setSampleRate(sr);
}

void NCompProcessor::AttRelEnvelope::run(double in, double& state) const noexcept
{
    if (in > state)
        attack.run(in, state);
    else
        release.run(in, state);
}

NCompProcessor::SimpleComp::SimpleComp()
    : AttRelEnvelope(10.0, 100.0)
{
    setSampleRate(44100.0);
    setAttack(10.0);
    setRelease(100.0);
    setThresh(0.0);
    setRatio(1.0);
}

void NCompProcessor::SimpleComp::setCharacterMode(int modeIndex)
{
    characterMode = juce::jlimit(0, 5, modeIndex);

    switch (characterMode)
    {
        case 1:
            detectorPeakWeight = 0.34;
            ratioResponse = 1.02;
            kneeWidthDb = 4.0;
            gainAttackScale = 0.22;
            gainReleaseFastScale = 0.12;
            gainReleaseSlowScale = 0.88;
            holdScale = 0.026;
            releaseTargetSustainWeight = 0.5;
            releaseTargetCrestWeight = 0.32;
            releaseTargetRiseWeight = 0.18;
            releaseTrackingRate = 0.08;
            releaseFastMinMap = 0.55;
            releaseFastMaxMap = 0.9;
            releaseSlowMinMap = 0.7;
            releaseSlowMaxMap = 1.14;
            fastRecoveryBase = 0.68;
            fastRecoveryProgramInfluence = 0.2;
            break;

        case 2:
            detectorPeakWeight = 0.82;
            ratioResponse = 1.14;
            kneeWidthDb = 2.2;
            gainAttackScale = 0.11;
            gainReleaseFastScale = 0.07;
            gainReleaseSlowScale = 0.54;
            holdScale = 0.014;
            releaseTargetSustainWeight = 0.36;
            releaseTargetCrestWeight = 0.23;
            releaseTargetRiseWeight = 0.41;
            releaseTrackingRate = 0.11;
            releaseFastMinMap = 0.44;
            releaseFastMaxMap = 0.74;
            releaseSlowMinMap = 0.6;
            releaseSlowMaxMap = 0.96;
            fastRecoveryBase = 0.8;
            fastRecoveryProgramInfluence = 0.12;
            break;

        case 3:
            detectorPeakWeight = 0.09;
            ratioResponse = 0.86;
            kneeWidthDb = 10.0;
            gainAttackScale = 0.42;
            gainReleaseFastScale = 0.2;
            gainReleaseSlowScale = 1.45;
            holdScale = 0.05;
            releaseTargetSustainWeight = 0.68;
            releaseTargetCrestWeight = 0.2;
            releaseTargetRiseWeight = 0.12;
            releaseTrackingRate = 0.062;
            releaseFastMinMap = 0.68;
            releaseFastMaxMap = 0.98;
            releaseSlowMinMap = 0.92;
            releaseSlowMaxMap = 1.55;
            fastRecoveryBase = 0.64;
            fastRecoveryProgramInfluence = 0.22;
            break;

        case 4:
            detectorPeakWeight = 0.18;
            ratioResponse = 0.76;
            kneeWidthDb = 12.5;
            gainAttackScale = 0.34;
            gainReleaseFastScale = 0.24;
            gainReleaseSlowScale = 1.78;
            holdScale = 0.065;
            releaseTargetSustainWeight = 0.76;
            releaseTargetCrestWeight = 0.15;
            releaseTargetRiseWeight = 0.09;
            releaseTrackingRate = 0.05;
            releaseFastMinMap = 0.78;
            releaseFastMaxMap = 1.08;
            releaseSlowMinMap = 1.0;
            releaseSlowMaxMap = 1.8;
            fastRecoveryBase = 0.6;
            fastRecoveryProgramInfluence = 0.18;
            break;

        case 5:
            detectorPeakWeight = 0.24;
            ratioResponse = 0.84;
            kneeWidthDb = 9.5;
            gainAttackScale = 0.26;
            gainReleaseFastScale = 0.18;
            gainReleaseSlowScale = 1.36;
            holdScale = 0.05;
            releaseTargetSustainWeight = 0.64;
            releaseTargetCrestWeight = 0.22;
            releaseTargetRiseWeight = 0.14;
            releaseTrackingRate = 0.07;
            releaseFastMinMap = 0.66;
            releaseFastMaxMap = 0.96;
            releaseSlowMinMap = 0.86;
            releaseSlowMaxMap = 1.46;
            fastRecoveryBase = 0.66;
            fastRecoveryProgramInfluence = 0.18;
            break;

        default:
            detectorPeakWeight = 0.2;
            ratioResponse = 1.0;
            kneeWidthDb = 6.0;
            gainAttackScale = 0.32;
            gainReleaseFastScale = 0.18;
            gainReleaseSlowScale = 1.28;
            holdScale = 0.042;
            releaseTargetSustainWeight = 0.58;
            releaseTargetCrestWeight = 0.24;
            releaseTargetRiseWeight = 0.18;
            releaseTrackingRate = 0.065;
            releaseFastMinMap = 0.62;
            releaseFastMaxMap = 0.98;
            releaseSlowMinMap = 0.74;
            releaseSlowMaxMap = 1.34;
            fastRecoveryBase = 0.72;
            fastRecoveryProgramInfluence = 0.24;
            break;
    }

    detectorRmsWeight = 1.0 - detectorPeakWeight;
    updateStaticCurve();
    updateGainSmoothing();
}

void NCompProcessor::SimpleComp::setSampleRate(double sr)
{
    sampleRateHz = std::max(sr, 1.0);
    AttRelEnvelope::setSampleRate(sampleRateHz);
    rmsAverage.setSampleRate(sampleRateHz);
    detectorEq.setSampleRate(sampleRateHz);
    updateGainSmoothing();
}

void NCompProcessor::SimpleComp::setAttack(double ms)
{
    if (std::abs(ms - getAttack()) < 1.0e-12)
        return;

    AttRelEnvelope::setAttack(ms);
    updateGainSmoothing();
}

void NCompProcessor::SimpleComp::setRelease(double ms)
{
    if (std::abs(ms - getRelease()) < 1.0e-12)
        return;

    AttRelEnvelope::setRelease(ms);
    updateGainSmoothing();
}

void NCompProcessor::SimpleComp::updateGainSmoothing() noexcept
{
    gainAttackMs = std::max(getAttack() * gainAttackScale, minGainAttackMs);
    gainReleaseFastMs = std::max(getRelease() * gainReleaseFastScale, minGainReleaseMs);
    gainReleaseSlowMs = std::max(getRelease() * gainReleaseSlowScale, minGainReleaseMs);
    const double holdMs = juce::jlimit(releaseHoldMinMs, releaseHoldMaxMs, getRelease() * holdScale);

    gainAttackCoef = coefFromMs(gainAttackMs);
    releaseHoldSamples = std::max(0, static_cast<int>(std::lround(sampleRateHz * holdMs * 0.001)));
}

void NCompProcessor::SimpleComp::updateStaticCurve() noexcept
{
    kneeWidthDb = juce::jlimit(1.0, 18.0, kneeWidthDb);
    kneeHalfWidthDb = kneeWidthDb * 0.5;
    kneeStartLin = NCompProcessor::dbToLin(threshDb - kneeHalfWidthDb);
    slope = (1.0 - (1.0 / std::max(ratio, 1.0))) * ratioResponse;
}

void NCompProcessor::SimpleComp::setThresh(double db)
{
    if (std::abs(db - threshDb) < 1.0e-12)
        return;

    threshDb = db;
    updateStaticCurve();
}

void NCompProcessor::SimpleComp::setRatio(double ratioIn)
{
    const double clamped = std::max(ratioIn, 1.0);
    if (std::abs(clamped - ratio) < 1.0e-12)
        return;

    ratio = clamped;
    updateStaticCurve();
}

void NCompProcessor::SimpleComp::setDetectorHighPass(double hz)
{
    detectorEq.setHighPass(hz);
}

void NCompProcessor::SimpleComp::setDetectorTilt(double db)
{
    detectorEq.setTilt(db);
}

void NCompProcessor::SimpleComp::initRuntime() noexcept
{
    envDb = dcOffset;
    rmsState = dcOffset;
    previousKeyLin = dcOffset;
    releaseProgramState = 0.5;
    gainLin = 1.0;
    releaseHoldCounter = 0;
    releasePhasePending = false;
    releaseBoundaryGainLin = 1.0;
    detectorEq.reset();
}

double NCompProcessor::SimpleComp::detectKeyLevel(double sideIn) noexcept
{
    const double filtered = detectorEq.processSample(sideIn);
    const double rect = std::fabs(filtered) + dcOffset;

    const double sq = rect * rect + dcOffset;
    rmsAverage.run(sq, rmsState);
    const double rms = std::sqrt(rmsState);
    const double keyLin = detectorPeakWeight * rect + detectorRmsWeight * rms;

    const double sustainRatio = juce::jlimit(0.0, 1.0, rms / std::max(rect, dcOffset));
    const double crestDb = NCompProcessor::linToDb(std::max(rect, dcOffset))
                         - NCompProcessor::linToDb(std::max(rms, dcOffset));
    const double crestNorm = juce::jlimit(0.0, 1.0, crestDb / 18.0);
    const double riseRatio = juce::jmax(0.0, (keyLin - previousKeyLin) / std::max(previousKeyLin, 1.0e-4));
    const double riseNorm = juce::jlimit(0.0, 1.0, riseRatio * 0.7);

        const double releaseTarget = juce::jlimit(0.0,
                                                                                            1.0,
                                                                                            releaseTargetSustainWeight * sustainRatio
                                                                                                + releaseTargetCrestWeight * (1.0 - crestNorm)
                                                                                                + releaseTargetRiseWeight * (1.0 - riseNorm));
        releaseProgramState += releaseTrackingRate * (releaseTarget - releaseProgramState);
    previousKeyLin = keyLin;

    return keyLin;
}

double NCompProcessor::SimpleComp::applyGainFromKeyLevel(double& in, double keyLin) noexcept
{
    keyLin = std::max(keyLin, dcOffset);

    double targetReductionDb = 0.0;

    if (keyLin > kneeStartLin)
    {
        const double keyDb = NCompProcessor::linToDb(keyLin);
        const double overDb = keyDb - threshDb;

        if (overDb >= kneeHalfWidthDb)
        {
            targetReductionDb = slope * overDb;
        }
        else if (overDb > -kneeHalfWidthDb)
        {
            const double x = overDb + kneeHalfWidthDb;
            targetReductionDb = slope * (x * x) / (2.0 * kneeWidthDb);
        }
    }

    run(targetReductionDb + dcOffset, envDb);
    const double appliedReductionDb = envDb - dcOffset;
    const double targetGainLin = NCompProcessor::dbToLin(-appliedReductionDb);
    const double releaseFastCoef = coefFromMs(gainReleaseFastMs * juce::jmap(releaseProgramState, releaseFastMinMap, releaseFastMaxMap));
    const double releaseSlowCoef = coefFromMs(gainReleaseSlowMs * juce::jmap(releaseProgramState, releaseSlowMinMap, releaseSlowMaxMap));
    const double fastRecoveryPortion = juce::jlimit(0.38, 0.84, fastRecoveryBase - fastRecoveryProgramInfluence * releaseProgramState);

    if (targetGainLin < gainLin - 1.0e-12)
    {
        gainLin = targetGainLin + gainAttackCoef * (gainLin - targetGainLin);
        releaseHoldCounter = releaseHoldSamples;
        releasePhasePending = true;
    }
    else if (targetGainLin > gainLin + 1.0e-12)
    {
        if (releaseHoldCounter > 0)
        {
            --releaseHoldCounter;
        }
        else
        {
            if (releasePhasePending)
            {
                const double currentReductionDb = gainToReductionDb(gainLin);
                const double targetReductionForReleaseDb = gainToReductionDb(targetGainLin);
                const double recoverySpanDb = juce::jmax(0.0, currentReductionDb - targetReductionForReleaseDb);

                if (recoverySpanDb > minTwoStageRecoveryDb)
                {
                    const double boundaryReductionDb = targetReductionForReleaseDb + recoverySpanDb * (1.0 - fastRecoveryPortion);
                    releaseBoundaryGainLin = NCompProcessor::dbToLin(-boundaryReductionDb);
                }
                else
                {
                    releaseBoundaryGainLin = targetGainLin;
                }

                releasePhasePending = false;
            }

            const double releaseCoef = (gainLin < releaseBoundaryGainLin) ? releaseFastCoef : releaseSlowCoef;
            gainLin = targetGainLin + releaseCoef * (gainLin - targetGainLin);
        }
    }
    else if (gainLin > 0.999999)
    {
        releaseHoldCounter = 0;
        releasePhasePending = false;
        releaseBoundaryGainLin = 1.0;
    }

    const double gr = NCompProcessor::linToDb(std::max(gainLin, dcOffset));

    in *= gainLin;
    return gr;
}

double NCompProcessor::SimpleComp::sidechain(double& in, double sideIn)
{
    return applyGainFromKeyLevel(in, detectKeyLevel(sideIn));
}

double NCompProcessor::SimpleComp::compression(double& in)
{
    return sidechain(in, in);
}

double NCompProcessor::SimpleComp::coefFromMs(double ms) const noexcept
{
    const double safeMs = std::max(ms, 0.01);
    return std::exp(-1000.0 / (safeMs * sampleRateHz));
}

void NCompProcessor::TapeSaturator::setSampleRate(double sr) noexcept
{
    static constexpr double splitHz = 200.0;
    oversampledSampleRate = std::max(sr, 1.0);
    splitCoef = 1.0 - std::exp((-2.0 * juce::MathConstants<double>::pi * splitHz) / oversampledSampleRate);
}

void NCompProcessor::TapeSaturator::reset() noexcept
{
    std::fill(std::begin(splitState), std::end(splitState), 0.0);
    std::fill(&previousIn[0][0], &previousIn[0][0] + 4, 0.0);
    std::fill(&toneState[0][0], &toneState[0][0] + 4, 0.0);
    std::fill(&dcState[0][0], &dcState[0][0] + 4, 0.0);
}

double NCompProcessor::TapeSaturator::processBand(double in, int band, int channel, double amount, int modeIndex) noexcept
{
    static constexpr double dcCoef = 0.005;

    const double amountClamped = juce::jlimit(0.0, 1.0, amount);
    const bool lowBand = (band == 0);
    const int clampedMode = juce::jlimit(0, 5, modeIndex);

    double slewSoftening = lowBand ? 0.06 : 0.045;
    double bandDriveScale = lowBand ? 1.0 : 0.72;
    double bandBiasScale = lowBand ? 1.0 : 0.55;
    double toneCoef = lowBand ? 0.1 : 0.18;
    double symmetricBlend = lowBand ? 0.8 : 0.88;
    double shapedBlend = lowBand ? 0.14 : 0.1;

    switch (clampedMode)
    {
        case 1:
            bandDriveScale *= lowBand ? 0.92 : 0.86;
            bandBiasScale *= 0.72;
            toneCoef = lowBand ? 0.085 : 0.14;
            symmetricBlend = lowBand ? 0.9 : 0.94;
            shapedBlend = lowBand ? 0.11 : 0.08;
            break;

        case 2:
            slewSoftening = lowBand ? 0.035 : 0.02;
            bandDriveScale *= lowBand ? 1.18 : 1.42;
            bandBiasScale *= lowBand ? 1.25 : 1.5;
            toneCoef = lowBand ? 0.13 : 0.24;
            symmetricBlend = lowBand ? 0.64 : 0.7;
            shapedBlend = lowBand ? 0.19 : 0.16;
            break;

        case 3:
            slewSoftening = lowBand ? 0.08 : 0.06;
            bandDriveScale *= lowBand ? 0.78 : 0.72;
            bandBiasScale *= 0.62;
            toneCoef = lowBand ? 0.065 : 0.11;
            symmetricBlend = lowBand ? 0.95 : 0.97;
            shapedBlend = lowBand ? 0.1 : 0.07;
            break;

        case 4:
            slewSoftening = lowBand ? 0.07 : 0.05;
            bandDriveScale *= lowBand ? 1.08 : 0.88;
            bandBiasScale *= lowBand ? 1.18 : 0.82;
            toneCoef = lowBand ? 0.11 : 0.13;
            symmetricBlend = lowBand ? 0.72 : 0.9;
            shapedBlend = lowBand ? 0.22 : 0.09;
            break;

        case 5:
            slewSoftening = lowBand ? 0.075 : 0.055;
            bandDriveScale *= lowBand ? 1.28 : 0.96;
            bandBiasScale *= lowBand ? 1.42 : 0.95;
            toneCoef = lowBand ? 0.12 : 0.145;
            symmetricBlend = lowBand ? 0.66 : 0.84;
            shapedBlend = lowBand ? 0.26 : 0.12;
            break;

        default:
            break;
    }

    const double drive = 1.0 + (1.08 * amountClamped * bandDriveScale);
    const double bias = 0.022 * amountClamped * bandBiasScale;

    const double slew = in - previousIn[band][channel];
    previousIn[band][channel] = in;

    const double softenedInput = in - slewSoftening * slew;
    const double driven = softenedInput * drive;
    const double symmetric = std::tanh(driven) / drive;
    const double asymmetric = (std::tanh(driven + bias) - std::tanh(bias)) / drive;

    double shaped = symmetricBlend * symmetric + (1.0 - symmetricBlend) * asymmetric;
    toneState[band][channel] += toneCoef * (shaped - toneState[band][channel]);
    shaped = (1.0 - shapedBlend) * shaped + shapedBlend * toneState[band][channel];

    dcState[band][channel] += dcCoef * (shaped - dcState[band][channel]);
    shaped -= dcState[band][channel];

    return in + amountClamped * (shaped - in);
}

double NCompProcessor::TapeSaturator::processSample(double in, int channel, double amount, int modeIndex) noexcept
{
    const double amountClamped = juce::jlimit(0.0, 1.0, amount);
    double highBandMultiplier = 1.35;

    switch (juce::jlimit(0, 5, modeIndex))
    {
        case 1: highBandMultiplier = 1.18; break;
        case 2: highBandMultiplier = 1.7; break;
        case 3: highBandMultiplier = 1.02; break;
        case 4: highBandMultiplier = 1.16; break;
        case 5: highBandMultiplier = 1.06; break;
        default: break;
    }

    splitState[channel] += splitCoef * (in - splitState[channel]);

    const double lowBand = splitState[channel];
    const double highBand = in - lowBand;
    const double lowProcessed = processBand(lowBand, 0, channel, amountClamped, modeIndex);
    const double highProcessed = processBand(highBand, 1, channel, amountClamped * highBandMultiplier, modeIndex);

    return lowProcessed + highProcessed;
}

NCompProcessor::SampleOversampler::SampleOversampler()
{
    reset();
}

void NCompProcessor::SampleOversampler::prepare(int newMaxBlockSize)
{
    maxBlockSize = juce::jmax(1, newMaxBlockSize);
    oversampler.initProcessing(static_cast<size_t>(maxBlockSize));
}

void NCompProcessor::SampleOversampler::reset()
{
    oversampler.reset();
}

int NCompProcessor::SampleOversampler::getLatencySamples() const noexcept
{
    return static_cast<int>(std::lround(oversampler.getLatencyInSamples()));
}

int NCompProcessor::SampleOversampler::getOversamplingFactor() const noexcept
{
    return static_cast<int>(oversampler.getOversamplingFactor());
}

void NCompProcessor::SampleOversampler::processBlock(double* output,
                                                    const double* input,
                                                    const double* amountEnvelope,
                                                    int numSamples,
                                                    TapeSaturator& saturator,
                                                    int channel,
                                                    int modeIndex)
{
    if (numSamples <= 0)
        return;

    if (numSamples > maxBlockSize)
        prepare(numSamples);

    const double* inputChannels[] { input };
    double* outputChannels[] { output };
    juce::dsp::AudioBlock<const double> inputBlock(inputChannels, 1u, static_cast<size_t>(numSamples));
    juce::dsp::AudioBlock<double> outputBlock(outputChannels, 1u, static_cast<size_t>(numSamples));

    auto oversampledBlock = oversampler.processSamplesUp(inputBlock);
    auto* oversampledData = oversampledBlock.getChannelPointer(0);
    const int oversampledCount = static_cast<int>(oversampledBlock.getNumSamples());
    const double oversamplingFactor = static_cast<double>(oversampler.getOversamplingFactor());

    for (int i = 0; i < oversampledCount; ++i)
    {
        double amount = 0.0;

        if (amountEnvelope != nullptr)
        {
            const double sourcePosition = static_cast<double>(i) / oversamplingFactor;
            const int sourceIndex = juce::jlimit(0, numSamples - 1, static_cast<int>(sourcePosition));
            const int nextIndex = juce::jmin(numSamples - 1, sourceIndex + 1);
            const double fraction = juce::jlimit(0.0, 1.0, sourcePosition - static_cast<double>(sourceIndex));
            amount = lerp(amountEnvelope[sourceIndex], amountEnvelope[nextIndex], fraction);
        }

        oversampledData[i] = saturator.processSample(oversampledData[i], channel, amount, modeIndex);
    }

    oversampler.processSamplesDown(outputBlock);
}

void NCompProcessor::process(juce::AudioBuffer<float>& mainBuffer,
                            const juce::AudioBuffer<float>* sideChainBuffer,
                            juce::AudioProcessorValueTreeState& state,
                            std::atomic<float>& meterL,
                            std::atomic<float>& meterR,
                            std::atomic<float>& transferInputDb,
                            std::atomic<float>& transferOutputDb,
                            std::atomic<float>& transferReductionDb)
{
    const int numSamples = mainBuffer.getNumSamples();
    if (numSamples == 0)
    {
        meterL.store(0.0f);
        meterR.store(0.0f);
        transferInputDb.store(-60.0f);
        transferOutputDb.store(-60.0f);
        transferReductionDb.store(0.0f);
        return;
    }

    ensureScratchCapacity(numSamples);

    const bool linkOn = static_cast<int>(getParamValue(state, params::link, 0.0)) == 0;

    const double inputDbL = getParamValue(state, params::input1, 0.0);
    const double inputDbR = linkOn ? inputDbL : getParamValue(state, params::input2, 0.0);
    const double outputDbL = getParamValue(state, params::output1, 0.0);
    const double outputDbR = linkOn ? outputDbL : getParamValue(state, params::output2, 0.0);

    const double threshL = getParamValue(state, params::threshold1, 0.0);
    const double threshR = linkOn ? threshL : getParamValue(state, params::threshold2, 0.0);
    const double ratioL = getParamValue(state, params::ratio1, 1.0);
    const double ratioR = linkOn ? ratioL : getParamValue(state, params::ratio2, 1.0);
    const double attackL = getParamValue(state, params::attack1, 80.0);
    const double attackR = linkOn ? attackL : getParamValue(state, params::attack2, 80.0);
    const double releaseL = getParamValue(state, params::release1, 200.0);
    const double releaseR = linkOn ? releaseL : getParamValue(state, params::release2, 200.0);

    const double mix = juce::jlimit(0.0, 1.0, getParamValue(state, params::mix, 100.0) / 100.0);
    const int characterMode = juce::jlimit(0, 5, juce::roundToInt(static_cast<float>(getParamValue(state, params::character, 0.0))));
    const double linkAmount = juce::jlimit(0.0, 1.0, getParamValue(state, params::linkAmount, 100.0) / 100.0);
    const double detectorHpf = getParamValue(state, params::sidechainHpf, 90.0) + characterDetectorHpfOffset(characterMode);
    const double detectorTilt = getParamValue(state, params::sidechainTilt, 0.0) + characterDetectorTiltOffset(characterMode);
    const int mslrMode = static_cast<int>(getParamValue(state, params::mslr, 0.0));
    const bool msMode = (mslrMode == 1);
    const bool sideChainOn = static_cast<int>(getParamValue(state, params::sidechain, 0.0)) == 1;
    const bool sidechainListen = static_cast<int>(getParamValue(state, params::sidechainListen, 0.0)) == 1;
    const bool powerOn = static_cast<int>(getParamValue(state, params::power, 0.0)) == 0;
    const bool autoGainOn = static_cast<int>(getParamValue(state, params::autoGain, 0.0)) == 1;

    if (!modeStateInitialised)
    {
        wasMsMode = msMode;
        modeStateInitialised = true;
    }
    else if (wasMsMode != msMode)
    {
        reset();
        resetParameterSmoothers();
        wasMsMode = msMode;
    }

    if (!parameterSmoothersReady)
    {
        inputDbSmoothedL.setCurrentAndTargetValue(inputDbL);
        inputDbSmoothedR.setCurrentAndTargetValue(inputDbR);
        outputDbSmoothedL.setCurrentAndTargetValue(outputDbL);
        outputDbSmoothedR.setCurrentAndTargetValue(outputDbR);
        thresholdDbSmoothedL.setCurrentAndTargetValue(threshL);
        thresholdDbSmoothedR.setCurrentAndTargetValue(threshR);
        mixSmoothed.setCurrentAndTargetValue(mix);
        autoGainBlendSmoothed.setCurrentAndTargetValue(autoGainOn ? 1.0 : 0.0);
        linkAmountSmoothed.setCurrentAndTargetValue(linkAmount);
        parameterSmoothersReady = true;
    }

    inputDbSmoothedL.setTargetValue(inputDbL);
    inputDbSmoothedR.setTargetValue(inputDbR);
    outputDbSmoothedL.setTargetValue(outputDbL);
    outputDbSmoothedR.setTargetValue(outputDbR);
    thresholdDbSmoothedL.setTargetValue(threshL);
    thresholdDbSmoothedR.setTargetValue(threshR);
    mixSmoothed.setTargetValue(mix);
    autoGainBlendSmoothed.setTargetValue(autoGainOn ? 1.0 : 0.0);
    linkAmountSmoothed.setTargetValue(linkAmount);

    const double effectiveAttackL = attackL * characterAttackMultiplier(characterMode);
    const double effectiveAttackR = attackR * characterAttackMultiplier(characterMode);
    const double effectiveReleaseL = releaseL * characterReleaseMultiplier(characterMode);
    const double effectiveReleaseR = releaseR * characterReleaseMultiplier(characterMode);

    comp1.setCharacterMode(characterMode);
    comp2.setCharacterMode(characterMode);
    comp1.setAttack(effectiveAttackL);
    comp1.setRelease(effectiveReleaseL);
    comp1.setThresh(threshL);
    comp1.setRatio(ratioL);
    comp1.setDetectorHighPass(detectorHpf);
    comp1.setDetectorTilt(detectorTilt);

    comp2.setAttack(effectiveAttackR);
    comp2.setRelease(effectiveReleaseR);
    comp2.setThresh(threshR);
    comp2.setRatio(ratioR);
    comp2.setDetectorHighPass(detectorHpf);
    comp2.setDetectorTilt(detectorTilt);
    updateAutoGainTiming(effectiveAttackL, effectiveAttackR, effectiveReleaseL, effectiveReleaseR, mix);
    detectorMonitorL.setHighPass(detectorHpf);
    detectorMonitorR.setHighPass(detectorHpf);
    detectorMonitorL.setTilt(detectorTilt);
    detectorMonitorR.setTilt(detectorTilt);
    const bool compActiveL = ratioL > 1.0001;
    const bool compActiveR = ratioR > 1.0001;

   #if JUCE_MAC
    constexpr double meterAttack = 0.05;
    constexpr double meterDecay = 0.05;
   #else
    constexpr double meterAttack = 0.009;
    constexpr double meterDecay = 0.009;
   #endif

    auto* outL = mainBuffer.getWritePointer(0);
    auto* outR = mainBuffer.getNumChannels() > 1 ? mainBuffer.getWritePointer(1) : nullptr;

    const auto* inLRead = mainBuffer.getReadPointer(0);
    const auto* inRRead = mainBuffer.getNumChannels() > 1 ? mainBuffer.getReadPointer(1) : inLRead;

    const int lookaheadSize = lookaheadSamples + 1;

    const auto* scL = (sideChainBuffer != nullptr && sideChainBuffer->getNumChannels() > 0) ? sideChainBuffer->getReadPointer(0) : nullptr;
    const auto* scR = (sideChainBuffer != nullptr && sideChainBuffer->getNumChannels() > 1) ? sideChainBuffer->getReadPointer(1) : scL;

    if (!powerOn)
    {
        if (wasPoweredOn)
        {
            comp1.initRuntime();
            comp2.initRuntime();
            tapeSaturator.reset();
            overSampler1.reset();
            overSampler2.reset();
            detectorMonitorL.reset();
            detectorMonitorR.reset();
            resetSidechainListenMonitor();
            resetAutoGainTracking();
            prevMeterL = 0.0;
            prevMeterR = 0.0;
            wasPoweredOn = false;
            wasSidechainListening = false;
        }

        for (int i = 0; i < numSamples; ++i)
        {
            const float curInL = inLRead[i];
            const float curInR = inRRead[i];

            lookaheadL[static_cast<size_t>(lookaheadWritePos)] = curInL;
            lookaheadR[static_cast<size_t>(lookaheadWritePos)] = curInR;

            const double delayedL = static_cast<double>(lookaheadL[static_cast<size_t>(lookaheadReadPos)]);
            const double delayedR = static_cast<double>(lookaheadR[static_cast<size_t>(lookaheadReadPos)]);

            double alignedL = 0.0;
            double alignedR = 0.0;
            processParallelDelaySample(delayedL, delayedR, alignedL, alignedR);

            outL[i] = static_cast<float>(alignedL);
            if (outR != nullptr)
                outR[i] = static_cast<float>(alignedR);

            ++lookaheadReadPos;
            if (lookaheadReadPos >= lookaheadSize)
                lookaheadReadPos = 0;

            ++lookaheadWritePos;
            if (lookaheadWritePos >= lookaheadSize)
                lookaheadWritePos = 0;
        }

        meterL.store(0.0f);
        meterR.store(0.0f);
        transferInputDb.store(-60.0f);
        transferOutputDb.store(-60.0f);
        transferReductionDb.store(0.0f);
        prevMeterL = 0.0;
        prevMeterR = 0.0;
        return;
    }

    if (sidechainListen)
    {
        if (!wasSidechainListening)
        {
            comp1.initRuntime();
            comp2.initRuntime();
            tapeSaturator.reset();
            overSampler1.reset();
            overSampler2.reset();
            detectorMonitorL.reset();
            detectorMonitorR.reset();
            resetSidechainListenMonitor();
            resetAutoGainTracking();
            prevMeterL = 0.0;
            prevMeterR = 0.0;
        }

        double monitorPeakIn = 0.0;
        double monitorPeakOut = 0.0;

        for (int i = 0; i < numSamples; ++i)
        {
            const double inputGainL = dbToLin(inputDbSmoothedL.getNextValue());
            const double inputGainR = dbToLin(inputDbSmoothedR.getNextValue());

            const double monitorInL = (sideChainOn && scL != nullptr) ? static_cast<double>(scL[i]) * inputGainL
                                                                       : static_cast<double>(inLRead[i]) * inputGainL;
            const double monitorInR = (sideChainOn && scR != nullptr) ? static_cast<double>(scR[i]) * inputGainR
                                                                       : static_cast<double>(inRRead[i]) * inputGainR;

            if (msMode)
            {
                const double monitorMid = detectorMonitorL.processSample(encodeMid(monitorInL, monitorInR));
                const double monitorSide = detectorMonitorR.processSample(encodeSide(monitorInL, monitorInR));

                const double rawLeft = decodeLeft(monitorMid, monitorSide);
                const double rawRight = decodeRight(monitorMid, monitorSide);
                const double listenGain = applySidechainListenSafetyGain(rawLeft, rawRight);
                const double outLeft = rawLeft * listenGain;
                const double outRight = rawRight * listenGain;

                outL[i] = static_cast<float>(outLeft);
                if (outR != nullptr)
                    outR[i] = static_cast<float>(outRight);

                monitorPeakOut = std::max(monitorPeakOut, std::max(std::fabs(outLeft), std::fabs(outRight)));
            }
            else
            {
                const double rawLeft = detectorMonitorL.processSample(monitorInL);
                const double rawRight = detectorMonitorR.processSample(monitorInR);
                const double listenGain = applySidechainListenSafetyGain(rawLeft, rawRight);
                const double listenL = rawLeft * listenGain;
                const double listenR = rawRight * listenGain;

                outL[i] = static_cast<float>(listenL);
                if (outR != nullptr)
                    outR[i] = static_cast<float>(listenR);

                monitorPeakOut = std::max(monitorPeakOut, std::max(std::fabs(listenL), std::fabs(listenR)));
            }

            monitorPeakIn = std::max(monitorPeakIn, std::max(std::fabs(monitorInL), std::fabs(monitorInR)));
        }

        wasPoweredOn = true;
        wasSidechainListening = true;
        meterL.store(0.0f);
        meterR.store(0.0f);
        transferInputDb.store(static_cast<float>(linToDb(std::max(monitorPeakIn, 1.0e-6))));
        transferOutputDb.store(static_cast<float>(linToDb(std::max(monitorPeakOut, 1.0e-6))));
        transferReductionDb.store(0.0f);
        prevMeterL = 0.0;
        prevMeterR = 0.0;
        return;
    }

    if (wasSidechainListening)
    {
        comp1.initRuntime();
        comp2.initRuntime();
        tapeSaturator.reset();
        overSampler1.reset();
        overSampler2.reset();
        detectorMonitorL.reset();
        detectorMonitorR.reset();
        resetSidechainListenMonitor();
        resetAutoGainTracking();
        prevMeterL = 0.0;
        prevMeterR = 0.0;
        wasSidechainListening = false;
    }

    wasPoweredOn = true;

    for (int i = 0; i < numSamples; ++i)
    {
        const float curInL = inLRead[i];
        const float curInR = inRRead[i];

        lookaheadL[static_cast<size_t>(lookaheadWritePos)] = curInL;
        lookaheadR[static_cast<size_t>(lookaheadWritePos)] = curInR;

        delayedBlockL[static_cast<size_t>(i)] = static_cast<double>(lookaheadL[static_cast<size_t>(lookaheadReadPos)]);
        delayedBlockR[static_cast<size_t>(i)] = static_cast<double>(lookaheadR[static_cast<size_t>(lookaheadReadPos)]);

        ++lookaheadReadPos;
        if (lookaheadReadPos >= lookaheadSize)
            lookaheadReadPos = 0;

        ++lookaheadWritePos;
        if (lookaheadWritePos >= lookaheadSize)
            lookaheadWritePos = 0;
    }

    double peakL = 0.0;
    double peakR = 0.0;
    double transferPeakInput = 0.0;
    double transferPeakOutput = 0.0;
    double transferPeakReduction = 0.0;

    for (int i = 0; i < numSamples; ++i)
    {
        const double inputGainL = dbToLin(inputDbSmoothedL.getNextValue());
        const double inputGainR = dbToLin(inputDbSmoothedR.getNextValue());
        const double outputGainL = dbToLin(outputDbSmoothedL.getNextValue());
        const double outputGainR = dbToLin(outputDbSmoothedR.getNextValue());
        const double thresholdNowL = thresholdDbSmoothedL.getNextValue();
        const double thresholdNowR = thresholdDbSmoothedR.getNextValue();
        const double mixNow = mixSmoothed.getNextValue();
        const double linkAmountNow = linkAmountSmoothed.getNextValue();

        outputGainBlockL[static_cast<size_t>(i)] = outputGainL;
        outputGainBlockR[static_cast<size_t>(i)] = outputGainR;
        mixBlock[static_cast<size_t>(i)] = mixNow;

        comp1.setThresh(thresholdNowL);
        comp2.setThresh(thresholdNowR);

    const double keyMainL = static_cast<double>(inLRead[i]) * inputGainL;
    const double keyMainR = static_cast<double>(inRRead[i]) * inputGainR;
    const double keyMainMid = encodeMid(static_cast<double>(inLRead[i]), static_cast<double>(inRRead[i])) * inputGainL;
    const double keyMainSide = encodeSide(static_cast<double>(inLRead[i]), static_cast<double>(inRRead[i])) * inputGainR;

        const double inLOrigin = delayedBlockL[static_cast<size_t>(i)];
        const double inROrigin = delayedBlockR[static_cast<size_t>(i)];

        double grL = 0.0;
        double grR = 0.0;

        if (msMode)
        {
            double inMid = encodeMid(inLOrigin, inROrigin) * inputGainL;
            double inSide = encodeSide(inLOrigin, inROrigin) * inputGainR;
            dryBlendBlockL[static_cast<size_t>(i)] = inMid;
            dryBlendBlockR[static_cast<size_t>(i)] = inSide;

            if (sideChainOn)
            {
                const double scInL = (scL != nullptr) ? static_cast<double>(scL[i]) : 0.0;
                const double scInR = (scR != nullptr) ? static_cast<double>(scR[i]) : 0.0;
                const double scMid = encodeMid(scInL, scInR) * inputGainL;
                const double scSide = encodeSide(scInL, scInR) * inputGainR;

                if (compActiveL)
                    grL = comp1.sidechain(inMid, scMid);
                if (compActiveR)
                    grR = comp2.sidechain(inSide, scSide);
            }
            else
            {
                if (compActiveL)
                    grL = comp1.sidechain(inMid, keyMainMid);
                if (compActiveR)
                    grR = comp2.sidechain(inSide, keyMainSide);
            }

            wetBlockL[static_cast<size_t>(i)] = inMid;
            wetBlockR[static_cast<size_t>(i)] = inSide;
        }
        else
        {
            double inLFull = inLOrigin * inputGainL;
            double inRFull = inROrigin * inputGainR;
            dryBlendBlockL[static_cast<size_t>(i)] = inLFull;
            dryBlendBlockR[static_cast<size_t>(i)] = inRFull;

            const auto applyLinkedKeys = [&](double independentKeyL, double independentKeyR)
            {
                const double dominantKey = std::max(independentKeyL, independentKeyR);
                const double energyKey = std::sqrt(0.5 * (independentKeyL * independentKeyL + independentKeyR * independentKeyR));
                const double linkedKey = stereoLinkPeakBlend * dominantKey + (1.0 - stereoLinkPeakBlend) * energyKey;
                const double linkedKeyL = independentKeyL + linkAmountNow * (linkedKey - independentKeyL);
                const double linkedKeyR = independentKeyR + linkAmountNow * (linkedKey - independentKeyR);

                if (compActiveL)
                    grL = comp1.applyGainFromKeyLevel(inLFull, linkedKeyL);
                if (compActiveR)
                    grR = comp2.applyGainFromKeyLevel(inRFull, linkedKeyR);
            };

            if (sideChainOn)
            {
                const double scInL = (scL != nullptr) ? static_cast<double>(scL[i]) : 0.0;
                const double scInR = (scR != nullptr) ? static_cast<double>(scR[i]) : 0.0;
                const double scLFull = scInL * inputGainL;
                const double scRFull = scInR * inputGainR;

                if (linkOn && (compActiveL || compActiveR))
                {
                    const double independentKeyL = comp1.detectKeyLevel(scLFull);
                    const double independentKeyR = comp2.detectKeyLevel(scRFull);
                    applyLinkedKeys(independentKeyL, independentKeyR);
                }
                else
                {
                    if (compActiveL)
                        grL = comp1.sidechain(inLFull, scLFull);
                    if (compActiveR)
                        grR = comp2.sidechain(inRFull, scRFull);
                }
            }
            else
            {
                if (linkOn && (compActiveL || compActiveR))
                {
                    const double independentKeyL = comp1.detectKeyLevel(keyMainL);
                    const double independentKeyR = comp2.detectKeyLevel(keyMainR);
                    applyLinkedKeys(independentKeyL, independentKeyR);
                }
                else
                {
                    if (compActiveL)
                        grL = comp1.sidechain(inLFull, keyMainL);
                    if (compActiveR)
                        grR = comp2.sidechain(inRFull, keyMainR);
                }
            }

            wetBlockL[static_cast<size_t>(i)] = inLFull;
            wetBlockR[static_cast<size_t>(i)] = inRFull;
        }

        const double colourReductionDb = std::max(-grL, -grR);
        compGainBlockL[static_cast<size_t>(i)] = compActiveL ? dbToLin(grL) : 1.0;
        compGainBlockR[static_cast<size_t>(i)] = compActiveR ? dbToLin(grR) : 1.0;
        const double tapeAmount = tapeAmountFromReductionDb(colourReductionDb, characterMode);
        tapeAmountBlock[static_cast<size_t>(i)] = tapeAmount;
        transferPeakInput = std::max(transferPeakInput,
                         std::max(std::fabs(dryBlendBlockL[static_cast<size_t>(i)]),
                              std::fabs(dryBlendBlockR[static_cast<size_t>(i)])));
        transferPeakReduction = std::max(transferPeakReduction, colourReductionDb);

        peakL = std::max(peakL, std::fabs(grL / 24.0));
        peakR = std::max(peakR, std::fabs(grR / 24.0));

        const double xL = (peakL < prevMeterL ? meterDecay : meterAttack);
        const double xR = (peakR < prevMeterR ? meterDecay : meterAttack);
        peakL = peakL * xL + prevMeterL * (1.0 - xL);
        peakR = peakR * xR + prevMeterR * (1.0 - xR);
        prevMeterL = peakL;
        prevMeterR = peakR;
    }

    for (int i = 0; i < numSamples; ++i)
    {
        double alignedL = 0.0;
        double alignedR = 0.0;
        processParallelDelaySample(dryBlendBlockL[static_cast<size_t>(i)],
                                   dryBlendBlockR[static_cast<size_t>(i)],
                                   alignedL,
                                   alignedR);
        alignedDryBlockL[static_cast<size_t>(i)] = alignedL;
        alignedDryBlockR[static_cast<size_t>(i)] = alignedR;
    }

    overSampler1.processBlock(saturatedBlockL.data(), wetBlockL.data(), tapeAmountBlock.data(), numSamples, tapeSaturator, 0, characterMode);
    overSampler2.processBlock(saturatedBlockR.data(), wetBlockR.data(), tapeAmountBlock.data(), numSamples, tapeSaturator, 1, characterMode);

    for (int i = 0; i < numSamples; ++i)
    {
        const double autoGainBlend = autoGainBlendSmoothed.getNextValue();

        if (msMode)
        {
            const double dryMid = alignedDryBlockL[static_cast<size_t>(i)];
            const double drySide = alignedDryBlockR[static_cast<size_t>(i)];
            const double wetMid = saturatedBlockL[static_cast<size_t>(i)];
            const double wetSide = saturatedBlockR[static_cast<size_t>(i)];
            const double mixedMid = dryMid + mixBlock[static_cast<size_t>(i)] * (wetMid - dryMid);
            const double mixedSide = drySide + mixBlock[static_cast<size_t>(i)] * (wetSide - drySide);
            const double referenceLeft = decodeLeft(dryMid, drySide);
            const double referenceRight = decodeRight(dryMid, drySide);
            const double processedLeft = decodeLeft(mixedMid, mixedSide);
            const double processedRight = decodeRight(mixedMid, mixedSide);
            const double mixedGainMid = (1.0 - mixBlock[static_cast<size_t>(i)])
                                      + mixBlock[static_cast<size_t>(i)] * compGainBlockL[static_cast<size_t>(i)];
            const double mixedGainSide = (1.0 - mixBlock[static_cast<size_t>(i)])
                                       + mixBlock[static_cast<size_t>(i)] * compGainBlockR[static_cast<size_t>(i)];
            const double guideCompensation = 1.0 / std::sqrt(0.5 * (mixedGainMid * mixedGainMid + mixedGainSide * mixedGainSide));
            const double autoGainCompensation = lerp(1.0,
                                                     updateAutoGainCompensation(referenceLeft,
                                                                                referenceRight,
                                                                                processedLeft,
                                                                                processedRight,
                                                                                guideCompensation),
                                                     autoGainBlend);
            const double compensatedMid = mixedMid * autoGainCompensation * outputGainBlockL[static_cast<size_t>(i)];
            const double compensatedSide = mixedSide * autoGainCompensation * outputGainBlockR[static_cast<size_t>(i)];
            const double outputLeft = decodeLeft(compensatedMid, compensatedSide);
            const double outputRight = decodeRight(compensatedMid, compensatedSide);

            outL[i] = static_cast<float>(outputLeft);
            if (outR != nullptr)
                outR[i] = static_cast<float>(outputRight);

            transferPeakOutput = std::max(transferPeakOutput, std::max(std::fabs(outputLeft), std::fabs(outputRight)));
        }
        else
        {
            const double dryL = alignedDryBlockL[static_cast<size_t>(i)];
            const double wetProcessedL = saturatedBlockL[static_cast<size_t>(i)];
            const double mixedLeft = dryL + mixBlock[static_cast<size_t>(i)] * (wetProcessedL - dryL);
            double referenceRight = dryL;
            double mixedRight = mixedLeft;

            if (outR != nullptr)
            {
                const double dryR = alignedDryBlockR[static_cast<size_t>(i)];
                const double wetProcessedR = saturatedBlockR[static_cast<size_t>(i)];
                referenceRight = dryR;
                mixedRight = dryR + mixBlock[static_cast<size_t>(i)] * (wetProcessedR - dryR);
            }

            const double mixedGainLeft = (1.0 - mixBlock[static_cast<size_t>(i)])
                                       + mixBlock[static_cast<size_t>(i)] * compGainBlockL[static_cast<size_t>(i)];
            const double mixedGainRight = (1.0 - mixBlock[static_cast<size_t>(i)])
                                        + mixBlock[static_cast<size_t>(i)] * compGainBlockR[static_cast<size_t>(i)];
            const double guideCompensation = 1.0 / std::sqrt(0.5 * (mixedGainLeft * mixedGainLeft + mixedGainRight * mixedGainRight));
            const double autoGainCompensation = lerp(1.0,
                                                     updateAutoGainCompensation(dryL,
                                                                                referenceRight,
                                                                                mixedLeft,
                                                                                mixedRight,
                                                                                guideCompensation),
                                                     autoGainBlend);
            const double outputLeft = mixedLeft * autoGainCompensation * outputGainBlockL[static_cast<size_t>(i)];
            outL[i] = static_cast<float>(outputLeft);

            if (outR != nullptr)
            {
                const double outputRight = mixedRight * autoGainCompensation * outputGainBlockR[static_cast<size_t>(i)];
                outR[i] = static_cast<float>(outputRight);
                transferPeakOutput = std::max(transferPeakOutput, std::max(std::fabs(outputLeft), std::fabs(outputRight)));
            }
            else
            {
                transferPeakOutput = std::max(transferPeakOutput, std::fabs(outputLeft));
            }
        }
    }

    meterL.store(static_cast<float>(peakL));
    meterR.store(static_cast<float>(peakR));
    transferInputDb.store(static_cast<float>(linToDb(std::max(transferPeakInput, 1.0e-6))));
    transferOutputDb.store(static_cast<float>(linToDb(std::max(transferPeakOutput, 1.0e-6))));
    transferReductionDb.store(static_cast<float>(transferPeakReduction));
}
} // namespace NComp
