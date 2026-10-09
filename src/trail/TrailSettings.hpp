#pragma once

#include "TrailTypes.hpp"
#include <array>
#include <cstddef>
#include <optional>

namespace hitboxtrail::trailsettings
{
    enum class ThickProfile : size_t
    {
        Normal,
        Wave
    };
    inline constexpr std::array kThickProfiles{ThickProfile::Normal, ThickProfile::Wave};
    static_assert(static_cast<size_t>(ThickProfile::Wave) + 1 == kThickProfiles.size());

    constexpr ThickProfile thickProfile(GameMode mode)
    {
        return mode == GameMode::Wave ? ThickProfile::Wave : ThickProfile::Normal;
    }

    constexpr size_t thickIndex(GameMode mode)
    {
        return static_cast<size_t>(thickProfile(mode));
    }

    // Blue hitbox collision detection was exactly at these values.
    constexpr float blueInsetFor(GameMode mode)
    {
        switch (mode)
        {
        case GameMode::Wave:
            return -0.25f;
        case GameMode::Spider:
            return -0.675f;
        default:
            return -0.75f;
        }
    }

    using ThickTable = std::array<std::array<float, 2>, kThickProfiles.size()>; // [profile][isMini]

    inline constexpr std::array<HitboxLayer, kHitboxLayerCount> kDefaultLayerOrder{
        HitboxLayer::Rotation, HitboxLayer::Circle, HitboxLayer::Main, HitboxLayer::Blue};

    struct CaptureSettings
    {
        bool trailEnabled;
        bool onlyClickRelease;
        bool forceSingle;
        bool showBetweenFrames;
        bool alwaysShow;
        bool showOnDeath;
        bool noLimit;
        size_t length;
        double captureRate;
    };

    struct RenderSettings
    {
        bool squareOn, blueOn, circleOn, rotOn, onlyCube, forceSingle;
        bool colorClicks, colorHeld, fadeWithAge;
        bool blueClicks;
        bool fillOn;
        bool batchLayers, onlyClickRelease;
        bool showBetweenFrames, clickOnTop;
        float opacity, masterThick;
        float fillAlpha;
        cocos2d::ccColor4F mainCol, blueCol, circleCol, rotCol;
        cocos2d::ccColor4F pressCol, releaseCol, holdCol;
        ThickTable mainThick, blueThick, circleThick, rotThick;
        // Bottom to top. Later entries are drawn over earlier ones.
        std::array<HitboxLayer, kHitboxLayerCount> layerOrder = kDefaultLayerOrder;
    };

    class TrailSettings
    {
    public:
        CaptureSettings const &cachedCaptureSettings();
        RenderSettings const &cachedRenderSettings();
        void invalidate();

    private:
        std::optional<CaptureSettings> m_capCache;
        std::optional<RenderSettings> m_renderCache;
    };

    void healThicknessSettings();

    // Layer draw order, bottom to top. Stored with the other in-game popup options.
    std::array<HitboxLayer, kHitboxLayerCount> loadLayerOrder();
    void saveLayerOrder(std::array<HitboxLayer, kHitboxLayerCount> const &order);
    char const *layerLabel(HitboxLayer layer);
}
