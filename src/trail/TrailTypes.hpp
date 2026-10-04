#pragma once

#include <Geode/Geode.hpp>

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
