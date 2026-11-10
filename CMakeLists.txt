cmake_minimum_required(VERSION 3.24)
project(BatakType LANGUAGES C CXX)

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

# --- BatakType (C version): the playable reaction game in game/ -------------
add_executable(batak_type game/main.c game/core.c)
target_link_libraries(batak_type PRIVATE raylib)
set_target_properties(batak_type PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
target_compile_options(batak_type PRIVATE -Wall -Wextra -Wpedantic -Werror)

# Tests for the game rules (no window needed): ctest --test-dir build
enable_testing()
add_executable(batak_type_tests game/tests/core_tests.c game/core.c)
set_target_properties(batak_type_tests PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
target_compile_options(batak_type_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
if(NOT WIN32)
    target_link_libraries(batak_type_tests PRIVATE m)
endif()
add_test(NAME batak_type_core COMMAND batak_type_tests)
