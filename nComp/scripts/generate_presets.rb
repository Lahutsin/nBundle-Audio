#!/usr/bin/env ruby

require 'fileutils'

PROJECT_ROOT = File.expand_path('..', __dir__)
USER_ROOT = File.expand_path('~/Music/Audio Music Apps/nComp/Presets')
REPO_ROOT = File.join(PROJECT_ROOT, 'Presets')

PRESET_TYPES = %w[STD VCA FET OPTO MU TUBE].freeze
FLOAT_KEYS = %i[input output threshold ratio attack release mix link_amount sidechain_hpf sidechain_tilt].freeze
CHOICE_KEYS = %i[link mslr auto_gain sidechain].freeze

PARAM_ORDER = %w[
  input1 input2 output1 output2 threshold1 threshold2 ratio1 ratio2 attack1 attack2
  release1 release2 mix character linkAmount sidechainHpf sidechainTilt link mslr power
  autoGain ab sidechain sidechainListen meterL meterR
].freeze

PROFILES = {
  'STD' => {
    character: 0,
    input: 0.0, output: 0.0, threshold: -12.0, ratio: 2.5, attack: 28.0, release: 170.0,
    mix: 100.0, link: 0, link_amount: 100.0, mslr: 0, auto_gain: 0, sidechain: 0,
    sidechain_hpf: 90.0, sidechain_tilt: 0.0
  },
  'VCA' => {
    character: 1,
    input: 0.4, output: 0.1, threshold: -14.0, ratio: 2.1, attack: 20.0, release: 120.0,
    mix: 100.0, link: 0, link_amount: 100.0, mslr: 0, auto_gain: 0, sidechain: 0,
    sidechain_hpf: 110.0, sidechain_tilt: 0.5
  },
  'FET' => {
    character: 2,
    input: 1.2, output: -0.4, threshold: -16.0, ratio: 4.5, attack: 7.0, release: 75.0,
    mix: 90.0, link: 0, link_amount: 72.0, mslr: 0, auto_gain: 0, sidechain: 0,
    sidechain_hpf: 135.0, sidechain_tilt: 1.5
  },
  'OPTO' => {
    character: 3,
    input: 0.0, output: 0.9, threshold: -17.0, ratio: 2.6, attack: 32.0, release: 240.0,
    mix: 100.0, link: 0, link_amount: 100.0, mslr: 0, auto_gain: 0, sidechain: 0,
    sidechain_hpf: 78.0, sidechain_tilt: -0.1
  },
  'MU' => {
    character: 4,
    input: 0.3, output: 0.6, threshold: -15.2, ratio: 2.2, attack: 22.0, release: 320.0,
    mix: 100.0, link: 0, link_amount: 100.0, mslr: 0, auto_gain: 0, sidechain: 0,
    sidechain_hpf: 82.0, sidechain_tilt: -0.7
  },
  'TUBE' => {
    character: 5,
    input: 0.9, output: 0.2, threshold: -15.8, ratio: 3.0, attack: 18.0, release: 180.0,
    mix: 100.0, link: 0, link_amount: 86.0, mslr: 0, auto_gain: 0, sidechain: 0,
    sidechain_hpf: 72.0, sidechain_tilt: -0.25
  }
}.freeze

VARIANTS = [
  { key: :mix_glue,      name: '01 Mix Bus Glue',        delta: { threshold: -2.0, ratio: -0.3, attack: 10.0, release: 40.0, sidechain_hpf: 10.0 } },
  { key: :mix_forward,   name: '02 Mix Bus Forward',     delta: { input: 0.7, threshold: -3.0, ratio: 0.5, attack: -5.0, release: -20.0, sidechain_tilt: 0.5 } },
  { key: :drum_punch,    name: '03 Drum Bus Punch',      delta: { input: 1.5, threshold: -4.0, ratio: 1.2, attack: -10.0, release: -60.0, mix: -12.0, sidechain_hpf: 25.0, sidechain_tilt: 1.0 } },
  { key: :drum_parallel, name: '04 Drum Crush Parallel', delta: { input: 3.0, threshold: -8.0, ratio: 3.0, attack: -15.0, release: -90.0, mix: -45.0, link: 1, link_amount: -40.0, sidechain_hpf: 35.0, sidechain_tilt: 1.4 } },
  { key: :vocal_level,   name: '05 Vocal Leveller',      delta: { threshold: -3.0, ratio: 0.3, attack: 5.0, release: 80.0, output: 0.8, sidechain_hpf: -10.0, sidechain_tilt: 0.3 } },
  { key: :vocal_air,     name: '06 Vocal Presence',      delta: { input: 0.8, threshold: -4.0, ratio: 0.8, attack: -4.0, release: 20.0, output: 1.0, sidechain_tilt: 1.2 } },
  { key: :bass_tight,    name: '07 Bass Tight',          delta: { input: 1.0, threshold: -4.0, ratio: 1.0, attack: -8.0, release: -20.0, sidechain_hpf: -30.0, sidechain_tilt: -0.5 } },
  { key: :bass_thick,    name: '08 Bass Thick',          delta: { input: 1.5, threshold: -5.0, ratio: 0.7, attack: 8.0, release: 70.0, output: 0.4, sidechain_tilt: -1.0 } },
  { key: :acoustic,      name: '09 Acoustic Tame',       delta: { threshold: -2.0, ratio: -0.2, attack: 15.0, release: 120.0, sidechain_hpf: 15.0, sidechain_tilt: -0.2 } },
  { key: :piano,         name: '10 Piano Round',         delta: { threshold: -3.0, ratio: 0.4, attack: 12.0, release: 150.0, output: 0.5, sidechain_hpf: -5.0 } },
  { key: :guitar_tight,  name: '11 Guitar Tight',        delta: { input: 0.9, threshold: -4.0, ratio: 1.0, attack: -6.0, release: -10.0, sidechain_tilt: 0.6 } },
  { key: :guitar_thick,  name: '12 Guitar Thick',        delta: { input: 1.2, threshold: -5.0, ratio: 0.9, attack: 2.0, release: 50.0, link: 1, link_amount: -45.0, sidechain_tilt: -0.3 } },
  { key: :keys,          name: '13 Keys Smooth',         delta: { threshold: -3.0, ratio: 0.1, attack: 20.0, release: 180.0, output: 0.3, mslr: 1 } },
  { key: :parallel,      name: '14 Parallel Energy',     delta: { input: 2.5, threshold: -7.0, ratio: 2.0, attack: -12.0, release: -70.0, mix: -60.0, sidechain_tilt: 0.9 } },
  { key: :master,        name: '15 Master Gentle',       delta: { threshold: -1.0, ratio: -0.4, attack: 20.0, release: 220.0, auto_gain: 1, sidechain_hpf: 5.0 } },
  { key: :duck,          name: '16 Sidechain Motion',    delta: { threshold: -6.0, ratio: 2.5, attack: -18.0, release: -40.0, sidechain: 1, sidechain_hpf: 40.0, sidechain_tilt: 1.0 } }
].freeze

TYPE_VARIANT_BIASES = {
  'STD' => {
    mix_glue:      { ratio: -0.2, attack: 6.0 },
    drum_punch:    { attack: 2.0, release: 10.0 },
    vocal_level:   { ratio: -0.2 },
    master:        { threshold: 0.5, ratio: -0.2 },
    duck:          { sidechain_hpf: -5.0 }
  },
  'VCA' => {
    mix_glue:      { ratio: 0.2, attack: -2.0, release: -20.0, sidechain_hpf: 8.0 },
    mix_forward:   { ratio: 0.3, attack: -4.0 },
    drum_punch:    { ratio: 0.8, attack: -5.0, release: -20.0, sidechain_hpf: 10.0 },
    master:        { ratio: 0.1, attack: -2.0, release: -30.0, auto_gain: 1 },
    keys:          { mslr: 1, ratio: -0.2 }
  },
  'FET' => {
    drum_punch:    { input: 1.2, ratio: 1.8, attack: -4.0, release: -15.0, mix: -8.0 },
    drum_parallel: { input: 2.0, ratio: 2.0, attack: -2.0, release: -20.0, mix: -10.0 },
    vocal_air:     { ratio: 1.0, attack: -2.0, release: -10.0, sidechain_tilt: 0.8 },
    guitar_tight:  { ratio: 0.8, attack: -2.0, release: -10.0 },
    parallel:      { input: 1.5, ratio: 1.5, attack: -3.0, mix: -5.0 },
    duck:          { ratio: 1.5, attack: -2.0 }
  },
  'OPTO' => {
    mix_glue:      { threshold: 1.0, ratio: -0.5, attack: 14.0, release: 70.0, sidechain_hpf: -10.0 },
    vocal_level:   { threshold: -1.5, ratio: -0.4, attack: 16.0, release: 140.0, output: 0.5 },
    vocal_air:     { threshold: -1.0, ratio: -0.3, attack: 10.0, release: 80.0, output: 0.6 },
    acoustic:      { ratio: -0.3, attack: 8.0, release: 90.0 },
    piano:         { ratio: -0.2, attack: 12.0, release: 120.0 },
    keys:          { ratio: -0.2, attack: 18.0, release: 130.0 }
  },
  'MU' => {
    mix_glue:      { threshold: 0.8, ratio: -0.4, attack: 10.0, release: 80.0, sidechain_tilt: -0.2 },
    bass_thick:    { ratio: -0.3, attack: 12.0, release: 130.0, output: 0.4 },
    piano:         { ratio: -0.3, attack: 14.0, release: 150.0 },
    guitar_thick:  { ratio: -0.2, attack: 10.0, release: 110.0 },
    master:        { threshold: 1.0, ratio: -0.5, attack: 14.0, release: 140.0, auto_gain: 1 }
  },
  'TUBE' => {
    vocal_level:   { input: 0.5, output: 0.5, threshold: -1.5, ratio: -0.1, attack: 6.0, release: 50.0, sidechain_tilt: -0.2 },
    vocal_air:     { input: 0.6, threshold: -1.0, ratio: 0.1, attack: 2.0, release: 30.0, output: 0.4 },
    bass_thick:    { input: 0.8, threshold: -1.0, ratio: 0.2, attack: 6.0, release: 60.0, sidechain_tilt: -0.4 },
    guitar_thick:  { input: 0.7, threshold: -1.2, ratio: 0.2, attack: 5.0, release: 45.0 },
    keys:          { ratio: -0.2, attack: 10.0, release: 80.0 },
    parallel:      { input: 0.8, ratio: 0.3, mix: 8.0 }
  }
}.freeze

def clamp(value, min, max)
  [[value, min].max, max].min
end

def apply_delta(settings, delta)
  delta.each do |key, value|
    if FLOAT_KEYS.include?(key)
      settings[key] = (settings[key] || 0.0) + value
    elsif CHOICE_KEYS.include?(key)
      settings[key] = value
    end
  end
end

def normalise_settings(settings)
  settings[:input] = clamp(settings[:input], -24.0, 24.0)
  settings[:output] = clamp(settings[:output], -24.0, 24.0)
  settings[:threshold] = clamp(settings[:threshold], -32.0, 0.0)
  settings[:ratio] = clamp(settings[:ratio], 2.0, 9.0)
  settings[:attack] = clamp(settings[:attack], 1.0, 640.0)
  settings[:release] = clamp(settings[:release], 5.0, 5000.0)
  settings[:mix] = clamp(settings[:mix], 0.0, 100.0)
  settings[:link_amount] = clamp(settings[:link_amount], 0.0, 100.0)
  settings[:sidechain_hpf] = clamp(settings[:sidechain_hpf], 30.0, 220.0)
  settings[:sidechain_tilt] = clamp(settings[:sidechain_tilt], -6.0, 6.0)
end

def to_attrs(settings)
  {
    'input1' => settings[:input],
    'input2' => settings[:input],
    'output1' => settings[:output],
    'output2' => settings[:output],
    'threshold1' => settings[:threshold],
    'threshold2' => settings[:threshold],
    'ratio1' => settings[:ratio],
    'ratio2' => settings[:ratio],
    'attack1' => settings[:attack],
    'attack2' => settings[:attack],
    'release1' => settings[:release],
    'release2' => settings[:release],
    'mix' => settings[:mix],
    'character' => settings[:character],
    'linkAmount' => settings[:link_amount],
    'sidechainHpf' => settings[:sidechain_hpf],
    'sidechainTilt' => settings[:sidechain_tilt],
    'link' => settings.fetch(:link, 0),
    'mslr' => settings.fetch(:mslr, 0),
    'power' => 0,
    'autoGain' => settings.fetch(:auto_gain, 0),
    'ab' => 0,
    'sidechain' => settings.fetch(:sidechain, 0),
    'sidechainListen' => 0,
    'meterL' => 0.0,
    'meterR' => 0.0
  }
end

def preset_xml(attrs)
  xml = String.new
  xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
  xml << "<ncomp_params>\n"

  PARAM_ORDER.each do |key|
    value = attrs.fetch(key)
    formatted = value.is_a?(Integer) ? value.to_s : format('%.4f', value)
    xml << "  <PARAM id=\"#{key}\" value=\"#{formatted}\"/>\n"
  end

  xml << "</ncomp_params>\n"
  xml
end

def sanitise_preset_filename(name)
  name.strip.gsub(/[[:space:]]+/, '_')
end

def write_library(root)
  FileUtils.mkdir_p(root)

  PRESET_TYPES.each do |type|
    dir = File.join(root, type)
    FileUtils.rm_rf(dir)
    FileUtils.mkdir_p(dir)

    VARIANTS.each do |variant|
      settings = PROFILES.fetch(type).dup
      apply_delta(settings, variant[:delta])
      apply_delta(settings, TYPE_VARIANT_BIASES.fetch(type, {}).fetch(variant[:key], {}))
      normalise_settings(settings)

      file_path = File.join(dir, "#{sanitise_preset_filename(variant[:name])}.ncompreset")
      File.write(file_path, preset_xml(to_attrs(settings)))
    end
  end
end

write_library(REPO_ROOT)
write_library(USER_ROOT)

total = PRESET_TYPES.sum do |type|
  Dir.glob(File.join(REPO_ROOT, type, '*.ncompreset')).size
end

puts "Generated #{total} presets into #{REPO_ROOT} and #{USER_ROOT}"