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
enum class ArpMode { up = 0, down, upDown, random, seq };
enum class ClockDiv { whole = 0, half, quarter, eighth, sixteenth, thirtySecond, quarterTriplet, eighthTriplet };
enum class ChorusMode { off = 0, one, two, oneTwo };

enum class KeyboardMode { single = 0, dual, split };
enum class Quality { x2 = 0, x4, x8 };
enum class MidiRouting { keyboardMode = 0, channelPerLayer };

template <typename E>
constexpr int toInt (E e) noexcept { return static_cast<int> (e); }
} // namespace nsynth
