#include "ParamRegistry.h"
#include "../DSP/Wavetables.h"

namespace nsynth
{
namespace
{
ParamSpec F (const char* suffix, const char* name, Section s, float lo, float hi, float def,
             Mapping m, float centre, const char* unit, int cc, const char*, const char* notes = "")
{
    ParamSpec p { suffix, name, s, Kind::floatParam };
    p.minValue = lo; p.maxValue = hi; p.defaultValue = def; p.mapping = m; p.skewCentre = centre;
    p.unit = unit; p.midiCC = cc; p.notes = notes;
    return p;
}

ParamSpec C (const char* suffix, const char* name, Section s, juce::StringArray choices, int def,
             int cc, const char*, const char* notes = "")
{
    ParamSpec p { suffix, name, s, Kind::choiceParam };
    p.minValue = 0.0f; p.maxValue = (float) (choices.size() - 1); p.defaultValue = (float) def;
    p.choices = std::move (choices); p.midiCC = cc; p.notes = notes;
    return p;
}

ParamSpec I (const char* suffix, const char* name, Section s, int lo, int hi, int def,
             const char* unit, int cc, const char*, const char* notes = "")
{
    ParamSpec p { suffix, name, s, Kind::intParam };
    p.minValue = (float) lo; p.maxValue = (float) hi; p.defaultValue = (float) def;
    p.unit = unit; p.midiCC = cc; p.notes = notes;
    return p;
}

ParamSpec B (const char* suffix, const char* name, Section s, bool def, int cc, const char*, const char* notes = "")
{
    ParamSpec p { suffix, name, s, Kind::boolParam };
    p.minValue = 0.0f; p.maxValue = 1.0f; p.defaultValue = def ? 1.0f : 0.0f;
    p.midiCC = cc; p.notes = notes;
    return p;
}

constexpr auto Lin = Mapping::linear;
constexpr auto Exp = Mapping::exponential;
constexpr auto Skw = Mapping::skewed;

const juce::StringArray tri3 { "Off", "1/2", "On" };
const juce::StringArray oscTarget { "DDS 1", "1 + 2", "DDS 2" };

std::vector<ParamSpec> buildLayerSpecs()
{
    using S = Section;
    return {
        // ---------------------------------------------------------------- DDS 1
        C (pid::dds1Wave, "DDS1 Waveform", S::dds1, { "Sine", "Sawtooth", "Square", "Triangle", "Noise", "Alt" }, 1, 29, "",
           "Saw: slightly convex analog ramp, Square: with AC-coupled plateau droop, Triangle: rounded tips; SUPER = 7-oscillator super saw."),
        C (pid::dds1Range, "DDS1 Range", S::dds1, { "64'", "32'", "16'", "8'", "4'", "2'" }, 3, 30, "p.33", "8' = concert pitch."),
        C (pid::dds1AltA, "DDS1 Alt Wave A", S::dds1, WaveBank::altWaveNames(), 0, -1, "p.33-34",
           "Alt waveform A: FM = 4-operator FM, PD = phase distortion, DG = digital spectra, WT = wavetable style."),
        C (pid::dds1AltB, "DDS1 Alt Wave B", S::dds1, WaveBank::altWaveNames(), 1, -1, "p.33-34",
           "Alt waveform B; PWM/WAVE morphs A->B."),

        // ---------------------------------------------------------------- DDS 2
        C (pid::dds2Wave, "DDS2 Waveform", S::dds2, { "Sine", "Sawtooth", "Square", "Triangle", "Noise", "Pulse" }, 1, 31, "p.36"),
        C (pid::dds2Range, "DDS2 Range", S::dds2, { "LFO", "32'", "16'", "8'", "4'", "2'" }, 3, 106, "p.36, 38",
           "LFO: DDS2 leaves the audio path, TUNE sets ~0.1-100 Hz."),
        F (pid::dds2Tune, "DDS2 Tune", S::dds2, -7.0f, 7.0f, 0.0f, Lin, 0, "st", 35, "p.36"),
        C (pid::dds2Mode, "DDS2 Mode", S::dds2, { "Norm / Sub Off", "Ring / Sub Square", "Sync / Sub Sine" }, 0, 36, "p.36-39",
           "Range=LFO: selects the sub oscillator (1 oct below DDS1)."),

        // ---------------------------------------------------------------- Mixer
        F (pid::mix, "Oscillator Mix", S::mixer, 0.0f, 1.0f, 0.0f, Lin, 0, "", 37, "p.40", "0 = DDS1 only, 1 = DDS2/Sub only."),
        F (pid::pan, "Layer Pan", S::mixer, -1.0f, 1.0f, 0.0f, Lin, 0, "", 10, "p.40", "Shift function of MIX."),

        // ---------------------------------------------------------------- DDS Modulator
        F (pid::modLfo1, "DDS Mod LFO1 Amount", S::ddsMod, 0.0f, 1.0f, 0.0f, Lin, 0, "", 21, "p.60", "Pitch mod depth (quadratic, max +/-12 st)."),
        F (pid::modEnv1, "DDS Mod ENV1 Amount", S::ddsMod, 0.0f, 1.0f, 0.0f, Lin, 0, "", 22, "p.60", "Pitch mod depth (quadratic, max +48 st)."),
        C (pid::modDest, "DDS Mod Destination", S::ddsMod, oscTarget, 1, 23, "p.60"),
        C (pid::superMode, "Super Mode", S::ddsMod, { "Off", "1/2", "On" }, 0, 24, "p.61"),
        F (pid::pwDetune, "PW / Detune", S::ddsMod, 0.0f, 1.0f, 0.0f, Lin, 0, "", 25, "p.62"),
        F (pid::pwmWave, "PWM / Wave", S::ddsMod, 0.0f, 1.0f, 0.0f, Lin, 0, "", 26, "p.62"),
        C (pid::pwmSource, "PWM / Wave Source", S::ddsMod, { "Manual", "LFO 1", "ENV 1" }, 0, 27, "p.62"),
        F (pid::crossMod, "Cross Mod", S::ddsMod, 0.0f, 1.0f, 0.0f, Lin, 0, "", 28, "p.63", "Exponential FM, max 4 octaves deviation."),
        F (pid::drift, "Drift", S::ddsMod, 0.0f, 1.0f, 0.0f, Lin, 0, "", 94, "p.62", "Shift function of PW/DETUNE."),

        // ---------------------------------------------------------------- VCF
        F (pid::hpf, "HPF Cutoff", S::vcf, 10.0f, 4000.0f, 10.0f, Exp, 0, "Hz", 95, "p.42", "1-pole 6 dB/oct."),
        F (pid::lpf, "LPF Cutoff", S::vcf, 15.0f, 25000.0f, 25000.0f, Exp, 0, "Hz", 74, "p.42", "Low-pass cutoff (see VCF Type)."),
        F (pid::res, "Resonance", S::vcf, 0.0f, 1.0f, 0.0f, Lin, 0, "", 71, "p.42", "Self-oscillates near max."),
        C (pid::vcfType, "VCF Type", S::vcf, { "Ladder 24", "Ladder 12", "Cascade 24", "SVF 12" }, 0, -1, "-",
           "Ladder 24/12: transistor ladder; Cascade 24: OTA cascade; SVF 12: state-variable filter."),
        C (pid::vcfDrive, "VCF Drive", S::vcf, { "Off", "1", "2" }, 0, 41, "p.42"),
        F (pid::vcfEnv, "VCF Env Amount", S::vcf, 0.0f, 1.0f, 0.0f, Lin, 0, "", 45, "p.42", "Max +8 octaves."),
        C (pid::vcfEnvSource, "VCF Env Source", S::vcf, { "ENV 1", "1 + 2", "ENV 2" }, 0, 44, "p.42"),
        F (pid::vcfLfo1, "VCF LFO1 Amount", S::vcf, 0.0f, 1.0f, 0.0f, Lin, 0, "", 46, "p.42", "Max +/-4 octaves."),
        F (pid::vcfDds2, "VCF DDS2 Amount", S::vcf, 0.0f, 1.0f, 0.0f, Lin, 0, "", 47, "p.42", "Audio-rate, max +/-4 octaves."),
        C (pid::vcfKeytrack, "VCF Keytrack", S::vcf, tri3, 0, 43, "p.43"),

        // ---------------------------------------------------------------- VCA
        F (pid::vcaEnv, "VCA Env Level", S::vca, 0.0f, 1.0f, 0.75f, Lin, 0, "", 7, "p.45",
           "Layer level; factory patches are matched in loudness with it."),
        F (pid::vcaLfo1, "VCA LFO1 Amount", S::vca, 0.0f, 1.0f, 0.0f, Lin, 0, "", 92, "p.45"),
        F (pid::vcaDds2, "VCA DDS2 Amount", S::vca, 0.0f, 1.0f, 0.0f, Lin, 0, "", 109, "p.45"),
        C (pid::vcaEnvMode, "VCA Env Mode", S::vca, { "ENV 2", "Fixed 1 (Gate)", "Fixed 2 (Gate+Rel)" }, 0, 49, "p.45"),

        // ---------------------------------------------------------------- ENV 1
        F (pid::env1AttackHold, "ENV1 Attack Hold", S::env1, 0.0f, 10.0f, 0.0f, Skw, 1.0f, "s", 52, "p.47"),
        F (pid::env1Attack, "ENV1 Attack", S::env1, 0.001f, 10.0f, 0.001f, Exp, 0, "s", 53, "p.47"),
        F (pid::env1DecayHold, "ENV1 Decay Hold", S::env1, 0.0f, 10.0f, 0.0f, Skw, 1.0f, "s", 32, "p.47"),
        F (pid::env1Decay, "ENV1 Decay", S::env1, 0.001f, 10.0f, 0.5f, Exp, 0, "s", 54, "p.48"),
        F (pid::env1Sustain, "ENV1 Sustain", S::env1, 0.0f, 1.0f, 0.5f, Lin, 0, "", 55, "p.48"),
        F (pid::env1Release, "ENV1 Release", S::env1, 0.001f, 10.0f, 0.3f, Exp, 0, "s", 56, "p.48"),
        C (pid::env1Mode, "ENV1 Mode", S::env1, { "Normal", "Inverted", "Loop" }, 0, 50, "p.48"),
        C (pid::env1Keytrack, "ENV1 Keytrack", S::env1, tri3, 0, 51, "p.49", "Shortens decay/release up the keyboard."),

        // ---------------------------------------------------------------- ENV 2
        F (pid::env2Attack, "ENV2 Attack", S::env2, 0.001f, 10.0f, 0.001f, Exp, 0, "s", 73, "p.52"),
        F (pid::env2DecayHold, "ENV2 Decay Hold", S::env2, 0.0f, 10.0f, 0.0f, Skw, 1.0f, "s", 33, "p.52"),
        F (pid::env2Decay, "ENV2 Decay", S::env2, 0.001f, 10.0f, 0.5f, Exp, 0, "s", 57, "p.52"),
        F (pid::env2Sustain, "ENV2 Sustain", S::env2, 0.0f, 1.0f, 1.0f, Lin, 0, "", 58, "p.53"),
        F (pid::env2Release, "ENV2 Release", S::env2, 0.001f, 10.0f, 0.1f, Exp, 0, "s", 72, "p.53"),

        // ---------------------------------------------------------------- LFO 1
        C (pid::lfo1Wave, "LFO1 Waveform", S::lfo1, { "Triangle", "Rev Sawtooth", "Sample & Hold", "Square", "HF", "HF TRK" }, 0, 16, "p.58-59"),
        F (pid::lfo1Rate, "LFO1 Rate", S::lfo1, 0.0f, 1.0f, 0.5f, Lin, 0, "", 17, "p.55",
           "LF: 0.05-30 Hz exp; SYNC: 16 divisions; HF: 20 Hz-20 kHz; HF TRK: note x 2^(+/-4 oct)."),
        F (pid::lfo1Delay, "LFO1 Delay", S::lfo1, 0.0f, 5.0f, 0.0f, Skw, 0.5f, "s", 18, "p.56", "Linear fade-in time after note-on."),
        F (pid::lfo1LrPhase, "LFO1 LR Phase / Spread", S::lfo1, 0.0f, 1.0f, 0.0f, Lin, 0, "", 19, "p.56-58", "0..1 = 0..2pi; non-binaural: pan spread."),
        C (pid::lfo1Mode, "LFO1 Mode", S::lfo1, { "Free / Norm", "Once / DDS 1", "Reset / DDS 2" }, 0, 20, "p.59"),
        B (pid::lfo1Sync, "LFO1 Clock Sync", S::lfo1, false, 81, "p.92", "LF rates snap to 16 tempo divisions."),

        // ---------------------------------------------------------------- Performance control (per layer)
        C (pid::lfo2Wave, "LFO2 Waveform", S::perfControl, { "Sine", "Rev Sawtooth", "Sample & Hold", "Square", "Sawtooth", "Noise" }, 0, 105, "p.70"),
        F (pid::lfo2Rate, "LFO2 Rate", S::perfControl, 0.0f, 1.0f, 0.5f, Lin, 0, "", 62, "p.71", "0.05-30 Hz exponential."),
        F (pid::lfo2Delay, "LFO2 Delay", S::perfControl, 0.0f, 5.0f, 0.0f, Skw, 0.5f, "s", 63, "p.71"),
        C (pid::lfo2Trigger, "LFO2 Trigger", S::perfControl, { "Trig", "AT + Trig", "On (AT->Bend)" }, 0, 60, "p.71"),
        F (pid::lfo2RateMod, "LFO2 Rate Mod", S::perfControl, 0.0f, 1.0f, 0.0f, Lin, 0, "", 110, "p.71", "Bender-up/AT speeds LFO2 up to +3 oct."),
        F (pid::lfo2Dds, "LFO2 DDS Amount", S::perfControl, 0.0f, 1.0f, 0.0f, Lin, 0, "", 70, "p.71", "Quadratic, max +/-12 st."),
        F (pid::lfo2Vcf, "LFO2 VCF Amount", S::perfControl, 0.0f, 1.0f, 0.0f, Lin, 0, "", 75, "p.71", "Max +/-4 octaves."),
        F (pid::lfo2Vca, "LFO2 VCA Amount", S::perfControl, 0.0f, 1.0f, 0.0f, Lin, 0, "", 42, "p.72"),
        C (pid::perfDest, "Bender/LFO2 Osc Destination", S::perfControl, oscTarget, 1, 61, "p.72"),
        F (pid::bendDds, "Bend DDS Amount", S::perfControl, 0.0f, 12.0f, 2.0f, Lin, 0, "st", 76, "p.69, 73"),
        F (pid::bendVcf, "Bend VCF Amount", S::perfControl, 0.0f, 1.0f, 0.0f, Lin, 0, "", 77, "p.69", "Max +/-10 octaves."),
        F (pid::portamento, "Portamento", S::perfControl, 0.0f, 1.0f, 0.0f, Lin, 0, "", 5, "p.74", "0 = off; 1 = 10 s per octave (cubic taper)."),
        I (pid::octave, "Octave", S::perfControl, -2, 2, 0, "oct", 67, "p.75"),

        // ---------------------------------------------------------------- Voice assign / velocity
        C (pid::voiceMode, "Voice Assign Mode", S::voiceAssign, { "Solo", "Legato", "Poly 1", "Poly 2" }, 2, 78, "p.90"),
        B (pid::unison, "Unison", S::voiceAssign, false, -1, "p.90"),
        C (pid::unisonSize, "Unison Size", S::voiceAssign, { "Half", "All", "Octave", "Fifth + Octave" }, 1, 79, "p.91"),
        C (pid::binaural, "Binaural", S::voiceAssign, { "Off", "x1", "x2" }, 2, 80, "p.91",
           "x1: one voice per ear; x2: a detuned pair per ear (4 voices per note) for a wider, denser sound."),
        F (pid::binauralDetune, "Binaural Detune", S::voiceAssign, 0.0f, 25.0f, 7.0f, Lin, 0, "ct", -1, "-",
           "x2: mirrored detune between the two voices of each ear. UNISON: stacked voices spread over +/- 1.5 x this."),
        F (pid::stereoWidth, "Stereo Width", S::voiceAssign, 0.0f, 1.0f, 0.65f, Lin, 0, "", -1, "-",
           "Binaural: how far apart the left and right voices sit (1 = hard left/right, 0 = mono). UNISON: stereo fan-out of the stack."),
        B (pid::hold, "Hold", S::voiceAssign, false, 87, "p.81"),
        C (pid::velMode, "Velocity Mode", S::voiceAssign, { "Dynamic", "Static" }, 1, -1, "-",
           "Static (default): every note uses STATIC VEL. Dynamic: played velocity through VEL CURVE."),
        I (pid::velFixed, "Static Velocity", S::voiceAssign, 1, 127, 100, "", -1, "-", "Velocity of every note in Static mode."),
        F (pid::velCurve, "Velocity Curve", S::voiceAssign, -1.0f, 1.0f, 0.0f, Lin, 0, "", -1, "-",
           "Dynamic mode: < 0 soft (light touch is louder), 0 linear, > 0 hard."),
        F (pid::velSens, "Velocity Sensitivity", S::voiceAssign, 0.0f, 1.0f, 0.5f, Lin, 0, "", 48, "p.45",
           "Velocity -> VCA level and LPF brightness (0 = velocity ignored)."),

        // ---------------------------------------------------------------- Chorus
        C (pid::chorus, "Chorus", S::fx, { "Off", "I", "II", "I + II" }, 0, 93, "p.64",
           "Modulated voices per side: I = 1, II = 2, I + II = 3."),
        F (pid::chorusRate, "Chorus Rate", S::fx, 0.05f, 10.0f, 0.5f, Exp, 0, "Hz", 102, "-"),
        F (pid::chorusDepth, "Chorus Depth", S::fx, 0.0f, 1.0f, 0.5f, Lin, 0, "", 103, "-", "Modulation depth relative to DELAY."),
        F (pid::chorusDelay, "Chorus Delay", S::fx, 1.0f, 20.0f, 3.5f, Exp, 0, "ms", -1, "-", "Centre delay of the modulated voices."),
        F (pid::chorusFeedback, "Chorus Feedback", S::fx, 0.0f, 0.9f, 0.0f, Lin, 0, "", -1, "-"),
        F (pid::chorusWidth, "Chorus Width", S::fx, 0.0f, 1.0f, 1.0f, Lin, 0, "", -1, "-", "0 = mono, 1 = opposite LFO phase left/right."),
        F (pid::chorusMix, "Chorus Mix", S::fx, 0.0f, 1.0f, 0.5f, Lin, 0, "", 104, "-"),

        // ---------------------------------------------------------------- Delay
        B (pid::delaySync, "Delay Sync", S::delay, true, 82, "p.65", "On: TIME follows the tempo division; off: milliseconds."),
        F (pid::delayMs, "Delay Time", S::delay, 1.0f, 2000.0f, 350.0f, Exp, 0, "ms", 12, "p.65"),
        C (pid::delayDivision, "Delay Division", S::delay,
           { "1/64", "1/32 T", "1/32", "1/16 T", "1/32 D", "1/16", "1/8 T", "1/16 D", "1/8",
             "1/4 T", "1/8 D", "1/4", "1/2 T", "1/4 D", "1/2", "1/2 D", "1/1" }, 11, 86, "p.65"),
        F (pid::delayFeedback, "Delay Feedback", S::delay, 0.0f, 1.0f, 0.35f, Lin, 0, "", 13, "p.66", "1.0 = infinite repeats, no degradation."),
        F (pid::delayMix, "Delay Mix", S::delay, 0.0f, 1.0f, 0.0f, Lin, 0, "", 85, "p.67",
           "0-50 %: echoes fade in under the full dry signal; 50-100 %: the dry signal fades out."),
        B (pid::delayPingPong, "Delay Ping-Pong", S::delay, false, 83, "-", "Echoes alternate between left and right."),
        B (pid::delayFreeze, "Delay Freeze", S::delay, false, 69, "p.67"),
        F (pid::delayLowCut, "Delay Low Cut", S::delay, 20.0f, 2000.0f, 120.0f, Exp, 0, "Hz", -1, "-", "High-pass in the echo path."),
        F (pid::delayHighCut, "Delay High Cut", S::delay, 1000.0f, 20000.0f, 9000.0f, Exp, 0, "Hz", -1, "-", "Low-pass in the echo path."),

        // ---------------------------------------------------------------- Reverb
        F (pid::reverbMix, "Reverb Mix", S::reverb, 0.0f, 1.0f, 0.0f, Lin, 0, "", 91, "-",
           "0-50 %: the tail fades in under the full dry signal; 50-100 %: the dry signal fades out."),
        F (pid::reverbSize, "Reverb Size", S::reverb, 0.0f, 1.0f, 0.6f, Lin, 0, "", 89, "-"),
        F (pid::reverbDecay, "Reverb Decay", S::reverb, 0.2f, 20.0f, 2.5f, Exp, 0, "s", 90, "-", "RT60 of the low/mid band."),
        F (pid::reverbPreDelay, "Reverb Pre-Delay", S::reverb, 0.0f, 250.0f, 15.0f, Skw, 50.0f, "ms", -1, "-"),
        F (pid::reverbDamping, "Reverb High Cut", S::reverb, 1000.0f, 20000.0f, 9000.0f, Exp, 0, "Hz", -1, "-",
           "High frequencies above this decay faster."),
        F (pid::reverbLowCut, "Reverb Low Cut", S::reverb, 20.0f, 1000.0f, 150.0f, Exp, 0, "Hz", -1, "-"),
        F (pid::reverbWidth, "Reverb Width", S::reverb, 0.0f, 1.0f, 1.0f, Lin, 0, "", -1, "-"),
        F (pid::reverbModulation, "Reverb Modulation", S::reverb, 0.0f, 1.0f, 0.3f, Lin, 0, "", -1, "-",
           "Slow delay-line modulation that smooths metallic ringing."),
    };
}

std::vector<ParamSpec> buildPerformanceSpecs()
{
    using S = Section;
    return {
        C (pid::kbdMode, "Keyboard Mode", S::performance, { "Single", "Dual", "Split" }, 0, -1, "p.82"),
        C (pid::singleLayer, "Single Mode Layer", S::performance, { "Upper", "Lower" }, 0, -1, "p.82"),
        I (pid::splitPoint, "Split Point", S::performance, 0, 127, 60, "note", -1, "p.82", "First note of the upper layer (C4 = 60)."),
        F (pid::lowerDetune, "Lower Layer Detune", S::performance, -7.0f, 7.0f, 0.0f, Lin, 0, "st", 108, "p.80"),
        F (pid::perfDetune, "Performance Detune", S::performance, -7.0f, 7.0f, 0.0f, Lin, 0, "st", 111, "p.80"),
        I (pid::transpose, "Transpose", S::performance, -12, 12, 0, "st", -1, "p.75"),
        F (pid::fineTune, "Fine Tune", S::performance, -100.0f, 100.0f, 0.0f, Lin, 0, "ct", -1, "p.76"),
        F (pid::tempo, "Tempo", S::performance, 30.0f, 300.0f, 120.0f, Lin, 0, "BPM", 3, "p.79", "Used when the host provides no tempo."),
        F (pid::masterVolume, "Master Volume", S::performance, -60.0f, 4.0f, 0.0f, Skw, -12.0f, "dB", -1, "p.79"),
        F (pid::ribbon, "Ribbon", S::performance, -1.0f, 1.0f, 0.0f, Lin, 0, "", 2, "p.77-78", "Relative displacement from touch point."),
        F (pid::modLever, "Mod Lever (Bender Up)", S::performance, 0.0f, 1.0f, 0.0f, Lin, 0, "", 1, "p.69"),
        F (pid::expression, "Expression", S::performance, 0.0f, 1.0f, 0.0f, Lin, 0, "", 11, "p.29"),
        C (pid::midiRouting, "MIDI Routing", S::performance, { "Keyboard Mode", "Ch N Upper / N+1 Lower" }, 0, -1, "p.99"),
        B (pid::dualPedal, "Dual Sustain Pedal", S::performance, false, -1, "-", "When enabled, CC64 channel 1 sustains UPPER and channel 2 sustains LOWER."),
        B (pid::hardwareCCMap, "Hardware CC Map", S::performance, false, -1, "", "Receive external MIDI CC and 14-bit NRPN parameter mappings."),
    };
}
} // namespace

const std::vector<ParamSpec>& getLayerParamSpecs()
{
    static const auto specs = buildLayerSpecs();
    return specs;
}

const std::vector<ParamSpec>& getPerformanceParamSpecs()
{
    static const auto specs = buildPerformanceSpecs();
    return specs;
}

const ParamSpec* findLayerSpec (const juce::String& suffix)
{
    for (auto& s : getLayerParamSpecs())
        if (suffix == s.suffix)
            return &s;
    return nullptr;
}

juce::String layerPrefix (Layer layer) { return layer == Layer::upper ? "up_" : "lo_"; }
juce::String layerParamId (Layer layer, const char* suffix) { return layerPrefix (layer) + suffix; }
juce::String perfParamId (const char* suffix) { return juce::String ("perf_") + suffix; }

juce::String sectionName (Section s)
{
    switch (s)
    {
        case Section::dds1:        return "DDS 1";
        case Section::dds2:        return "DDS 2";
        case Section::mixer:       return "Mixer";
        case Section::ddsMod:      return "DDS Modulator";
        case Section::vcf:         return "VCF";
        case Section::vca:         return "VCA";
        case Section::env1:        return "ENV 1";
        case Section::env2:        return "ENV 2";
        case Section::lfo1:        return "LFO 1";
        case Section::perfControl: return "Performance Control";
        case Section::voiceAssign: return "Voice / Velocity";
        case Section::fx:          return "Chorus";
        case Section::delay:       return "Delay";
        case Section::reverb:      return "Reverb";
        case Section::performance: return "Performance";
    }
    return {};
}

juce::NormalisableRange<float> makeRange (const ParamSpec& spec)
{
    const float lo = spec.minValue, hi = spec.maxValue;

    switch (spec.kind)
    {
        case Kind::choiceParam:
        case Kind::intParam:
        case Kind::boolParam:
            return { lo, hi, 1.0f };
        case Kind::floatParam:
            break;
    }

    if (spec.mapping == Mapping::exponential)
    {
        jassert (lo > 0.0f);
        return { lo, hi,
                 [] (float start, float end, float x) { return start * std::pow (end / start, x); },
                 [] (float start, float end, float v) { return std::log (v / start) / std::log (end / start); },
                 [] (float start, float end, float v) { return juce::jlimit (start, end, v); } };
    }

    juce::NormalisableRange<float> r { lo, hi };
    if (spec.mapping == Mapping::skewed)
        r.setSkewForCentre (spec.skewCentre);
    return r;
}

juce::String formatValue (const ParamSpec& spec, float v)
{
    const juce::String unit (spec.unit);
    if (unit == "Hz")  return v < 1000.0f ? juce::String (v, v < 10.0f ? 2 : v < 100.0f ? 1 : 0) + " Hz" : juce::String (v / 1000.0f, 2) + " kHz";
    if (unit == "s")   return v < 1.0f ? juce::String (v * 1000.0f, v < 0.01f ? 1 : 0) + " ms" : juce::String (v, 2) + " s";
    if (unit == "ms")  return v < 1000.0f ? juce::String (v, v < 10.0f ? 1 : 0) + " ms" : juce::String (v / 1000.0f, 2) + " s";
    if (unit == "st")  return (v > 0.0f ? "+" : "") + juce::String (v, 2) + " st";
    if (unit == "dB")  return v <= -59.9f ? juce::String ("-inf dB") : juce::String (v, 1) + " dB";
    if (unit == "ct")  return (v > 0.0f ? "+" : "") + juce::String (juce::roundToInt (v)) + " ct";
    if (unit == "BPM") return juce::String (v, 1) + " BPM";
    if (spec.minValue < 0.0f) return (v > 0.0f ? "+" : "") + juce::String (juce::roundToInt (v * 100.0f)) + " %";
    return juce::String (juce::roundToInt (v * 100.0f)) + " %";
}

float parseValue (const ParamSpec& spec, const juce::String& text)
{
    const auto t = text.trim().toLowerCase();
    float v = t.retainCharacters ("+-.0123456789").getFloatValue();
    const juce::String unit (spec.unit);
    if (unit == "Hz" && t.contains ("k")) v *= 1000.0f;
    else if (unit == "s" && t.endsWith ("ms")) v *= 0.001f;
    else if (unit == "ms" && t.endsWith ("s") && ! t.endsWith ("ms")) v *= 1000.0f;
    else if (unit.isEmpty()) v *= 0.01f; // percent
    return juce::jlimit (spec.minValue, spec.maxValue, v);
}

namespace
{
std::unique_ptr<juce::RangedAudioParameter> makeParameter (const juce::String& id, const juce::String& name, const ParamSpec& spec)
{
    const juce::ParameterID pid { id, 1 };

    switch (spec.kind)
    {
        case Kind::choiceParam:
            return std::make_unique<juce::AudioParameterChoice> (pid, name, spec.choices, (int) spec.defaultValue);
        case Kind::intParam:
        {
            auto attributes = juce::AudioParameterIntAttributes().withLabel (spec.unit);
            if (juce::String (spec.unit) == "note") // MIDI note names, middle C = C4
                attributes = attributes.withStringFromValueFunction ([] (int v, int) { return juce::MidiMessage::getMidiNoteName (v, true, true, 4); });
            return std::make_unique<juce::AudioParameterInt> (pid, name, (int) spec.minValue, (int) spec.maxValue, (int) spec.defaultValue, attributes);
        }
        case Kind::boolParam:
            return std::make_unique<juce::AudioParameterBool> (pid, name, spec.defaultValue > 0.5f);
        case Kind::floatParam:
            break;
    }

    return std::make_unique<juce::AudioParameterFloat> (
        pid, name, makeRange (spec), spec.defaultValue,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([spec] (float v, int) { return formatValue (spec, v); })
            .withValueFromStringFunction ([spec] (const juce::String& t) { return parseValue (spec, t); }));
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (auto layer : { Layer::upper, Layer::lower })
    {
        const juce::String layerName = layer == Layer::upper ? "Upper " : "Lower ";
        for (auto& spec : getLayerParamSpecs())
            layout.add (makeParameter (layerParamId (layer, spec.suffix), layerName + spec.name, spec));
    }

    for (auto& spec : getPerformanceParamSpecs())
        layout.add (makeParameter (perfParamId (spec.suffix), spec.name, spec));

    return layout;
}
} // namespace nsynth
