cmake_minimum_required(VERSION 3.22)
project(VegaSaturator VERSION 1.0.0)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

if(DEFINED JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.9 GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
endif()

set(VEGA_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND VEGA_FORMATS AU)
endif()

juce_add_plugin(VegaSaturator
    COMPANY_NAME "Vega Productions"
    BUNDLE_ID com.vegaproductions.saturator
    PLUGIN_MANUFACTURER_CODE Vgap
    PLUGIN_CODE Vsa1
    FORMATS ${VEGA_FORMATS}
    PRODUCT_NAME "Vega Productions Saturator"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Fx Distortion
    AU_MAIN_TYPE kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(VegaSaturator PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_compile_definitions(VegaSaturator PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(VegaSaturator
    PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

option(VEGA_TESTS "Build offline test" OFF)
if(VEGA_TESTS)
    juce_add_console_app(VegaSaturatorTest PRODUCT_NAME "VegaSaturatorTest")
    target_sources(VegaSaturatorTest PRIVATE tests/SaturatorTest.cpp)
    target_include_directories(VegaSaturatorTest PRIVATE $<TARGET_PROPERTY:VegaSaturator,INCLUDE_DIRECTORIES>)
    target_compile_definitions(VegaSaturatorTest PRIVATE $<TARGET_PROPERTY:VegaSaturator,COMPILE_DEFINITIONS>)
    target_link_libraries(VegaSaturatorTest PRIVATE VegaSaturator)
endif()
