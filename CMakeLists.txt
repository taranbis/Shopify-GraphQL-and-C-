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

# ---- Target ---------------------------------------------------------------

set(SOURCES
    src/main.cpp
    src/graphql_client.cpp
    src/throttle.cpp
    src/pagination.cpp
    src/mapping.cpp
    src/util.cpp
)

add_executable(graphql_sync ${SOURCES})

target_include_directories(graphql_sync PRIVATE src)

target_link_libraries(graphql_sync
    PRIVATE
        Boost::system
        Threads::Threads
        nlohmann_json::nlohmann_json
)

# ---- Platform specifics ---------------------------------------------------

if(WIN32)
    # Winsock libraries required by Boost.Asio on Windows.
    target_link_libraries(graphql_sync PRIVATE ws2_32 mswsock)
endif()

# ---- Optional SSL support -------------------------------------------------

if(OpenSSL_FOUND)
    message(STATUS "OpenSSL found — HTTPS support enabled")
    target_compile_definitions(graphql_sync PRIVATE GRAPHQL_SYNC_HAS_SSL)
    target_link_libraries(graphql_sync PRIVATE OpenSSL::SSL OpenSSL::Crypto)
else()
    message(STATUS "OpenSSL not found — HTTPS support disabled (HTTP only)")
endif()

# ---- Compiler warnings ----------------------------------------------------

if(MSVC)
    target_compile_options(graphql_sync PRIVATE /W4)
else()
    target_compile_options(graphql_sync PRIVATE -Wall -Wextra -Wpedantic)
endif()
