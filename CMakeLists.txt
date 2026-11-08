cmake_minimum_required(VERSION 3.24)
project(BatakType LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 5.5
    GIT_SHALLOW TRUE
)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(raylib)

add_executable(batak app/main.cpp)
target_link_libraries(batak PRIVATE raylib)
target_compile_options(batak PRIVATE -Wall -Wextra -Wpedantic -Werror)