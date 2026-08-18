add_executable(audiorack_tests
    SanityTests.cpp)

audiorack_strict_sources(${CMAKE_CURRENT_SOURCE_DIR}/SanityTests.cpp)

target_link_libraries(audiorack_tests PRIVATE Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(audiorack_tests)
