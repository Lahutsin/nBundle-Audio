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
    voiceAssign, fx, delay, reverb, performance
};

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
};

const std::vector<ParamSpec>& getLayerParamSpecs();
const std::vector<ParamSpec>& getPerformanceParamSpecs();
const ParamSpec* findLayerSpec (const juce::String& suffix);

juce::String layerPrefix (Layer layer);
juce::String layerParamId (Layer layer, const char* suffix);
juce::String perfParamId (const char* suffix);
juce::String sectionName (Section s);

juce::NormalisableRange<float> makeRange (const ParamSpec& spec);
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
