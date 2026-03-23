cmake_minimum_required(VERSION 3.20)

include(FetchContent)

# ── Asio (standalone, no Boost) ───────────────────────────────────────────────
FetchContent_Declare(
    asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG        asio-1-28-2
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(asio)

# ── nlohmann/json ─────────────────────────────────────────────────────────────
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp
    DOWNLOAD_NO_EXTRACT TRUE
    DOWNLOAD_DIR ${CMAKE_CURRENT_BINARY_DIR}/include/nlohmann
)
FetchContent_MakeAvailable(nlohmann_json)

# ── bettersend_core shared library ────────────────────────────────────────────
add_library(bettersend_core SHARED
    src/MdnsDiscovery.cpp
    src/TcpTransport.cpp
    src/TransferProtocol.cpp
    src/bettersend_api.cpp
)

target_include_directories(bettersend_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${asio_SOURCE_DIR}/asio/include
        ${CMAKE_CURRENT_BINARY_DIR}/include
)

target_compile_definitions(bettersend_core
    PUBLIC ASIO_STANDALONE
)

# ── Platform-specific linking ─────────────────────────────────────────────────
if(APPLE)
    target_link_libraries(bettersend_core PRIVATE "-framework Foundation")
elseif(ANDROID)
    target_link_libraries(bettersend_core PRIVATE log)
elseif(WIN32)
    target_link_libraries(bettersend_core PRIVATE ws2_32 mswsock)
endif()
