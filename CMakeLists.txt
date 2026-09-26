cmake_minimum_required(VERSION 3.22)

# macOS: build universal binaries for Apple Silicon + Intel unless the caller overrides it.
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures" FORCE)
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(NOVA_MIX_AI VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(NOVA_BUILD_TESTS "Build unit / integration tests" ON)
option(NOVA_BUILD_TOOLS "Build developer tools (scanner helper, host check, screenshot)" ON)
option(NOVA_ENABLE_HOSTING "Enable hosting of third-party VST3/AU plugins inside NOVA" ON)

include(cmake/NovaDependencies.cmake)

add_subdirectory(plugin)
