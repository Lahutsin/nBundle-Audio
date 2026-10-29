// nSynthFactoryGenerator: builds the factory library (Presets/Patches/<category>/*.nsynthpatch and
// Presets/Performances/*.nsynthperf) from the sound designs below.
//
// Every patch is a hand-made design on top of its category's base settings. After a patch is
// configured it is rendered with a phrase typical for its category and its VCA level is adjusted
// so that all factory sounds play at a similar loudness (loudest 400 ms around kTargetLoudnessDb,
// peaks below kPeakCeilingDb).
#include "PhraseRenderer.h"
#include <iostream>
#include <map>

using namespace nsynth;

namespace
{
// Choice values, by index of the parameter's choice list.
namespace v
{
    constexpr float sine = 0, saw = 1, square = 2, tri = 3, noise = 4, alt = 5, pulse = 5;
    constexpr float r64 = 0, r32 = 1, r16 = 2, r8 = 3, r4 = 4, r2 = 5, rLfo = 0;
    constexpr float norm = 0, ring = 1, sync = 2, subSquare = 1, subSine = 2;
    constexpr float off = 0, half = 1, on = 2;
    constexpr float toDds1 = 0, toBoth = 1, toDds2 = 2;
    constexpr float manual = 0, byLfo1 = 1, byEnv1 = 2;
    constexpr float env1 = 0, env12 = 1, env2 = 2;
    constexpr float lTri = 0, lRevSaw = 1, lSampleHold = 2, lSquare = 3;
    constexpr float invert = 1, loop = 2;
    constexpr float solo = 0, legato = 1, poly = 2, poly2 = 3;
    constexpr float x1 = 1, x2 = 2;
    constexpr float chI = 1, chII = 2, chIandII = 3;
    constexpr float d16 = 5, d8T = 6, d16D = 7, d8 = 8, d4T = 9, d8D = 10, d4 = 11, d2T = 12, d4D = 13, d2 = 14;
    constexpr float ladder24 = 0, ladder12 = 1, cascade24 = 2, svf12 = 3;
} // namespace v

// Alt waveform index by name (see WaveBank::altWaveNames()).
float altWave (const char* name)
{
    const int index = WaveBank::altWaveNames().indexOf (name);
    if (index < 0)
        throw std::runtime_error ("Unknown alt waveform: " + std::string (name));
    return (float) index;
}

constexpr double kTargetLoudnessDb = -15.0;
constexpr double kPeakCeilingDb = -2.0;

struct P { const char* suffix; float value; };
struct Route { ModSource source; ModTarget target; float amount; };
struct Direct { ModSource source; DirectModTarget target; float amount; };

struct Design
{
    const char* name;
    std::vector<P> params;
    std::vector<Route> routes {};
    std::vector<Direct> direct {};
};

struct Category
{
    const char* directory;
    const char* name;
    std::vector<P> base;
    std::vector<Design> patches;
};

// Applied to every patch before its category base.
const std::vector<P> globalBase {
    { "binaural", v::x2 }, { "binaural_detune", 7.0f }, { "stereo_width", 0.6f }, { "voice_mode", v::poly },
    { "vel_mode", 1.0f }, { "vel_fixed", 100.0f }, { "vcf_type", v::ladder24 },
    { "vca_dynamics", 0.5f }, { "drift", 0.12f }, { "vcf_keytrack", v::half }, { "hpf", 20.0f },
    { "dds1_range", v::r8 }, { "dds2_range", v::r8 }, { "dds2_mode", v::norm }, { "mix", 0.0f },
    { "lfo2_rate", 0.7f }, { "lfo2_dds", 0.1f }, { "bend_dds", 2.0f },
    { "delay_sync", 1.0f }, { "delay_lowcut", 150.0f }, { "delay_highcut", 7000.0f },
    { "reverb_size", 0.7f }, { "reverb_decay", 2.5f }, { "reverb_predelay", 20.0f }, { "reverb_damp", 8000.0f },
    { "reverb_lowcut", 180.0f }, { "reverb_mod", 0.4f }, { "reverb_width", 0.85f }, { "chorus_width", 0.8f },
};

std::vector<Category> buildCategories()
{
    using namespace v;
    std::vector<Category> c;

    // ------------------------------------------------------------------------------------ Pads
    c.push_back ({ "Pads", "Pads",
        { { "vcf_type", cascade24 },
          { "env2_attack", 0.6f }, { "env2_decay", 1.5f }, { "env2_sustain", 0.85f }, { "env2_release", 2.8f },
          { "lpf", 3000.0f }, { "res", 0.1f }, { "vcf_env", 0.15f },
          { "env1_attack", 1.2f }, { "env1_decay", 3.0f }, { "env1_sustain", 0.4f }, { "env1_release", 3.0f },
          { "binaural_detune", 9.0f }, { "stereo_width", 0.55f }, { "vca_dynamics", 0.3f }, { "drift", 0.2f }, { "hpf", 90.0f },
          { "chorus", chI }, { "chorus_rate", 0.4f }, { "chorus_depth", 0.5f }, { "chorus_mix", 0.4f },
          { "reverb_mix", 0.3f }, { "reverb_size", 0.85f }, { "reverb_decay", 5.0f }, { "reverb_predelay", 30.0f },
          { "reverb_damp", 7000.0f }, { "reverb_lowcut", 200.0f },
          { "delay_mix", 0.1f }, { "delay_div", d4D }, { "delay_fb", 0.35f }, { "delay_pingpong", 1.0f } },
        {
            { "Afterglow", { { "dds1_wave", saw }, { "super_mode", on }, { "pw_detune", 0.3f },
                             { "dds2_wave", saw }, { "dds2_range", r16 }, { "mix", 0.25f }, { "lpf", 2600.0f } } },
            { "Blue Cathedral", { { "dds1_wave", saw }, { "dds2_wave", pulse }, { "dds2_range", r16 }, { "mix", 0.5f },
                                  { "pwm_wave", 0.55f }, { "pwm_src", byLfo1 }, { "lfo1_rate", 0.38f }, { "lfo1_lr_phase", 0.25f },
                                  { "lpf", 1800.0f }, { "res", 0.2f }, { "reverb_mix", 0.38f }, { "reverb_decay", 8.0f } } },
            { "Cloud Atlas", { { "dds1_wave", saw }, { "super_mode", on }, { "pw_detune", 0.45f }, { "lpf", 2400.0f },
                               { "vcf_env", 0.3f }, { "env1_attack", 2.5f }, { "env1_sustain", 0.6f },
                               { "chorus", chIandII }, { "chorus_mix", 0.35f }, { "reverb_mix", 0.35f } } },
            { "Distant Shore", { { "dds1_wave", tri }, { "dds2_wave", sine }, { "dds2_range", r4 }, { "mix", 0.3f },
                                 { "lpf", 6000.0f }, { "vcf_env", 0.05f }, { "env2_attack", 1.2f }, { "env2_release", 4.0f },
                                 { "reverb_mix", 0.4f }, { "reverb_decay", 7.0f }, { "delay_mix", 0.15f } } },
            { "Echowood", { { "dds1_wave", saw }, { "dds2_wave", saw }, { "dds2_tune", 7.0f }, { "mix", 0.35f },
                            { "lpf", 2000.0f }, { "res", 0.25f }, { "lfo1_rate", 0.42f }, { "vcf_lfo1", 0.08f },
                            { "delay_div", d8D }, { "delay_mix", 0.18f }, { "delay_fb", 0.45f } } },
            { "Frosted Glass", { { "dds1_wave", alt }, { "dds1_alt_a", altWave ("DG Bell") }, { "dds1_alt_b", altWave ("PD Reso Tri") }, { "pwm_wave", 0.8f },
                                 { "pwm_src", byLfo1 }, { "lfo1_rate", 0.33f }, { "lfo1_lr_phase", 0.5f },
                                 { "lpf", 7000.0f }, { "vcf_env", 0.0f }, { "chorus", chII }, { "reverb_mix", 0.4f } } },
            { "Gravity Well", { { "vcf_type", ladder24 }, { "dds1_wave", saw }, { "dds1_range", r16 }, { "dds2_wave", saw }, { "dds2_tune", 0.1f },
                                { "mix", 0.5f }, { "lpf", 700.0f }, { "res", 0.3f }, { "vcf_env", 0.35f },
                                { "env1_attack", 2.0f }, { "env1_decay", 4.0f }, { "env1_sustain", 0.3f } } },
            { "Hollow Moon", { { "dds1_wave", alt }, { "dds1_alt_a", altWave ("WT Vocal O") }, { "dds1_alt_b", altWave ("WT Vocal A") },
                               { "pwm_wave", 1.0f }, { "pwm_src", byLfo1 }, { "lfo1_rate", 0.3f }, { "dds2_wave", sine }, { "dds2_range", r4 },
                               { "mix", 0.3f }, { "lpf", 3000.0f }, { "chorus", chIandII }, { "chorus_rate", 0.25f },
                               { "reverb_mix", 0.35f } } },
            { "Indigo Current", { { "dds1_wave", saw }, { "super_mode", on }, { "pw_detune", 0.25f }, { "pwm_wave", 0.6f },
                                  { "pwm_src", byLfo1 }, { "lfo1_rate", 0.35f }, { "vcf_lfo1", 0.05f }, { "lpf", 2400.0f },
                                  { "delay_mix", 0.12f } } },
            { "Jade Horizon", { { "dds1_wave", saw }, { "super_mode", on }, { "pw_detune", 0.28f }, { "lpf", 8000.0f },
                                { "res", 0.0f }, { "vcf_env", 0.0f }, { "env2_attack", 0.35f }, { "env2_release", 2.5f },
                                { "hpf", 150.0f }, { "reverb_mix", 0.3f } } },
            { "Keystone", { { "dds1_wave", saw }, { "dds2_wave", pulse }, { "pw_detune", 0.3f }, { "mix", 0.45f },
                            { "lpf", 1500.0f }, { "res", 0.15f }, { "vcf_env", 0.35f }, { "env1_attack", 0.4f },
                            { "env1_decay", 2.5f }, { "env1_sustain", 0.35f }, { "drift", 0.35f }, { "chorus_rate", 0.5f } } },
            { "Lumen Field", { { "dds1_wave", tri }, { "dds2_wave", saw }, { "dds2_range", r2 }, { "mix", 0.2f },
                               { "lpf", 9000.0f }, { "vcf_env", 0.0f }, { "reverb_mix", 0.45f }, { "reverb_decay", 9.0f },
                               { "reverb_size", 1.0f }, { "delay_mix", 0.2f }, { "delay_fb", 0.55f } } },
            { "Memory Foam", { { "vcf_type", svf12 }, { "dds1_wave", sine }, { "pwm_wave", 0.4f }, { "dds2_wave", tri }, { "dds2_range", r16 },
                               { "mix", 0.35f }, { "lpf", 1200.0f }, { "env2_attack", 1.5f }, { "env2_release", 4.0f },
                               { "vca_dynamics", 0.2f }, { "reverb_mix", 0.35f } } },
            { "Northern Veil", { { "dds1_wave", saw }, { "super_mode", on }, { "pw_detune", 0.35f }, { "dds2_wave", noise },
                                 { "mix", 0.12f }, { "lpf", 3500.0f }, { "hpf", 200.0f }, { "reverb_mix", 0.4f } } },
            { "Open Sky", { { "dds1_wave", saw }, { "super_mode", on }, { "pw_detune", 0.38f }, { "dds2_wave", saw },
                            { "dds2_range", r4 }, { "mix", 0.3f }, { "lpf", 10000.0f }, { "vcf_env", 0.0f },
                            { "env2_attack", 0.25f }, { "chorus", chIandII }, { "reverb_mix", 0.3f } } },
            { "Quiet Machine", { { "dds1_wave", saw }, { "dds2_wave", saw }, { "dds2_mode", sync }, { "dds2_tune", 5.0f },
                                 { "mix", 0.6f }, { "mod_env1", 0.25f }, { "mod_dest", toDds2 }, { "env1_attack", 1.5f },
                                 { "env1_decay", 4.0f }, { "env1_sustain", 0.2f }, { "vcf_env", 0.0f }, { "lpf", 3000.0f },
                                 { "reverb_mix", 0.35f } } },
        } });

    // ------------------------------------------------------------------------------------ Leads
    c.push_back ({ "Leads", "Leads",
        { { "voice_mode", legato }, { "portamento", 0.08f }, { "binaural_detune", 6.0f }, { "stereo_width", 0.45f },
          { "env2_attack", 0.005f }, { "env2_decay", 0.5f }, { "env2_sustain", 0.85f }, { "env2_release", 0.25f },
          { "lpf", 3500.0f }, { "res", 0.2f }, { "vcf_env", 0.35f },
          { "env1_attack", 0.002f }, { "env1_decay", 0.6f }, { "env1_sustain", 0.4f }, { "env1_release", 0.3f },
          { "lfo1_rate", 0.71f }, { "lfo1_delay", 0.6f }, { "mod_lfo1", 0.07f }, { "lfo2_dds", 0.14f },
          { "vca_dynamics", 0.4f }, { "hpf", 60.0f },
          { "delay_div", d8D }, { "delay_mix", 0.14f }, { "delay_fb", 0.35f }, { "delay_pingpong", 1.0f },
          { "reverb_mix", 0.12f }, { "reverb_size", 0.55f }, { "reverb_decay", 2.0f } },
        {
            { "Arc Runner", { { "dds1_wave", saw }, { "dds2_wave", saw }, { "dds2_tune", 0.12f }, { "mix", 0.5f }, { "lpf", 4200.0f } } },
            { "Brightwire", { { "dds1_wave", saw }, { "super_mode", on }, { "pw_detune", 0.25f }, { "lpf", 6000.0f }, { "res", 0.1f } } },
            { "Copperline", { { "dds1_wave", square }, { "dds2_wave", saw }, { "dds2_range", r16 }, { "mix", 0.35f },
                              { "lpf", 2800.0f }, { "res", 0.35f }, { "vcf_drive", 1.0f } } },
            { "Daybreak", { { "vcf_type", svf12 }, { "dds1_wave", tri }, { "dds2_wave", sine }, { "dds2_range", r4 }, { "mix", 0.25f },
                            { "lpf", 5000.0f }, { "vcf_env", 0.1f }, { "portamento", 0.12f }, { "mod_lfo1", 0.09f } } },
            { "Edgewalker", { { "dds1_wave", saw }, { "dds2_wave", saw }, { "dds2_mode", sync }, { "dds2_tune", 7.0f },
                              { "mix", 0.7f }, { "mod_env1", 0.22f }, { "mod_dest", toDds2 }, { "env1_decay", 0.4f },
                              { "lpf", 5000.0f } } },
            { "Fast Forward", { { "vcf_type", ladder12 }, { "dds1_wave", saw }, { "dds2_wave", pulse }, { "pwm_wave", 0.45f }, { "pwm_src", byLfo1 },
                                { "lfo1_rate", 0.55f }, { "lfo1_delay", 0.0f }, { "mod_lfo1", 0.0f }, { "mix", 0.8f },
                                { "lpf", 3800.0f } } },
            { "Gold Signal", { { "dds1_wave", saw }, { "vcf_env", 0.45f }, { "env1_attack", 0.03f }, { "env1_decay", 0.3f },
                               { "env1_sustain", 0.5f }, { "lpf", 1200.0f }, { "res", 0.15f }, { "vcf_drive", 1.0f } } },
            { "High Voltage", { { "dds1_wave", saw }, { "dds2_wave", saw }, { "dds2_tune", -0.1f }, { "mix", 0.5f },
                                { "cross_mod", 0.06f }, { "vcf_drive", 2.0f }, { "lpf", 3000.0f }, { "res", 0.3f } } },
            { "Ion Trail", { { "dds1_wave", saw }, { "super_mode", on }, { "pw_detune", 0.4f }, { "portamento", 0.15f },
                             { "lpf", 5000.0f }, { "delay_mix", 0.2f }, { "reverb_mix", 0.2f } } },
            { "Jetstream", { { "dds1_wave", square }, { "mod_env1", 0.12f }, { "mod_dest", toDds1 }, { "env1_decay", 0.08f },
                             { "env1_sustain", 0.0f }, { "vcf_env", 0.25f }, { "lpf", 4500.0f } } },
            { "Kinetic", { { "vcf_type", svf12 }, { "dds1_wave", saw }, { "dds2_wave", square }, { "mix", 0.4f }, { "lfo1_wave", lSampleHold },
                           { "lfo1_rate", 0.8f }, { "lfo1_delay", 0.0f }, { "mod_lfo1", 0.0f }, { "vcf_lfo1", 0.15f },
                           { "lpf", 2500.0f }, { "res", 0.45f } } },
            { "Laser Bloom", { { "dds1_wave", saw }, { "res", 0.55f }, { "lpf", 800.0f }, { "vcf_env", 0.55f },
                               { "env1_decay", 0.5f }, { "env1_sustain", 0.2f }, { "vcf_drive", 1.0f } } },
            { "Midnight Call", { { "dds1_wave", square }, { "pwm_wave", 0.3f }, { "dds2_wave", sine }, { "dds2_range", r16 },
                                 { "mix", 0.3f }, { "lpf", 2200.0f }, { "portamento", 0.18f }, { "mod_lfo1", 0.1f } } },
            { "Neon Arc", { { "dds1_wave", alt }, { "dds1_alt_a", altWave ("PD Reso Saw") }, { "dds1_alt_b", altWave ("PD Saw") },
                            { "pwm_wave", 1.0f }, { "pwm_src", byEnv1 }, { "env1_decay", 0.7f }, { "env1_sustain", 0.3f },
                            { "lpf", 8000.0f }, { "vcf_env", 0.1f }, { "res", 0.0f } } },
            { "Overtone", { { "dds1_wave", saw }, { "dds2_wave", saw }, { "dds2_range", r4 }, { "mix", 0.4f },
                            { "lpf", 5000.0f }, { "res", 0.1f }, { "reverb_mix", 0.18f } } },
            { "Pulse Driver", { { "dds1_wave", square }, { "vcf_drive", 2.0f }, { "lpf", 3200.0f }, { "res", 0.25f },
                                { "vcf_env", 0.25f } } },
        } });

    // ------------------------------------------------------------------------------------ Bass
    c.push_back ({ "Bass", "Bass",
        { { "voice_mode", solo }, { "binaural", 0.0f }, { "stereo_width", 0.0f }, { "vcf_keytrack", off },
          { "env2_attack", 0.001f }, { "env2_decay", 0.6f }, { "env2_sustain", 0.8f }, { "env2_release", 0.12f },
          { "lpf", 960.0f }, { "res", 0.2f }, { "vcf_env", 0.4f },
          { "env1_attack", 0.001f }, { "env1_decay", 0.35f }, { "env1_sustain", 0.15f }, { "env1_release", 0.15f },
          { "vca_dynamics", 0.5f }, { "hpf", 25.0f }, { "drift", 0.08f }, { "lfo2_dds", 0.0f } },
        {
            { "Anchor Point", { { "dds1_wave", saw }, { "dds2_range", rLfo }, { "dds2_mode", subSquare }, { "mix", 0.4f },
                                { "lpf", 830.0f }, { "vcf_env", 0.45f }, { "res", 0.25f } } },
            { "Black Current", { { "dds1_wave", square }, { "dds2_range", rLfo }, { "dds2_mode", subSine }, { "mix", 0.45f },
                                 { "lpf", 610.0f }, { "vcf_env", 0.5f }, { "res", 0.3f }, { "vcf_drive", 1.0f } } },
            { "Concrete", { { "dds1_wave", saw }, { "dds1_range", r16 }, { "dds2_wave", saw }, { "dds2_range", r16 },
                            { "dds2_tune", 0.08f }, { "mix", 0.5f }, { "lpf", 1440.0f }, { "res", 0.15f },
                            { "vcf_drive", 2.0f }, { "vcf_env", 0.3f } } },
            { "Deep Marker", { { "dds1_wave", tri }, { "dds2_range", rLfo }, { "dds2_mode", subSine }, { "mix", 0.4f },
                               { "lpf", 1440.0f }, { "vcf_env", 0.1f }, { "res", 0.0f } } },
            { "Earthline", { { "vcf_type", svf12 }, { "dds1_wave", sine }, { "dds2_wave", sine }, { "cross_mod", 0.06f }, { "lpf", 3200.0f },
                             { "vcf_env", 0.15f }, { "res", 0.0f } },
                           { { ModSource::env1, ModTarget::crossMod, 0.3f } } },
            { "Floorboard", { { "dds1_wave", saw }, { "env2_decay", 0.35f }, { "env2_sustain", 0.0f }, { "env2_release", 0.1f },
                              { "lpf", 480.0f }, { "vcf_env", 0.6f }, { "env1_decay", 0.18f }, { "env1_sustain", 0.0f },
                              { "res", 0.35f } } },
            { "Gravel Road", { { "dds1_wave", saw }, { "dds2_wave", square }, { "dds2_tune", 0.1f }, { "mix", 0.4f },
                               { "vcf_drive", 2.0f }, { "lpf", 1760.0f }, { "res", 0.35f }, { "vcf_env", 0.35f } } },
            { "Heavy Weather", { { "dds1_wave", saw }, { "super_mode", on }, { "pw_detune", 0.15f }, { "dds2_range", rLfo },
                                 { "dds2_mode", subSine }, { "mix", 0.35f }, { "lpf", 1120.0f }, { "vcf_env", 0.45f } } },
            { "Iron Bloom", { { "dds1_wave", saw }, { "dds2_wave", saw }, { "dds2_mode", sync }, { "dds2_tune", 5.0f },
                              { "mix", 0.6f }, { "mod_env1", 0.18f }, { "mod_dest", toDds2 }, { "lpf", 2400.0f } } },
            { "Jet Black", { { "dds1_wave", saw }, { "dds2_wave", saw }, { "dds2_tune", 0.15f }, { "mix", 0.5f },
                             { "lpf", 1040.0f }, { "vcf_drive", 1.0f }, { "lfo1_rate", 0.3f }, { "vcf_lfo1", 0.06f } } },
            { "Low Orbit", { { "voice_mode", legato }, { "portamento", 0.1f }, { "dds1_wave", square }, { "dds2_range", rLfo },
                             { "dds2_mode", subSine }, { "mix", 0.4f }, { "lpf", 720.0f } } },
            { "Moraine", { { "voice_mode", legato }, { "portamento", 0.05f }, { "dds1_wave", saw }, { "lpf", 420.0f },
                           { "res", 0.72f }, { "vcf_env", 0.6f }, { "env1_decay", 0.22f }, { "env1_sustain", 0.0f },
                           { "vcf_drive", 1.0f }, { "vca_dynamics", 0.7f } } },
            { "Night Shift", { { "vcf_type", ladder12 }, { "dds1_wave", saw }, { "dds2_wave", pulse }, { "pwm_wave", 0.4f }, { "pwm_src", byLfo1 },
                               { "lfo1_rate", 0.5f }, { "mix", 0.65f }, { "lpf", 1280.0f } } },
            { "Oil Drum", { { "dds1_wave", alt }, { "dds1_alt_a", altWave ("DG Digi Bass") }, { "dds2_wave", sine },
                            { "dds2_range", r4 }, { "cross_mod", 0.0f },
                            { "lpf", 1120.0f }, { "vcf_env", 0.4f } },
                          { { ModSource::env1, ModTarget::crossMod, 0.35f } } },
            { "Pressure Drop", { { "dds1_wave", saw }, { "dds2_range", rLfo }, { "dds2_mode", subSine }, { "mix", 0.4f },
                                 { "mod_env1", 0.25f }, { "mod_dest", toDds1 }, { "env1_decay", 0.15f }, { "env1_sustain", 0.0f },
                                 { "lpf", 1440.0f }, { "vcf_env", 0.25f } } },
            { "Quarry", { { "dds1_wave", square }, { "dds1_range", r16 }, { "dds2_wave", saw }, { "dds2_tune", 0.06f },
                          { "mix", 0.35f }, { "lpf", 1920.0f }, { "res", 0.1f }, { "vcf_env", 0.2f }, { "vcf_drive", 1.0f },
                          { "vca_dynamics", 0.3f } } },
        } });

    // ------------------------------------------------------------------------------------ Keys
    c.push_back ({ "Keys", "Keys",
        { { "binaural", 0.0f }, { "stereo_width", 0.55f }, { "vcf_type", ladder12 },
          { "env2_attack", 0.002f }, { "env2_decay", 2.2f }, { "env2_sustain", 0.35f }, { "env2_release", 0.5f },
          { "lpf", 3000.0f }, { "res", 0.05f }, { "vcf_env", 0.3f },
          { "env1_attack", 0.001f }, { "env1_decay", 1.2f }, { "env1_sustain", 0.2f }, { "env1_release", 0.5f },
          { "vca_dynamics", 0.7f }, { "hpf", 60.0f },
          { "chorus", chI }, { "chorus_mix", 0.35f }, { "chorus_rate", 0.6f },
          { "reverb_mix", 0.14f }, { "reverb_size", 0.6f }, { "reverb_decay", 2.2f } },
        {
            { "Amber Keys", { { "dds1_wave", sine }, { "dds2_wave", sine }, { "cross_mod", 0.03f }, { "mix", 0.08f },
                              { "env2_decay", 3.0f }, { "env2_sustain", 0.15f }, { "lpf", 6000.0f }, { "vcf_env", 0.1f } },
                            { { ModSource::env1, ModTarget::crossMod, 0.26f }, { ModSource::velocity, ModTarget::crossMod, 0.1f } } },
            { "Bellwether", { { "dds1_wave", sine }, { "dds2_wave", sine }, { "dds2_range", r4 }, { "dds2_tune", 7.0f },
                              { "cross_mod", 0.05f }, { "lpf", 8000.0f }, { "vcf_env", 0.0f }, { "env2_decay", 3.5f },
                              { "env2_sustain", 0.0f }, { "env2_release", 1.5f }, { "reverb_mix", 0.22f } },
                            { { ModSource::env1, ModTarget::crossMod, 0.25f } } },
            { "Candlelight", { { "vcf_type", ladder24 }, { "dds1_wave", saw }, { "dds2_wave", tri }, { "mix", 0.4f }, { "lpf", 1500.0f },
                               { "vcf_env", 0.45f }, { "env1_decay", 0.6f }, { "env1_sustain", 0.0f }, { "env2_decay", 3.0f },
                               { "env2_sustain", 0.0f }, { "vca_dynamics", 0.8f } } },
            { "Dusk Piano", { { "dds1_wave", square }, { "pwm_wave", 0.6f }, { "dds2_wave", saw }, { "dds2_range", r4 },
                              { "mix", 0.2f }, { "lpf", 2000.0f }, { "vcf_env", 0.4f }, { "env1_decay", 0.45f },
                              { "env1_sustain", 0.05f }, { "env2_decay", 2.8f }, { "env2_sustain", 0.0f } } },
            { "Electric Linen", { { "vcf_type", cascade24 }, { "dds1_wave", square }, { "pwm_wave", 0.5f }, { "dds2_wave", pulse }, { "pw_detune", 0.3f },
                                  { "mix", 0.5f }, { "lpf", 2500.0f }, { "vcf_env", 0.35f }, { "env1_decay", 0.5f },
                                  { "vcf_drive", 1.0f }, { "lfo1_rate", 0.62f }, { "vca_lfo1", 0.15f } } },
            { "Felt Room", { { "dds1_wave", tri }, { "dds2_wave", sine }, { "dds2_range", r2 }, { "mix", 0.1f },
                             { "lpf", 1800.0f }, { "vcf_env", 0.25f }, { "env1_decay", 0.3f }, { "env2_decay", 2.0f },
                             { "env2_sustain", 0.0f }, { "reverb_mix", 0.2f } } },
            { "Greenhouse", { { "dds1_wave", sine }, { "dds2_wave", sine }, { "dds2_range", r4 }, { "mix", 0.45f },
                              { "env2_attack", 0.003f }, { "env2_decay", 0.1f }, { "env2_sustain", 1.0f }, { "env2_release", 0.08f },
                              { "lpf", 9000.0f }, { "vcf_env", 0.0f }, { "vca_dynamics", 0.0f },
                              { "chorus", chII }, { "chorus_rate", 5.5f }, { "chorus_depth", 0.3f }, { "chorus_mix", 0.45f } } },
            { "Honeycomb", { { "dds1_wave", saw }, { "dds2_wave", pulse }, { "pw_detune", 0.7f }, { "mix", 0.5f },
                             { "hpf", 300.0f }, { "lpf", 4000.0f }, { "vcf_env", 0.4f }, { "env1_decay", 0.2f },
                             { "res", 0.3f }, { "env2_decay", 0.8f }, { "env2_sustain", 0.1f }, { "env2_release", 0.1f },
                             { "chorus", 0.0f } } },
            { "Ivory Circuit", { { "dds1_wave", alt }, { "dds1_alt_a", altWave ("DG E.Piano") }, { "dds1_alt_b", altWave ("DG Hard EP") }, { "pwm_wave", 0.7f },
                                 { "pwm_src", byEnv1 }, { "env1_decay", 0.8f }, { "dds2_wave", sine }, { "mix", 0.3f },
                                 { "lpf", 6000.0f }, { "vcf_env", 0.1f } } },
            { "Juniper", { { "dds1_wave", sine }, { "dds2_wave", sine }, { "dds2_range", r2 }, { "dds2_mode", ring },
                           { "mix", 0.2f }, { "lpf", 7000.0f }, { "vcf_env", 0.0f }, { "env2_decay", 2.5f }, { "env2_sustain", 0.1f } } },
            { "Keyframe", { { "dds1_wave", saw }, { "super_mode", half }, { "pw_detune", 0.2f }, { "lpf", 1800.0f },
                            { "vcf_env", 0.5f }, { "env1_decay", 0.35f }, { "env1_sustain", 0.1f }, { "env2_decay", 1.2f },
                            { "env2_sustain", 0.3f } } },
            { "Little Hours", { { "dds1_wave", tri }, { "dds1_range", r4 }, { "dds2_wave", sine }, { "dds2_range", r2 },
                                { "mix", 0.3f }, { "env2_decay", 1.5f }, { "env2_sustain", 0.0f }, { "env2_release", 1.0f },
                                { "lpf", 6000.0f }, { "vcf_env", 0.0f }, { "reverb_mix", 0.3f }, { "delay_mix", 0.12f },
                                { "delay_div", d8D } } },
            { "Marble Hall", { { "dds1_wave", alt }, { "dds1_alt_a", altWave ("DG Organ") }, { "dds2_wave", sine }, { "dds2_range", r16 },
                               { "mix", 0.3f }, { "vcf_drive", 1.0f }, { "env2_decay", 0.1f }, { "env2_sustain", 1.0f },
                               { "env2_release", 0.1f }, { "lpf", 7000.0f }, { "vcf_env", 0.0f }, { "vca_dynamics", 0.0f },
                               { "chorus", chII }, { "chorus_rate", 6.5f }, { "chorus_depth", 0.35f }, { "chorus_mix", 0.5f } } },
            { "Nylon Sky", { { "dds1_wave", saw }, { "lpf", 1200.0f }, { "vcf_env", 0.5f }, { "env1_decay", 0.25f },
                             { "env1_sustain", 0.0f }, { "res", 0.2f }, { "env2_decay", 1.6f }, { "env2_sustain", 0.0f },
                             { "env2_release", 0.4f }, { "vca_dynamics", 0.8f } } },
            { "Opal Keys", { { "dds1_wave", alt }, { "dds1_alt_a", altWave ("FM Half Sine") }, { "dds2_wave", sine },
                             { "dds2_tune", 0.05f }, { "cross_mod", 0.05f },
                             { "lpf", 5000.0f }, { "vcf_env", 0.1f }, { "env2_decay", 2.6f }, { "env2_sustain", 0.2f } },
                           { { ModSource::velocity, ModTarget::crossMod, 0.2f }, { ModSource::env1, ModTarget::crossMod, 0.1f } } },
            { "Paper Lantern", { { "dds1_wave", square }, { "dds2_wave", pulse }, { "pwm_wave", 0.35f }, { "pwm_src", byLfo1 },
                                 { "lfo1_rate", 0.4f }, { "mix", 0.5f }, { "lpf", 2200.0f }, { "vcf_env", 0.3f },
                                 { "vca_lfo1", 0.08f } } },
        } });

    // ------------------------------------------------------------------------------------ Plucks
    c.push_back ({ "Plucks", "Plucks",
        { { "binaural_detune", 6.0f }, { "stereo_width", 0.65f },
          { "env2_attack", 0.001f }, { "env2_decay", 0.45f }, { "env2_sustain", 0.0f }, { "env2_release", 0.3f },
          { "lpf", 500.0f }, { "res", 0.25f }, { "vcf_env", 0.55f },
          { "env1_attack", 0.001f }, { "env1_decay", 0.25f }, { "env1_sustain", 0.0f }, { "env1_release", 0.25f },
          { "vca_dynamics", 0.6f }, { "hpf", 80.0f },
          { "delay_div", d8D }, { "delay_mix", 0.18f }, { "delay_fb", 0.4f }, { "delay_pingpong", 1.0f },
          { "reverb_mix", 0.18f }, { "reverb_size", 0.7f } },
        {
            { "Apricot Wire", { { "dds1_wave", saw }, { "dds2_wave", square }, { "dds2_range", r16 }, { "mix", 0.3f } } },
            { "Bamboo Glass", { { "vcf_type", svf12 }, { "dds1_wave", sine }, { "dds2_wave", sine }, { "dds2_range", r2 }, { "mix", 0.25f },
                                { "lpf", 3000.0f }, { "vcf_env", 0.3f }, { "env2_decay", 0.35f }, { "res", 0.0f } } },
            { "Cedar Spark", { { "dds1_wave", square }, { "res", 0.4f }, { "lpf", 400.0f }, { "vcf_env", 0.6f },
                               { "env1_decay", 0.18f } } },
            { "Dry River", { { "dds1_wave", saw }, { "env2_decay", 0.15f }, { "env1_decay", 0.12f }, { "delay_mix", 0.0f },
                             { "reverb_mix", 0.1f } } },
            { "Elasticity", { { "dds1_wave", saw }, { "dds2_wave", saw }, { "dds2_tune", 0.1f }, { "mix", 0.5f },
                              { "mod_env1", 0.12f }, { "mod_dest", toBoth }, { "env1_decay", 0.05f } } },
            { "Falling Pin", { { "dds1_wave", alt }, { "dds1_alt_a", altWave ("DG Bell") }, { "lpf", 5000.0f }, { "vcf_env", 0.2f },
                               { "env2_decay", 0.9f }, { "res", 0.05f }, { "delay_div", d4T } } },
            { "Gossamer", { { "dds1_wave", saw }, { "super_mode", on }, { "pw_detune", 0.3f }, { "lpf", 700.0f },
                            { "vcf_env", 0.6f }, { "reverb_mix", 0.3f } } },
            { "Harp Engine", { { "dds1_wave", tri }, { "dds2_wave", saw }, { "dds2_range", r4 }, { "mix", 0.3f },
                               { "lpf", 1500.0f }, { "vcf_env", 0.4f }, { "env1_decay", 0.5f }, { "env2_decay", 1.5f },
                               { "env2_release", 1.0f } } },
            { "Icicle", { { "dds1_wave", saw }, { "dds2_wave", saw }, { "dds2_range", r4 }, { "dds2_mode", sync }, { "mix", 0.5f },
                          { "mod_env1", 0.15f }, { "mod_dest", toDds2 }, { "lpf", 1200.0f } } },
            { "Jasper String", { { "vcf_type", cascade24 }, { "dds1_wave", saw }, { "dds2_wave", pulse }, { "pw_detune", 0.55f }, { "mix", 0.45f },
                                  { "lpf", 600.0f }, { "vcf_env", 0.55f } } },
            { "Koto Echo", { { "dds1_wave", square }, { "pwm_wave", 0.4f }, { "dds2_wave", sine }, { "dds2_range", r4 },
                             { "dds2_mode", ring }, { "mix", 0.3f }, { "lpf", 900.0f }, { "delay_div", d4T },
                             { "delay_mix", 0.25f } } },
            { "Lightwell", { { "dds1_wave", sine }, { "dds2_wave", sine }, { "dds2_range", r4 }, { "dds2_tune", 7.0f },
                             { "cross_mod", 0.04f }, { "lpf", 4000.0f }, { "vcf_env", 0.2f }, { "res", 0.0f } },
                           { { ModSource::env1, ModTarget::crossMod, 0.3f } } },
            { "Mallet Study", { { "dds1_wave", tri }, { "dds2_wave", noise }, { "mix", 0.06f }, { "lpf", 2500.0f },
                                { "vcf_env", 0.3f }, { "env1_decay", 0.1f }, { "res", 0.0f }, { "env2_decay", 0.6f } } },
            { "Needlepoint", { { "dds1_wave", square }, { "dds1_range", r4 }, { "env2_decay", 0.12f }, { "env1_decay", 0.08f },
                               { "lpf", 1200.0f }, { "delay_div", d16 } } },
            { "Orbit Pluck", { { "dds1_wave", saw }, { "res", 0.55f }, { "lpf", 350.0f }, { "vcf_env", 0.6f },
                               { "env1_decay", 0.3f }, { "delay_div", d4D } } },
            { "Petal Fall", { { "vcf_type", svf12 }, { "dds1_wave", sine }, { "pwm_wave", 0.3f }, { "lpf", 1600.0f }, { "vcf_env", 0.35f },
                              { "env2_decay", 0.8f }, { "reverb_mix", 0.35f }, { "delay_mix", 0.2f } } },
        } });

    // ------------------------------------------------------------------------------------ Brass & Strings
    c.push_back ({ "Brass-Strings", "Brass & Strings",
        { { "binaural_detune", 8.0f }, { "stereo_width", 0.55f }, { "vcf_type", cascade24 },
          { "env2_attack", 0.35f }, { "env2_decay", 1.0f }, { "env2_sustain", 0.9f }, { "env2_release", 1.2f },
          { "lpf", 3000.0f }, { "res", 0.05f }, { "vcf_env", 0.2f },
          { "env1_attack", 0.3f }, { "env1_decay", 1.0f }, { "env1_sustain", 0.6f }, { "env1_release", 1.0f },
          { "lfo1_rate", 0.7f }, { "lfo1_delay", 0.7f }, { "mod_lfo1", 0.05f },
          { "vca_dynamics", 0.45f }, { "hpf", 100.0f }, { "drift", 0.25f },
          { "chorus", chI }, { "chorus_mix", 0.3f },
          { "reverb_mix", 0.25f }, { "reverb_size", 0.8f }, { "reverb_decay", 3.5f } },
        {
            { "Alto Horizon", { { "dds1_wave", saw }, { "super_mode", on }, { "pw_detune", 0.3f }, { "lpf", 4500.0f },
                                { "env2_attack", 0.4f }, { "chorus", chIandII } } },
            { "Brass Lantern", { { "vcf_type", ladder24 }, { "dds1_wave", saw }, { "lpf", 900.0f }, { "vcf_env", 0.45f }, { "env1_attack", 0.06f },
                                 { "env1_decay", 0.5f }, { "env1_sustain", 0.45f }, { "env2_attack", 0.03f },
                                 { "res", 0.1f }, { "vcf_drive", 1.0f }, { "reverb_mix", 0.18f }, { "chorus", 0.0f } } },
            { "Cobalt Ensemble", { { "dds1_wave", saw }, { "dds2_wave", pulse }, { "pwm_wave", 0.35f }, { "pwm_src", byLfo1 },
                                   { "lfo1_rate", 0.4f }, { "lfo1_delay", 0.0f }, { "mod_lfo1", 0.0f }, { "mix", 0.45f },
                                   { "chorus", chIandII }, { "lpf", 3500.0f }, { "env2_attack", 0.5f } } },
            { "Dawn Section", { { "vcf_type", ladder24 }, { "dds1_wave", saw }, { "dds2_wave", square }, { "dds2_range", r16 }, { "mix", 0.3f },
                                { "lpf", 700.0f }, { "vcf_env", 0.4f }, { "env1_attack", 0.15f }, { "env1_sustain", 0.55f },
                                { "env2_attack", 0.08f } } },
            { "Evergreen Bow", { { "dds1_wave", saw }, { "lpf", 2500.0f }, { "res", 0.1f }, { "env2_attack", 0.25f },
                                 { "lfo1_delay", 0.6f }, { "mod_lfo1", 0.07f }, { "hpf", 120.0f } } },
            { "Film Score", { { "dds1_wave", saw }, { "super_mode", on }, { "pw_detune", 0.35f }, { "dds2_wave", saw },
                              { "dds2_range", r16 }, { "mix", 0.3f }, { "env2_attack", 0.6f }, { "env2_release", 2.0f },
                              { "reverb_mix", 0.35f }, { "reverb_decay", 5.0f } } },
            { "Golden Fanfare", { { "vcf_type", ladder24 }, { "dds1_wave", saw }, { "super_mode", half }, { "pw_detune", 0.18f }, { "vcf_env", 0.5f },
                                  { "env1_attack", 0.04f }, { "env1_decay", 0.35f }, { "env1_sustain", 0.4f }, { "lpf", 1100.0f },
                                  { "vcf_drive", 1.0f }, { "env2_attack", 0.02f }, { "env2_decay", 0.6f }, { "env2_sustain", 0.8f },
                                  { "env2_release", 0.3f }, { "chorus", 0.0f } } },
            { "Highland Air", { { "dds1_wave", tri }, { "dds2_wave", noise }, { "mix", 0.08f }, { "env2_attack", 0.12f },
                                { "lpf", 3500.0f }, { "vcf_env", 0.1f }, { "mod_lfo1", 0.08f }, { "lfo1_delay", 0.4f },
                                { "binaural", v::x1 }, { "stereo_width", 0.4f } } },
            { "Ivory Section", { { "dds1_wave", saw }, { "lpf", 5000.0f }, { "hpf", 200.0f }, { "env2_attack", 0.2f },
                                 { "env2_release", 1.0f }, { "vcf_env", 0.0f }, { "chorus", chIandII }, { "chorus_depth", 0.8f },
                                 { "chorus_mix", 0.55f }, { "mod_lfo1", 0.0f } } },
            { "Juniper Brass", { { "vcf_type", ladder24 }, { "dds1_wave", saw }, { "dds2_wave", saw }, { "dds2_tune", 0.08f }, { "mix", 0.5f },
                                 { "vcf_env", 0.5f }, { "env1_attack", 0.08f }, { "env1_decay", 0.7f }, { "env1_sustain", 0.4f },
                                 { "lpf", 800.0f }, { "res", 0.15f }, { "env2_attack", 0.04f }, { "chorus", 0.0f } } },
            { "Kindred", { { "dds1_wave", alt }, { "dds1_alt_a", altWave ("WT Vocal A") }, { "dds1_alt_b", altWave ("WT Vocal O") }, { "pwm_wave", 1.0f },
                           { "pwm_src", byLfo1 }, { "lfo1_rate", 0.3f }, { "lfo1_delay", 0.0f }, { "mod_lfo1", 0.0f },
                           { "lpf", 4000.0f }, { "env2_attack", 0.5f }, { "chorus", chII }, { "reverb_mix", 0.35f } } },
            { "Long Arc", { { "dds1_wave", saw }, { "super_mode", on }, { "pw_detune", 0.28f }, { "env2_attack", 1.5f },
                            { "lpf", 2000.0f }, { "vcf_env", 0.25f }, { "env1_attack", 2.0f } } },
            { "Majestic Room", { { "dds1_wave", square }, { "pwm_wave", 0.3f }, { "dds2_wave", saw }, { "mix", 0.5f },
                                 { "vcf_env", 0.4f }, { "env1_attack", 0.1f }, { "env1_decay", 0.9f }, { "env1_sustain", 0.5f },
                                 { "lpf", 1000.0f }, { "env2_attack", 0.06f }, { "reverb_mix", 0.3f } } },
            { "New Century", { { "dds1_wave", saw }, { "super_mode", on }, { "pw_detune", 0.22f }, { "dds2_wave", saw },
                               { "dds2_range", r4 }, { "mix", 0.3f }, { "lpf", 6000.0f }, { "vcf_env", 0.0f },
                               { "delay_mix", 0.1f }, { "delay_div", d4 } } },
            { "Open Reeds", { { "vcf_type", svf12 }, { "dds1_wave", square }, { "pwm_wave", 0.15f }, { "lpf", 1800.0f }, { "res", 0.1f },
                              { "env2_attack", 0.05f }, { "mod_lfo1", 0.07f }, { "binaural", v::x1 }, { "stereo_width", 0.4f },
                              { "chorus", 0.0f } } },
            { "Velvet Bow", { { "dds1_wave", saw }, { "dds2_wave", tri }, { "mix", 0.4f }, { "lpf", 1800.0f },
                              { "env2_attack", 0.7f }, { "reverb_mix", 0.3f } } },
        } });

    // ------------------------------------------------------------------------------------ FX
    c.push_back ({ "FX", "FX",
        { { "binaural_detune", 10.0f }, { "stereo_width", 0.6f }, { "vcf_type", svf12 },
          { "env2_attack", 0.01f }, { "env2_decay", 1.5f }, { "env2_sustain", 0.8f }, { "env2_release", 1.5f },
          { "lpf", 4000.0f }, { "vca_dynamics", 0.3f }, { "hpf", 60.0f },
          { "delay_div", d8D }, { "delay_mix", 0.2f }, { "delay_fb", 0.5f }, { "delay_pingpong", 1.0f },
          { "reverb_mix", 0.35f }, { "reverb_size", 0.9f }, { "reverb_decay", 6.0f } },
        {
            { "Aether Engine", { { "dds1_wave", noise }, { "lpf", 900.0f }, { "res", 0.65f }, { "lfo1_rate", 0.3f },
                                 { "vcf_lfo1", 0.35f }, { "env2_attack", 1.0f }, { "env2_release", 3.0f }, { "reverb_mix", 0.45f } } },
            { "Broken Compass", { { "vcf_type", ladder24 }, { "dds1_wave", saw }, { "lpf", 600.0f }, { "res", 0.6f }, { "lfo1_wave", lSampleHold },
                                  { "lfo1_rate", 0.75f }, { "vcf_lfo1", 0.3f }, { "delay_mix", 0.25f }, { "reverb_mix", 0.3f } } },
            { "Clockwork Tide", { { "dds1_wave", sine }, { "dds2_wave", sine }, { "dds2_range", r4 }, { "dds2_tune", 5.0f },
                                  { "dds2_mode", ring }, { "mix", 0.8f }, { "env2_decay", 2.0f }, { "env2_sustain", 0.0f },
                                  { "lpf", 9000.0f }, { "delay_mix", 0.25f } } },
            { "Drift Signal", { { "dds1_wave", sine }, { "dds2_wave", tri }, { "dds2_tune", 0.2f }, { "mix", 0.5f },
                                { "drift", 1.0f }, { "binaural_detune", 20.0f }, { "env2_attack", 2.0f }, { "reverb_mix", 0.45f } } },
            { "Eventide Wire", { { "dds1_wave", saw }, { "dds2_wave", saw }, { "dds2_mode", sync }, { "mix", 0.8f },
                                 { "mod_lfo1", 0.4f }, { "mod_dest", toDds2 }, { "lfo1_rate", 0.35f }, { "lpf", 5000.0f } } },
            { "Faultline", { { "dds1_wave", sine }, { "dds1_range", r4 }, { "mod_env1", 0.45f }, { "mod_dest", toDds1 },
                             { "env1_decay", 0.3f }, { "env1_sustain", 0.0f }, { "dds2_wave", sine }, { "cross_mod", 0.2f },
                             { "env2_decay", 0.6f }, { "env2_sustain", 0.0f }, { "lpf", 8000.0f } } },
            { "Glass Weather", { { "dds1_wave", sine }, { "dds2_wave", sine }, { "dds2_range", r2 }, { "dds2_tune", 3.3f },
                                 { "cross_mod", 0.15f }, { "env2_decay", 3.0f }, { "env2_sustain", 0.0f }, { "lpf", 9000.0f },
                                 { "reverb_mix", 0.4f } },
                               { { ModSource::env1, ModTarget::crossMod, 0.3f } } },
            { "Harmonic Dust", { { "dds1_wave", tri }, { "lfo1_wave", lSampleHold }, { "lfo1_rate", 0.7f }, { "mod_lfo1", 0.3f },
                                 { "lpf", 6000.0f }, { "delay_mix", 0.3f }, { "delay_fb", 0.6f } } },
            { "Interference", { { "dds1_wave", square }, { "dds2_wave", square }, { "dds2_tune", 0.5f }, { "mix", 0.5f },
                                { "lfo1_wave", lSquare }, { "lfo1_rate", 0.55f }, { "vcf_lfo1", 0.2f }, { "lpf", 1500.0f },
                                { "res", 0.5f } } },
            { "Junction", { { "dds1_wave", saw }, { "dds2_wave", noise }, { "mix", 0.3f }, { "env1_attack", 4.0f },
                            { "env1_sustain", 1.0f }, { "vcf_env", 0.7f }, { "lpf", 150.0f }, { "res", 0.4f },
                            { "mod_env1", 0.15f }, { "env2_attack", 3.0f } } },
            { "Kepler Drift", { { "dds1_wave", alt }, { "dds1_alt_a", altWave ("PD Reso Saw") }, { "dds1_alt_b", altWave ("FM Camel Sine") }, { "pwm_wave", 1.0f },
                                { "pwm_src", byEnv1 }, { "env1_mode", loop }, { "env1_attack", 2.0f }, { "env1_decay", 2.0f },
                                { "env1_sustain", 0.3f }, { "lpf", 3000.0f }, { "env2_attack", 1.0f } } },
            { "Lunar Debris", { { "dds1_wave", saw }, { "dds2_wave", noise }, { "cross_mod", 0.3f }, { "lpf", 2000.0f },
                                { "res", 0.3f }, { "reverb_mix", 0.4f } } },
            { "Magnetic North", { { "dds1_wave", tri }, { "res", 0.92f }, { "lpf", 400.0f }, { "vcf_keytrack", on },
                                  { "vcf_env", 0.3f }, { "env1_attack", 1.5f }, { "env1_decay", 3.0f } } },
            { "Null Garden", { { "dds1_wave", tri }, { "dds2_wave", sine }, { "dds2_range", r4 }, { "mix", 0.3f },
                               { "lfo1_wave", lSampleHold }, { "lfo1_rate", 0.78f }, { "vca_lfo1", 0.8f }, { "delay_mix", 0.3f } } },
            { "Orbit Decay", { { "dds1_wave", square }, { "env2_decay", 0.1f }, { "env2_sustain", 0.0f }, { "lpf", 2500.0f },
                               { "delay_fb", 0.7f }, { "delay_mix", 0.35f } } },
            { "Phase Bloom", { { "dds1_wave", saw }, { "super_mode", on }, { "pw_detune", 0.2f }, { "chorus", chI },
                               { "chorus_rate", 0.1f }, { "chorus_depth", 0.9f }, { "chorus_delay", 2.0f },
                               { "chorus_fb", 0.8f }, { "chorus_mix", 0.5f }, { "env2_attack", 0.5f }, { "lpf", 5000.0f } } },
        } });

    return c;
}

struct PerformanceDesign
{
    const char* name;
    const char* category;           // "Layers" or "Splits"
    const char* upperCategory; const char* upperPatch;
    const char* lowerCategory; const char* lowerPatch;
    int splitPoint;                 // only for splits
};

const std::vector<PerformanceDesign> performances {
    { "Aurora Assembly",   "Layers", "Pads", "Open Sky",           "Keys", "Amber Keys",            0 },
    { "Brass Orchard",     "Layers", "Brass-Strings", "Golden Fanfare", "Brass-Strings", "Film Score", 0 },
    { "Carbon Bloom",      "Layers", "Plucks", "Gossamer",         "Pads", "Cloud Atlas",           0 },
    { "Daylight Circuit",  "Layers", "Keys", "Bellwether",         "Pads", "Jade Horizon",          0 },
    { "Echoes in Glass",   "Layers", "Plucks", "Falling Pin",      "Pads", "Distant Shore",         0 },
    { "Folding Horizon",   "Layers", "Brass-Strings", "Alto Horizon", "Pads", "Gravity Well",       0 },
    { "Gravity Garden",    "Layers", "Keys", "Electric Linen",     "Pads", "Memory Foam",           0 },
    { "Hollow Geometry",   "Layers", "Leads", "Neon Arc",          "Pads", "Hollow Moon",           0 },
    { "Ivory Current",     "Splits", "Keys", "Amber Keys",         "Bass", "Deep Marker",          55 },
    { "Jade Machinery",    "Splits", "Leads", "Arc Runner",        "Bass", "Anchor Point",         55 },
    { "Kindred Motion",    "Splits", "Brass-Strings", "Cobalt Ensemble", "Bass", "Low Orbit",      53 },
    { "Luminous Engine",   "Splits", "Plucks", "Jasper String",   "Bass", "Moraine",              52 },
    { "Marble Weather",    "Splits", "Keys", "Greenhouse",         "Bass", "Black Current",        55 },
    { "Northbound Signal", "Splits", "Leads", "Ion Trail",         "Bass", "Heavy Weather",        57 },
    { "Open Waterline",    "Splits", "Pads", "Afterglow",          "Bass", "Earthline",            55 },
    { "Prism Ritual",      "Splits", "Leads", "Edgewalker",        "Bass", "Iron Bloom",           57 },
};

//==============================================================================
void setPlain (NSynthAudioProcessor& processor, Layer layer, const char* suffix, float value)
{
    auto* parameter = processor.getState().getParameter (layerParamId (layer, suffix));
    if (parameter == nullptr)
        throw std::runtime_error ("Missing layer parameter: " + std::string (suffix));
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

float getPlain (NSynthAudioProcessor& processor, Layer layer, const char* suffix)
{
    auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (processor.getState().getParameter (layerParamId (layer, suffix)));
    return parameter->convertFrom0to1 (parameter->getValue());
}

void setPerformance (NSynthAudioProcessor& processor, const char* suffix, float value)
{
    auto* parameter = processor.getState().getParameter (perfParamId (suffix));
    if (parameter == nullptr)
        throw std::runtime_error ("Missing performance parameter: " + std::string (suffix));
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

void configureLayer (NSynthAudioProcessor& processor, Layer layer, const Category& category, const Design& design)
{
    processor.initLayer (layer);
    for (const auto* list : { &globalBase, &category.base, &design.params })
        for (const auto& p : *list)
            setPlain (processor, layer, p.suffix, p.value);
    // The super oscillator already stacks seven detuned cores; a poly patch with binaural x2 on
    // top would cost four voices per note and leave only 32 notes of polyphony.
    const bool polyphonic = getPlain (processor, layer, pid::voiceMode) >= v::poly;
    if (polyphonic && getPlain (processor, layer, pid::superMode) > 0.5f && getPlain (processor, layer, pid::binaural) > 1.5f)
        setPlain (processor, layer, pid::binaural, v::x1);

    auto& matrix = processor.getEngine().modMatrix (layer);
    for (const auto& r : design.routes)
        matrix.setFixedAmount (r.source, r.target, r.amount);
    for (const auto& d : design.direct)
        if (! matrix.setDirectAmount (d.source, d.target, d.amount))
            throw std::runtime_error ("Invalid direct route in " + std::string (design.name));
}

// Scales the VCA level until the rendered phrase reaches the target loudness. Returns the loudness.
tools::Loudness matchLoudness (NSynthAudioProcessor& processor, Layer layer, const juce::String& phrase, double targetDb)
{
    tools::Loudness loudness;
    for (int pass = 0; pass < 3; ++pass)
    {
        loudness = tools::measure (tools::renderPhrase (processor, phrase, 1.5));
        const double gainDb = juce::jmin (targetDb - loudness.momentaryMaxDb, kPeakCeilingDb - loudness.peakDb);
        const float level = getPlain (processor, layer, pid::vcaEnv);
        const float newLevel = juce::jlimit (0.03f, 1.0f, level * (float) juce::Decibels::decibelsToGain (gainDb));
        if (std::abs (gainDb) < 0.3 || newLevel == level)
            break;
        setPlain (processor, layer, pid::vcaEnv, newLevel);
    }
    return loudness;
}

juce::String fileStem (int index, const juce::String& name)
{
    return juce::String (index + 1).paddedLeft ('0', 2) + "_" + name.replaceCharacter (' ', '_');
}

bool writePatch (NSynthAudioProcessor& processor, Layer layer, const Category& category, const Design& design,
                 const juce::File& file, juce::String& error)
{
    juce::ValueTree tree ("NSYNTH_PATCH");
    tree.setProperty ("layer", toInt (layer), nullptr);
    tree.setProperty ("stateVersion", 4, nullptr);
    tree.setProperty ("factoryContent", true, nullptr);
    tree.setProperty ("name", design.name, nullptr);
    tree.setProperty ("category", category.name, nullptr);
    tree.addChild (processor.getEngine().modMatrix (layer).toValueTree (toInt (layer)), -1, nullptr);
    for (const auto& spec : getLayerParamSpecs())
    {
        auto* parameter = processor.getState().getParameter (layerParamId (layer, spec.suffix));
        if (parameter == nullptr)
        {
            error = "Could not find factory patch parameter: " + juce::String (spec.suffix);
            return false;
        }
        juce::ValueTree child ("PARAM");
        child.setProperty ("id", layerParamId (layer, spec.suffix), nullptr);
        child.setProperty ("value", parameter->getValue(), nullptr);
        tree.addChild (child, -1, nullptr);
    }

    auto xml = tree.createXml();
    if (xml == nullptr || ! file.getParentDirectory().createDirectory() || ! xml->writeTo (file))
    {
        error = "Could not write factory patch: " + file.getFullPathName();
        return false;
    }
    return true;
}

const Design& findDesign (const std::vector<Category>& categories, const char* directory, const char* name, const Category*& owner)
{
    for (const auto& category : categories)
        if (juce::String (category.directory) == directory)
            for (const auto& design : category.patches)
                if (juce::String (design.name) == name)
                {
                    owner = &category;
                    return design;
                }
    throw std::runtime_error ("Unknown patch in performance: " + std::string (name));
}

bool generateFactoryContent (const juce::File& root)
{
    const auto categories = buildCategories();
    NSynthAudioProcessor processor;
    juce::String error;

    // Start from a clean library so renamed presets do not linger.
    for (auto& old : root.findChildFiles (juce::File::findFiles, true, "*.nsynthpatch;*.nsynthperf"))
        old.deleteFile();

    // Matched VCA levels, reused by the performances.
    std::map<juce::String, float> matchedLevel;
    int patchCount = 0;
    for (const auto& category : categories)
    {
        auto directory = root.getChildFile ("Patches").getChildFile (category.directory);
        if (! directory.createDirectory())
        {
            std::cerr << "Could not create " << directory.getFullPathName() << std::endl;
            return false;
        }

        for (size_t index = 0; index < category.patches.size(); ++index)
        {
            const auto& design = category.patches[index];
            configureLayer (processor, Layer::upper, category, design);
            const auto loudness = matchLoudness (processor, Layer::upper, tools::phraseForCategory (category.name), kTargetLoudnessDb);
            const float level = getPlain (processor, Layer::upper, pid::vcaEnv);
            matchedLevel[juce::String (category.directory) + "/" + design.name] = level;
            std::cout << juce::String (category.name).paddedRight (' ', 16) << juce::String (design.name).paddedRight (' ', 18)
                      << " level " << juce::String (level, 2) << "  loudest " << juce::String (loudness.momentaryMaxDb, 1)
                      << " dB  peak " << juce::String (loudness.peakDb, 1) << " dB"
                      << (level >= 0.999f && loudness.momentaryMaxDb < kTargetLoudnessDb - 1.5 ? "   (QUIET)" : "") << std::endl;

            const auto file = directory.getChildFile (fileStem ((int) index, design.name)).withFileExtension ("nsynthpatch");
            if (! writePatch (processor, Layer::upper, category, design, file, error))
            {
                std::cerr << error << std::endl;
                return false;
            }
            ++patchCount;
        }
    }

    auto performanceDirectory = root.getChildFile ("Performances");
    if (! performanceDirectory.createDirectory())
    {
        std::cerr << "Could not create " << performanceDirectory.getFullPathName() << std::endl;
        return false;
    }
    for (size_t index = 0; index < performances.size(); ++index)
    {
        const auto& perf = performances[index];
        for (const auto& spec : getPerformanceParamSpecs())
            if (auto* parameter = processor.getState().getParameter (perfParamId (spec.suffix)))
                parameter->setValueNotifyingHost (parameter->getDefaultValue());

        const bool split = juce::String (perf.category) == "Splits";
        for (auto [layer, directory, name] : { std::tuple { Layer::upper, perf.upperCategory, perf.upperPatch },
                                               std::tuple { Layer::lower, perf.lowerCategory, perf.lowerPatch } })
        {
            const Category* owner = nullptr;
            const auto& design = findDesign (categories, directory, name, owner);
            configureLayer (processor, layer, *owner, design);
            // Two stacked layers: each a little quieter so the sum matches a single patch.
            const float level = matchedLevel[juce::String (directory) + "/" + name] * (split ? 1.0f : 0.75f);
            setPlain (processor, layer, pid::vcaEnv, level);
            // DUAL/SPLIT give each layer half of the voices; binaural x2 costs four per note.
            if (getPlain (processor, layer, pid::binaural) > 1.5f && ! split)
                setPlain (processor, layer, pid::binaural, v::x1);
        }

        setPerformance (processor, pid::kbdMode, split ? toInt (KeyboardMode::split) : toInt (KeyboardMode::dual));
        if (split)
            setPerformance (processor, pid::splitPoint, (float) perf.splitPoint);

        const auto file = performanceDirectory.getChildFile (fileStem ((int) index, perf.name)).withFileExtension ("nsynthperf");
        if (! processor.savePerformance (file, &error))
        {
            std::cerr << error << std::endl;
            return false;
        }
        // Add the display metadata the browser shows.
        if (auto xml = juce::XmlDocument::parse (file))
        {
            xml->setAttribute ("name", perf.name);
            xml->setAttribute ("category", perf.category);
            xml->setAttribute ("upperPatch", perf.upperPatch);
            xml->setAttribute ("lowerPatch", perf.lowerPatch);
            xml->removeAttribute ("presetKey");
            xml->removeAttribute ("presetName");
            xml->removeAttribute ("editorScale");
            xml->writeTo (file);
        }
        std::cout << "Performance " << perf.name << std::endl;
    }

    std::cout << "Generated " << patchCount << " factory patches and " << performances.size() << " performances in "
              << root.getFullPathName() << std::endl;
    return true;
}
} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc != 3 || juce::String (argv[1]) != "--output")
    {
        std::cerr << "Usage: nSynthFactoryGenerator --output <Presets directory>" << std::endl;
        return 2;
    }

    const auto output = juce::File::isAbsolutePath (argv[2])
                      ? juce::File (argv[2])
                      : juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]);
    try
    {
        return generateFactoryContent (output) ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << std::endl;
        return 1;
    }
}
