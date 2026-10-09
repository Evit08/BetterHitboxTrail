#pragma once

#include <Geode/Geode.hpp>
#include <cstddef>

namespace hitboxtrail
{
    enum class GameMode
    {
        Cube,
        Ship,
        Ball,
        Ufo,
        Wave,
        Robot,
        Spider,
        Swing
    };

    // Hitbox layers drawn for every trail state. Default draw order is the declaration
    // order: later layers are drawn on top of earlier ones.
    enum class HitboxLayer
    {
        Rotation,
        Circle,
        Main,
        Blue
    };
    inline constexpr size_t kHitboxLayerCount = 4;

    struct TrailState
    {
        cocos2d::CCRect rect;
        cocos2d::CCRect miniRect;
        float rotation = 0.f;
        enum class Click
        {
            None,
            Press,
            Release,
            Hold
        } click = Click::None;
        GameMode mode = GameMode::Cube;
        float vehicleSize = 1.f;
        bool betweenFrame = false;
    };

    // Used both for the capture flash lifetime and the renderer's force-single
    // lookback window. Changing one side without the other silently breaks the other.
    static constexpr int kFlashTicks = 2; // 1 is not displayed
}
