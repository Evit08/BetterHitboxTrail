#include "TrailSettings.hpp"

#include <algorithm>
#include <limits>
#include <string_view>

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
            return mod->getSavedValue<bool>("hitbox-between-frames", true) &&
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

        ThicknessEntry const &thickEntry(ThickKind kind, ThickProfile profile, bool isMini)
        {
            static constexpr size_t kKindSlot[] = {0, 2, 3, 1};
            auto slot = (profile == ThickProfile::Wave ? 8 : 0) + (isMini ? 4 : 0) + kKindSlot[static_cast<size_t>(kind)];
            return kThicknessEntries[slot];
        }

        float loadThick(ThickKind kind, ThickProfile profile, bool isMini)
        {
            auto const &entry = thickEntry(kind, profile, isMini);
            auto value = Mod::get()->getSavedValue<double>(entry.key, entry.fallback);
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
            s.showBetweenFrames = mod->getSavedValue<bool>("hitbox-between-frames", true);
            s.alwaysShow = mod->getSavedValue<bool>("always-show-hitbox-trail", false);
            s.modDetection = mod->getSavedValue<bool>("mod-detection", true);
            s.showOnDeath = mod->getSavedValue<bool>("hitbox-trail-on-death", true);
            s.noLimit = mod->getSavedValue<bool>("trail-no-limit", false);
            s.length = s.noLimit
                           ? std::numeric_limits<size_t>::max()
                           : static_cast<size_t>(mod->getSavedValue<int64_t>("trail-length", 240));
            s.captureRate = utils::numFromString<double>(mod->getSavedValue<std::string>("trail-capture-rate", "240")).unwrapOr(240.0);
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
                .rotOn = mod->getSavedValue<bool>("rotation-hitbox-enabled", true),
                .onlyCube = shared.onlyCube,
                .forceSingle = shared.forceSingle,
                .colorClicks = mod->getSavedValue<bool>("color-clicks", true),
                .colorHeld = mod->getSavedValue<bool>("color-when-held", true),
                .fadeWithAge = mod->getSavedValue<bool>("fade-with-age", false),
                .blueClicks = mod->getSavedValue<bool>("blue-hitbox-color-clicks", false),
                .fillOn = mod->getSavedValue<bool>("fill-hitbox", false),
                .clickDot = mod->getSavedValue<bool>("click-dot", false),
                .dotCircle = mod->getSavedValue<bool>("click-dot-circle", true),
                .batchLayers = mod->getSavedValue<bool>("batch-layer-overlap", false),
                .onlyClickRelease = shared.onlyClickRelease,
                .showBetweenFrames = betweenVisible(),
                .clickOnTop = mod->getSavedValue<bool>("click-release-on-top", false),
                .opacity = std::clamp(static_cast<float>(mod->getSavedValue<double>("opacity", 1.0)), 0.f, 1.f),
                .masterThick = std::clamp(static_cast<float>(mod->getSavedValue<double>("thickness", 1.0)), 0.01f, 2.f),
                .fillAlpha = static_cast<float>(mod->getSavedValue<double>("fill-opacity", 0.25)),
                .mainCol = toColor4F(loadColor("square-hitbox-color")),
                .blueCol = toColor4F(loadColor("blue-hitbox-color")),
                .circleCol = toColor4F(loadColor("circle-hitbox-color")),
                .rotCol = toColor4F(loadColor("rotation-hitbox-color")),
                .pressCol = toColor4F(loadColor("click-color")),
                .releaseCol = toColor4F(loadColor("release-color")),
                .holdCol = toColor4F(loadColor("hold-color")),
                .dotCol = toColor4F(loadColor("click-dot-color")),
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
            s.layerOrder = loadLayerOrder();
            return s;
        }
    }

    namespace
    {
        constexpr char const *kLayerOrderKey = "layer-order";

        char const *layerKey(HitboxLayer layer)
        {
            switch (layer)
            {
            case HitboxLayer::Rotation:
                return "rotation";
            case HitboxLayer::Circle:
                return "circle";
            case HitboxLayer::Main:
                return "main";
            case HitboxLayer::Blue:
                return "blue";
            }
            return "main";
        }

        std::string layerOrderText(std::array<HitboxLayer, kHitboxLayerCount> const &order)
        {
            std::string text;
            for (size_t i = 0; i < order.size(); ++i)
            {
                if (i > 0)
                    text += ',';
                text += layerKey(order[i]);
            }
            return text;
        }
    }

    char const *layerLabel(HitboxLayer layer)
    {
        switch (layer)
        {
        case HitboxLayer::Rotation:
            return "Rotation Hitbox";
        case HitboxLayer::Circle:
            return "Circle Hitbox";
        case HitboxLayer::Main:
            return "Square Hitbox";
        case HitboxLayer::Blue:
            return "Blue Hitbox";
        }
        return "";
    }

    // Stored as a comma separated list, bottom to top (e.g. "rotation,circle,main,blue").
    // Unknown or duplicate names are ignored and missing layers are appended in default order,
    // so a damaged value can never lose a layer.
    std::array<HitboxLayer, kHitboxLayerCount> loadLayerOrder()
    {
        auto saved = Mod::get()->getSavedValue<std::string>(kLayerOrderKey, layerOrderText(kDefaultLayerOrder));

        std::array<HitboxLayer, kHitboxLayerCount> order{};
        std::array<bool, kHitboxLayerCount> used{};
        size_t count = 0;
        size_t start = 0;
        while (start <= saved.size() && count < kHitboxLayerCount)
        {
            auto end = saved.find(',', start);
            if (end == std::string::npos)
                end = saved.size();
            auto token = saved.substr(start, end - start);
            for (auto layer : kDefaultLayerOrder)
            {
                auto index = static_cast<size_t>(layer);
                if (!used[index] && token == layerKey(layer))
                {
                    used[index] = true;
                    order[count++] = layer;
                    break;
                }
            }
            start = end + 1;
        }
        for (auto layer : kDefaultLayerOrder)
            if (!used[static_cast<size_t>(layer)])
                order[count++] = layer;
        return order;
    }

    void saveLayerOrder(std::array<HitboxLayer, kHitboxLayerCount> const &order)
    {
        Mod::get()->setSavedValue<std::string>(kLayerOrderKey, layerOrderText(order));
    }

    namespace
    {
        constexpr std::array kColorKeys{
            "click-color", "release-color", "hold-color",
            "square-hitbox-color", "blue-hitbox-color",
            "circle-hitbox-color", "rotation-hitbox-color",
            "click-dot-color"};

        cocos2d::ccColor3B defaultColor(std::string_view name)
        {
            if (name == "click-color")
                return {255, 255, 0};
            if (name == "release-color")
                return {0, 255, 255};
            if (name == "hold-color")
                return {0, 208, 0};
            if (name == "square-hitbox-color")
                return {255, 0, 0};
            if (name == "blue-hitbox-color")
                return {0, 0, 255};
            if (name == "circle-hitbox-color" || name == "rotation-hitbox-color")
                return {178, 0, 0};
            return {255, 255, 255};
        }
    }

    cocos2d::ccColor3B loadColor(char const *key)
    {
        return Mod::get()->getSavedValue<cocos2d::ccColor3B>(key, defaultColor(key));
    }

    void saveColor(char const *key, cocos2d::ccColor3B color)
    {
        Mod::get()->setSavedValue<cocos2d::ccColor3B>(key, color);
    }

    void resetColors()
    {
        for (auto key : kColorKeys)
            saveColor(key, defaultColor(key));
    }

    // Puts every value saved by the in-game menus back to its default.
    // The defaults here must match the fallbacks used when the values are read.
    void resetAll()
    {
        auto mod = Mod::get();

        struct BoolDefault
        {
            char const *key;
            bool value;
        };
        static constexpr BoolDefault kBools[] = {
            {"hitbox-trail-enabled", true},
            {"hitbox-trail-on-death", true},
            {"always-show-hitbox-trail", false},
            {"mod-detection", true},
            {"only-player-enabled", false},
            {"cube-hitbox-enabled", false},
            {"only-click-release-enabled", false},
            {"square-hitbox-enabled", true},
            {"blue-hitbox-enabled", true},
            {"circle-hitbox-enabled", true},
            {"rotation-hitbox-enabled", true},
            {"batch-layer-overlap", false},
            {"trail-no-limit", false},
            {"color-clicks", true},
            {"click-release-on-top", false},
            {"color-when-held", true},
            {"fade-with-age", false},
            {"fill-hitbox", false},
            {"click-dot", false},
            {"click-dot-circle", true},
            {"hitbox-between-frames", true},
            {"blue-hitbox-color-clicks", false},
        };
        for (auto const &entry : kBools)
            mod->setSavedValue<bool>(entry.key, entry.value);

        mod->setSavedValue<int64_t>("trail-length", 240);
        mod->setSavedValue<double>("opacity", 1.0);
        mod->setSavedValue<double>("thickness", 1.0);
        mod->setSavedValue<double>("fill-opacity", 0.25);
        mod->setSavedValue<std::string>("trail-capture-rate", "240");
        mod->setSavedValue<std::string>("menu-font", "Ubuntu Mono");
        for (auto const &entry : kThicknessEntries)
            mod->setSavedValue<double>(entry.key, entry.fallback);
#ifndef GEODE_IS_MOBILE
        mod->setSettingValue<bool>("hide-pause-menu-button", false);
#endif

        saveOpenMenuKey(cocos2d::KEY_H);
        saveLayerOrder(kDefaultLayerOrder);
        resetColors();
    }

    cocos2d::enumKeyCodes loadOpenMenuKey()
    {
        return static_cast<cocos2d::enumKeyCodes>(
            Mod::get()->getSavedValue<int64_t>("open-menu-key", static_cast<int64_t>(cocos2d::KEY_H)));
    }

    void saveOpenMenuKey(cocos2d::enumKeyCodes key)
    {
        Mod::get()->setSavedValue<int64_t>("open-menu-key", static_cast<int64_t>(key));
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
}
