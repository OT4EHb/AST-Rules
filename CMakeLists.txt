cmake_minimum_required(VERSION 3.15)
project(astrules CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Zc:__cplusplus /utf-8")
endif()


set(BUILD_TESTS OFF)
set(PEGLIB_BUILD_BENCHMARK OFF)

include(FetchContent)
FetchContent_Declare(
    peglib
    GIT_REPOSITORY https://github.com/yhirose/cpp-peglib.git
    GIT_TAG v1.17.0
)
FetchContent_MakeAvailable(peglib)

add_executable(astrules src/main.cpp)
target_link_libraries(astrules PRIVATE peglib)