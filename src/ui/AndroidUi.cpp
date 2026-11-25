#include "SettingsMenu.hpp"

#include <Geode/Geode.hpp>

#ifdef GEODE_IS_ANDROID

#include <Geode/ui/ColorPickPopup.hpp>
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
        constexpr float kRowWidth = 300.f;
        constexpr float kRowHeight = 26.f;
        constexpr float kRowSpacing = 29.f;
        constexpr float kFirstRowY = 72.f; // 팝업 중앙 기준, 첫 번째 행의 y 위치
        constexpr float kNavY = -107.f;    // 팝업 중앙 기준, 페이지 이동 버튼의 y 위치
        constexpr float kResetGap = 10.f; // 제목과 Reset 버튼 사이의 간격
        constexpr int kPageCount = 4;

        constexpr char const *kPageNames[kPageCount] = {"General", "Layers", "Trail", "Colors"};

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

        // 스크롤 대신 페이지 방식: 모든 행은 팝업 배경(m_bgSprite) 중앙을 기준으로 직접 배치한다.
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
            CCNode *m_root = nullptr;     // 팝업 배경의 중앙에 놓이는 기준 노드
            CCNode *m_pageRoot = nullptr; // 현재 페이지의 행들이 들어가는 노드
            CCLabelBMFont *m_pageLabel = nullptr;
            int m_page = 0;
            int m_rowIndex = 0;

            bool setupPopup()
            {
                if (!Popup::init(kPopupWidth, kPopupHeight))
                    return false;
                setTitle("Better Hitbox Trail");

                // m_mainLayer의 원점/크기에 의존하지 않도록 배경 스프라이트의 중앙을 기준점으로 쓴다.
                auto bgSize = m_bgSprite->getContentSize();
                auto anchor = m_bgSprite->getAnchorPoint();
                CCPoint center = m_bgSprite->getPosition() +
                                 CCPoint{(0.5f - anchor.x) * bgSize.width, (0.5f - anchor.y) * bgSize.height};

                m_root = CCNode::create();
                m_root->setPosition(center);
                m_mainLayer->addChild(m_root);

                m_pageRoot = CCNode::create();
                m_root->addChild(m_pageRoot);

                // 페이지 이동 버튼 (좌 / 라벨 / 우)
                auto nav = CCMenu::create();
                nav->setPosition({0.f, 0.f});
                m_root->addChild(nav);

                auto left = CCMenuItemExt::createSpriteExtraWithFrameName("edit_leftBtn_001.png", 0.8f, [this](CCMenuItemSpriteExtra *)
                                                                          { changePage(-1); });
                left->setPosition({-90.f, kNavY});
                nav->addChild(left);

                auto right = CCMenuItemExt::createSpriteExtraWithFrameName("edit_rightBtn_001.png", 0.8f, [this](CCMenuItemSpriteExtra *)
                                                                           { changePage(1); });
                right->setPosition({90.f, kNavY});
                nav->addChild(right);

                addTitleResetButton(); // 제목 오른쪽의 Reset 버튼 (모든 페이지에서 보임)

                m_pageLabel = CCLabelBMFont::create("", "bigFont.fnt");
                m_pageLabel->setScale(0.45f);
                m_pageLabel->setPosition({0.f, kNavY});
                m_root->addChild(m_pageLabel);

                populate();
                return true;
            }

            void changePage(int delta)
            {
                m_page = (m_page + delta + kPageCount) % kPageCount;
                rebuildLater();
            }

            void updatePageLabel()
            {
                auto str = fmt::format("{} ({}/{})", kPageNames[m_page], m_page + 1, kPageCount);
                m_pageLabel->setString(str.c_str());
            }

            void onRebuild(float)
            {
                m_pageRoot->removeAllChildren();
                populate();
                // 팝업이 열린 뒤에 새로 만든 버튼도 터치를 받도록 우선순위를 맞춘다.
                handleTouchPriority(this);
            }

            // 버튼 콜백 안에서 곧바로 버튼을 지우면 위험하므로 다음 프레임에 다시 만든다.
            void rebuildLater()
            {
                scheduleOnce(schedule_selector(SettingsPopup::onRebuild), 0.f);
            }

            CCMenu *newRow()
            {
                auto row = CCMenu::create();
                row->ignoreAnchorPointForPosition(false);
                row->setContentSize({kRowWidth, kRowHeight});
                row->setLayout(RowLayout::create()
                                   ->setAxisAlignment(AxisAlignment::Start)
                                   ->setAutoScale(false)
                                   ->setGap(8.f));
                row->setPosition({0.f, kFirstRowY - static_cast<float>(m_rowIndex) * kRowSpacing});
                ++m_rowIndex;
                m_pageRoot->addChild(row);
                return row;
            }

            CCMenu *toggleRow(char const *label, char const *key, bool fallback, char const *clears = nullptr)
            {
                auto row = newRow();
                auto toggler = CCMenuItemExt::createTogglerWithStandardSprites(0.6f, [this, key, clears](CCMenuItemToggler *t)
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
                text->setScale(0.5f);
                row->addChild(text);
                return row;
            }

            void addDotShapes()
            {
                auto row = newRow();
                auto add = [this, row](char const *label, bool circle)
                {
                    auto toggler = CCMenuItemExt::createTogglerWithStandardSprites(0.6f, [this, circle](CCMenuItemToggler *)
                                                                                   {
                        Mod::get()->setSavedValue<bool>("click-dot-circle", circle);
                        applySettings();
                        rebuildLater(); });
                    toggler->toggle(Mod::get()->getSavedValue<bool>("click-dot-circle", false) == circle);
                    row->addChild(toggler);
                    auto text = CCLabelBMFont::create(label, "bigFont.fnt");
                    text->setScale(0.5f);
                    row->addChild(text);
                };
                add("Rect", false);
                add("Circle", true);
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
                text->setScale(0.5f);
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

            // 제목(Better Hitbox Trail) 바로 오른쪽에 Reset 버튼을 놓는다. 제목 위치는 그대로 둔다.
            // 누르면 확인창이 뜨고, 확인하면 모든 설정을 기본값으로 되돌린다.
            void addTitleResetButton()
            {
                auto sprite = ButtonSprite::create("Reset");
                sprite->setScale(0.45f);
                auto button = CCMenuItemExt::createSpriteExtra(sprite, [this](CCMenuItemSpriteExtra *)
                                                               {
                    createQuickPopup("Reset", "Reset all settings to their default values?", "Cancel", "Reset",
                                     [this](FLAlertLayer *, bool confirmed)
                                     {
                                         if (!confirmed)
                                             return;
                                         trailsettings::resetAll();
                                         applySettings();
                                         rebuildLater();
                                     }); });

                // 제목과 같은 부모 안에 두면 좌표 변환 없이 제목의 오른쪽 끝에 바로 붙일 수 있다.
                button->setPosition(m_title->getPosition() +
                                    CCPoint{(m_title->getScaledContentSize().width + sprite->getScaledContentSize().width) / 2.f + kResetGap, 0.f});
                auto menu = CCMenu::create();
                menu->setPosition({0.f, 0.f});
                menu->addChild(button);
                m_title->getParent()->addChild(menu);
            }

            void populate()
            {
                m_rowIndex = 0;
                updatePageLabel();

                switch (m_page)
                {
                case 0: // General
                    toggleRow("Hitbox Trail", "hitbox-trail-enabled", true);
                    toggleRow("Show on Death", "hitbox-trail-on-death", true);
                    toggleRow("Always Show", "always-show-hitbox-trail", false);
                    toggleRow("Only Player", "only-player-enabled", false, "cube-hitbox-enabled");
                    toggleRow("Only Cube", "cube-hitbox-enabled", false, "only-player-enabled");
                    toggleRow("Only Click and Release", "only-click-release-enabled", false);
                    break;

                case 1: // Layers
                {
                    auto order = trailsettings::loadLayerOrder();
                    constexpr int kLast = static_cast<int>(kHitboxLayerCount) - 1;
                    for (int pos = kLast; pos >= 0; --pos)
                    {
                        auto row = toggleRow(trailsettings::layerLabel(order[pos]), enabledKeyOf(order[pos]), true);
                        addColor(row, colorKeyOf(order[pos]));
                        addMove(row, "edit_upBtn_001.png", pos, pos + 1);
                        addMove(row, "edit_downBtn_001.png", pos, pos - 1);
                    }

                    toggleRow("Batch Layer Overlap", "batch-layer-overlap", false);
                    break;
                }

                case 2: // Trail
                    addNumber({"Length", "trail-length", 240, 1, 10, 1, 1024, true});
                    toggleRow("Unlimited", "trail-no-limit", false);
                    addNumber({"Opacity", "opacity", 1.0, 100, 5, 0, 100, false});
                    addNumber({"Thickness", "thickness", 1.0, 100, 5, 1, 200, false});
                    toggleRow("Fade with Age", "fade-with-age", false);
                    break;

                default: // Colors
                {
                    auto clicks = toggleRow("Color Clicks", "color-clicks", true);
                    addColor(clicks, "click-color");
                    addColor(clicks, "release-color");
                    toggleRow("Draw on Top", "click-release-on-top", false);
                    auto held = toggleRow("Color when Held", "color-when-held", true);
                    addColor(held, "hold-color");
                    toggleRow("Fill Hitbox", "fill-hitbox", false);
                    addNumber({"Fill Opacity", "fill-opacity", 0.25, 100, 5, 0, 100, false});
                    auto dot = toggleRow("Click Dot", "click-dot", false);
                    addColor(dot, "click-dot-color");
                    addDotShapes();
                    break;
                }
                }

                for (auto child : CCArrayExt<CCNode *>(m_pageRoot->getChildren()))
                    child->updateLayout();
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
