cmake_minimum_required(VERSION 3.20)
project(safe-shm)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the build type" FORCE)
endif()

# Sanitizer selection: SANITIZER=asan (default), tsan, or none
set(SANITIZER "asan" CACHE STRING "Sanitizer: asan, tsan, or none")

# Dependencies
find_package(fmt REQUIRED)
find_package(shm REQUIRED)
find_package(exception-rt REQUIRED)

# Compiler options
function(set_warnings target)
    target_compile_options(${target} PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wshadow -Wnon-virtual-dtor
    )
endfunction()

function(set_sanitizers target)
    if (SANITIZER STREQUAL "tsan")
        target_compile_options(${target} PRIVATE -fsanitize=thread -g)
        target_link_options(${target} PRIVATE -fsanitize=thread)
    elseif (SANITIZER STREQUAL "asan")
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -g)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
endfunction()

# Header-only library
add_library(${PROJECT_NAME} INTERFACE)

target_include_directories(${PROJECT_NAME}
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(${PROJECT_NAME}
    INTERFACE
        fmt
        shm::shm
        exception-rt::exception-rt
)

# Install
install(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
    DESTINATION include
)

install(
    TARGETS ${PROJECT_NAME}
    EXPORT ${PROJECT_NAME}Targets
    DESTINATION lib
    INCLUDES DESTINATION include
)

install(
    EXPORT ${PROJECT_NAME}Targets
    FILE ${PROJECT_NAME}Targets.cmake
    NAMESPACE ${PROJECT_NAME}::
    DESTINATION lib/cmake/${PROJECT_NAME}
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/${PROJECT_NAME}Config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
    INSTALL_DESTINATION lib/cmake/${PROJECT_NAME}
    NO_SET_AND_CHECK_MACRO
    NO_CHECK_REQUIRED_COMPONENTS_MACRO
)

install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
    DESTINATION lib/cmake/${PROJECT_NAME}
)

# Optional: Python bindings (nanobind)
find_package(Python3 COMPONENTS Interpreter Development QUIET)
find_package(nanobind CONFIG QUIET HINTS /usr/local/nanobind/cmake)

if (nanobind_FOUND AND Python3_FOUND)
    message(STATUS "Building Python bindings (nanobind found)")
    nanobind_add_module(image_shm_dblbuff
        NB_STATIC
        src/bindings/nanobind_image_shm.cpp
    )
    target_include_directories(image_shm_dblbuff PRIVATE include)
    target_link_libraries(image_shm_dblbuff PRIVATE fmt shm::shm exception-rt::exception-rt)

    if (CMAKE_BUILD_TYPE STREQUAL "Release")
        target_compile_options(image_shm_dblbuff PRIVATE -O3 -DNDEBUG)
    else()
        set_warnings(image_shm_dblbuff)
        set_sanitizers(image_shm_dblbuff)
    endif()

    install(TARGETS image_shm_dblbuff
        DESTINATION lib/python${Python3_VERSION_MAJOR}.${Python3_VERSION_MINOR}/site-packages
    )
else()
    message(STATUS "Skipping Python bindings (nanobind or Python3 not found)")
endif()

# Tests
include(FetchContent)
FetchContent_Declare(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.4.12
)
FetchContent_MakeAvailable(doctest)

enable_testing()

# Unit tests (no shm dependency — pure C++ components)
function(add_unit_test name source)
    add_executable(${name} ${source})
    target_include_directories(${name} PRIVATE include)
    target_link_libraries(${name} PRIVATE doctest::doctest)
    set_warnings(${name})
    set_sanitizers(${name})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_unit_test(flat_type_test test/flat_type_test.cpp)
add_unit_test(double_buffer_swapper_test test/double_buffer_swapper_test.cpp)
add_unit_test(swap_runner_test test/swap_runner_test.cpp)

# Integration tests (require shm dependency)
function(add_integration_test name source)
    add_executable(${name} ${source})
    target_include_directories(${name} PRIVATE include)
    target_link_libraries(${name} PRIVATE doctest::doctest fmt shm::shm exception-rt::exception-rt)
    set_warnings(${name})
    set_sanitizers(${name})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_integration_test(safe_shm_test test/safe_shm_test.cpp)
add_integration_test(shared_memory_test test/shared_memory_test.cpp)
add_integration_test(image_shm_test test/image_shm_test.cpp)
add_integration_test(integration_test test/integration_test.cpp)

# Benchmarks (always built with -O3, no sanitizers)
add_executable(benchmark bench/benchmark.cpp)
target_include_directories(benchmark PRIVATE include)
target_link_libraries(benchmark PRIVATE doctest::doctest fmt shm::shm exception-rt::exception-rt)
target_compile_options(benchmark PRIVATE -O3 -DNDEBUG)
set_warnings(benchmark)
add_test(NAME benchmark COMMAND benchmark)
