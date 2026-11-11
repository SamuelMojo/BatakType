# CMakeLists.txt - tells CMake how to build BatakType.
#
# Desktop:   cmake -B build && cmake --build build && ./build/batak_type
# Tests:     ctest --test-dir build
# Browser:   emcmake cmake -B build-web -DCMAKE_BUILD_TYPE=MinSizeRel
#            cmake --build build-web        (makes build-web/batak_type.html)
cmake_minimum_required(VERSION 3.24)
project(BatakType VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# When building for the browser (with emcmake), tell raylib so.
if(EMSCRIPTEN)
    set(PLATFORM Web CACHE STRING "" FORCE)
endif()

# Download raylib 5.5 (the graphics and input library) the first time, and
# build it as part of this project. Nothing to install by hand.
include(FetchContent)
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 5.5
    GIT_SHALLOW TRUE
)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(raylib)

# Strict warnings, treated as errors: the compiler points out likely
# mistakes and refuses to build until they're fixed.
set(BATAK_WARNINGS -Wall -Wextra -Wpedantic -Werror)

# --- app/: the C++ learning version from Docs/PLAN.md (desktop only) --------
if(NOT EMSCRIPTEN)
    add_executable(batak app/main.cpp)
    target_link_libraries(batak PRIVATE raylib)
    target_compile_options(batak PRIVATE ${BATAK_WARNINGS})
endif()

# --- game/: BatakType itself -------------------------------------------------
#   main.c  the game      core.c  rules and maths      ui.c  text and buttons
add_executable(batak_type game/main.c game/core.c game/ui.c)
target_link_libraries(batak_type PRIVATE raylib)
set_target_properties(batak_type PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
target_compile_options(batak_type PRIVATE ${BATAK_WARNINGS})

if(EMSCRIPTEN)
    # Browser build. By default everything (code and font) is packed into one
    # batak_type.html that opens straight from disk or from any web host.
    # Turn this OFF for separate .html + .js + .wasm files instead (slightly
    # smaller, but they need a web server).
    option(BATAK_WEB_SINGLE_FILE "Inline everything into one .html file" ON)

    set(BATAK_SHELL ${CMAKE_CURRENT_SOURCE_DIR}/game/web/shell.html)
    set_target_properties(batak_type PROPERTIES SUFFIX ".html")
    target_compile_options(batak_type PRIVATE -Os)   # optimise for small size
    target_link_options(batak_type PRIVATE
        -Os
        -sUSE_GLFW=3               # the window/input layer raylib uses
        -sALLOW_MEMORY_GROWTH=1    # let it ask for more memory if needed
        -sENVIRONMENT=web          # browsers only: leaves out Node.js support
        --shell-file ${BATAK_SHELL})   # the page the game sits in
    if(BATAK_WEB_SINGLE_FILE)
        target_link_options(batak_type PRIVATE -sSINGLE_FILE=1)
    endif()
    # Newer Emscripten needs this flag for raylib's OpenGL setup; older
    # versions don't know it. Only add it when the linker accepts it.
    include(CheckLinkerFlag)
    check_linker_flag(C "-sGL_ENABLE_GET_PROC_ADDRESS=1" BATAK_HAS_GL_PROC_ADDRESS)
    if(BATAK_HAS_GL_PROC_ADDRESS)
        target_link_options(batak_type PRIVATE -sGL_ENABLE_GET_PROC_ADDRESS=1)
    endif()
    # Rebuild the page if the shell changes.
    set_property(TARGET batak_type APPEND PROPERTY LINK_DEPENDS ${BATAK_SHELL})
else()
    # Tests for the rules in core.c. They need no window, so they aren't
    # linked against raylib at all.
    enable_testing()
    add_executable(batak_type_tests game/tests/core_tests.c game/core.c)
    set_target_properties(batak_type_tests PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
    target_compile_options(batak_type_tests PRIVATE ${BATAK_WARNINGS})
    if(NOT WIN32)
        target_link_libraries(batak_type_tests PRIVATE m)   # the maths library
    endif()
    add_test(NAME batak_type_core COMMAND batak_type_tests)
endif()
