cmake_minimum_required(VERSION 3.22)

# CMake 4.x refuses projects whose dependencies ask for compatibility below 3.5. JUCE 8.0.15
# itself requires 3.22, but this keeps a stray policy error in a transitive script from
# blocking the configure on CMake 4.4+.
if(CMAKE_VERSION VERSION_GREATER_EQUAL 4.0)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.22 CACHE STRING "" FORCE)
endif()

project(VIIBEDEALER VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Static CRT on Windows. With the default dynamic runtime the plugin imports
# MSVCP140.dll / VCRUNTIME140.dll, and on any machine without the VC++ 2015-2022
# redistributable the host fails to load it -- usually with no useful error, the plugin
# simply never appears after a scan. Statically linking makes the binary self-contained,
# which is what an end user downloading a plugin needs.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Warnings)

# ---------------------------------------------------------------------------- JUCE
include(FetchContent)

set(VBD_JUCE_TAG "8.0.15" CACHE STRING "JUCE tag to build against")

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        ${VBD_JUCE_TAG}
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE)
FetchContent_MakeAvailable(JUCE)

# ---------------------------------------------------------------------------- plugin
set(VBD_FORMATS VST3 Standalone)
if(APPLE)
    # Untested from Windows -- correct by construction, unverified until built on a Mac.
    list(APPEND VBD_FORMATS AU)
endif()

juce_add_plugin(viibedealer
    PRODUCT_NAME                "VIIBEDEALER"
    COMPANY_NAME                "FLOOD"
    BUNDLE_ID                   "com.flood.viibedealer"
    PLUGIN_MANUFACTURER_CODE    Flod
    PLUGIN_CODE                 Vbdl
    FORMATS                     ${VBD_FORMATS}
    VST3_CATEGORIES             Fx Filter Modulation
    AU_MAIN_TYPE                kAudioUnitType_Effect
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    # Installing into C:\Program Files\Common Files\VST3 needs elevation, which would make
    # ordinary builds fail. Use tools/install_vst3.ps1 instead.
    COPY_PLUGIN_AFTER_BUILD     FALSE)

# ---------------------------------------------------------------------------- assets
#
# The character art is embedded, so the plugin has no runtime file dependency and nothing
# to install alongside it. Blender is only needed to REGENERATE the art
# (tools/build_assets.py); building from a clean checkout does not require it.
file(GLOB VBD_ASSET_PNGS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/layers/*.png")
set(VBD_ASSET_FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/manifest.json" ${VBD_ASSET_PNGS})

if(VBD_ASSET_PNGS)
    list(LENGTH VBD_ASSET_PNGS VBD_ASSET_COUNT)
    message(STATUS "VIIBEDEALER: embedding ${VBD_ASSET_COUNT} character layers")
else()
    message(WARNING "VIIBEDEALER: no art in assets/layers -- the character layer will be "
                    "disabled at runtime. Run: python tools/build_assets.py")
endif()

juce_add_binary_data(viibedealer_assets
    HEADER_NAME BinaryData.h
    NAMESPACE BinaryData
    SOURCES ${VBD_ASSET_FILES})

set_target_properties(viibedealer_assets PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

set(VBD_SOURCES
    src/Params.cpp
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/dsp/StftEngine.cpp
    src/dsp/SpectralEnvelope.cpp
    src/dsp/SpectralEngine.cpp
    src/dsp/ChordCarrier.cpp
    src/dsp/FractalPattern.cpp
    src/dsp/FractalSlicer.cpp
    src/dsp/SpectralDelay.cpp
    src/dsp/StereoDelay.cpp
    src/dsp/ColorStage.cpp
    src/dsp/SpectralChain.cpp
    src/state/Randomizer.cpp
    src/state/PresetManager.cpp
    src/gui/VbdLookAndFeel.cpp
    src/gui/FractalVisualizer.cpp
    src/gui/SpectrumAnalyzer.cpp
    src/gui/Panels.cpp
    src/gui/TopBar.cpp
    src/character/CharacterAssets.cpp
    src/character/CharacterLayer.cpp)

vbd_strict_sources(${VBD_SOURCES})

target_sources(viibedealer PRIVATE ${VBD_SOURCES})

target_include_directories(viibedealer PUBLIC src)

target_compile_definitions(viibedealer
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1)
# Note: JUCE_DISPLAY_SPLASH_SCREEN is deliberately not set. JUCE 8.0.15 has no splash
# screen and warns that the flag is ignored, so the old "splash unless licensed" constraint
# does not apply to this build.

# PUBLIC so the test target inherits module include paths.
target_link_libraries(viibedealer
    PUBLIC
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_gui_basics
        viibedealer_assets
    PRIVATE
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)

# ---------------------------------------------------------------------------- tests
option(VBD_BUILD_TESTS "Build the unit test runner" ON)
if(VBD_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
