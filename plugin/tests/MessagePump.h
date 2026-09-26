#pragma once

// Test-only: dispatch pending JUCE messages on the (main) message thread for a while. The plugin
// is built with JUCE_MODAL_LOOPS_PERMITTED=0, so MessageManager::runDispatchLoopUntil() is not
// available; hosted VST3 plugins, however, need the message thread to answer while a worker
// thread talks to them.

#include <juce_events/juce_events.h>

#if JUCE_MAC || JUCE_IOS
 #include <CoreFoundation/CoreFoundation.h>
#else
namespace juce::detail
{
bool dispatchNextMessageOnSystemQueue (bool returnIfNoPendingMessages);   // juce_events, Linux/Windows
}
#endif

namespace nova::test
{
inline void pumpMessages (int milliseconds)
{
    jassert (juce::MessageManager::existsAndIsCurrentThread());
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) milliseconds;
    while (juce::Time::getMillisecondCounter() < end)
    {
       #if JUCE_MAC || JUCE_IOS
        CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.005, true);
       #else
        if (! juce::detail::dispatchNextMessageOnSystemQueue (true))
            juce::Thread::sleep (2);
       #endif
    }
}
} // namespace nova::test
