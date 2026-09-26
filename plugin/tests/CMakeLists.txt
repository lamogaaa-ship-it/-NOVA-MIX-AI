# NOVA unit + integration tests (Catch2). Tests link the plugin's shared-code target so they
# exercise exactly the code that ships in the VST3/AU.

file(GLOB NOVA_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
add_executable(NovaTests ${NOVA_TEST_SOURCES})
target_link_libraries(NovaTests PRIVATE NovaMixAI Catch2::Catch2)
target_include_directories(NovaTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(NovaTests PRIVATE NOVA_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

include(CTest)
include(${Catch2_SOURCE_DIR}/extras/Catch.cmake)
catch_discover_tests(NovaTests DISCOVERY_MODE PRE_TEST)
