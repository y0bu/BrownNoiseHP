# ==============================================================================
#  BrownSweep - Perceptual High-Pass
#  Yoav Audio
#
#  Targets
#    brownsweep_dsp        static library, no JUCE, the whole signal chain
#    BrownSweep            VST3 + Standalone plugin (needs JUCE)
#    brownsweep_tests      unit + perceptual tests (no JUCE)
#    brownsweep_analysis   the measurement rig used to tune the design
#
#  The DSP core deliberately has no JUCE dependency, so the tests and the
#  analysis tool build and run without downloading anything.
# ==============================================================================

cmake_minimum_required(VERSION 3.22)

project(BrownSweep VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(BROWNSWEEP_BUILD_PLUGIN "Build the VST3 / Standalone plugin (requires JUCE)" ON)
option(BROWNSWEEP_BUILD_TESTS  "Build the test suite"                              ON)
option(BROWNSWEEP_BUILD_TOOLS  "Build the sweep analysis tool"                     ON)
option(BROWNSWEEP_COPY_AFTER_BUILD "Install the built plugin into the user plug-in folders" OFF)
option(BROWNSWEEP_BUILD_GUI_SNAPSHOT "Build the headless editor screenshot tool (requires JUCE)" OFF)

set(BROWNSWEEP_JUCE_PATH "" CACHE PATH
    "Path to an existing JUCE checkout. Leave empty to download JUCE with FetchContent.")
set(BROWNSWEEP_JUCE_TAG "8.0.6" CACHE STRING
    "JUCE git tag to fetch when BROWNSWEEP_JUCE_PATH is not set.")

# ------------------------------------------------------------------------------
#  DSP core - no external dependencies at all
# ------------------------------------------------------------------------------

add_library(brownsweep_dsp STATIC
    Source/dsp/BrownSweepEngine.cpp
    Source/dsp/LoudnessSchedule.cpp
    Source/dsp/Oversampler.cpp
    Source/dsp/ResponseModel.cpp)

target_include_directories(brownsweep_dsp PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Source")
target_compile_features(brownsweep_dsp PUBLIC cxx_std_17)

if(MSVC)
    target_compile_options(brownsweep_dsp PRIVATE /W4)
else()
    target_compile_options(brownsweep_dsp PRIVATE -Wall -Wextra)
endif()

# ------------------------------------------------------------------------------
#  Plugin
# ------------------------------------------------------------------------------

if(BROWNSWEEP_BUILD_PLUGIN)
    if(BROWNSWEEP_JUCE_PATH)
        message(STATUS "BrownSweep: using JUCE from ${BROWNSWEEP_JUCE_PATH}")
        add_subdirectory("${BROWNSWEEP_JUCE_PATH}" "${CMAKE_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)
    else()
        include(FetchContent)
        message(STATUS "BrownSweep: fetching JUCE ${BROWNSWEEP_JUCE_TAG}")
        FetchContent_Declare(JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG        ${BROWNSWEEP_JUCE_TAG}
            GIT_SHALLOW    TRUE)
        FetchContent_MakeAvailable(JUCE)
    endif()

    juce_add_plugin(BrownSweep
        PRODUCT_NAME              "BrownSweep"
        COMPANY_NAME              "Yoav Audio"
        COMPANY_COPYRIGHT         "Copyright (c) Yoav Audio"
        BUNDLE_ID                 "audio.yoav.brownsweep"
        PLUGIN_MANUFACTURER_CODE  Yoav
        PLUGIN_CODE               Bswp
        FORMATS                   VST3 Standalone
        DESCRIPTION               "Perceptual High-Pass"
        VST3_CATEGORIES           Fx Filter EQ
        AU_MAIN_TYPE              kAudioUnitType_Effect
        IS_SYNTH                  FALSE
        NEEDS_MIDI_INPUT          FALSE
        NEEDS_MIDI_OUTPUT         FALSE
        IS_MIDI_EFFECT            FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD   ${BROWNSWEEP_COPY_AFTER_BUILD}
        VST3_AUTO_MANIFEST        TRUE)

    target_sources(BrownSweep PRIVATE
        Source/Parameters.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/gui/LookAndFeel.cpp
        Source/gui/ResponseDisplay.cpp)

    target_compile_definitions(BrownSweep PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        JUCE_MODAL_LOOPS_PERMITTED=0)

    target_link_libraries(BrownSweep
        PRIVATE
            brownsweep_dsp
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    if(BROWNSWEEP_BUILD_GUI_SNAPSHOT)
        juce_add_console_app(brownsweep_snapshot PRODUCT_NAME "brownsweep_snapshot")

        target_sources(brownsweep_snapshot PRIVATE
            Tools/GuiSnapshot.cpp
            Source/Parameters.cpp
            Source/PluginProcessor.cpp
            Source/PluginEditor.cpp
            Source/gui/LookAndFeel.cpp
            Source/gui/ResponseDisplay.cpp)

        target_compile_definitions(brownsweep_snapshot PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_MODAL_LOOPS_PERMITTED=0
            JUCE_STRICT_REFCOUNTEDPOINTER=1
            JucePlugin_Name="BrownSweep")

        target_link_libraries(brownsweep_snapshot
            PRIVATE brownsweep_dsp juce::juce_audio_utils
            PUBLIC  juce::juce_recommended_config_flags)
    endif()
endif()

# ------------------------------------------------------------------------------
#  Tests and tools
# ------------------------------------------------------------------------------

if(BROWNSWEEP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()

if(BROWNSWEEP_BUILD_TOOLS)
    add_executable(brownsweep_analysis Tools/SweepAnalysis.cpp)
    target_link_libraries(brownsweep_analysis PRIVATE brownsweep_dsp)
    target_compile_features(brownsweep_analysis PRIVATE cxx_std_17)

    add_executable(brownsweep_demo Tools/RenderDemo.cpp)
    target_link_libraries(brownsweep_demo PRIVATE brownsweep_dsp)
    target_compile_features(brownsweep_demo PRIVATE cxx_std_17)
endif()
