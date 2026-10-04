cmake_minimum_required(VERSION 3.21)
project(BetterHitboxTrail VERSION 1.0.0)

file(GLOB_RECURSE BETTER_HITBOX_TRAIL_SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(${PROJECT_NAME} SHARED ${BETTER_HITBOX_TRAIL_SOURCES})

set(BETTER_HITBOX_TRAIL_BUILD_STAMP "${CMAKE_CURRENT_BINARY_DIR}/better-hitbox-trail-build.stamp")
file(TOUCH "${BETTER_HITBOX_TRAIL_BUILD_STAMP}")
add_custom_target(BetterHitboxTrailBuildStamp
    COMMAND "${CMAKE_COMMAND}" -E touch "${BETTER_HITBOX_TRAIL_BUILD_STAMP}"
)
add_dependencies(${PROJECT_NAME} BetterHitboxTrailBuildStamp)
set_property(SOURCE ${BETTER_HITBOX_TRAIL_SOURCES} APPEND PROPERTY OBJECT_DEPENDS "${BETTER_HITBOX_TRAIL_BUILD_STAMP}")

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "GEODE_SDK must point to the Geode SDK")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

CPMAddPackage("gh:matcool/gd-imgui-cocos#geode")
target_link_libraries(${PROJECT_NAME} imgui-cocos)

setup_geode_mod(${PROJECT_NAME})
