cmake_minimum_required(VERSION 3.16)
project(simpler_sampler LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(SDL2_MIXER REQUIRED IMPORTED_TARGET SDL2_mixer)
pkg_check_modules(ALSA REQUIRED IMPORTED_TARGET alsa)

add_executable(simpler_sampler src/main.cpp src/editor.cpp src/effects.cpp
    src/graphics_effects.cpp src/midi.cpp)
target_link_libraries(simpler_sampler PRIVATE PkgConfig::SDL2 PkgConfig::SDL2_MIXER
    PkgConfig::ALSA m)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(simpler_sampler PRIVATE -Wall -Wextra -Wpedantic -O4)
endif()
