cmake_minimum_required(VERSION 3.20)
project(nec-pc-vd VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)

add_executable(nec-pc-vd
    src/main.cpp
    src/emulator.cpp
    src/cpu/i386.cpp
    src/mediagx/mediagx.cpp
    src/mediagx/display_ctrl.cpp
    src/mediagx/mem_ctrl.cpp
    src/bus/memory_bus.cpp
    src/bus/pci_bus.cpp
    src/frontend/sdl_window.cpp
    src/frontend/opengl_output.cpp
    src/voodoo/voodoo2.cpp
    src/voodoo/voodoo_fifo.cpp
    src/voodoo/tmu.cpp
)

target_include_directories(nec-pc-vd PRIVATE src)
target_link_libraries(nec-pc-vd PRIVATE SDL2::SDL2 OpenGL::GL)
