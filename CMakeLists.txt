cmake_minimum_required(VERSION 3.24)

# Use GCC 14 for full C++23 standard library support (e.g. <print>).
# Override at configure time: cmake -DCMAKE_CXX_COMPILER=... if needed.
if(NOT DEFINED CMAKE_CXX_COMPILER)
    find_program(GXX14 g++-14)
    if(GXX14)
        set(CMAKE_CXX_COMPILER "${GXX14}" CACHE STRING "" FORCE)
    endif()
endif()

project(LimitOrderBook VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)
FetchContent_Declare(
    maroof_net
    GIT_REPOSITORY https://github.com/smhussa22/maroof-net.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(maroof_net)

find_package(Threads REQUIRED)

# ---- Engine library ----
# header-only; downstream services FetchContent this repo and link lob_engine.
# maroof_net and Threads propagate transitively so callers need nothing else.
add_library(lob_engine INTERFACE)
target_include_directories(lob_engine INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(lob_engine INTERFACE maroof_net Threads::Threads)

# ---- Executables and tests (only when building the engine standalone) ----
if(PROJECT_IS_TOP_LEVEL)

    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
        GIT_SHALLOW    TRUE
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    add_executable(matcher src/main.cc)
    target_link_libraries(matcher PRIVATE lob_engine)

    add_executable(server src/server_main.cc)
    target_link_libraries(server PRIVATE lob_engine)

    add_executable(synth_client src/synth_client.cc)
    target_link_libraries(synth_client PRIVATE lob_engine hdr_histogram_static)

    add_executable(engine_bench benchmarks/engine_bench.cc)
    target_link_libraries(engine_bench PRIVATE lob_engine hdr_histogram_static)

    enable_testing()
    include(GoogleTest)

    add_executable(OrderBook_test tests/OrderBook_test.cc)
    target_link_libraries(OrderBook_test PRIVATE lob_engine GTest::gtest_main)
    gtest_discover_tests(OrderBook_test)

    add_executable(ExchangeListener_test tests/ExchangeListener_test.cc)
    target_link_libraries(ExchangeListener_test PRIVATE lob_engine GTest::gtest_main)
    gtest_discover_tests(ExchangeListener_test)

    add_executable(OrderPool_test tests/OrderPool_test.cc)
    target_link_libraries(OrderPool_test PRIVATE lob_engine GTest::gtest_main)
    gtest_discover_tests(OrderPool_test)

    add_executable(CommandQueue_test tests/CommandQueue_test.cc)
    target_link_libraries(CommandQueue_test PRIVATE lob_engine GTest::gtest_main)
    gtest_discover_tests(CommandQueue_test)

    add_executable(ExchangeServer_test tests/ExchangeServer_test.cc)
    target_link_libraries(ExchangeServer_test PRIVATE lob_engine GTest::gtest_main)
    gtest_discover_tests(ExchangeServer_test)

endif()
