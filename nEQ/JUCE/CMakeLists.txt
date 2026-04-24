cmake_minimum_required(VERSION 3.22)

project(nEQ_JUCE VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(NEQ_FETCH_JUCE "Fetch JUCE via FetchContent" ON)
option(NEQ_ENABLE_TESTS "Build nEQ JUCE tests" OFF)

include(FetchContent)

set(NEQ_PLUGIN_FORMATS VST3)

if(APPLE)
  list(APPEND NEQ_PLUGIN_FORMATS AU)
endif()

if(NEQ_FETCH_JUCE)
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

juce_add_plugin(nEQ
  VERSION ${PROJECT_VERSION}
  COMPANY_NAME "nBundle Audio"
  COMPANY_COPYRIGHT "nBundle Audio"
  BUNDLE_ID com.opensource.neq
  PLUGIN_MANUFACTURER_CODE NEEQ
  PLUGIN_CODE nEq1
  FORMATS ${NEQ_PLUGIN_FORMATS}
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD TRUE
  PRODUCT_NAME "nEQ"
  VST3_CATEGORIES "Fx" "EQ"
  AU_MAIN_TYPE kAudioUnitType_Effect
)

juce_generate_juce_header(nEQ)

set(NEQ_PRIVATE_LIBS
  juce::juce_audio_utils
  juce::juce_dsp
)

target_sources(nEQ PRIVATE
  Source/PluginProcessor.cpp
  Source/PluginProcessor.h
  Source/PluginEditor.cpp
  Source/PluginEditor.h
  Source/Parameters.h
  Source/NEQProcessor.cpp
  Source/NEQProcessor.h
)

target_compile_definitions(nEQ
  PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(nEQ
  PRIVATE
    ${NEQ_PRIVATE_LIBS}
)

if(NEQ_ENABLE_TESTS)
  enable_testing()
endif()