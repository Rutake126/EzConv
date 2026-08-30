cmake_minimum_required(VERSION 3.16)
project(jp2convert VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable MSVC Static Runtime (/MT) for zero external DLL dependency
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_compile_options(/W4 /utf-8)
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O3")
endif()

add_compile_definitions(OPJ_STATIC)

include(FetchContent)

# Automatically fetch and statically compile OpenJPEG 2.5.2
message(STATUS "Fetching OpenJPEG 2.5.2 ...")
FetchContent_Declare(
    openjpeg
    GIT_REPOSITORY https://github.com/uclouvain/openjpeg.git
    GIT_TAG        v2.5.2
    GIT_SHALLOW    TRUE
)

# OpenJPEG build options to ensure lightweight static library
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libraries" FORCE)
set(BUILD_CODEC OFF CACHE BOOL "Do not build default CLI binaries" FORCE)
set(BUILD_DOC OFF CACHE BOOL "Do not build doc" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "Do not build tests" FORCE)

FetchContent_MakeAvailable(openjpeg)

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    ${openjpeg_SOURCE_DIR}/src/lib/openjp2
    ${openjpeg_BINARY_DIR}/src/lib/openjp2
)

# Shared Core Engine Sources
set(SOURCES
    src/thread_pool.cpp
    src/image_encoder.cpp
    src/jp2_decoder.cpp
    src/converter_engine.cpp
)

# CLI Target Executable
add_executable(jp2convert src/main.cpp ${SOURCES})
target_link_libraries(jp2convert PRIVATE openjp2)

if(WIN32)
    # Link Windows standard libraries
    target_link_libraries(jp2convert PRIVATE ws2_32)

    # Windows Native GUI Target Executable (EzConv)
    add_executable(EzConv WIN32 src/gui_main.cpp ${SOURCES})
    target_include_directories(EzConv PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/webview2/build/native/include
    )
    target_link_libraries(EzConv PRIVATE 
        openjp2 
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/webview2/build/native/x64/WebView2LoaderStatic.lib
        shlwapi ole32 shell32 user32 gdi32 advapi32 version dwmapi
    )
endif()

message(STATUS "Configured jp2convert and EzConv targets successfully.")
