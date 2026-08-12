cmake_minimum_required(VERSION 3.22)

# Universal macOS binary, same as the rest of the workspace: some hosts here run
# under Rosetta.
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()

project(Edge VERSION 0.12.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE lives outside this tree - the same ~/JUCE checkout (JUCE 9) every plugin
# in this workspace uses. CI would check JUCE out into ./JUCE instead.
if(DEFINED ENV{HOME} AND EXISTS "$ENV{HOME}/JUCE/CMakeLists.txt")
    set(EDGE_JUCE_DIR "$ENV{HOME}/JUCE")
elseif(DEFINED ENV{USERPROFILE} AND EXISTS "$ENV{USERPROFILE}/JUCE/CMakeLists.txt")
    set(EDGE_JUCE_DIR "$ENV{USERPROFILE}/JUCE")
else()
    set(EDGE_JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/JUCE")
endif()
add_subdirectory(${EDGE_JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)

# --- build identity ------------------------------------------------------------
# The version alone does not identify a build - three of them can call themselves
# 0.7.0. The git description does, so it is compiled in and shown in the UI.
set(EDGE_VERSION "${PROJECT_VERSION}")

find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" describe --tags --always --dirty
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE EDGE_GIT_DESCRIBE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
endif()

if(NOT EDGE_GIT_DESCRIBE)
    # A source zip has no .git. Say so rather than inventing a hash.
    set(EDGE_GIT_DESCRIBE "no-git")
endif()

string(TIMESTAMP EDGE_BUILD_DATE "%Y-%m-%d" UTC)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/Core/EdgeVersion.h.in"
    "${CMAKE_BINARY_DIR}/generated/EdgeVersion.h"
    @ONLY)

message(STATUS "EDGE ${EDGE_VERSION} (${EDGE_GIT_DESCRIBE}, ${EDGE_BUILD_DATE})")

if(APPLE)
    set(EDGE_FORMATS VST3 AU Standalone)
else()
    set(EDGE_FORMATS VST3 Standalone)
endif()

option(EDGE_COPY_AFTER_BUILD "Copy built plug-ins into the user plug-in folders" ON)
option(EDGE_WERROR "Treat warnings in EDGE's own sources as errors" OFF)
option(EDGE_SANITIZERS "Add an EdgeTestsSan target built with ASan/UBSan" OFF)

# Warning flags are attached to EDGE's own translation units only: JUCE's module
# sources compile into our targets and we do not own their warnings.
if(MSVC)
    # /utf-8 is not optional insurance: without it MSVC reads the source in the
    # machine's ANSI codepage, so the first non-ASCII character anyone types into
    # a comment or a juce::String becomes a Windows-only compile error.
    set(EDGE_WARNING_FLAGS /W4 /permissive- /utf-8)
    if(EDGE_WERROR)
        list(APPEND EDGE_WARNING_FLAGS /WX)
    endif()
else()
    set(EDGE_WARNING_FLAGS
        -Wall -Wextra -Wpedantic -Wshadow -Wcast-align -Wunused
        -Woverloaded-virtual -Wnon-virtual-dtor -Wimplicit-fallthrough)

    # MSVC lives on the other machine, so these stand in for the two /W4
    # warnings that actually fire on DSP code: C4244 (double -> float) and
    # C4267 (size_t -> int). Catching them here is the only way this build can
    # say anything true about the Windows one.
    list(APPEND EDGE_WARNING_FLAGS -Wfloat-conversion)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        list(APPEND EDGE_WARNING_FLAGS -Wshorten-64-to-32)
    endif()

    if(EDGE_WERROR)
        list(APPEND EDGE_WARNING_FLAGS -Werror)
    endif()
endif()

# The DSP, shared by the plug-in and the tools. Source/Vendor is FOUR COLOR's
# saturation engine, copied byte-identical - see Source/Vendor/VENDOR.md. It is
# deliberately NOT given EDGE_WARNING_FLAGS: it is not our code to warn about.
set(EDGE_VENDOR_SOURCES
    Source/Vendor/FourColor/Dsp/ColorEngine.cpp)

set(EDGE_DSP_SOURCES
    Source/Dsp/ColorStage.cpp
    Source/Dsp/EdgeEngine.cpp)

set(EDGE_PLUGIN_SOURCES
    ${EDGE_DSP_SOURCES}
    Source/Core/Parameters.cpp
    Source/PluginEditor.cpp
    Source/PluginProcessor.cpp
    Source/Ui/CurveView.cpp
    Source/Ui/ShapePanel.cpp
    Source/Ui/Theme.cpp)

set(EDGE_OWN_SOURCES ${EDGE_PLUGIN_SOURCES} Source/Tools/test_dsp.cpp
    Source/Tools/test_host.cpp Source/Tools/screenshot_tool.cpp)
set_source_files_properties(${EDGE_OWN_SOURCES}
    PROPERTIES COMPILE_OPTIONS "${EDGE_WARNING_FLAGS}")

set(EDGE_COMMON_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

# --- the plug-in ---------------------------------------------------------------
juce_add_plugin(Edge
    COMPANY_NAME "Naaman"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${EDGE_COPY_AFTER_BUILD}
    PLUGIN_MANUFACTURER_CODE Naam
    PLUGIN_CODE Edge
    FORMATS ${EDGE_FORMATS}
    # Without an explicit subcategory a VST3 declares only the generic "Fx" and
    # Cubase files it under Other rather than Filter.
    VST3_CATEGORIES "Fx" "Filter" "EQ"
    AU_MAIN_TYPE kAudioUnitType_Effect
    VERSION "${EDGE_VERSION}"
    PRODUCT_NAME "EDGE")

target_sources(Edge PRIVATE ${EDGE_PLUGIN_SOURCES} ${EDGE_VENDOR_SOURCES})
target_include_directories(Edge PUBLIC "${CMAKE_BINARY_DIR}/generated")
target_compile_definitions(Edge PUBLIC ${EDGE_COMMON_DEFS})
target_link_libraries(Edge
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# --- DSP measurement suite -----------------------------------------------------
#     build/EdgeTests_artefacts/<config>/EdgeTests
#
# Links only the DSP: no editor, no plug-in wrapper, so a UI mistake cannot stop
# the measurements from running.
juce_add_console_app(EdgeTests PRODUCT_NAME "EdgeTests")
juce_generate_juce_header(EdgeTests)
target_sources(EdgeTests PRIVATE
    Source/Tools/test_dsp.cpp
    Source/Core/Parameters.cpp
    ${EDGE_DSP_SOURCES}
    ${EDGE_VENDOR_SOURCES})
target_include_directories(EdgeTests PUBLIC "${CMAKE_BINARY_DIR}/generated")
target_compile_definitions(EdgeTests PUBLIC
    ${EDGE_COMMON_DEFS}
    JUCE_STANDALONE_APPLICATION=1)
target_link_libraries(EdgeTests
    PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags)

# --- sanitiser build of the suite (opt-in) -------------------------------------
#     cmake -B build-san -DEDGE_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
if(EDGE_SANITIZERS)
    juce_add_console_app(EdgeTestsSan PRODUCT_NAME "EdgeTestsSan")
    juce_generate_juce_header(EdgeTestsSan)
    target_sources(EdgeTestsSan PRIVATE
        Source/Tools/test_dsp.cpp
        Source/Core/Parameters.cpp
        ${EDGE_DSP_SOURCES}
        ${EDGE_VENDOR_SOURCES})
    target_include_directories(EdgeTestsSan PUBLIC "${CMAKE_BINARY_DIR}/generated")
target_compile_definitions(EdgeTestsSan PUBLIC
        ${EDGE_COMMON_DEFS}
        JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(EdgeTestsSan
        PRIVATE juce::juce_audio_utils juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags)

    if(MSVC)
        target_compile_options(EdgeTestsSan PRIVATE /fsanitize=address)
    else()
        target_compile_options(EdgeTestsSan PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
        target_link_options(EdgeTestsSan PRIVATE -fsanitize=address,undefined)
    endif()

    # The host suite under the same sanitisers. It is the only target that
    # builds and destroys the EDITOR, so it is the only one that can catch a
    # dangling listener or a timer that outlives its component.
    juce_add_console_app(EdgeHostTestsSan PRODUCT_NAME "EdgeHostTestsSan")
    juce_generate_juce_header(EdgeHostTestsSan)
    target_sources(EdgeHostTestsSan PRIVATE
        Source/Tools/test_host.cpp
        ${EDGE_PLUGIN_SOURCES}
        ${EDGE_VENDOR_SOURCES})
    target_include_directories(EdgeHostTestsSan PUBLIC "${CMAKE_BINARY_DIR}/generated")
target_compile_definitions(EdgeHostTestsSan PUBLIC
        ${EDGE_COMMON_DEFS}
        JUCE_STANDALONE_APPLICATION=1
        JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(EdgeHostTestsSan
        PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_gui_extra
        PUBLIC juce::juce_recommended_config_flags)

    if(MSVC)
        target_compile_options(EdgeHostTestsSan PRIVATE /fsanitize=address)
    else()
        target_compile_options(EdgeHostTestsSan PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
        target_link_options(EdgeHostTestsSan PRIVATE -fsanitize=address,undefined)
    endif()
endif()

# --- host-contract suite -------------------------------------------------------
#     build/EdgeHostTests_artefacts/<config>/EdgeHostTests
#
# EdgeTests measures the DSP. This one drives the whole AudioProcessor the way a
# host does - parameters, prepare/process lifecycle, state, editor - so it needs
# the editor linked in, unlike EdgeTests.
juce_add_console_app(EdgeHostTests PRODUCT_NAME "EdgeHostTests")
juce_generate_juce_header(EdgeHostTests)
target_sources(EdgeHostTests PRIVATE
    Source/Tools/test_host.cpp
    ${EDGE_PLUGIN_SOURCES}
    ${EDGE_VENDOR_SOURCES})
target_include_directories(EdgeHostTests PUBLIC "${CMAKE_BINARY_DIR}/generated")
target_compile_definitions(EdgeHostTests PUBLIC
    ${EDGE_COMMON_DEFS}
    JUCE_STANDALONE_APPLICATION=1
    # The editor checks pump the message loop so the analyser's timer actually
    # runs while audio is being processed.
    JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(EdgeHostTests
    PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_gui_extra
    PUBLIC juce::juce_recommended_config_flags)

# --- deterministic UI renderer -------------------------------------------------
#     build/EdgeShot_artefacts/<config>/EdgeShot ui-shots
juce_add_console_app(EdgeShot PRODUCT_NAME "EdgeShot")
juce_generate_juce_header(EdgeShot)
target_sources(EdgeShot PRIVATE
    Source/Tools/screenshot_tool.cpp
    ${EDGE_PLUGIN_SOURCES}
    ${EDGE_VENDOR_SOURCES})
target_include_directories(EdgeShot PUBLIC "${CMAKE_BINARY_DIR}/generated")
target_compile_definitions(EdgeShot PUBLIC
    ${EDGE_COMMON_DEFS}
    JUCE_STANDALONE_APPLICATION=1
    JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(EdgeShot
    PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_gui_extra
    PUBLIC juce::juce_recommended_config_flags)
