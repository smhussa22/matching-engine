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

# Emit build/compile_commands.json so clangd / VS Code IntelliSense can resolve includes.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)
FetchContent_Declare(
    maroof_net
    GIT_REPOSITORY https://github.com/smhussa22/maroof-net.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(maroof_net)

# Linking maroof_net is what gives this target the `#include <maroof/net/...>` path.
add_executable(matcher src/main.cc)
target_link_libraries(matcher PRIVATE maroof_net)
