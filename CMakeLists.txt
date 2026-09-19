cmake_minimum_required(VERSION 3.16)
project(Hydroampf LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT WIN32)
    message(WARNING
        "Hydroampf is a VST2 plugin for Vegas Pro, which only runs on Windows.\n"
        "You can still edit/browse the project on this platform, but to get a\n"
        "loadable .dll you must build it on Windows (MSVC or MinGW), or cross-\n"
        "compile from Linux with mingw-w64 (see README.md).")
endif()

add_library(Hydroampf SHARED
    src/PluginMain.cpp
    src/Hydroampf.cpp
    src/Editor.cpp
)

target_include_directories(Hydroampf PRIVATE src)

set_target_properties(Hydroampf PROPERTIES
    OUTPUT_NAME "Hydroampf"
    SUFFIX ".dll"
)

if (MSVC)
    target_sources(Hydroampf PRIVATE src/Hydroampf.def)
    target_compile_options(Hydroampf PRIVATE /W3)
elseif (WIN32) # MinGW
    target_link_options(Hydroampf PRIVATE
        -static -static-libgcc -static-libstdc++
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Hydroampf.def")
    target_link_libraries(Hydroampf PRIVATE gdi32 user32)
endif()
