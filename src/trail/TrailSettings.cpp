#include "TrailSettings.hpp"

#include <algorithm>
#include <limits>

using namespace geode::prelude;

namespace hitboxtrail::trailsettings
{
    namespace
    {
        struct SharedOpts
        {
            bool onlyClickRelease;
            bool onlyCube;
            bool forceSingle;
        };

        SharedOpts loadShared()
        {
            auto mod = Mod::get();
            auto onlyClickRelease = mod->getSavedValue<bool>("only-click-release-enabled", false);
            auto onlyPlayer = mod->getSavedValue<bool>("only-player-enabled", false);
            auto onlyCube = mod->getSavedValue<bool>("cube-hitbox-enabled", false);
            return {onlyClickRelease, onlyCube, onlyPlayer || onlyCube};
        }

        bool betweenVisible()
        {
            auto mod = Mod::get();
            return mod->getSettingValue<bool>("hitbox-between-frames") &&
                   (mod->getSavedValue<bool>("color-clicks", true) ||
                    mod->getSavedValue<bool>("only-click-release-enabled", false));
        }

        cocos2d::ccColor4F toColor4F(cocos2d::ccColor3B color)
        {
            return {color.r / 255.f, color.g / 255.f, color.b / 255.f, 1.f};
        }

        enum class ThickKind
        {
            Main,
            Circle,
            Rotation,
            Blue
        };

        char const *thickKey(ThickKind kind, ThickProfile profile, bool isMini)
        {
            auto isWave = profile == ThickProfile::Wave;
            switch (kind)
            {
            case ThickKind::Main:
                return isWave
                           ? (isMini ? "wave-mini-square-hitbox-thickness" : "wave-square-hitbox-thickness")
                           : (isMini ? "mini-square-hitbox-thickness" : "square-hitbox-thickness");
            case ThickKind::Circle:
                return isWave
                           ? (isMini ? "wave-mini-circle-hitbox-thickness" : "wave-circle-hitbox-thickness")
                           : (isMini ? "mini-circle-hitbox-thickness" : "circle-hitbox-thickness");
            case ThickKind::Rotation:
                return isWave
                           ? (isMini ? "player-rotation-wave-mini-thickness" : "player-rotation-wave-thickness")
                           : (isMini ? "player-rotation-mini-thickness" : "player-rotation-thickness");
            case ThickKind::Blue:
                return isWave
                           ? (isMini ? "wave-mini-blue-hitbox-thickness" : "wave-blue-hitbox-thickness")
                           : (isMini ? "mini-blue-hitbox-thickness" : "blue-hitbox-thickness");
            }
            return "square-hitbox-thickness";
        }

        float loadThick(ThickKind kind, ThickProfile profile, bool isMini)
        {
            auto value = Mod::get()->getSettingValue<double>(thickKey(kind, profile, isMini));
            return std::clamp(static_cast<float>(value), 0.01f, 10.f);
        }

        CaptureSettings makeCapture()
        {
            auto mod = Mod::get();
            auto shared = loadShared();
            CaptureSettings s{};
            s.trailEnabled = mod->getSavedValue<bool>("hitbox-trail-enabled", true);
            s.onlyClickRelease = shared.onlyClickRelease;
            s.forceSingle = shared.forceSingle;
            s.showBetweenFrames = mod->getSettingValue<bool>("hitbox-between-frames");
            s.alwaysShow = mod->getSavedValue<bool>("always-show-hitbox-trail", false);
            s.showOnDeath = mod->getSavedValue<bool>("hitbox-trail-on-death", true);
            s.noLimit = mod->getSavedValue<bool>("trail-no-limit", false);
            s.length = s.noLimit
                           ? std::numeric_limits<size_t>::max()
                           : static_cast<size_t>(mod->getSavedValue<int64_t>("trail-length", 240));
            s.captureRate = utils::numFromString<double>(mod->getSettingValue<std::string>("trail-capture-rate")).unwrapOr(240.0);
            return s;
        }

        RenderSettings makeRender()
        {
            auto mod = Mod::get();
            auto shared = loadShared();
            RenderSettings s{
                .squareOn = mod->getSavedValue<bool>("square-hitbox-enabled", true),
                .blueOn = mod->getSavedValue<bool>("blue-hitbox-enabled", true),
                .circleOn = mod->getSavedValue<bool>("circle-hitbox-enabled", true),
                .rotOn = mod->getSavedValue<bool>("player-rotation", true),
                .onlyCube = shared.onlyCube,
                .forceSingle = shared.forceSingle,
                .colorClicks = mod->getSavedValue<bool>("color-clicks", true),
                .colorHeld = mod->getSavedValue<bool>("color-when-held", true),
                .fadeWithAge = mod->getSavedValue<bool>("fade-with-age", false),
                .blueClicks = mod->getSettingValue<bool>("blue-hitbox-color-clicks"),
                .fillOn = mod->getSavedValue<bool>("fill-hitbox", false),
                .batchLayers = mod->getSettingValue<bool>("batch-layer-overlap"),
                .onlyClickRelease = shared.onlyClickRelease,
                .showBetweenFrames = betweenVisible(),
                .clickOnTop = mod->getSavedValue<bool>("click-release-on-top", false),
                .opacity = std::clamp(static_cast<float>(mod->getSavedValue<double>("opacity", 1.0)), 0.f, 1.f),
                .masterThick = std::clamp(static_cast<float>(mod->getSavedValue<double>("thickness", 1.0)), 0.01f, 2.f),
                .fillAlpha = static_cast<float>(mod->getSavedValue<double>("fill-opacity", 0.25)),
                .mainCol = toColor4F(mod->getSettingValue<cocos2d::ccColor3B>("square-hitbox-color")),
                .blueCol = toColor4F(mod->getSettingValue<cocos2d::ccColor3B>("blue-hitbox-color")),
                .circleCol = toColor4F(mod->getSettingValue<cocos2d::ccColor3B>("circle-hitbox-color")),
                .rotCol = toColor4F(mod->getSettingValue<cocos2d::ccColor3B>("player-rotation-color")),
                .pressCol = toColor4F(mod->getSettingValue<cocos2d::ccColor3B>("click-color")),
                .releaseCol = toColor4F(mod->getSettingValue<cocos2d::ccColor3B>("release-color")),
                .holdCol = toColor4F(mod->getSettingValue<cocos2d::ccColor3B>("hold-color")),
            };
            for (auto profile : kThickProfiles)
                for (size_t mini = 0; mini < 2; ++mini)
                {
                    auto index = static_cast<size_t>(profile);
                    auto isMini = mini != 0;
                    s.mainThick[index][mini] = loadThick(ThickKind::Main, profile, isMini);
                    s.blueThick[index][mini] = loadThick(ThickKind::Blue, profile, isMini);
                    s.circleThick[index][mini] = loadThick(ThickKind::Circle, profile, isMini);
                    s.rotThick[index][mini] = loadThick(ThickKind::Rotation, profile, isMini);
                }
            return s;
        }
    }

    CaptureSettings const &TrailSettings::cachedCaptureSettings()
    {
        if (!m_capCache)
            m_capCache = makeCapture();
        return *m_capCache;
    }

    RenderSettings const &TrailSettings::cachedRenderSettings()
    {
        if (!m_renderCache)
            m_renderCache = makeRender();
        return *m_renderCache;
    }

    void TrailSettings::invalidate()
    {
        m_renderCache.reset();
        m_capCache.reset();
    }

    void healThicknessSettings()
    {
        auto mod = Mod::get();
        auto heal = [mod](char const *key, double lo, double hi)
        {
            auto value = mod->getSettingValue<double>(key);
            auto clamped = std::clamp(value, lo, hi);
            if (clamped != value)
                mod->setSettingValue<double>(key, clamped);
        };
        for (auto profile : kThickProfiles)
        {
            for (auto isMini : {false, true})
            {
                heal(thickKey(ThickKind::Main, profile, isMini), 0.01, 10.0);
                heal(thickKey(ThickKind::Blue, profile, isMini), 0.01, 10.0);
                heal(thickKey(ThickKind::Circle, profile, isMini), 0.01, 10.0);
                heal(thickKey(ThickKind::Rotation, profile, isMini), 0.01, 10.0);
            }
        }
    }
}
