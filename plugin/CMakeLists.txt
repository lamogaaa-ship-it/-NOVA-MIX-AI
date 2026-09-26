# NOVA MIX AI - plugin target (VST3 + AU on macOS + Standalone)

set(NOVA_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND NOVA_FORMATS AU)
endif()

set(NOVA_HOST_AU FALSE)
if(APPLE AND NOVA_ENABLE_HOSTING)
    set(NOVA_HOST_AU TRUE)
endif()

juce_add_plugin(NovaMixAI
    PRODUCT_NAME "NOVA MIX AI"
    VERSION ${PROJECT_VERSION}
    COMPANY_NAME "NOVA Audio"
    COMPANY_WEBSITE "https://novamix.ai"
    BUNDLE_ID "ai.novamix.novamixai"
    PLUGIN_MANUFACTURER_CODE Nvax
    PLUGIN_CODE Nmix
    FORMATS ${NOVA_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Dynamics EQ Mastering
    AU_MAIN_TYPE kAudioUnitType_Effect
    AU_EXPORT_PREFIX NovaMixAIAU
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "NOVA MIX AI uses the microphone for voice commands and standalone audio input."
    HARDENED_RUNTIME_ENABLED TRUE
    # disable-library-validation: the Standalone app hosts third-party plug-ins in its rack
    HARDENED_RUNTIME_OPTIONS com.apple.security.device.audio-input com.apple.security.network.client
                             com.apple.security.cs.disable-library-validation
    PLUGINHOST_AU ${NOVA_HOST_AU}
    NEEDS_CURL TRUE
    NEEDS_WEB_BROWSER FALSE)

file(GLOB_RECURSE NOVA_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h)
target_sources(NovaMixAI PRIVATE ${NOVA_SOURCES})
target_include_directories(NovaMixAI PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Embedded resources: fonts (OFL) + knowledge base used by the AI engineer offline.
file(GLOB NOVA_FONT_FILES ${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/*.ttf)
file(GLOB NOVA_KNOWLEDGE_FILES ${CMAKE_SOURCE_DIR}/shared/knowledge/*.md)
juce_add_binary_data(NovaBinaryData
    NAMESPACE NovaBinary
    HEADER_NAME NovaBinaryData.h
    SOURCES ${NOVA_FONT_FILES} ${NOVA_KNOWLEDGE_FILES})

set(NOVA_PLUGINHOST_VST3 0)
if(NOVA_ENABLE_HOSTING)
    set(NOVA_PLUGINHOST_VST3 1)
endif()

target_compile_definitions(NovaMixAI
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=1
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_PLUGINHOST_VST3=${NOVA_PLUGINHOST_VST3}
        JUCE_USE_MP3AUDIOFORMAT=1
        JUCE_USE_FLAC=1
        JUCE_USE_OGGVORBIS=1
        JUCE_MODAL_LOOPS_PERMITTED=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        NOVA_VERSION_STRING="${PROJECT_VERSION}")

target_link_libraries(NovaMixAI
    PRIVATE
        NovaBinaryData
    PUBLIC
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_gui_extra
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(NOVA_BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(NOVA_BUILD_TOOLS)
    add_subdirectory(tools)
endif()
