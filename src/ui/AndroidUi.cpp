#include "SettingsMenu.hpp"

#include <Geode/Geode.hpp>

#ifdef GEODE_IS_ANDROID

#include <Geode/ui/ColorPickPopup.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <algorithm>
#include <cmath>
#include <utility>

#include "../trail/TrailSettings.hpp"

using namespace geode::prelude;

namespace hitboxtrail
{
    namespace
    {
        constexpr float kPopupWidth = 360.f;
        constexpr float kPopupHeight = 260.f;
        constexpr float kRowHeight = 28.f;

        struct NumberSetting
        {
            char const *label;
            char const *key;
            double fallback;
            double scale;
            double step;
            double lo;
            double hi;
            bool isInt;
        };

        char const *enabledKeyOf(HitboxLayer layer)
        {
            switch (layer)
            {
            case HitboxLayer::Main: return "square-hitbox-enabled";
            case HitboxLayer::Blue: return "blue-hitbox-enabled";
            case HitboxLayer::Circle: return "circle-hitbox-enabled";
            case HitboxLayer::Rotation: return "rotation-hitbox-enabled";
            }
            return "";
        }

        char const *colorKeyOf(HitboxLayer layer)
        {
            switch (layer)
            {
            case HitboxLayer::Main: return "square-hitbox-color";
            case HitboxLayer::Blue: return "blue-hitbox-color";
            case HitboxLayer::Circle: return "circle-hitbox-color";
            case HitboxLayer::Rotation: return "rotation-hitbox-color";
            }
            return "";
        }

        class SettingsPopup : public Popup
        {
        public:
            static SettingsPopup *create()
            {
                auto popup = new SettingsPopup;
                if (popup->setupPopup())
                {
                    popup->autorelease();
                    return popup;
                }
                delete popup;
                return nullptr;
            }

        private:
            ScrollLayer *m_scroll = nullptr;

            bool setupPopup()
            {
                if (!Popup::init(kPopupWidth, kPopupHeight))
                    return false;
                setTitle("Better Hitbox Trail");
                CCSize size{kPopupWidth - 40.f, kPopupHeight - 60.f};
                m_scroll = ScrollLayer::create(size);
                auto mainSize = m_mainLayer->getContentSize();
                m_scroll->setPosition({mainSize.width / 2.f - size.width / 2.f,
                                       mainSize.height / 2.f - size.height / 2.f - 10.f});
                m_mainLayer->addChild(m_scroll);
                m_scroll->m_contentLayer->setLayout(ColumnLayout::create()
                                                        ->setAxisReverse(true)
                                                        ->setAxisAlignment(AxisAlignment::End)
                                                        ->setAutoGrowAxis(size.height)
                                                        ->setGap(4.f));
                populate();
                m_scroll->moveToTop();
                return true;
            }

            void onRebuild(float)
            {
                auto content = m_scroll->m_contentLayer;
                auto y = content->getPositionY();
                content->removeAllChildren();
                populate();
                content->setPositionY(y);
            }

            void rebuildLater()
            {
                scheduleOnce(schedule_selector(SettingsPopup::onRebuild), 0.f);
            }

            CCMenu *newRow()
            {
                auto row = CCMenu::create();
                row->ignoreAnchorPointForPosition(false);
                row->setContentSize({m_scroll->getContentWidth(), kRowHeight});
                row->setLayout(RowLayout::create()
                                   ->setAxisAlignment(AxisAlignment::Start)
                                   ->setAutoScale(false)
                                   ->setGap(8.f));
                m_scroll->m_contentLayer->addChild(row);
                return row;
            }

            CCMenu *toggleRow(char const *label, char const *key, bool fallback, char const *clears = nullptr)
            {
                auto row = newRow();
                auto toggler = CCMenuItemExt::createTogglerWithStandardSprites(0.5f, [this, key, clears](CCMenuItemToggler *t)
                                                                               {
                    auto on = !t->isToggled();
                    Mod::get()->setSavedValue<bool>(key, on);
                    if (on && clears)
                    {
                        Mod::get()->setSavedValue<bool>(clears, false);
                        rebuildLater();
                    }
                    applySettings(); });
                toggler->toggle(Mod::get()->getSavedValue<bool>(key, fallback));
                row->addChild(toggler);
                auto text = CCLabelBMFont::create(label, "bigFont.fnt");
                text->setScale(0.4f);
                row->addChild(text);
                return row;
            }

            void addColor(CCMenu *row, char const *key)
            {
                auto swatch = CCSprite::createWithSpriteFrameName("whiteSquare60_001.png");
                swatch->setScale(0.3f);
                swatch->setColor(trailsettings::loadColor(key));
                Ref<CCSprite> target = swatch;
                row->addChild(CCMenuItemExt::createSpriteExtra(swatch, [key, target](CCMenuItemSpriteExtra *)
                                                               {
                    auto picker = ColorPickPopup::create(trailsettings::loadColor(key));
                    picker->setCallback([key, target](auto const &color)
                                        {
                        ccColor3B rgb{color.r, color.g, color.b};
                        trailsettings::saveColor(key, rgb);
                        target->setColor(rgb);
                        applySettings(); });
                    picker->show(); }));
            }

            void addMove(CCMenu *row, char const *frame, int from, int to)
            {
                row->addChild(CCMenuItemExt::createSpriteExtraWithFrameName(frame, 0.6f, [this, from, to](CCMenuItemSpriteExtra *)
                                                                            {
                    if (to < 0 || to >= static_cast<int>(kHitboxLayerCount))
                        return;
                    auto order = trailsettings::loadLayerOrder();
                    std::swap(order[from], order[to]);
                    trailsettings::saveLayerOrder(order);
                    applySettings();
                    rebuildLater(); }));
            }

            void addNumber(NumberSetting const &s)
            {
                auto row = newRow();
                auto text = CCLabelBMFont::create("", "bigFont.fnt");
                text->setScale(0.4f);
                Ref<CCLabelBMFont> shown = text;
                auto read = [s]
                {
                    auto mod = Mod::get();
                    auto stored = s.isInt
                                      ? static_cast<double>(mod->getSavedValue<int64_t>(s.key, static_cast<int64_t>(s.fallback)))
                                      : mod->getSavedValue<double>(s.key, s.fallback);
                    return std::round(stored * s.scale);
                };
                auto show = [s, shown, read, row]
                {
                    auto str = fmt::format("{}: {}{}", s.label, static_cast<int>(read()), s.isInt ? "" : "%");
                    shown->setString(str.c_str());
                    row->updateLayout();
                };
                auto change = [s, read, show](double delta)
                {
                    auto next = std::clamp(read() + delta, s.lo, s.hi);
                    if (s.isInt)
                        Mod::get()->setSavedValue<int64_t>(s.key, static_cast<int64_t>(next));
                    else
                        Mod::get()->setSavedValue<double>(s.key, next / s.scale);
                    show();
                    applySettings();
                };
                show();
                row->addChild(CCMenuItemExt::createSpriteExtraWithFrameName("edit_leftBtn_001.png", 0.6f, [change, s](CCMenuItemSpriteExtra *)
                                                                            { change(-s.step); }));
                row->addChild(text);
                row->addChild(CCMenuItemExt::createSpriteExtraWithFrameName("edit_rightBtn_001.png", 0.6f, [change, s](CCMenuItemSpriteExtra *)
                                                                            { change(s.step); }));
            }

            void populate()
            {
                toggleRow("Hitbox Trail", "hitbox-trail-enabled", true);
                toggleRow("Show on Death", "hitbox-trail-on-death", true);
                toggleRow("Always Show", "always-show-hitbox-trail", false);
                toggleRow("Only Player", "only-player-enabled", false, "cube-hitbox-enabled");
                toggleRow("Only Cube", "cube-hitbox-enabled", false, "only-player-enabled");
                toggleRow("Only Click and Release", "only-click-release-enabled", false);

                auto order = trailsettings::loadLayerOrder();
                constexpr int kLast = static_cast<int>(kHitboxLayerCount) - 1;
                for (int pos = kLast; pos >= 0; --pos)
                {
                    auto row = toggleRow(trailsettings::layerLabel(order[pos]), enabledKeyOf(order[pos]), true);
                    addColor(row, colorKeyOf(order[pos]));
                    addMove(row, "edit_upBtn_001.png", pos, pos + 1);
                    addMove(row, "edit_downBtn_001.png", pos, pos - 1);
                }

                auto reset = newRow();
                auto resetSprite = ButtonSprite::create("Reset");
                resetSprite->setScale(0.5f);
                reset->addChild(CCMenuItemExt::createSpriteExtra(resetSprite, [this](CCMenuItemSpriteExtra *)
                                                                 {
                    trailsettings::saveLayerOrder(trailsettings::kDefaultLayerOrder);
                    trailsettings::resetColors();
                    applySettings();
                    rebuildLater(); }));

                toggleRow("Batch Layer Overlap", "batch-layer-overlap", false);
                addNumber({"Length", "trail-length", 240, 1, 10, 1, 1024, true});
                toggleRow("Unlimited", "trail-no-limit", false);
                addNumber({"Opacity", "opacity", 1.0, 100, 5, 0, 100, false});
                addNumber({"Thickness", "thickness", 1.0, 100, 5, 1, 200, false});

                auto clicks = toggleRow("Color Clicks", "color-clicks", true);
                addColor(clicks, "click-color");
                addColor(clicks, "release-color");
                toggleRow("Draw on Top", "click-release-on-top", false);
                auto held = toggleRow("Color when Held", "color-when-held", true);
                addColor(held, "hold-color");
                toggleRow("Fade with Age", "fade-with-age", false);

                toggleRow("Fill Hitbox", "fill-hitbox", false);
                addNumber({"Fill Opacity", "fill-opacity", 0.25, 100, 5, 0, 100, false});

                auto content = m_scroll->m_contentLayer;
                for (auto child : CCArrayExt<CCNode *>(content->getChildren()))
                    child->updateLayout();
                content->updateLayout();
            }
        };
    }

    void showTrailSettings()
    {
        if (auto popup = SettingsPopup::create())
            popup->show();
    }
}

#endif
