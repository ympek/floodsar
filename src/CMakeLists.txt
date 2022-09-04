cmake_minimum_required(VERSION 3.16)
project(floodsar)

set(CMAKE_CXX_STANDARD 17)

add_executable(floodsar main.cpp)
add_executable(mapper mapper.cpp)
# Configure dependencies
include(FetchContent)

FetchContent_Declare(
        cxxopts
        GIT_REPOSITORY https://github.com/jarro2783/cxxopts
        GIT_TAG v3.0.0
)

FetchContent_MakeAvailable(cxxopts)
target_link_libraries(floodsar gdal pthread cxxopts)
target_link_libraries(mapper gdal pthread cxxopts)
