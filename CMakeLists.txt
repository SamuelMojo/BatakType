cmake_minimum_required(VERSION 3.24)
project(BatakType LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Browser build: configure with `emcmake cmake ...` (see README).
if(EMSCRIPTEN)
    set(PLATFORM Web CACHE STRING "" FORCE)
endif()

include(FetchContent)
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 5.5
    GIT_SHALLOW TRUE
)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(raylib)

if(NOT EMSCRIPTEN)
    add_executable(batak app/main.cpp)
    target_link_libraries(batak PRIVATE raylib)
    target_compile_options(batak PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# --- BatakType (C version): the playable reaction game in game/ -------------
add_executable(batak_type game/main.c game/core.c)
target_link_libraries(batak_type PRIVATE raylib)
set_target_properties(batak_type PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
target_compile_options(batak_type PRIVATE -Wall -Wextra -Wpedantic -Werror)

if(EMSCRIPTEN)
    # One self-contained batak_type.html (code and font inlined): open it
    # straight from disk or host it anywhere. Turn this OFF to get the usual
    # .html + .js + .wasm trio instead (slightly smaller, needs a web server).
    option(BATAK_WEB_SINGLE_FILE "Inline everything into one .html file" ON)

    set(BATAK_SHELL ${CMAKE_CURRENT_SOURCE_DIR}/game/web/shell.html)
    set_target_properties(batak_type PROPERTIES SUFFIX ".html")
    target_compile_options(batak_type PRIVATE -Os)
    target_link_options(batak_type PRIVATE
        -Os
        -sUSE_GLFW=3
        -sALLOW_MEMORY_GROWTH=1
        -sENVIRONMENT=web
        --shell-file ${BATAK_SHELL})
    if(BATAK_WEB_SINGLE_FILE)
        target_link_options(batak_type PRIVATE -sSINGLE_FILE=1)
    endif()
    # Newer Emscripten needs this for raylib's OpenGL setup; older versions
    # don't know the flag, so only add it when the linker accepts it.
    include(CheckLinkerFlag)
    check_linker_flag(C "-sGL_ENABLE_GET_PROC_ADDRESS=1" BATAK_HAS_GL_PROC_ADDRESS)
    if(BATAK_HAS_GL_PROC_ADDRESS)
        target_link_options(batak_type PRIVATE -sGL_ENABLE_GET_PROC_ADDRESS=1)
    endif()
    set_property(TARGET batak_type APPEND PROPERTY LINK_DEPENDS ${BATAK_SHELL})
else()
    # Tests for the game rules (no window needed): ctest --test-dir build
    enable_testing()
    add_executable(batak_type_tests game/tests/core_tests.c game/core.c)
    set_target_properties(batak_type_tests PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
    target_compile_options(batak_type_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    if(NOT WIN32)
        target_link_libraries(batak_type_tests PRIVATE m)
    endif()
    add_test(NAME batak_type_core COMMAND batak_type_tests)
endif()
