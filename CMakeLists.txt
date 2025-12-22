cmake_minimum_required(VERSION 3.15)
project(OuterWildsECS LANGUAGES CXX)

# Use vcpkg toolchain by passing -DCMAKE_TOOLCHAIN_FILE=path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add /FS flag for MSVC to avoid PDB conflicts
# Add /utf-8 flag to tell MSVC to treat source files as UTF-8
if(MSVC)
    add_compile_options(/FS /utf-8)
endif()

# Collect sources
file(GLOB_RECURSE SRC_FILES
    "${CMAKE_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/*.c"
    "${CMAKE_SOURCE_DIR}/src/*.h"
    "${CMAKE_SOURCE_DIR}/src/core/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/core/*.h"
    "${CMAKE_SOURCE_DIR}/src/gameplay/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/gameplay/*.h"
    "${CMAKE_SOURCE_DIR}/src/physics/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/physics/*.h"
    "${CMAKE_SOURCE_DIR}/src/graphics/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/graphics/*.h"
    "${CMAKE_SOURCE_DIR}/src/input/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/input/*.h"
    "${CMAKE_SOURCE_DIR}/src/physics/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/physics/*.h"
    "${CMAKE_SOURCE_DIR}/src/scene/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/scene/*.h"
)

add_executable(OuterWildsECS ${SRC_FILES})

target_include_directories(OuterWildsECS PRIVATE ${CMAKE_SOURCE_DIR}/src)

# Find EnTT (header-only)
find_package(EnTT CONFIG REQUIRED)
target_link_libraries(OuterWildsECS PRIVATE EnTT::EnTT)

# Find PhysX (vcpkg provides it as unofficial-omniverse-physx-sdk)
find_package(unofficial-omniverse-physx-sdk CONFIG REQUIRED)
target_link_libraries(OuterWildsECS PRIVATE unofficial::omniverse-physx-sdk::sdk)

# Find Assimp
find_package(assimp CONFIG REQUIRED)
target_link_libraries(OuterWildsECS PRIVATE assimp::assimp)

# Find ImGui
find_package(imgui CONFIG REQUIRED)
target_link_libraries(OuterWildsECS PRIVATE imgui::imgui)

# Find minimp3 (header-only)
find_path(MINIMP3_INCLUDE_DIRS "minimp3/minimp3.h")
target_include_directories(OuterWildsECS PRIVATE ${MINIMP3_INCLUDE_DIRS})

# Find stb (header-only for image loading)
find_path(STB_INCLUDE_DIRS "stb_image.h")
target_include_directories(OuterWildsECS PRIVATE ${STB_INCLUDE_DIRS})

# Add Windows libraries and compiler flags
if(WIN32)
    target_link_libraries(OuterWildsECS PRIVATE d3d11 dxgi d3dcompiler xaudio2)
    # Add /FS flag for MSVC to allow multiple CL.EXE to write to the same PDB file
    if(MSVC)
        target_compile_options(OuterWildsECS PRIVATE /FS)
        # Set console subsystem for main() entry point (allows console output)
        set_target_properties(OuterWildsECS PROPERTIES
            LINK_FLAGS "/SUBSYSTEM:CONSOLE"
        )
    endif()
endif()
