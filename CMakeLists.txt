cmake_minimum_required(VERSION 3.20)
project(BetterSend VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── TLS CA bundle auto-detect (Windows) ──────────────────────────────────────
# winlibs-bundled cmake ships without trust anchors, so FetchContent over HTTPS
# fails with "certificate signer not trusted". Auto-pick a CA bundle from a
# common location so devs don't need to pass -DCMAKE_TLS_CAINFO every time.
if(WIN32 AND NOT CMAKE_TLS_CAINFO AND NOT DEFINED ENV{CMAKE_TLS_CAINFO})
    foreach(_ca_candidate
        "$ENV{CURL_CA_BUNDLE}"
        "C:/Program Files/Git/mingw64/etc/ssl/certs/ca-bundle.crt"
        "C:/Program Files/Git/usr/ssl/certs/ca-bundle.crt"
        "C:/Software/StrawberryPerl/perl/vendor/lib/Mozilla/CA/cacert.pem"
    )
        if(_ca_candidate AND EXISTS "${_ca_candidate}")
            set(CMAKE_TLS_CAINFO "${_ca_candidate}" CACHE FILEPATH "CA bundle for HTTPS downloads")
            message(STATUS "CMAKE_TLS_CAINFO auto-detected: ${_ca_candidate}")
            break()
        endif()
    endforeach()
endif()

# ── Compiler hardening (optional but recommended) ─────────────────────────────
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ── Subprojects ───────────────────────────────────────────────────────────────
add_subdirectory(cpp_core)

option(BUILD_TESTS "Build unit tests" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(cpp_core/tests)
endif()
