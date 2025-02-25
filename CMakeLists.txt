cmake_minimum_required(VERSION 3.20)
project(safe-shm)

# Require C++23
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# default to release build for the library if not specified
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the build type" FORCE)
endif()
message(STATUS "Library will be built using CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")

# Find dependencies
find_package(cmake-library REQUIRED)
find_package(fmt REQUIRED)
find_package(single-task-runner REQUIRED)
find_package(double-buffer-swapper REQUIRED)
find_package(flat-type REQUIRED)
find_package(shm REQUIRED)
find_package(exception-rt REQUIRED)

# Create an INTERFACE library target for the header-only library.
add_library(${PROJECT_NAME} INTERFACE)

target_include_directories(${PROJECT_NAME}
    INTERFACE
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:include>
)

# Install header files
install(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
    DESTINATION include
)

# Export the target so that consumers can use the namespaced alias.
install(
    TARGETS ${PROJECT_NAME}
    EXPORT ${PROJECT_NAME}Targets
    INCLUDES DESTINATION include
)

install(
    EXPORT ${PROJECT_NAME}Targets
    FILE ${PROJECT_NAME}Targets.cmake
    NAMESPACE ${PROJECT_NAME}::
    DESTINATION lib/cmake/${PROJECT_NAME}
)

# Configure the package configuration file.
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

# Test executable
add_executable(${PROJECT_NAME}_test test/safe_shm_test.cpp)
target_include_directories(${PROJECT_NAME}_test PRIVATE include)
target_link_libraries(${PROJECT_NAME}_test PRIVATE fmt flat-type::flat-type shm::shm exception-rt::exception-rt single-task-runner::single-task-runner double-buffer-swapper::double-buffer-swapper)

set_debug_options(${PROJECT_NAME}_test)
enable_sanitizers(${PROJECT_NAME}_test)

# Export the target
install(
    TARGETS ${PROJECT_NAME}_test
    EXPORT ${PROJECT_NAME}Targets
    DESTINATION bin
)