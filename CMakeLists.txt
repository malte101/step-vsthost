cmake_minimum_required(VERSION 3.22)

project(step-vsthost VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    message(FATAL_ERROR
        "JUCE not found!\n"
        "Clone JUCE into this repo before configuring:\n"
        "  git clone https://github.com/juce-framework/JUCE.git\n")
endif()

add_subdirectory(JUCE)

set(STEP_VSTHOST_FORMATS VST3)
set(STEP_VSTHOST_AU_ARGS)
if(APPLE)
    list(APPEND STEP_VSTHOST_FORMATS AU)
    list(APPEND STEP_VSTHOST_AU_ARGS AU_MAIN_TYPE kAudioUnitType_MusicDevice)
endif()

juce_add_plugin(step_vsthost
    COMPANY_NAME "step-vsthost"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE

    PLUGIN_MANUFACTURER_CODE StpH
    PLUGIN_CODE StVH

    FORMATS ${STEP_VSTHOST_FORMATS}
    PRODUCT_NAME "step-vsthost"

    VST3_CATEGORIES Instrument Synth
    ${STEP_VSTHOST_AU_ARGS}

    BUNDLE_ID "com.stepvsthost.plugin"
    DESCRIPTION "Step sequencer instrument host with Monome support"

    PLUGIN_CHANNEL_CONFIGURATIONS {0, 2}
)

target_sources(step_vsthost
    PRIVATE
        Source/StepHostProcessor.cpp
        Source/StepHostProcessor.h
        Source/StepHostEditor.cpp
        Source/StepHostEditor.h
        Source/StepSequencerEngine.cpp
        Source/StepSequencerEngine.h
        Source/HostedInstrumentRack.cpp
        Source/HostedInstrumentRack.h
        Source/MonomeConnection.cpp
        Source/MonomeConnection.h
        Source/StepMonomeController.cpp
        Source/StepMonomeController.h
        Source/StepSampler.h
        Source/StepSequencerDisplay.h
        Source/PlayheadSpeedQuantizer.h
)

target_compile_definitions(step_vsthost
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_REPORT_APP_USAGE=0
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        JUCE_PLUGINHOST_VST3=1
)

if(APPLE)
    target_compile_definitions(step_vsthost PRIVATE JUCE_PLUGINHOST_AU=1)
endif()

target_link_libraries(step_vsthost
    PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_data_structures
        juce::juce_dsp
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
        juce::juce_osc
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

target_include_directories(step_vsthost
    PRIVATE
        Source
)
