cmake_minimum_required(VERSION 3.20)

# ── Google Test (fetched automatically) ──────────────────────────────────────
include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
# Prevent GTest from overriding compiler/linker settings on Windows
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# ── Test executable ───────────────────────────────────────────────────────────
add_executable(bettersend_tests
    test_protocol.cpp
    test_transport.cpp
    test_discovery.cpp
)

target_link_libraries(bettersend_tests
    PRIVATE
        bettersend_core
        GTest::gtest_main
        GTest::gmock_main
)

include(GoogleTest)
gtest_discover_tests(bettersend_tests)
