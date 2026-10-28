cmake_minimum_required(VERSION 3.22)

project(nSynth_JUCE VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(NSYNTH_FETCH_JUCE "Fetch JUCE via FetchContent" ON)
option(NSYNTH_BUILD_TESTS "Build the nSynthTests console target" ON)

include(FetchContent)

set(NSYNTH_PLUGIN_FORMATS VST3)
if(APPLE)
  list(APPEND NSYNTH_PLUGIN_FORMATS AU)
endif()

if(NSYNTH_FETCH_JUCE)
  # Pinned for reproducible builds (was "master" in v0.1, see docs/08-PLAN-REVIEW.md).
  FetchContent_Declare(
    juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.4
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(juce)
else()
  find_package(juce 9 CONFIG REQUIRED)
endif()

# DSP/engine sources shared by the plug-in and the test runner. JUCE modules are
# interface libraries, so the core is compiled into each target instead of being
# a separate static library (which would duplicate JUCE symbols).
set(NSYNTH_CORE_SOURCES
  Source/Core/Params/Enums.h
  Source/Core/Params/ParamRegistry.h
  Source/Core/Params/ParamRegistry.cpp
  Source/Core/DSP/Wavetables.h
  Source/Core/DSP/Wavetables.cpp
  Source/Core/DSP/Dds1.h
  Source/Core/DSP/Dds1.cpp
  Source/Core/DSP/Dds2.h
  Source/Core/DSP/Dds2.cpp
  Source/Core/DSP/Envelope.h
  Source/Core/DSP/Envelope.cpp
  Source/Core/DSP/Lfo.h
  Source/Core/DSP/Lfo.cpp
  Source/Core/DSP/VoiceFilter.h
  Source/Core/DSP/VoiceFilter.cpp
  Source/Core/DSP/Effects.h
  Source/Core/DSP/Effects.cpp
  Source/Core/Engine/LayerSnapshot.h
  Source/Core/Engine/LayerSnapshot.cpp
  Source/Core/Engine/Voice.h
  Source/Core/Engine/Voice.cpp
  Source/Core/Engine/VoiceRenderPool.h
  Source/Core/Engine/VoiceRenderPool.cpp
  Source/Core/Engine/ModMatrix.h
  Source/Core/Engine/ModMatrix.cpp
  Source/Core/Engine/SynthLayer.h
  Source/Core/Engine/SynthLayer.cpp
  Source/Core/Engine/SynthEngine.h
  Source/Core/Engine/SynthEngine.cpp
  Source/PresetManager.h
  Source/PresetManager.cpp
  Source/PresetLibrary.h
  Source/PresetLibrary.cpp
)

# Factory patches and performances are compiled into the binary, so every plug-in format
# finds them regardless of where the host or installer puts the bundle.
file(GLOB_RECURSE NSYNTH_FACTORY_PRESETS CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/../Presets/*.nsynthpatch"
  "${PROJECT_SOURCE_DIR}/../Presets/*.nsynthperf"
)
juce_add_binary_data(nSynthPresetData
  HEADER_NAME NSynthPresetData.h
  NAMESPACE NSynthPresetData
  SOURCES ${NSYNTH_FACTORY_PRESETS}
)

set(NSYNTH_DEFINITIONS
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
)

juce_add_plugin(nSynth
  VERSION ${PROJECT_VERSION}
  COMPANY_NAME "nBundle Audio"
  COMPANY_COPYRIGHT "nBundle Audio"
  BUNDLE_ID com.opensource.nsynth
  PLUGIN_MANUFACTURER_CODE NSYA
  PLUGIN_CODE nSyn
  FORMATS ${NSYNTH_PLUGIN_FORMATS}
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD TRUE
  PRODUCT_NAME "nSynth"
  VST3_CATEGORIES "Instrument" "Synth"
  AU_MAIN_TYPE kAudioUnitType_MusicDevice
)

juce_generate_juce_header(nSynth)

target_sources(nSynth PRIVATE
  ${NSYNTH_CORE_SOURCES}
  Source/PluginProcessor.h
  Source/PluginProcessor.cpp
  Source/PluginEditor.h
  Source/PluginEditor.cpp
  Source/PresetBrowser.h
  Source/PresetBrowser.cpp
  Source/UiPalette.h
)

target_include_directories(nSynth PRIVATE Source)
target_compile_definitions(nSynth PRIVATE ${NSYNTH_DEFINITIONS})
target_link_libraries(nSynth
  PRIVATE
    nSynthPresetData
    juce::juce_audio_utils
    juce::juce_dsp
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

function(nsynth_add_engine_tool target source_file)
  juce_add_console_app(${target} PRODUCT_NAME "${target}")
  target_sources(${target} PRIVATE
    ${NSYNTH_CORE_SOURCES}
    Source/PluginProcessor.h
    Source/PluginProcessor.cpp
    Source/PluginEditor.h
    Source/PluginEditor.cpp
    Source/PresetBrowser.h
    Source/PresetBrowser.cpp
    Source/UiPalette.h
    ${source_file}
  )
  target_include_directories(${target} PRIVATE Source)
  target_compile_definitions(${target} PRIVATE ${NSYNTH_DEFINITIONS})
  target_link_libraries(${target}
    PRIVATE
      nSynthPresetData
      juce::juce_audio_utils
      juce::juce_dsp
      juce::juce_recommended_config_flags
      juce::juce_recommended_warning_flags
  )
  set_target_properties(${target} PROPERTIES EXCLUDE_FROM_ALL TRUE)
endfunction()

nsynth_add_engine_tool(nSynthFactoryGenerator Source/Tools/FactoryContentGenerator.cpp)
nsynth_add_engine_tool(nSynthAnalysis Source/Analysis/AnalysisMain.cpp)
nsynth_add_engine_tool(nSynthRender Source/Tools/RenderTool.cpp)

if(NSYNTH_BUILD_TESTS)
  juce_add_console_app(nSynthTests PRODUCT_NAME "nSynthTests")

  target_sources(nSynthTests PRIVATE
    ${NSYNTH_CORE_SOURCES}
    Source/PluginProcessor.h
    Source/PluginProcessor.cpp
    Source/PluginEditor.h
    Source/PluginEditor.cpp
    Source/PresetBrowser.h
    Source/PresetBrowser.cpp
    Source/UiPalette.h
    Source/Tests/TestHelpers.h
    Source/Tests/TestMain.cpp
    Source/Tests/ParamTests.cpp
    Source/Tests/WavetableTests.cpp
    Source/Tests/OscillatorTests.cpp
    Source/Tests/EnvelopeTests.cpp
    Source/Tests/LfoTests.cpp
    Source/Tests/FilterTests.cpp
    Source/Tests/EffectsTests.cpp
    Source/Tests/EngineTests.cpp
    Source/Tests/ModMatrixTests.cpp
    Source/Tests/MidiInputTests.cpp
    Source/Tests/PluginTests.cpp
    Source/Tests/FactoryPresetTests.cpp
  )

  target_include_directories(nSynthTests PRIVATE Source)
  target_compile_definitions(nSynthTests PRIVATE
    ${NSYNTH_DEFINITIONS}
    NSYNTH_TESTS=1
    NSYNTH_FACTORY_PRESETS_DIR="${PROJECT_SOURCE_DIR}/../Presets"
  )
  target_link_libraries(nSynthTests
    PRIVATE
      nSynthPresetData
      juce::juce_audio_utils
      juce::juce_dsp
      juce::juce_recommended_config_flags
      juce::juce_recommended_warning_flags
  )

  enable_testing()
  add_test(NAME nSynthTests COMMAND nSynthTests)
endif()
