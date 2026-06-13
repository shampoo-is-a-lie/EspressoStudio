cmake_minimum_required(VERSION 3.22)

project(EspressoEngine VERSION 0.1.0)

add_subdirectory(libs/tracktion_engine/modules/juce ./cmake_build_juce)
add_subdirectory(libs/tracktion_engine/modules ./cmake_build_tracktion)

juce_add_console_app(EspressoEngine PRODUCT_NAME "EspressoEngine")
juce_generate_juce_header(EspressoEngine)

target_compile_features(EspressoEngine PRIVATE cxx_std_20)

# --- Neural Amp Modeler core (MIT) — compiled straight into the engine ---
set(NAM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs/NeuralAmpModelerCore)
file(GLOB NAM_SOURCES
    ${NAM_DIR}/NAM/*.cpp
    ${NAM_DIR}/NAM/wavenet/*.cpp)

target_sources(EspressoEngine PRIVATE src/Main.cpp src/NamAmpPlugin.cpp ${NAM_SOURCES})

target_include_directories(EspressoEngine PRIVATE
    ${NAM_DIR}
    ${NAM_DIR}/NAM
    ${NAM_DIR}/Dependencies/eigen
    ${NAM_DIR}/Dependencies/nlohmann)

# NAM's A2 fast-path WaveNet (matches the upstream default build)
target_compile_definitions(EspressoEngine PRIVATE NAM_ENABLE_A2_FAST=1)

target_compile_definitions(EspressoEngine PRIVATE
    JUCE_JACK=1
    JUCE_JACK_CLIENT_NAME="EspressoStudio"
    JUCE_ALSA=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_LV2=1
    JUCE_PLUGINHOST_LADSPA=1
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1)

target_link_libraries(EspressoEngine PRIVATE
    tracktion::tracktion_core
    tracktion::tracktion_engine
    tracktion::tracktion_graph
    juce::juce_audio_devices
    juce::juce_audio_utils
    juce::juce_recommended_warning_flags)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(EspressoEngine PRIVATE "-latomic")
endif()

# Copy the binary to a stable path the Electron app can find: engine/build/EspressoEngine
add_custom_command(TARGET EspressoEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:EspressoEngine> ${CMAKE_BINARY_DIR}/EspressoEngine)
