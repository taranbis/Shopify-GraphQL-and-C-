cmake_minimum_required(VERSION 3.16)
project(graphql_sync VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- Dependencies (provided by Conan) -------------------------------------

find_package(Boost 1.75 REQUIRED COMPONENTS system)
find_package(nlohmann_json REQUIRED)
find_package(OpenSSL QUIET)
find_package(Threads REQUIRED)

# ---- Library (shared between main executable and tests) --------------------

set(LIB_SOURCES
    src/graphql_client.cpp
    src/throttle.cpp
    src/pagination.cpp
    src/mapping.cpp
    src/util.cpp
)

add_library(graphql_sync_lib STATIC ${LIB_SOURCES})

target_include_directories(graphql_sync_lib PUBLIC src)

target_link_libraries(graphql_sync_lib
    PUBLIC
        Boost::system
        Threads::Threads
        nlohmann_json::nlohmann_json
)

# ---- Platform specifics ---------------------------------------------------

if(WIN32)
    target_link_libraries(graphql_sync_lib PUBLIC ws2_32 mswsock)
endif()

# ---- Optional SSL support -------------------------------------------------

if(OpenSSL_FOUND)
    message(STATUS "OpenSSL found — HTTPS support enabled")
    target_compile_definitions(graphql_sync_lib PUBLIC GRAPHQL_SYNC_HAS_SSL)
    target_link_libraries(graphql_sync_lib PUBLIC OpenSSL::SSL OpenSSL::Crypto)
else()
    message(STATUS "OpenSSL not found — HTTPS support disabled (HTTP only)")
endif()

# ---- Compiler warnings ----------------------------------------------------

if(MSVC)
    target_compile_options(graphql_sync_lib PRIVATE /W4)
else()
    target_compile_options(graphql_sync_lib PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---- Main executable -------------------------------------------------------

add_executable(graphql_sync src/main.cpp)
target_link_libraries(graphql_sync PRIVATE graphql_sync_lib)

if(MSVC)
    target_compile_options(graphql_sync PRIVATE /W4)
else()
    target_compile_options(graphql_sync PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---- Tests -----------------------------------------------------------------

option(BUILD_TESTS "Build unit and integration tests" ON)

if(BUILD_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)

    # Unit tests (no network needed)
    add_executable(unit_tests
        tests/test_util.cpp
        tests/test_mapping.cpp
        tests/test_throttle.cpp
    )
    target_link_libraries(unit_tests PRIVATE graphql_sync_lib GTest::gtest_main)

    if(MSVC)
        target_compile_options(unit_tests PRIVATE /W4)
    else()
        target_compile_options(unit_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME unit_tests COMMAND unit_tests)

    # Integration tests (requires mock server running on localhost:4000)
    add_executable(integration_tests
        tests/test_integration.cpp
    )
    target_link_libraries(integration_tests PRIVATE graphql_sync_lib GTest::gtest_main)

    if(MSVC)
        target_compile_options(integration_tests PRIVATE /W4)
    else()
        target_compile_options(integration_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME integration_tests COMMAND integration_tests)
endif()
