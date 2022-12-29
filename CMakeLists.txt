cmake_minimum_required(VERSION 3.8)

project(
  oneshot
  VERSION 0.1.0
  LANGUAGES CXX)

include(GNUInstallDirs)

add_library(oneshot INTERFACE)
target_include_directories(oneshot INTERFACE include/)

option(BUILD_EXAMPLES "" ON)
option(BUILD_TESTS "" ON)

if(BUILD_EXAMPLES)
    add_subdirectory(example)
endif()

if(BUILD_TESTS)
    add_subdirectory(test)
endif()

# Install headers
install(DIRECTORY "${CMAKE_SOURCE_DIR}/include/"
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING PATTERN "*.hpp")
