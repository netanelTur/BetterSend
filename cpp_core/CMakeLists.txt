cmake_minimum_required(VERSION 3.20)


project(BetterSendCore LANGUAGES CXX)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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
    src/TcpTransport.cpp
    src/TransferProtocol.cpp
    src/bettersend_api.cpp
)

# IDiscovery impl is chosen per platform. Strategy pattern means swapping
# the .cpp here is all it takes — every other file uses IDiscovery / makeDiscovery().
#   Apple   → BonjourDiscovery (dns_sd.h + AWDL, sandbox-compatible)
#   Windows → BleDiscovery (WinRT BLE advertising + scanning — Phase 1)
#   Other   → MdnsDiscovery (raw mDNS via mdns.h — dev fallback / Android later)
if(APPLE)
    target_sources(bettersend_core PRIVATE src/BonjourDiscovery.cpp)
elseif(WIN32)
    target_sources(bettersend_core PRIVATE src/BleDiscovery_Windows.cpp)
else()
    target_sources(bettersend_core PRIVATE src/MdnsDiscovery.cpp)
endif()

target_include_directories(bettersend_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${asio_SOURCE_DIR}/asio/include
        ${CMAKE_CURRENT_BINARY_DIR}/include
)

target_compile_definitions(bettersend_core
    PUBLIC ASIO_STANDALONE
)

if(MSVC)
    target_compile_options(bettersend_core PRIVATE /Zc:preprocessor)
endif()

# ── Platform-specific linking ─────────────────────────────────────────────────
if(APPLE)
    target_link_libraries(bettersend_core PRIVATE "-framework Foundation")
elseif(ANDROID)
    target_link_libraries(bettersend_core PRIVATE log)
elseif(WIN32)
    target_link_libraries(bettersend_core PRIVATE
        ws2_32 mswsock iphlpapi
        # WinRT runtime + COM/OLE for BleDiscovery_Windows.cpp (cppwinrt)
        runtimeobject ole32 oleaut32
    )

    # cppwinrt projection headers live in the Windows SDK, not in MinGW.
    # Locate the newest installed version and add its `cppwinrt/` include dir.
    if(NOT BETTERSEND_CPPWINRT_INCLUDE)
        file(GLOB _sdk_versions
            LIST_DIRECTORIES TRUE
            "C:/Program Files (x86)/Windows Kits/10/Include/*"
        )
        list(SORT _sdk_versions ORDER DESCENDING)
        foreach(_sdk_dir ${_sdk_versions})
            if(IS_DIRECTORY "${_sdk_dir}/cppwinrt/winrt")
                set(BETTERSEND_CPPWINRT_INCLUDE "${_sdk_dir}/cppwinrt"
                    CACHE PATH "Path to Windows SDK cppwinrt projection headers")
                break()
            endif()
        endforeach()
    endif()
    if(BETTERSEND_CPPWINRT_INCLUDE)
        message(STATUS "cppwinrt include: ${BETTERSEND_CPPWINRT_INCLUDE}")
        target_include_directories(bettersend_core PRIVATE "${BETTERSEND_CPPWINRT_INCLUDE}")
    else()
        message(FATAL_ERROR
            "Could not find cppwinrt headers under any Windows SDK. "
            "Install the Windows SDK (any 10.0.x) or set BETTERSEND_CPPWINRT_INCLUDE manually.")
    endif()

    # Windows 10+ required for BLE Advertisement APIs.
    target_compile_definitions(bettersend_core PRIVATE
        WINVER=0x0A00 _WIN32_WINNT=0x0A00
    )
endif()