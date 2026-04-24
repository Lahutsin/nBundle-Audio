#!/usr/bin/env ruby

require 'fileutils'

OUTPUT = File.expand_path('../JUCE/Source/Generated/NEQCoefficientLut.h', __dir__)

SAMPLE_RATES = [44_100.0, 48_000.0, 88_200.0, 96_000.0, 176_400.0, 192_000.0].freeze
HF_FREQS = [1500.0, 2000.0, 3000.0, 5000.0, 8000.0, 12_000.0, 16_000.0, 20_000.0].freeze
HMF_FREQS = [600.0, 800.0, 1000.0, 1500.0, 2000.0, 3000.0, 4500.0, 6000.0, 7000.0].freeze
LMF_FREQS = [200.0, 300.0, 400.0, 600.0, 800.0, 1000.0, 1500.0, 2000.0, 2500.0].freeze
LF_FREQS = [30.0, 40.0, 60.0, 100.0, 150.0, 220.0, 300.0, 400.0, 450.0].freeze
Q_VALUES = [0.1, 0.2, 0.35, 0.5, 0.8, 1.2, 1.8, 2.8, 4.0, 5.5, 7.0].freeze
GAIN_VALUES = (-180..180).map { |step| step / 10.0 }.freeze

def gain_to_linear(db)
  10.0 ** (db / 20.0)
end

def clamp(value, min_value, max_value)
  [[value, min_value].max, max_value].min
end

def lerp(start_value, end_value, amount)
  start_value + (end_value - start_value) * amount
end

def normalised_gain_drive(gain_db, black_mode)
  clamp(gain_db.abs / (black_mode ? 18.0 : 15.0), 0.0, 1.0)
end

def cap_gain(db, black_mode)
  limit = black_mode ? 18.0 : 15.0
  clamp(db, -limit, limit)
end

def map_mid_q(user_q, gain_db, black_mode, high_mid)
  base_q = clamp(user_q, 0.1, 7.0)
  drive = normalised_gain_drive(gain_db, black_mode)

  if black_mode
    q_scale = lerp(1.0, high_mid ? 1.9 : 1.7, drive)
    band_shape = high_mid ? 1.08 : 0.96
    clamp(base_q * q_scale * band_shape, 0.1, 7.0)
  else
    q_scale = lerp(0.9, high_mid ? 1.18 : 1.08, drive)
    band_shape = high_mid ? 0.97 : 0.9
    clamp(base_q * q_scale * band_shape, 0.1, 7.0)
  end
end

def high_shelf_q(gain_db, black_mode)
  drive = normalised_gain_drive(gain_db, black_mode)
  black_mode ? lerp(0.74, 0.96, drive) : lerp(0.56, 0.72, drive)
end

def low_shelf_q(gain_db, black_mode)
  drive = normalised_gain_drive(gain_db, black_mode)
  black_mode ? lerp(0.8, 1.02, drive) : lerp(0.58, 0.74, drive)
end

def high_bell_q(gain_db, black_mode)
  drive = normalised_gain_drive(gain_db, black_mode)
  black_mode ? lerp(0.92, 1.28, drive) : lerp(0.74, 0.96, drive)
end

def low_bell_q(gain_db, black_mode)
  drive = normalised_gain_drive(gain_db, black_mode)
  black_mode ? lerp(1.0, 1.4, drive) : lerp(0.82, 1.06, drive)
end

def high_shelf(sample_rate, frequency, q, gain_factor)
  a = Math.sqrt([gain_factor, 1.0e-9].max)
  aminus1 = a - 1.0
  aplus1 = a + 1.0
  omega = (2.0 * Math::PI * [frequency, 2.0].max) / sample_rate
  coso = Math.cos(omega)
  beta = Math.sin(omega) * Math.sqrt(a) / q
  aminus1_times_coso = aminus1 * coso

  [
    a * (aplus1 + aminus1_times_coso + beta),
    a * -2.0 * (aminus1 + aplus1 * coso),
    a * (aplus1 + aminus1_times_coso - beta),
    aplus1 - aminus1_times_coso + beta,
    2.0 * (aminus1 - aplus1 * coso),
    aplus1 - aminus1_times_coso - beta
  ]
end

def low_shelf(sample_rate, frequency, q, gain_factor)
  a = Math.sqrt([gain_factor, 1.0e-9].max)
  aminus1 = a - 1.0
  aplus1 = a + 1.0
  omega = (2.0 * Math::PI * [frequency, 2.0].max) / sample_rate
  coso = Math.cos(omega)
  beta = Math.sin(omega) * Math.sqrt(a) / q
  aminus1_times_coso = aminus1 * coso

  [
    a * (aplus1 - aminus1_times_coso + beta),
    a * 2.0 * (aminus1 - aplus1 * coso),
    a * (aplus1 - aminus1_times_coso - beta),
    aplus1 + aminus1_times_coso + beta,
    -2.0 * (aminus1 + aplus1 * coso),
    aplus1 + aminus1_times_coso - beta
  ]
end

def peak(sample_rate, frequency, q, gain_factor)
  a = Math.sqrt([gain_factor, 1.0e-9].max)
  omega = (2.0 * Math::PI * [frequency, 2.0].max) / sample_rate
  alpha = Math.sin(omega) / (q * 2.0)
  c2 = -2.0 * Math.cos(omega)
  alpha_times_a = alpha * a
  alpha_over_a = alpha / a

  [
    1.0 + alpha_times_a,
    c2,
    1.0 - alpha_times_a,
    1.0 + alpha_over_a,
    c2,
    1.0 - alpha_over_a
  ]
end

def high_band_coefficients(sample_rate, frequency, gain_db, bell_mode, black_mode)
  capped_gain_db = cap_gain(gain_db, black_mode)
  gain = gain_to_linear(capped_gain_db)
  return peak(sample_rate, frequency, high_bell_q(capped_gain_db, black_mode), gain) if bell_mode

  high_shelf(sample_rate, frequency, high_shelf_q(capped_gain_db, black_mode), gain)
end

def low_band_coefficients(sample_rate, frequency, gain_db, bell_mode, black_mode)
  capped_gain_db = cap_gain(gain_db, black_mode)
  gain = gain_to_linear(capped_gain_db)
  return peak(sample_rate, frequency, low_bell_q(capped_gain_db, black_mode), gain) if bell_mode

  low_shelf(sample_rate, frequency, low_shelf_q(capped_gain_db, black_mode), gain)
end

def mid_band_coefficients(sample_rate, frequency, gain_db, user_q, black_mode, high_mid)
  capped_gain_db = cap_gain(gain_db, black_mode)
  q = map_mid_q(user_q, capped_gain_db, black_mode, high_mid)
  gain = gain_to_linear(capped_gain_db)
  peak(sample_rate, frequency, q, gain)
end

def emit_biquad_line(values)
  formatted = values.map { |value| format('%.64ef', value) }.join(', ')
  "    { #{formatted} },\n"
end

FileUtils.mkdir_p(File.dirname(OUTPUT))

File.open(OUTPUT, 'w') do |file|
  file << "#pragma once\n\n"
  file << "#include <array>\n\n"
  file << "namespace NEQ::coeff\n{\n"
  file << "struct Biquad\n{\n    float b0;\n    float b1;\n    float b2;\n    float a0;\n    float a1;\n    float a2;\n};\n\n"

  {
    sampleRates: SAMPLE_RATES,
    gainValues: GAIN_VALUES,
    hfFrequencies: HF_FREQS,
    hmfFrequencies: HMF_FREQS,
    lmfFrequencies: LMF_FREQS,
    lfFrequencies: LF_FREQS,
    qValues: Q_VALUES
  }.each do |name, values|
    file << "inline constexpr std::array<float, #{values.length}> #{name} {{\n"
    file << values.each_slice(8).map { |slice| "    #{slice.map { |value| format('%.6ff', value) }.join(', ')}" }.join(",\n")
    file << "\n}};\n\n"
  end

  file << "inline constexpr size_t kSampleRateCount = #{SAMPLE_RATES.length};\n"
  file << "inline constexpr size_t kGainCount = #{GAIN_VALUES.length};\n"
  file << "inline constexpr size_t kHfFreqCount = #{HF_FREQS.length};\n"
  file << "inline constexpr size_t kMidFreqCount = #{HMF_FREQS.length};\n"
  file << "inline constexpr size_t kLfFreqCount = #{LF_FREQS.length};\n"
  file << "inline constexpr size_t kQCount = #{Q_VALUES.length};\n\n"

  hf_total = SAMPLE_RATES.length * 2 * 2 * GAIN_VALUES.length * HF_FREQS.length
  file << "inline constexpr std::array<Biquad, #{hf_total}> hfTable {{\n"
  SAMPLE_RATES.each do |sample_rate|
    [false, true].each do |black_mode|
      [false, true].each do |bell_mode|
        GAIN_VALUES.each do |gain_db|
          HF_FREQS.each do |frequency|
            file << emit_biquad_line(high_band_coefficients(sample_rate, frequency, gain_db, bell_mode, black_mode))
          end
        end
      end
    end
  end
  file << "}};\n\n"

  lf_total = SAMPLE_RATES.length * 2 * 2 * GAIN_VALUES.length * LF_FREQS.length
  file << "inline constexpr std::array<Biquad, #{lf_total}> lfTable {{\n"
  SAMPLE_RATES.each do |sample_rate|
    [false, true].each do |black_mode|
      [false, true].each do |bell_mode|
        GAIN_VALUES.each do |gain_db|
          LF_FREQS.each do |frequency|
            file << emit_biquad_line(low_band_coefficients(sample_rate, frequency, gain_db, bell_mode, black_mode))
          end
        end
      end
    end
  end
  file << "}};\n\n"

  hmf_total = SAMPLE_RATES.length * 2 * GAIN_VALUES.length * HMF_FREQS.length * Q_VALUES.length
  file << "inline constexpr std::array<Biquad, #{hmf_total}> hmfTable {{\n"
  SAMPLE_RATES.each do |sample_rate|
    [false, true].each do |black_mode|
      GAIN_VALUES.each do |gain_db|
        HMF_FREQS.each do |frequency|
          Q_VALUES.each do |q|
            file << emit_biquad_line(mid_band_coefficients(sample_rate, frequency, gain_db, q, black_mode, true))
          end
        end
      end
    end
  end
  file << "}};\n\n"

  lmf_total = SAMPLE_RATES.length * 2 * GAIN_VALUES.length * LMF_FREQS.length * Q_VALUES.length
  file << "inline constexpr std::array<Biquad, #{lmf_total}> lmfTable {{\n"
  SAMPLE_RATES.each do |sample_rate|
    [false, true].each do |black_mode|
      GAIN_VALUES.each do |gain_db|
        LMF_FREQS.each do |frequency|
          Q_VALUES.each do |q|
            file << emit_biquad_line(mid_band_coefficients(sample_rate, frequency, gain_db, q, black_mode, false))
          end
        end
      end
    end
  end
  file << "}};\n"
  file << "} // namespace NEQ::coeff\n"
end

puts "Generated #{OUTPUT}"