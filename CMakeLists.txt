# CMake base configuration
cmake_minimum_required(VERSION 3.21)

set(PROJECT_NAME "J1T")
# project base configuration
project(
    ${PROJECT_NAME}
    DESCRIPTION ""
    LANGUAGES C CXX
)

# C base configuration
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED true)
set(CMAKE_C_EXTENSIONS OFF)

# C base configuration
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED true)
set(CMAKE_CXX_EXTENSIONS OFF)

# common configuration
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)

add_subdirectory(src)
 
