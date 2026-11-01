cmake_minimum_required(VERSION 3.24)
project(TrackmaniaVR LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT so the DLL doesn't depend on a particular VC++ redistributable being installed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Trackmania is 64-bit; configure with -A x64")
endif()

# Prebuilt OpenXR loader + headers from Khronos.
set(OPENXR_VERSION 1.1.60)
include(FetchContent)
FetchContent_Declare(openxr_loader_windows
    URL https://github.com/KhronosGroup/OpenXR-SDK-Source/releases/download/release-${OPENXR_VERSION}/openxr_loader_windows-${OPENXR_VERSION}.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(openxr_loader_windows)

file(GLOB_RECURSE OPENXR_HEADER "${openxr_loader_windows_SOURCE_DIR}/*include/openxr/openxr.h")
file(GLOB_RECURSE OPENXR_DLLS "${openxr_loader_windows_SOURCE_DIR}/*openxr_loader.dll")
list(FILTER OPENXR_DLLS INCLUDE REGEX "/x64/bin/openxr_loader\\.dll$")
if(NOT OPENXR_HEADER OR NOT OPENXR_DLLS)
    message(FATAL_ERROR "Unexpected layout in openxr_loader_windows-${OPENXR_VERSION}.zip")
endif()
list(GET OPENXR_HEADER 0 OPENXR_HEADER)
list(GET OPENXR_DLLS 0 OPENXR_LOADER_DLL)
get_filename_component(OPENXR_INCLUDE_DIR "${OPENXR_HEADER}" DIRECTORY)
get_filename_component(OPENXR_INCLUDE_DIR "${OPENXR_INCLUDE_DIR}" DIRECTORY)

add_library(TrackmaniaVR SHARED
    src/camera_math.cpp
    src/cockpit.cpp
    src/crash_report.cpp
    src/dllmain.cpp
    src/log.cpp
    src/present_hook.cpp
    src/xr_runtime.cpp
)
target_include_directories(TrackmaniaVR PRIVATE "${OPENXR_INCLUDE_DIR}")
target_compile_definitions(TrackmaniaVR PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_compile_options(TrackmaniaVR PRIVATE /W4 /permissive- /EHsc /utf-8)
target_link_libraries(TrackmaniaVR PRIVATE d3d11 dxgi dbghelp)

add_executable(dxgi_probe tools/dxgi_probe.cpp)
target_compile_definitions(dxgi_probe PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(dxgi_probe PRIVATE d3d11 dxgi)
add_dependencies(TrackmaniaVR dxgi_probe)

add_executable(math_test tests/math_test.cpp src/camera_math.cpp src/cockpit.cpp)

add_executable(hook_test tests/hook_test.cpp)
target_compile_definitions(hook_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(hook_test PRIVATE d3d11 dxgi)

# The DLL loads openxr_loader.dll from its own folder at runtime.
add_custom_command(TARGET TrackmaniaVR POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${OPENXR_LOADER_DLL}" "$<TARGET_FILE_DIR:TrackmaniaVR>"
)
