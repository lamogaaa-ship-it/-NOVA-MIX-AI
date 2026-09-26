# Developer tools

# Renders the real editor (with real audio flowing through the real processor) to a PNG.
juce_add_console_app(NovaScreenshot PRODUCT_NAME "nova-screenshot")
target_sources(NovaScreenshot PRIVATE screenshot/Screenshot.cpp)
target_include_directories(NovaScreenshot PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../tests)
target_link_libraries(NovaScreenshot PRIVATE NovaMixAI juce::juce_gui_extra)

# Out-of-process plugin scanner: loads third-party plugins in its own process so a crashing
# plugin can never take the DAW (or NOVA) down; writes the plugin database JSON.
juce_add_console_app(NovaPluginScanner PRODUCT_NAME "nova-plugin-scanner")
target_sources(NovaPluginScanner PRIVATE scanner/Scanner.cpp)
target_link_libraries(NovaPluginScanner PRIVATE NovaMixAI juce::juce_audio_utils)

# Loads a built plugin binary through JUCE's hosting layer (like a DAW would), streams audio
# through it and verifies passthrough, latency reporting, parameter effect and state restore.
juce_add_console_app(NovaHostCheck PRODUCT_NAME "nova-host-check")
target_sources(NovaHostCheck PRIVATE host_check/HostCheck.cpp)
target_include_directories(NovaHostCheck PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../tests)
target_link_libraries(NovaHostCheck PRIVATE NovaMixAI juce::juce_audio_utils)

foreach(tool NovaScreenshot NovaPluginScanner NovaHostCheck)
    target_compile_definitions(${tool} PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=1)
endforeach()

# juce_add_console_app does not apply PRODUCT_NAME to the executable file name
set_target_properties(NovaScreenshot PROPERTIES OUTPUT_NAME nova-screenshot)
set_target_properties(NovaPluginScanner PROPERTIES OUTPUT_NAME nova-plugin-scanner)
set_target_properties(NovaHostCheck PROPERTIES OUTPUT_NAME nova-host-check)
