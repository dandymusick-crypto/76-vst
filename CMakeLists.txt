cmake_minimum_required(VERSION 3.22)
project(Dandy76 VERSION 1.0.0)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.3
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(Dandy76
    COMPANY_NAME "Dandy Musica"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Dndy
    PLUGIN_CODE Dn76
    FORMATS VST3
    PRODUCT_NAME "Dandy 76"
)

juce_generate_juce_header(Dandy76)

juce_add_binary_data(Dandy76Data
    SOURCES
        Gemini_Generated_Image_y3klsty3klsty3kl.jfif
)

target_sources(Dandy76
    PRIVATE
        Plugin.cpp
)

target_compile_definitions(Dandy76
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(Dandy76
    PRIVATE
        Dandy76Data
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
