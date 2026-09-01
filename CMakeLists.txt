cmake_minimum_required(VERSION 3.16)
project(Utils VERSION 0.1.0 LANGUAGES CXX)

# Are we being built on our own, or pulled in via add_subdirectory?
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(UTILS_STANDALONE TRUE)
else()
    set(UTILS_STANDALONE FALSE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(UTILS_STANDALONE)
    if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_BUILD_TYPE Release)
    endif()
    message(STATUS "Utils build type: ${CMAKE_BUILD_TYPE}")
endif()

# All library sources. CONFIGURE_DEPENDS re-globs when files are added.
file(GLOB_RECURSE UTILS_SRC CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

add_library(utils STATIC ${UTILS_SRC})
# Namespaced alias — this is the name consumers link against.
add_library(Utils::utils ALIAS utils)

# PUBLIC include dirs propagate to anything that links utils. glm is vendored
# here so every consumer sees the exact same glm version.
target_include_directories(utils PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/glm"
)

# PUBLIC so consumers compiling utils headers also get C++20.
target_compile_features(utils PUBLIC cxx_std_20)

# Warnings stay PRIVATE — not forced on consumers.
if(MSVC)
    target_compile_options(utils PRIVATE /W4)
else()
    target_compile_options(utils PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-unknown-pragmas)
endif()

# Tests. Registered with CTest, which ships with CMake, so the suite pulls in no
# third-party dependency. Only built when utils is the top-level project: a
# consumer that adds this directory gets the library and nothing else.
option(UTILS_BUILD_TESTS "Build the utils test suite" ${UTILS_STANDALONE})
if(UTILS_BUILD_TESTS)
    enable_testing()

    add_executable(dekker_arithmetic_test tests/dekker_arithmetic_test.cpp)
    target_link_libraries(dekker_arithmetic_test PRIVATE utils)
    if(MSVC)
        target_compile_options(dekker_arithmetic_test PRIVATE /W4)
    else()
        target_compile_options(dekker_arithmetic_test PRIVATE -Wall -Wextra)
    endif()

    add_test(NAME dekker_arithmetic COMMAND dekker_arithmetic_test)
endif()
