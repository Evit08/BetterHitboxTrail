#include "SettingsMenu.hpp"

#include <Geode/Geode.hpp>

#include <Geode/ui/ColorPickPopup.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/Scrollbar.hpp>
#include <Geode/ui/TextInput.hpp>
#include <algorithm>
#include <cmath>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "../trail/TrailSettings.hpp"

using namespace geode::prelude;

namespace hitboxtrail
{
    namespace
    {
        constexpr float kPopupWidth = 350.f;
        constexpr float kBottomTrim = 20.f;
        constexpr float kPopupHeight = 260.f - kBottomTrim;
        constexpr float kInfoWidth = 26.f;
        constexpr float kRowWidth = 246.f;
        constexpr float kRowHeight = 26.f;
        constexpr float kRowSpacing = 29.f;
        constexpr float kContentX = 38.f;
        constexpr float kFirstRowY = 72.f; // 팝업 중앙 기준, 첫 번째 행의 y 위치
        constexpr float kSidebarX = -134.f;
        constexpr float kSidebarWidth = 48.f;
        constexpr float kSidebarSpacing = 24.f;
        constexpr float kSidebarButtonHeight = 21.f;
        constexpr float kToggleLabelMax = 205.f;
        constexpr float kNumberLabelMax = 120.f;
        constexpr float kChoiceLabelMax = 84.f;
        constexpr float kChoiceValueMax = 96.f;
        constexpr float kResetMargin = 6.f;
        constexpr float kResetScale = 0.54f;
        constexpr float kSwatchScale = 0.4f;
        constexpr float kTabPanelY = 0.f;
        constexpr float kTabPanelWidth = 70.f;
        constexpr float kTabPanelHeight = 178.f;
        constexpr float kContentPanelX = 38.f;
        constexpr float kContentPanelY = 0.f;
        constexpr float kContentPanelWidth = 262.f;
        constexpr float kContentPanelHeight = 178.f;
        constexpr ccColor3B kBackgroundTint{87, 135, 255};
        constexpr ccColor4B kBackgroundOverlay{52, 45, 255, 18};
        constexpr GLubyte kPanelOpacity = 90;
#ifdef GEODE_IS_ANDROID
        constexpr int kPageCount = 6;
#else
        constexpr int kPageCount = 7;
#endif

        constexpr char const *kPageNames[] = {"General", "Layers", "Trail", "Clicks", "Visual", "Thickness", "Menu"};

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

        void fitWidth(CCNode *node, float baseScale, float maxWidth)
        {
            auto width = node->getContentSize().width;
            node->setScale(width * baseScale > maxWidth ? maxWidth / width : baseScale);
        }

#ifndef GEODE_IS_ANDROID
        std::string keyName(enumKeyCodes key)
        {
            auto name = CCKeyboardDispatcher::get()->keyToString(key);
            return name && *name ? name : "?";
        }
#endif

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
            CCNode *m_rowParent = nullptr;
            float m_rowX = kContentX;
            float m_rowY = kFirstRowY;
            int m_page = 0;
            int m_rowIndex = 0;
            std::vector<CCMenu *> m_layerRows;
            CCMenu *m_dragRow = nullptr;
            int m_dragPos = 0;
            float m_dragBaseY = 0.f;

            ~SettingsPopup()
            {
#ifndef GEODE_IS_ANDROID
                cancelOpenKeyCapture();
#endif
            }

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

                m_bgSprite->setColor(kBackgroundTint);
                addOverlay();
                addOutline();

                m_root = CCNode::create();
                m_root->setPosition(center + CCPoint{0.f, -kBottomTrim / 2.f});
                m_mainLayer->addChild(m_root);
                addPanel(m_root, {kSidebarX, kTabPanelY}, {kTabPanelWidth, kTabPanelHeight});
                addPanel(m_root, {kContentPanelX, kContentPanelY}, {kContentPanelWidth, kContentPanelHeight});

                m_pageRoot = CCNode::create();
                m_root->addChild(m_pageRoot);

                addTitleResetButton(); // 제목 오른쪽의 Reset 버튼 (모든 페이지에서 보임)

                populate();
                return true;
            }

            void addOverlay()
            {
                auto size = m_bgSprite->getContentSize();
                m_bgSprite->addChild(CCLayerColor::create(kBackgroundOverlay, size.width, size.height));
            }

            void addOutline()
            {
                using Background = std::remove_pointer_t<decltype(m_bgSprite)>;
                auto size = m_bgSprite->getContentSize();
                auto outline = Background::create("GJ_square07.png");
                outline->setContentSize(size);
                outline->setPosition({size.width / 2.f, size.height / 2.f});
                m_bgSprite->addChild(outline);
            }

            void addPanel(CCNode *parent, CCPoint center, CCSize size)
            {
                using Background = std::remove_pointer_t<decltype(m_bgSprite)>;
                auto panel = Background::create("GJ_square01.png");
                panel->setContentSize(size);
                panel->setColor(ccBLACK);
                panel->setOpacity(kPanelOpacity);
                panel->setPosition(center);
                parent->addChild(panel, -1);
            }

            void selectPage(int page)
            {
                if (page == m_page)
                    return;
                m_page = page;
                rebuildLater();
            }

            void addSidebar()
            {
                auto menu = CCMenu::create();
                menu->setPosition({0.f, 0.f});
                m_pageRoot->addChild(menu);
                for (int i = 0; i < kPageCount; ++i)
                {
                    auto sprite = ButtonSprite::create(kPageNames[i], static_cast<int>(kSidebarWidth), true, "bigFont.fnt",
                                                       i == m_page ? "GJ_button_01.png" : "GJ_button_04.png", kSidebarButtonHeight, 0.4f);
                    auto button = CCMenuItemExt::createSpriteExtra(sprite, [this, i](CCMenuItemSpriteExtra *)
                                                                   { selectPage(i); });
                    button->setPosition({kSidebarX, kFirstRowY - static_cast<float>(i) * kSidebarSpacing});
                    menu->addChild(button);
                }
            }

            void onRebuild(float)
            {
#ifndef GEODE_IS_ANDROID
                cancelOpenKeyCapture();
#endif
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
                row->setPosition({m_rowX, m_rowY - static_cast<float>(m_rowIndex) * kRowSpacing});
                ++m_rowIndex;
                m_rowParent->addChild(row);
                return row;
            }

            CCMenu *toggleRow(char const *label, char const *key, bool fallback, char const *clears = nullptr, bool geodeSetting = false, char const *info = nullptr)
            {
                auto row = newRow();
                auto toggler = CCMenuItemExt::createTogglerWithStandardSprites(0.6f, [this, key, clears, geodeSetting](CCMenuItemToggler *t)
                                                                               {
                    auto on = !t->isToggled();
                    if (geodeSetting)
                        Mod::get()->setSettingValue<bool>(key, on);
                    else
                        Mod::get()->setSavedValue<bool>(key, on);
                    if (on && clears)
                    {
                        Mod::get()->setSavedValue<bool>(clears, false);
                        rebuildLater();
                    }
                    applySettings(); });
                toggler->toggle(geodeSetting ? Mod::get()->getSettingValue<bool>(key) : Mod::get()->getSavedValue<bool>(key, fallback));
                row->addChild(toggler);
                auto text = CCLabelBMFont::create(label, "bigFont.fnt");
                fitWidth(text, 0.5f, info ? kToggleLabelMax - kInfoWidth : kToggleLabelMax);
                row->addChild(text);
                if (info)
                    row->addChild(CCMenuItemExt::createSpriteExtraWithFrameName("GJ_infoIcon_001.png", 0.5f, [label, info](CCMenuItemSpriteExtra *)
                                                                                { FLAlertLayer::create(label, info, "OK")->show(); }));
                return row;
            }

            void addChoice(char const *label, char const *key, char const *fallback, std::vector<std::string> const &options)
            {
                auto row = newRow();
                auto text = CCLabelBMFont::create(fmt::format("{}:", label).c_str(), "bigFont.fnt");
                fitWidth(text, 0.5f, kChoiceLabelMax);

                auto longest = std::max_element(options.begin(), options.end(), [](auto const &a, auto const &b)
                                                { return a.size() < b.size(); });
                auto value = CCLabelBMFont::create(longest->c_str(), "bigFont.fnt");
                fitWidth(value, 0.5f, kChoiceValueMax);

                auto indexOf = [options, key, fallback]
                {
                    auto current = Mod::get()->getSavedValue<std::string>(key, fallback);
                    auto it = std::find(options.begin(), options.end(), current);
                    return it == options.end() ? 0 : static_cast<int>(it - options.begin());
                };
                value->setString(options[indexOf()].c_str());

                auto step = [row, value, options, key, indexOf](int delta)
                {
                    auto count = static_cast<int>(options.size());
                    auto next = (indexOf() + delta + count) % count;
                    Mod::get()->setSavedValue<std::string>(key, options[next]);
                    value->setString(options[next].c_str());
                    row->updateLayout();
                    applySettings();
                };

                row->addChild(CCMenuItemExt::createSpriteExtraWithFrameName("edit_leftBtn_001.png", 0.6f, [step](CCMenuItemSpriteExtra *)
                                                                            { step(-1); }));
                row->addChild(text);
                row->addChild(value);
                row->addChild(CCMenuItemExt::createSpriteExtraWithFrameName("edit_rightBtn_001.png", 0.6f, [step](CCMenuItemSpriteExtra *)
                                                                            { step(1); }));
            }

#ifndef GEODE_IS_ANDROID
            void addOpenKey()
            {
                auto row = newRow();
                auto text = CCLabelBMFont::create("Open Menu:", "bigFont.fnt");
                text->setScale(0.5f);
                row->addChild(text);

                auto sprite = ButtonSprite::create(keyName(trailsettings::loadOpenMenuKey()).c_str());
                sprite->setScale(0.6f);
                Ref<ButtonSprite> shown = sprite;
                row->addChild(CCMenuItemExt::createSpriteExtra(sprite, [shown](CCMenuItemSpriteExtra *)
                                                               {
                    shown->setString("...");
                    beginOpenKeyCapture([shown](enumKeyCodes key)
                                        {
                        if (key != KEY_Escape)
                            trailsettings::saveOpenMenuKey(key);
                        shown->setString(keyName(trailsettings::loadOpenMenuKey()).c_str()); }); }));
            }
#endif

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
                    toggler->toggle(Mod::get()->getSavedValue<bool>("click-dot-circle", true) == circle);
                    row->addChild(toggler);
                    auto text = CCLabelBMFont::create(label, "bigFont.fnt");
                    text->setScale(0.5f);
                    row->addChild(text);
                };
                add("Circle", true);
                add("Rect", false);
            }

            void addColor(CCMenu *row, char const *key)
            {
                auto swatch = CCSprite::createWithSpriteFrameName("whiteSquare60_001.png");
                swatch->setScale(kSwatchScale);
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

            bool ccTouchBegan(CCTouch *touch, CCEvent *event) override
            {
                auto local = m_rowParent->convertToNodeSpace(touch->getLocation());
                for (size_t i = 0; i < m_layerRows.size(); ++i)
                {
                    auto row = m_layerRows[i];
                    if (row->boundingBox().containsPoint(local))
                    {
                        m_dragRow = row;
                        m_dragPos = static_cast<int>(m_layerRows.size() - 1 - i);
                        m_dragBaseY = row->getPositionY();
                        row->setZOrder(1);
                        break;
                    }
                }
                return Popup::ccTouchBegan(touch, event);
            }

            void ccTouchMoved(CCTouch *touch, CCEvent *event) override
            {
                if (!m_dragRow)
                    return Popup::ccTouchMoved(touch, event);
                m_dragRow->setPositionY(m_dragBaseY + touch->getLocation().y - touch->getStartLocation().y);
            }

            void ccTouchEnded(CCTouch *touch, CCEvent *event) override
            {
                if (!m_dragRow)
                    return Popup::ccTouchEnded(touch, event);
                auto last = static_cast<int>(m_layerRows.size()) - 1;
                auto shift = static_cast<int>(std::lround((m_dragRow->getPositionY() - m_dragBaseY) / kRowSpacing));
                auto from = m_dragPos;
                auto to = std::clamp(from + shift, 0, last);
                m_dragRow = nullptr;
                if (to != from)
                {
                    auto order = trailsettings::loadLayerOrder();
                    auto step = to > from ? 1 : -1;
                    for (int i = from; i != to; i += step)
                        std::swap(order[i], order[i + step]);
                    trailsettings::saveLayerOrder(order);
                    applySettings();
                }
                rebuildLater();
            }

            void ccTouchCancelled(CCTouch *touch, CCEvent *event) override
            {
                if (!m_dragRow)
                    return Popup::ccTouchCancelled(touch, event);
                ccTouchEnded(touch, event);
            }

            void addNumber(NumberSetting const &s)
            {
                auto row = newRow();
                auto text = CCLabelBMFont::create(fmt::format("{}:", s.label).c_str(), "bigFont.fnt");
                fitWidth(text, 0.5f, kNumberLabelMax);

                auto input = TextInput::create(70.f, "", "bigFont.fnt");
                input->setScale(0.8f);
                input->setLayoutOptions(AxisLayoutOptions::create()->setPrevGap(2.f));
                input->setCommonFilter(CommonFilter::Uint);
                input->setMaxCharCount(static_cast<int>(std::to_string(static_cast<int>(s.hi)).size()));
                Ref<TextInput> shown = input;

                auto read = [s]
                {
                    auto mod = Mod::get();
                    auto stored = s.isInt
                                      ? static_cast<double>(mod->getSavedValue<int64_t>(s.key, static_cast<int64_t>(s.fallback)))
                                      : mod->getSavedValue<double>(s.key, s.fallback);
                    return std::round(stored * s.scale);
                };
                auto show = [shown, read]
                {
                    shown->setString(std::to_string(static_cast<int>(read())), false);
                };
                auto store = [s](double value)
                {
                    auto next = std::clamp(value, s.lo, s.hi);
                    if (s.isInt)
                        Mod::get()->setSavedValue<int64_t>(s.key, static_cast<int64_t>(next));
                    else
                        Mod::get()->setSavedValue<double>(s.key, next / s.scale);
                    applySettings();
                };
                auto change = [store, read, show](double delta)
                {
                    store(read() + delta);
                    show();
                };
                input->setCallback([store](std::string const &typed)
                                   {
                    auto value = utils::numFromString<int>(typed);
                    if (value.isOk())
                        store(value.unwrap()); });
                show();

                row->addChild(CCMenuItemExt::createSpriteExtraWithFrameName("edit_leftBtn_001.png", 0.6f, [change, s](CCMenuItemSpriteExtra *)
                                                                            { change(-s.step); }));
                row->addChild(text);
                row->addChild(input);
                if (!s.isInt)
                {
                    auto percent = CCLabelBMFont::create("%", "bigFont.fnt");
                    percent->setScale(0.4f);
                    percent->setColor({170, 170, 170});
                    percent->setAnchorPoint({1.f, 0.f});
                    percent->setPosition({input->getContentSize().width - 4.f, 3.f});
                    input->addChild(percent);
                }
                row->addChild(CCMenuItemExt::createSpriteExtraWithFrameName("edit_rightBtn_001.png", 0.6f, [change, s](CCMenuItemSpriteExtra *)
                                                                            { change(s.step); }));
            }

            // 누르면 확인창이 뜨고, 확인하면 모든 설정을 기본값으로 되돌린다.
            void addTitleResetButton()
            {
                auto sprite = ButtonSprite::create("Reset");
                sprite->setScale(kResetScale);
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

                button->setPosition({m_title->getPositionX() + (kPopupWidth - sprite->getScaledContentSize().width) / 2.f - kResetMargin,
                                     m_title->getPositionY()});
                auto menu = CCMenu::create();
                menu->setPosition({0.f, 0.f});
                menu->addChild(button);
                m_title->getParent()->addChild(menu);
            }

            void populate()
            {
                m_rowIndex = 0;
                m_layerRows.clear();
                m_rowParent = m_pageRoot;
                m_rowX = kContentX;
                m_rowY = kFirstRowY;
                addSidebar();

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
                        m_layerRows.push_back(row);
                    }

                    toggleRow("Batch Layer Overlap", "batch-layer-overlap", false, nullptr, false,
                              "Draws hitboxes in row order across the whole trail.");
                    break;
                }

                case 2: // Trail
                    addChoice("Capture Rate", "trail-capture-rate", "240",
                              {"240", "120", "80", "60", "48", "40", "30", "24", "20", "16", "15", "12", "10", "8", "6", "5", "4", "3", "2", "1"});
                    addNumber({"Length", "trail-length", 240, 1, 10, 1, 1024, true});
                    toggleRow("Unlimited", "trail-no-limit", false);
                    addNumber({"Opacity", "opacity", 1.0, 100, 5, 0, 100, false});
                    addNumber({"Thickness", "thickness", 1.0, 100, 5, 1, 200, false});
                    toggleRow("Hitbox Between Frames", "hitbox-between-frames", true, nullptr, false,
                              "When using CBF or CBS, this also records a hitbox position at the exact sub-tick moment a click/release occurs. "
                              "Turn off to only record hitbox positions on regular game ticks, since sub-tick hitboxes aren't actual collision points.");
                    break;

                case 3: // Clicks
                {
                    auto clicks = toggleRow("Color Clicks", "color-clicks", true);
                    addColor(clicks, "click-color");
                    addColor(clicks, "release-color");
                    toggleRow("Draw on Top", "click-release-on-top", false);
                    auto held = toggleRow("Color when Held", "color-when-held", true);
                    addColor(held, "hold-color");
                    auto dot = toggleRow("Click Dot", "click-dot", false);
                    addColor(dot, "click-dot-color");
                    addDotShapes();
                    toggleRow("Blue Hitbox Color Clicks", "blue-hitbox-color-clicks", false);
                    break;
                }

                case 4: // Visual
                    toggleRow("Fill Hitbox", "fill-hitbox", false);
                    addNumber({"Fill Opacity", "fill-opacity", 0.25, 100, 5, 0, 100, false});
                    toggleRow("Fade with Age", "fade-with-age", false);
                    break;

                case 5: // Thickness
                {
                    auto height = static_cast<float>(trailsettings::kThicknessEntries.size()) * kRowSpacing + 8.f;
                    auto scroll = ScrollLayer::create(CCSize{kContentPanelWidth, kContentPanelHeight}, true, true);
                    scroll->ignoreAnchorPointForPosition(false);
                    scroll->setAnchorPoint({0.f, 0.f});
                    scroll->setPosition({kContentPanelX - kContentPanelWidth / 2.f, kContentPanelY - kContentPanelHeight / 2.f});
                    scroll->m_contentLayer->setContentSize({kContentPanelWidth, height});
                    m_pageRoot->addChild(scroll);
                    m_rowParent = scroll->m_contentLayer;
                    m_rowX = kContentPanelWidth / 2.f;
                    m_rowY = height - 4.f - kRowHeight / 2.f;
                    for (auto const &entry : trailsettings::kThicknessEntries)
                        addNumber({entry.label, entry.key, entry.fallback, 100, 10, 1, 1000, false});
                    scroll->scrollToTop();
                    auto bar = Scrollbar::create(scroll, Scrollbar::Style::GDScrollbar);
                    bar->setPosition({kContentPanelX + kContentPanelWidth / 2.f - 6.f, kContentPanelY});
                    m_pageRoot->addChild(bar);
                    break;
                }

#ifndef GEODE_IS_ANDROID
                default: // Menu
                    addOpenKey();
                    addChoice("Menu Font", "menu-font", "Ubuntu Mono", {"DejaVu Sans Mono", "Ubuntu Mono", "Hack", "Liberation Mono"});
                    toggleRow("Hide Pause Menu Button", "hide-pause-menu-button", false, nullptr, true);
                    break;
#endif
                }

                for (auto child : CCArrayExt<CCNode *>(m_rowParent->getChildren()))
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
