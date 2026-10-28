cmake_minimum_required(VERSION 3.22)
project(SampleKeyAnalyzer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(SKA_BUILD_TESTS "Build the key-detection unit tests" ON)

add_library(SampleKeyCore STATIC KeyDetector.cpp KeyDetector.h)
target_include_directories(SampleKeyCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(SKA_BUILD_TESTS)
    enable_testing()
    add_executable(KeyDetectorTests KeyDetectorTests.cpp)
    target_link_libraries(KeyDetectorTests PRIVATE SampleKeyCore)
    add_test(NAME KeyDetectorTests COMMAND KeyDetectorTests)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(SampleKeyAnalyzer
    COMPANY_NAME "SampleKey"
    PLUGIN_MANUFACTURER_CODE Skey
    PLUGIN_CODE Skan
    FORMATS VST3
    PRODUCT_NAME "SampleKey Analyzer"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    VST3_CATEGORIES Fx Analyzer
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(SampleKeyAnalyzer PRIVATE
    PluginProcessor.cpp PluginProcessor.h
    PluginEditor.cpp PluginEditor.h)

target_compile_definitions(SampleKeyAnalyzer PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(SampleKeyAnalyzer
    PRIVATE
        SampleKeyCore
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
