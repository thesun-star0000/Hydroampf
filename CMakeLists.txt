cmake_minimum_required(VERSION 3.16)
project(Hydroampf LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- VST2 (Hydroampf.dll) ---------------------------------------------------
add_library(Hydroampf_VST SHARED src/PluginMain.cpp src/Hydroampf.cpp src/Editor.cpp)
target_include_directories(Hydroampf_VST PRIVATE src)
set_target_properties(Hydroampf_VST PROPERTIES OUTPUT_NAME "Hydroampf" PREFIX "")

# ---- CLAP (Hydroampf.clap) --------------------------------------------------
add_library(Hydroampf_CLAP SHARED src/ClapMain.cpp src/Editor.cpp)
target_include_directories(Hydroampf_CLAP PRIVATE src)
set_target_properties(Hydroampf_CLAP PROPERTIES OUTPUT_NAME "Hydroampf" PREFIX "" SUFFIX ".clap")

if (MSVC)
    target_sources(Hydroampf_VST PRIVATE src/Hydroampf.def)
    target_compile_options(Hydroampf_VST PRIVATE /W3)
    target_compile_options(Hydroampf_CLAP PRIVATE /W3)
    target_link_libraries(Hydroampf_VST PRIVATE gdi32 user32 gdiplus)
    target_link_libraries(Hydroampf_CLAP PRIVATE gdi32 user32 gdiplus)
elseif (WIN32) # MinGW
    target_link_options(Hydroampf_VST PRIVATE -static -static-libgcc -static-libstdc++
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Hydroampf.def")
    target_link_options(Hydroampf_CLAP PRIVATE -static -static-libgcc -static-libstdc++)
    target_link_libraries(Hydroampf_VST PRIVATE gdi32 user32 gdiplus)
    target_link_libraries(Hydroampf_CLAP PRIVATE gdi32 user32 gdiplus)
    set_target_properties(Hydroampf_VST PROPERTIES SUFFIX ".dll")
else()
    message(WARNING "Hydroampf GUI is Win32-only; on this OS only the headless DSP builds.")
endif()

# Windows: keep windows.h from defining min/max macros, and split import-lib names so the two DLLs do not collide.
if (WIN32)
    target_compile_definitions(Hydroampf_VST  PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _USE_MATH_DEFINES)
    target_compile_definitions(Hydroampf_CLAP PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _USE_MATH_DEFINES)
    set_target_properties(Hydroampf_VST  PROPERTIES ARCHIVE_OUTPUT_NAME "Hydroampf_vst_import")
    set_target_properties(Hydroampf_CLAP PROPERTIES ARCHIVE_OUTPUT_NAME "Hydroampf_clap_import")
endif()
