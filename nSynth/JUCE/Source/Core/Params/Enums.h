#pragma once

// Choice-parameter enums. The order of every enum MUST match the order of the
// corresponding choice list in ParamRegistry.cpp (checked by ParamTests).

namespace nsynth
{
enum class Layer { upper = 0, lower = 1 };

enum class Dds1Wave { sine = 0, saw, square, triangle, noise, alt };
enum class Dds1Range { ft64 = 0, ft32, ft16, ft8, ft4, ft2 };
enum class Dds2Wave { sine = 0, saw, square, triangle, noise, pulse };
enum class Dds2Range { lfo = 0, ft32, ft16, ft8, ft4, ft2 };
// When DDS2 range == LFO the same switch selects the sub oscillator.
enum class Dds2Mode { normOrSubOff = 0, ringOrSubSquare, syncOrSubSine };
enum class OscTarget { dds1 = 0, both, dds2 };
enum class SuperMode { off = 0, half, on };
enum class PwmSource { manual = 0, lfo1, env1 };
enum class Drive { off = 0, one, two };
// Analog-modelled low-pass types (see VoiceFilter).
enum class FilterType { ladder24 = 0, ladder12, cascade24, svf12 };
enum class EnvSource { env1 = 0, both, env2 };
enum class TriState { off = 0, half, on };
enum class VcaEnvMode { env2 = 0, fixed1, fixed2 };
enum class Env1Mode { normal = 0, inverted, loop };
enum class Lfo1Wave { triangle = 0, revSaw, sampleHold, square, hf, hfTrack };
// In HF modes the same switch selects the audio routing (Norm / ->DDS1 / ->DDS2).
enum class Lfo1Mode { freeOrNorm = 0, onceOrDds1, resetOrDds2 };
enum class Lfo2Wave { sine = 0, revSaw, sampleHold, square, saw, noise };
enum class Lfo2Trigger { trig = 0, atPlusTrig, on };
enum class VoiceMode { solo = 0, legato, poly1, poly2 };
enum class UnisonSize { half = 0, all, octave, fifthOctave };
// x1: one voice per ear; x2: a detuned pair of voices per ear (4 voices per note).
enum class BinauralMode { off = 0, x1, x2 };
enum class VelocityMode { dynamic = 0, fixed };
// Number of modulated chorus voices per side: I = 1, II = 2, I + II = 3.
enum class ChorusMode { off = 0, one, two, oneTwo };
// Tempo-synced delay divisions, strictly short -> long (T = triplet, D = dotted).
enum class DelayDivision
{
    d1_64 = 0, d1_32T, d1_32, d1_16T, d1_32D, d1_16, d1_8T, d1_16D, d1_8,
    d1_4T, d1_8D, d1_4, d1_2T, d1_4D, d1_2, d1_2D, d1_1
};

// MSEG: one-shot envelope, retriggered loop, or free-running loop on the layer clock.
enum class MsegMode { envelope = 0, loop, free };
enum class MsegDivision { d1_16 = 0, d1_8, d1_4, d1_2, bar1, bar2, bar4, bar8 };
enum class Filter2Routing { off = 0, serial, parallel };
enum class Filter2Type { lp12 = 0, lp24, hp12, hp24, bandpass, notch, combPlus, combMinus, formant };
enum class DistortionType { soft = 0, hard, tube, fold, crush };
enum class CompressorKey { self = 0, otherLayer };
// Layer effect modules; their processing order is patch data (see ModMatrixData::fxOrder).
enum class FxModule { distortion = 0, eq, compressor, chorus, phaser, flanger, delay, reverb, count };

enum class KeyboardMode { single = 0, dual, split };
enum class MidiRouting { keyboardMode = 0, channelPerLayer };

template <typename E>
constexpr int toInt (E e) noexcept { return static_cast<int> (e); }
} // namespace nsynth
