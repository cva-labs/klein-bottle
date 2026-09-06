cmake_minimum_required(VERSION 3.22)

project(KleinBottleSynth VERSION 1.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Use an existing JUCE installation/release (override with -DJUCE_PATH=<path>)
set(JUCE_PATH "C:/JUCE" CACHE PATH "Path to an existing JUCE checkout or release")
if (NOT EXISTS "${JUCE_PATH}/CMakeLists.txt")
    message(FATAL_ERROR "JUCE not found at \"${JUCE_PATH}\". Configure with -DJUCE_PATH=<path to JUCE>")
endif()

add_subdirectory("${JUCE_PATH}" JUCE)

# --------------------------------------------------------------------------------------
# Klein Bottle Experimental (v1.1) by CVA Labs
# --------------------------------------------------------------------------------------
set(KLEIN_STANDALONE_ICON "${CMAKE_CURRENT_SOURCE_DIR}/klein_icon2.png")
set(_klein_icon_args)
if (EXISTS "${KLEIN_STANDALONE_ICON}")
    list(APPEND _klein_icon_args
        ICON_BIG "${KLEIN_STANDALONE_ICON}"
        ICON_SMALL "${KLEIN_STANDALONE_ICON}"
    )
    message(STATUS "Standalone icon set from: ${KLEIN_STANDALONE_ICON}")
else()
    message(WARNING "Standalone icon not found: ${KLEIN_STANDALONE_ICON}")
endif()

juce_add_plugin(KleinBottle
    COMPANY_NAME "CVA Labs"
    PLUGIN_MANUFACTURER_CODE CvAl
    PLUGIN_CODE KBEx
    BUNDLE_ID com.cvalabs.kleinbottleexperimental
    FORMATS VST3 Standalone
    PRODUCT_NAME "Klein Bottle Experimental"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Synth
    ${_klein_icon_args}
)

# --- CVA Labs logo embedded as a binary resource -------------------------------------
file (GLOB _logo_candidates "${CMAKE_CURRENT_SOURCE_DIR}/*.png")
list (SORT _logo_candidates)
list (LENGTH _logo_candidates _logo_count)
if (_logo_count GREATER 0)
    list (GET _logo_candidates 0 _logo_src)
    file (MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/assets")
    file (COPY_FILE "${_logo_src}" "${CMAKE_CURRENT_BINARY_DIR}/assets/logo.png")
    juce_add_binary_data(KleinAssets SOURCES "${CMAKE_CURRENT_BINARY_DIR}/assets/logo.png")
    target_link_libraries(KleinBottle PRIVATE KleinAssets)
    message (STATUS "CVA Labs logo embedded from: ${_logo_src}")
else()
    message (WARNING "No logo PNG found in the project root - GUI falls back to text title")
endif()

target_sources(KleinBottle PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/KleinVisualizer.cpp
)

# --- Klein mesh core ------------------------------------------------------------------
# Compiled once, shared by the plugin and the headless test/bench tools.  The fast
# FP model lets the compiler auto-vectorise the mesh sweeps (see KleinMesh::step);
# the NaN/Inf safety net uses a bit-level finiteness test so it survives fast-math.
# AVX2 needs ~2013+ CPUs (Haswell or later).
add_library(KleinMeshCore STATIC Source/KleinMesh.cpp)
target_compile_features(KleinMeshCore PRIVATE cxx_std_20)
if (MSVC)
    target_compile_options(KleinMeshCore PRIVATE /fp:fast /arch:AVX2)
else()
    target_compile_options(KleinMeshCore PRIVATE -ffast-math)
endif()

target_compile_definitions(KleinBottle PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(KleinBottle
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        KleinMeshCore
    PUBLIC
        juce::juce_recommended_config_flags
)

# --------------------------------------------------------------------------------------
# Headless DSP tests & benchmark (no JUCE dependency - KleinMesh is pure C++)
# --------------------------------------------------------------------------------------
add_executable(KleinMeshTests
    Tests/KleinMeshTests.cpp
)
target_compile_features(KleinMeshTests PRIVATE cxx_std_20)
target_link_libraries(KleinMeshTests PRIVATE KleinMeshCore)

add_executable(KleinMeshBench
    Tests/KleinMeshBench.cpp
)
target_compile_features(KleinMeshBench PRIVATE cxx_std_20)
target_link_libraries(KleinMeshBench PRIVATE KleinMeshCore)

enable_testing()
add_test(NAME KleinMeshTests COMMAND KleinMeshTests)