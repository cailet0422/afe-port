# Native part of the Android build: SDL2 and the game as libmain.so.
cmake_minimum_required(VERSION 3.16)
project(afe_android C CXX)

if(NOT SDL_SOURCE_DIR OR NOT EXISTS "${SDL_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "SDL2 source not found at '${SDL_SOURCE_DIR}' (see docs/android.md)")
endif()

set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory("${SDL_SOURCE_DIR}" SDL2)

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../.." afe)
