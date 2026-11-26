cmake_minimum_required(VERSION 3.21)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (APPLE)
    if ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS")
        set(CMAKE_OSX_ARCHITECTURES "arm64")
    else()
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
    endif()
endif()

set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(BetterHitboxTrail VERSION 1.0.0)

file(GLOB_RECURSE BETTER_HITBOX_TRAIL_SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(${PROJECT_NAME} SHARED ${BETTER_HITBOX_TRAIL_SOURCES})

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "GEODE_SDK must point to the Geode SDK")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

if (NOT IOS)
    CPMAddPackage("gh:matcool/gd-imgui-cocos#b93f08ccef778a53ebba09b20c347f6a63980119")
    target_link_libraries(${PROJECT_NAME} imgui-cocos)

    # FreeType font rendering for sharp ImGui text at every size.
    CPMAddPackage(
        NAME freetype
        GIT_REPOSITORY https://github.com/freetype/freetype.git
        GIT_TAG VER-2-13-3
        OPTIONS
            "FT_DISABLE_ZLIB ON"
            "FT_DISABLE_BZIP2 ON"
            "FT_DISABLE_PNG ON"
            "FT_DISABLE_HARFBUZZ ON"
            "FT_DISABLE_BROTLI ON"
    )
    # Already added by gd-imgui-cocos; this only exposes imgui_SOURCE_DIR here.
    CPMAddPackage("gh:ocornut/imgui#v1.92.4")
    target_sources(${PROJECT_NAME} PRIVATE ${imgui_SOURCE_DIR}/misc/freetype/imgui_freetype.cpp)
    target_compile_definitions(${PROJECT_NAME} PRIVATE IMGUI_ENABLE_FREETYPE)
    target_link_libraries(${PROJECT_NAME} freetype)
endif()

setup_geode_mod(${PROJECT_NAME})
