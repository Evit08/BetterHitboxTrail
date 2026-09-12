cmake_minimum_required(VERSION 3.21)
project(ClickHitboxTrail VERSION 1.0.0)

add_library(${PROJECT_NAME} SHARED src/main.cpp)

# Copying an older revision over src/main.cpp can preserve that revision's
# original timestamp. CMake would then consider a newer object file current
# and package stale code. Refresh this dependency before each build so a
# rollback always recompiles main.cpp, regardless of copied file timestamps.
set(CLICK_HITBOX_TRAIL_BUILD_STAMP "${CMAKE_CURRENT_BINARY_DIR}/click-hitbox-trail-build.stamp")
file(TOUCH "${CLICK_HITBOX_TRAIL_BUILD_STAMP}")
add_custom_target(ClickHitboxTrailBuildStamp
    COMMAND "${CMAKE_COMMAND}" -E touch "${CLICK_HITBOX_TRAIL_BUILD_STAMP}"
)
add_dependencies(${PROJECT_NAME} ClickHitboxTrailBuildStamp)
set_property(SOURCE src/main.cpp APPEND PROPERTY OBJECT_DEPENDS "${CLICK_HITBOX_TRAIL_BUILD_STAMP}")

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "GEODE_SDK must point to the Geode SDK")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

CPMAddPackage("gh:matcool/gd-imgui-cocos#geode")
target_link_libraries(${PROJECT_NAME} imgui-cocos)

setup_geode_mod(${PROJECT_NAME})
