// M0 placeholder keeping the Catch2 harness wired up; real DSP and engine
// tests arrive with M1+.

#include <catch2/catch_test_macros.hpp>

TEST_CASE ("test harness runs", "[sanity]")
{
    REQUIRE (1 + 1 == 2);
}
