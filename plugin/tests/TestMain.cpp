#include <catch2/catch_session.hpp>
#include <juce_events/juce_events.h>

#include <cstdlib>

// JUCE needs its message manager for parameter listeners, timers and async callbacks.
int main (int argc, char* argv[])
{
    // isolate settings / taste profile / experiences from the developer's real profile
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("nova-tests-" + juce::String (juce::Time::currentTimeMillis()));
    dir.createDirectory();
   #if JUCE_WINDOWS
    _putenv_s ("NOVA_USER_DATA_DIR", dir.getFullPathName().toRawUTF8());
   #else
    setenv ("NOVA_USER_DATA_DIR", dir.getFullPathName().toRawUTF8(), 1);
    unsetenv ("ANTHROPIC_API_KEY");   // tests must never call a real model
   #endif
    juce::ScopedJuceInitialiser_GUI juceInit;
    const int result = Catch::Session().run (argc, argv);
    dir.deleteRecursively();
    return result;
}
