# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

file(GLOB HAL_DIRS     LIST_DIRECTORIES true ${CMAKE_CURRENT_LIST_DIR}/components/hals/*)
file(GLOB MANAGER_DIRS LIST_DIRECTORIES true ${CMAKE_CURRENT_LIST_DIR}/components/managers/*)
set(EXTRA_COMPONENT_DIRS ${HAL_DIRS} ${MANAGER_DIRS})

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ADAM)
