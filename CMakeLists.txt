#
# Copyright 2017 Milan Izai <milan.izai@gmail.com>
#
# Permission is hereby granted, free of charge, to any person obtaining a copy of
# this software and associated documentation files (the "Software"), to deal in
# the Software without restriction, including without limitation the rights to
# use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
# the Software, and to permit persons to whom the Software is furnished to do so,
# subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
# FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
# COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
# IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
# CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#

cmake_minimum_required(VERSION 3.0)

project(OceanTool)

add_executable(oceantool
    code/common.cpp
    code/dft.cpp
    code/oceantool.cpp
    code/math.cpp
    code/opengl.cpp
)

target_compile_options(oceantool PUBLIC
    -std=c++11 -Wall -Wextra -fno-rtti -fno-exceptions -fno-strict-aliasing
    -Wno-missing-field-initializers
)

option(USE_SIMD "" ON)
option(DEBUG_OPENGL "" OFF)

target_compile_definitions(oceantool PUBLIC
    USE_SIMD=$<BOOL:${USE_SIMD}>
    DEBUG_OPENGL=$<BOOL:${DEBUG_OPENGL}>
)

find_library(SDL2_LIBRARY SDL2)
target_link_libraries(oceantool PUBLIC ${SDL2_LIBRARY})
find_path(SDL2_INCLUDE_DIR SDL.h PATH_SUFFIXES SDL2)
target_include_directories(oceantool PUBLIC ${SDL2_INCLUDE_DIR})

add_library(imgui STATIC
    imgui/imgui.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_demo.cpp
)

target_link_libraries(oceantool PUBLIC imgui)
target_include_directories(oceantool PUBLIC imgui)
