# Pinned third-party dependencies.
#
# JUCE      9.0.2  (AGPLv3 / commercial JUCE 9 licence - see docs/BUILD.md "Licensing")
# Catch2    v3.16.0 (BSL-1.0) - tests only
#
# Set NOVA_JUCE_PATH to a local JUCE checkout to avoid a network fetch (used by the dev container).

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

set(NOVA_JUCE_TAG "9.0.2" CACHE STRING "Pinned JUCE tag")
set(NOVA_CATCH2_TAG "v3.16.0" CACHE STRING "Pinned Catch2 tag")
set(NOVA_JUCE_PATH "" CACHE PATH "Optional local JUCE checkout")

if(NOVA_JUCE_PATH AND EXISTS "${NOVA_JUCE_PATH}/CMakeLists.txt")
    message(STATUS "NOVA: using local JUCE at ${NOVA_JUCE_PATH}")
    add_subdirectory("${NOVA_JUCE_PATH}" "${CMAKE_BINARY_DIR}/_deps/juce-build" EXCLUDE_FROM_ALL)
else()
    message(STATUS "NOVA: fetching JUCE ${NOVA_JUCE_TAG}")
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        ${NOVA_JUCE_TAG}
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE)
    FetchContent_MakeAvailable(juce)
endif()

if(NOVA_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        ${NOVA_CATCH2_TAG}
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(Catch2)
endif()
