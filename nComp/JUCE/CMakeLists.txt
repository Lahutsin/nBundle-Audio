cmake_minimum_required(VERSION 3.22)

project(nComp_JUCE VERSION 0.8.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(NCOMP_FETCH_JUCE "Fetch JUCE via FetchContent" ON)
option(NCOMP_ENABLE_TESTS "Build nComp JUCE tests" OFF)
option(NCOMP_BUILD_ANALYSIS "Build the nComp offline analysis harness" ON)

include(FetchContent)

set(NCOMP_PLUGIN_FORMATS VST3)

if(APPLE)
  list(APPEND NCOMP_PLUGIN_FORMATS AU)
endif()

if(NCOMP_FETCH_JUCE)
  set(JUCE_GIT_TAG master)
  FetchContent_Declare(
    juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG ${JUCE_GIT_TAG}
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(juce)
else()
  find_package(juce 7 CONFIG REQUIRED)
endif()

juce_add_plugin(nComp
  VERSION ${PROJECT_VERSION}
  COMPANY_NAME "nBundle Audio"
  COMPANY_COPYRIGHT "nBundle Audio"
  BUNDLE_ID com.opensource.ncomp
  PLUGIN_MANUFACTURER_CODE NCMP
  PLUGIN_CODE nCmp
  FORMATS ${NCOMP_PLUGIN_FORMATS}
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD TRUE
  PRODUCT_NAME "nComp"
  VST3_CATEGORIES "Fx" "Dynamics"
  AU_MAIN_TYPE kAudioUnitType_Effect
)

juce_generate_juce_header(nComp)

file(GLOB_RECURSE NCOMP_RESOURCES CONFIGURE_DEPENDS Resources/img/*)

set(NCOMP_PRIVATE_LIBS
  juce::juce_audio_utils
  juce::juce_dsp
)

if(NCOMP_RESOURCES)
  juce_add_binary_data(NCompResources
    NAMESPACE NCompBinaryData
    SOURCES ${NCOMP_RESOURCES}
  )

  list(APPEND NCOMP_PRIVATE_LIBS NCompResources)
else()
  message(STATUS "NCompResources skipped: no files found under JUCE/Resources/img")
endif()

target_sources(nComp PRIVATE
  Source/PluginProcessor.cpp
  Source/PluginProcessor.h
  Source/PluginEditor.cpp
  Source/PluginEditor.h
  Source/Parameters.h
  Source/NCompProcessor.cpp
  Source/NCompProcessor.h
)

target_compile_definitions(nComp
  PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(nComp
  PRIVATE
    ${NCOMP_PRIVATE_LIBS}
)

if(NCOMP_BUILD_ANALYSIS)
  juce_add_console_app(nCompAnalysis
    PRODUCT_NAME "nCompAnalysis"
  )

  target_sources(nCompAnalysis PRIVATE
    Analysis/AnalysisMain.cpp
    Source/Parameters.h
    Source/NCompProcessor.cpp
    Source/NCompProcessor.h
  )

  target_include_directories(nCompAnalysis PRIVATE Source)

  target_compile_definitions(nCompAnalysis
    PRIVATE
      JUCE_WEB_BROWSER=0
      JUCE_USE_CURL=0
  )

  target_link_libraries(nCompAnalysis
    PRIVATE
      juce::juce_audio_processors
      juce::juce_audio_utils
      juce::juce_dsp
  )
endif()

if(NCOMP_ENABLE_TESTS)
  enable_testing()
  # Tests can be added later.
endif()
