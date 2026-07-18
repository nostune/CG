cmake_minimum_required(VERSION 3.25)

project(OuterWildsECS VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(OUTERWILDS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

file(GLOB_RECURSE OUTERWILDS_ENGINE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)
list(REMOVE_ITEM OUTERWILDS_ENGINE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp"
)

add_library(OuterWildsEngine STATIC ${OUTERWILDS_ENGINE_SOURCES})
add_library(OuterWilds::Engine ALIAS OuterWildsEngine)

target_include_directories(OuterWildsEngine
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

find_package(EnTT CONFIG REQUIRED)
find_package(unofficial-omniverse-physx-sdk CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_path(MINIMP3_INCLUDE_DIRS NAMES minimp3/minimp3.h REQUIRED)
find_path(STB_INCLUDE_DIRS NAMES stb_image.h REQUIRED)

target_include_directories(OuterWildsEngine
    PRIVATE
        ${MINIMP3_INCLUDE_DIRS}
        ${STB_INCLUDE_DIRS}
)

target_link_libraries(OuterWildsEngine
    PUBLIC
        EnTT::EnTT
        unofficial::omniverse-physx-sdk::sdk
        assimp::assimp
        imgui::imgui
)

if(WIN32)
    target_compile_definitions(OuterWildsEngine
        PUBLIC
            NOMINMAX
            WIN32_LEAN_AND_MEAN
    )
    target_link_libraries(OuterWildsEngine
        PUBLIC
            d3d11
            dxgi
            d3dcompiler
            xaudio2
    )
endif()

if(MSVC)
    target_compile_options(OuterWildsEngine PRIVATE /FS /utf-8 /W4 /permissive-)
    if(OUTERWILDS_WARNINGS_AS_ERRORS)
        target_compile_options(OuterWildsEngine PRIVATE /WX)
    endif()
endif()

add_executable(OuterWildsECS "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
target_link_libraries(OuterWildsECS PRIVATE OuterWilds::Engine)

if(MSVC)
    target_compile_options(OuterWildsECS PRIVATE /FS /utf-8 /W4 /permissive-)
    if(OUTERWILDS_WARNINGS_AS_ERRORS)
        target_compile_options(OuterWildsECS PRIVATE /WX)
    endif()
endif()

set_target_properties(OuterWildsECS PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Source" FILES ${OUTERWILDS_ENGINE_SOURCES})
