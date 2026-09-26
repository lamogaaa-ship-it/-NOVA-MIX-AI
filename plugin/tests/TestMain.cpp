#include <catch2/catch_session.hpp>
#include <juce_events/juce_events.h>

// JUCE needs its message manager for parameter listeners, timers and async callbacks.
int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    return Catch::Session().run (argc, argv);
}
