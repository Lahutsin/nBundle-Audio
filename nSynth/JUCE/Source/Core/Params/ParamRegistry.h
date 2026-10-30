#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>
#include "Enums.h"

namespace nsynth
{
// Single source of truth for every automatable parameter.
// - Layer parameters exist twice: "up_<suffix>" (upper) and "lo_<suffix>" (lower).
// - Performance parameters use "perf_<suffix>".
// docs/02-PARAMETERS.md is generated from this table (nSynthTests --dump-params).
enum class Section
{
    dds1, dds2, mixer, ddsMod, vcf, vca, env1, env2, lfo1, perfControl,
    voiceAssign, fx, delay, reverb, performance,
    // Added in parameter version 2 (appended after every version 1 parameter).
    macros, mseg, filter2, fxDistortion, fxEq, fxCompressor, fxPhaser, fxFlanger
};

// MSEGs per layer (the patch says how many exist, see ModMatrixData::msegCount).
inline constexpr int kMaxMsegs = 16;
enum class MsegField { mode = 0, sync, time, division };

enum class Kind { floatParam, choiceParam, intParam, boolParam };

enum class Mapping
{
    linear,       // min..max linear
    exponential,  // min * (max/min)^x  (min > 0)
    skewed        // JUCE skew with centre value = skewCentre
};

struct ParamSpec
{
    const char* suffix;
    const char* name;
    Section section;
    Kind kind;
    float minValue { 0.0f };
    float maxValue { 1.0f };
    float defaultValue { 0.0f };
    Mapping mapping { Mapping::linear };
    float skewCentre { 0.0f };
    juce::StringArray choices {};
    const char* unit { "" };
    int midiCC { -1 };          // external MIDI CC number, -1 = none
    const char* notes { "" };
    int version { 1 };          // juce::ParameterID version hint (AU parameter ordering)
};

const std::vector<ParamSpec>& getLayerParamSpecs();
const std::vector<ParamSpec>& getPerformanceParamSpecs();
const ParamSpec* findLayerSpec (const juce::String& suffix);
// Index of a layer parameter in getLayerParamSpecs(), -1 if unknown.
int findLayerSpecIndex (const juce::String& suffix);

juce::String layerPrefix (Layer layer);
juce::String layerParamId (Layer layer, const char* suffix);
juce::String perfParamId (const char* suffix);
juce::String sectionName (Section s);
// Suffix of one MSEG's parameter: "mseg<N>_mode", "_sync", "_time" or "_div" (index 0 = MSEG 1).
const char* msegSuffix (int index, MsegField field);

juce::NormalisableRange<float> makeRange (const ParamSpec& spec);
// makeRange() of every layer parameter, built once (getLayerParamSpecs() order).
const juce::NormalisableRange<float>& getLayerParamRange (int index);
// Human-readable value text with units (used by the host and the editor) and its inverse.
juce::String formatValue (const ParamSpec& spec, float value);
float parseValue (const ParamSpec& spec, const juce::String& text);
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

// Layer parameter suffixes used by DSP code (compile-time checked against the table by tests).
namespace pid
{
    inline constexpr const char* dds1Wave = "dds1_wave";
    inline constexpr const char* dds1Range = "dds1_range";
    inline constexpr const char* dds1AltA = "dds1_alt_a";
    inline constexpr const char* dds1AltB = "dds1_alt_b";
    inline constexpr const char* dds2Wave = "dds2_wave";
    inline constexpr const char* dds2Range = "dds2_range";
    inline constexpr const char* dds2Tune = "dds2_tune";
    inline constexpr const char* dds2Mode = "dds2_mode";
    inline constexpr const char* mix = "mix";
    inline constexpr const char* pan = "pan";
    inline constexpr const char* modLfo1 = "mod_lfo1";
    inline constexpr const char* modEnv1 = "mod_env1";
    inline constexpr const char* modDest = "mod_dest";
    inline constexpr const char* superMode = "super_mode";
    inline constexpr const char* pwDetune = "pw_detune";
    inline constexpr const char* pwmWave = "pwm_wave";
    inline constexpr const char* pwmSource = "pwm_src";
    inline constexpr const char* crossMod = "cross_mod";
    inline constexpr const char* drift = "drift";
    inline constexpr const char* hpf = "hpf";
    inline constexpr const char* lpf = "lpf";
    inline constexpr const char* res = "res";
    inline constexpr const char* vcfType = "vcf_type";
    inline constexpr const char* vcfDrive = "vcf_drive";
    inline constexpr const char* vcfEnv = "vcf_env";
    inline constexpr const char* vcfEnvSource = "vcf_env_src";
    inline constexpr const char* vcfLfo1 = "vcf_lfo1";
    inline constexpr const char* vcfDds2 = "vcf_dds2";
    inline constexpr const char* vcfKeytrack = "vcf_keytrack";
    inline constexpr const char* vcaEnv = "vca_env";
    inline constexpr const char* vcaLfo1 = "vca_lfo1";
    inline constexpr const char* vcaDds2 = "vca_dds2";
    inline constexpr const char* vcaEnvMode = "vca_env_mode";
    // Kept as "vca_dynamics": patches saved with the former Off / 1/2 / On switch keep their depth.
    inline constexpr const char* velSens = "vca_dynamics";
    inline constexpr const char* env1AttackHold = "env1_ahold";
    inline constexpr const char* env1Attack = "env1_attack";
    inline constexpr const char* env1DecayHold = "env1_dhold";
    inline constexpr const char* env1Decay = "env1_decay";
    inline constexpr const char* env1Sustain = "env1_sustain";
    inline constexpr const char* env1Release = "env1_release";
    inline constexpr const char* env1Mode = "env1_mode";
    inline constexpr const char* env1Keytrack = "env1_keytrack";
    inline constexpr const char* env2Attack = "env2_attack";
    inline constexpr const char* env2DecayHold = "env2_dhold";
    inline constexpr const char* env2Decay = "env2_decay";
    inline constexpr const char* env2Sustain = "env2_sustain";
    inline constexpr const char* env2Release = "env2_release";
    inline constexpr const char* lfo1Wave = "lfo1_wave";
    inline constexpr const char* lfo1Rate = "lfo1_rate";
    inline constexpr const char* lfo1Delay = "lfo1_delay";
    inline constexpr const char* lfo1LrPhase = "lfo1_lr_phase";
    inline constexpr const char* lfo1Mode = "lfo1_mode";
    inline constexpr const char* lfo1Sync = "clk_sync";
    inline constexpr const char* lfo2Wave = "lfo2_wave";
    inline constexpr const char* lfo2Rate = "lfo2_rate";
    inline constexpr const char* lfo2Delay = "lfo2_delay";
    inline constexpr const char* lfo2Trigger = "lfo2_trigger";
    inline constexpr const char* lfo2RateMod = "lfo2_rate_mod";
    inline constexpr const char* lfo2Dds = "lfo2_dds";
    inline constexpr const char* lfo2Vcf = "lfo2_vcf";
    inline constexpr const char* lfo2Vca = "lfo2_vca";
    inline constexpr const char* perfDest = "perf_dest";
    inline constexpr const char* bendDds = "bend_dds";
    inline constexpr const char* bendVcf = "bend_vcf";
    inline constexpr const char* portamento = "portamento";
    inline constexpr const char* octave = "octave";
    inline constexpr const char* voiceMode = "voice_mode";
    inline constexpr const char* unison = "unison";
    inline constexpr const char* unisonSize = "unison_size";
    inline constexpr const char* binaural = "binaural";
    inline constexpr const char* binauralDetune = "binaural_detune";
    inline constexpr const char* stereoWidth = "stereo_width";
    inline constexpr const char* hold = "hold";
    inline constexpr const char* velMode = "vel_mode";
    inline constexpr const char* velFixed = "vel_fixed";
    inline constexpr const char* velCurve = "vel_curve";
    inline constexpr const char* chorus = "chorus";
    inline constexpr const char* chorusRate = "chorus_rate";
    inline constexpr const char* chorusDepth = "chorus_depth";
    inline constexpr const char* chorusDelay = "chorus_delay";
    inline constexpr const char* chorusFeedback = "chorus_fb";
    inline constexpr const char* chorusWidth = "chorus_width";
    inline constexpr const char* chorusMix = "chorus_mix";
    inline constexpr const char* delaySync = "delay_sync";
    inline constexpr const char* delayMs = "delay_ms";
    inline constexpr const char* delayDivision = "delay_div";
    inline constexpr const char* delayFeedback = "delay_fb";
    inline constexpr const char* delayMix = "delay_mix";
    inline constexpr const char* delayPingPong = "delay_pingpong";
    inline constexpr const char* delayLowCut = "delay_lowcut";
    inline constexpr const char* delayHighCut = "delay_highcut";
    inline constexpr const char* delayFreeze = "delay_freeze";
    inline constexpr const char* reverbMix = "reverb_mix";
    inline constexpr const char* reverbSize = "reverb_size";
    inline constexpr const char* reverbDecay = "reverb_decay";
    inline constexpr const char* reverbPreDelay = "reverb_predelay";
    inline constexpr const char* reverbDamping = "reverb_damp";
    inline constexpr const char* reverbLowCut = "reverb_lowcut";
    inline constexpr const char* reverbWidth = "reverb_width";
    inline constexpr const char* reverbModulation = "reverb_mod";

    // ---- version 2 -------------------------------------------------------------
    inline constexpr const char* macro1 = "macro1";
    inline constexpr const char* macro2 = "macro2";
    inline constexpr const char* macro3 = "macro3";
    inline constexpr const char* macro4 = "macro4";
    inline constexpr const char* macro5 = "macro5";
    inline constexpr const char* macro6 = "macro6";
    inline constexpr const char* macro7 = "macro7";
    inline constexpr const char* macro8 = "macro8";
    inline constexpr const char* mseg1Mode = "mseg1_mode";
    inline constexpr const char* mseg1Sync = "mseg1_sync";
    inline constexpr const char* mseg1Time = "mseg1_time";
    inline constexpr const char* mseg1Division = "mseg1_div";
    inline constexpr const char* mseg2Mode = "mseg2_mode";
    inline constexpr const char* mseg2Sync = "mseg2_sync";
    inline constexpr const char* mseg2Time = "mseg2_time";
    inline constexpr const char* mseg2Division = "mseg2_div";
    inline constexpr const char* filter2Routing = "f2_routing";
    inline constexpr const char* filter2Type = "f2_type";
    inline constexpr const char* filter2Cutoff = "f2_cutoff";
    inline constexpr const char* filter2Res = "f2_res";
    inline constexpr const char* filter2Env = "f2_env";
    inline constexpr const char* filter2EnvSource = "f2_env_src";
    inline constexpr const char* filter2Keytrack = "f2_keytrack";
    inline constexpr const char* filter2Drive = "f2_drive";
    inline constexpr const char* filter2Mix = "f2_mix";
    inline constexpr const char* delayOn = "delay_on";
    inline constexpr const char* delayDuck = "delay_duck";
    inline constexpr const char* reverbOn = "reverb_on";
    inline constexpr const char* reverbDuck = "reverb_duck";
    inline constexpr const char* distOn = "dist_on";
    inline constexpr const char* distType = "dist_type";
    inline constexpr const char* distDrive = "dist_drive";
    inline constexpr const char* distTone = "dist_tone";
    inline constexpr const char* distMix = "dist_mix";
    inline constexpr const char* eqOn = "eq_on";
    inline constexpr const char* eqLowFreq = "eq_low_freq";
    inline constexpr const char* eqLowGain = "eq_low_gain";
    inline constexpr const char* eqMidFreq = "eq_mid_freq";
    inline constexpr const char* eqMidGain = "eq_mid_gain";
    inline constexpr const char* eqMidQ = "eq_mid_q";
    inline constexpr const char* eqHighFreq = "eq_high_freq";
    inline constexpr const char* eqHighGain = "eq_high_gain";
    inline constexpr const char* compOn = "comp_on";
    inline constexpr const char* compThreshold = "comp_threshold";
    inline constexpr const char* compRatio = "comp_ratio";
    inline constexpr const char* compAttack = "comp_attack";
    inline constexpr const char* compRelease = "comp_release";
    inline constexpr const char* compMakeup = "comp_makeup";
    inline constexpr const char* compKey = "comp_key";
    inline constexpr const char* compMix = "comp_mix";
    inline constexpr const char* phaserOn = "phaser_on";
    inline constexpr const char* phaserRate = "phaser_rate";
    inline constexpr const char* phaserDepth = "phaser_depth";
    inline constexpr const char* phaserFeedback = "phaser_fb";
    inline constexpr const char* phaserMix = "phaser_mix";
    inline constexpr const char* flangerOn = "flanger_on";
    inline constexpr const char* flangerRate = "flanger_rate";
    inline constexpr const char* flangerDepth = "flanger_depth";
    inline constexpr const char* flangerFeedback = "flanger_fb";
    inline constexpr const char* flangerMix = "flanger_mix";

    // ---- version 3 -------------------------------------------------------------
    inline constexpr const char* analog = "analog";
    inline constexpr const char* crossModMode = "cross_mod_mode";

    // ---- version 4 (and the parameters of MSEG 3..16, see msegSuffix()) ---------
    inline constexpr const char* eqLowQ = "eq_low_q";
    inline constexpr const char* eqHighQ = "eq_high_q";

    // performance
    inline constexpr const char* kbdMode = "kbd_mode";
    inline constexpr const char* singleLayer = "single_layer";
    inline constexpr const char* splitPoint = "split";
    inline constexpr const char* lowerDetune = "lower_detune";
    inline constexpr const char* perfDetune = "detune";
    inline constexpr const char* transpose = "transpose";
    inline constexpr const char* fineTune = "fine";
    inline constexpr const char* tempo = "tempo";
    inline constexpr const char* masterVolume = "master";
    inline constexpr const char* ribbon = "ribbon";
    inline constexpr const char* modLever = "mod_lever";
    inline constexpr const char* expression = "expression";
    inline constexpr const char* midiRouting = "midi_routing";
    inline constexpr const char* dualPedal = "dual_pedal";
    inline constexpr const char* hardwareCCMap = "hardware_cc_map";
}
} // namespace nsynth
