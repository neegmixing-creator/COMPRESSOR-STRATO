cmake_minimum_required(VERSION 3.22)
project(CompressorPlugin VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_PLUGIN "Compilar o plugin VST3 (baixa o JUCE via FetchContent)" ON)
option(BUILD_TESTS  "Compilar os testes do núcleo DSP (sem JUCE)" ON)

# ---- Núcleo DSP: C++ puro, sem JUCE (testável isoladamente) ------------------------------
add_library(CompressorCore STATIC
    Source/CompressorEngine.cpp Source/Detector.cpp Source/Saturation.cpp
    Source/Oversampler.cpp Source/Meter.cpp)
target_include_directories(CompressorCore PUBLIC Source)
if(MSVC)
    target_compile_options(CompressorCore PRIVATE /W4)       # sem /fp:fast: precisamos de isfinite confiável
else()
    target_compile_options(CompressorCore PRIVATE -Wall -Wextra)
endif()

# ---- Testes ---------------------------------------------------------------------------------
if(BUILD_TESTS)
    enable_testing()
    add_executable(CompressorTests Tests/CompressorTests.cpp)
    target_link_libraries(CompressorTests PRIVATE CompressorCore)
    add_test(NAME dsp_core COMMAND CompressorTests)
endif()

# ---- Plugin VST3 ---------------------------------------------------------------------------
if(BUILD_PLUGIN)
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.6
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)

    juce_add_plugin(CompressorPlugin
        COMPANY_NAME "YourCompany"
        PRODUCT_NAME "Strata"
        PLUGIN_MANUFACTURER_CODE Yrco
        PLUGIN_CODE Strt
        FORMATS VST3
        VST3_CATEGORIES Fx Dynamics
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE)      # TRUE copia para C:/Program Files/Common Files/VST3 (exige admin)

    target_sources(CompressorPlugin PRIVATE
        Source/PluginProcessor.cpp Source/PluginEditor.cpp
        Source/CustomLookAndFeel.cpp Source/StrengthKnob.cpp Source/MeterViews.cpp
        Source/TransferCurve.cpp Source/AdvancedPanel.cpp)

    target_compile_definitions(CompressorPlugin PUBLIC
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(CompressorPlugin
        PRIVATE CompressorCore juce::juce_audio_utils
        PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
