#include "SettingsMenu.hpp"

#include <Geode/Geode.hpp>
#ifndef GEODE_IS_ANDROID
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#include <imgui-cocos.hpp>
#include <misc/freetype/imgui_freetype.h>
#endif
#include <algorithm>
#include <array>
#include <cmath>

#include "../TrailNode.hpp"
#include "../trail/TrailSettings.hpp"

using namespace geode::prelude;

namespace hitboxtrail
{
    void applySettings()
    {
        if (auto layer = GJBaseGameLayer::get())
            if (auto trail = getTrail(layer))
            {
                trail->invalidateRenderSettings();
                trail->refreshDrawing(layer);
            }
    }

#ifndef GEODE_IS_ANDROID
    namespace
    {
        bool s_settingsOpen = false;
        std::function<void(cocos2d::enumKeyCodes)> s_keyCapture;

        constexpr float kBaseFontSize = 13.f; // font size at scale 1.0
        struct MenuFont
        {
            char const *name; // value of the "menu-font" setting
            char const *file;
            ImFont *font;
        };
        std::array<MenuFont, 4> s_menuFonts{{
            {"DejaVu Sans Mono", "DejaVuSansMono.ttf", nullptr},
            {"Ubuntu Mono", "UbuntuMono-Regular.ttf", nullptr},
            {"Hack", "Hack-Regular.ttf", nullptr},
            {"Liberation Mono", "LiberationMono-Regular.ttf", nullptr},
        }};
        ImFont *s_fallbackFont = nullptr; // used when a font file is missing

        // FreeType rasterizes these fonts at whatever size is pushed, snapped to whole pixels.
        // Must run inside setup(): ImGui is re-initialized when toggling fullscreen.
        void loadMenuFonts()
        {
            ImFontConfig config;
            // No anti-aliasing, so glyph edges are never blurry at any size.
            config.FontLoaderFlags = ImGuiFreeTypeLoaderFlags_MonoHinting | ImGuiFreeTypeLoaderFlags_Monochrome;
            auto &io = ImGui::GetIO();
            s_fallbackFont = io.Fonts->AddFontDefault(&config);
            for (auto &entry : s_menuFonts)
            {
                entry.font = nullptr;
                auto path = Mod::get()->getResourcesDir() / entry.file;
                if (!path.exists())
                {
                    log::warn("Menu font not found: {}", entry.file);
                    continue;
                }
                entry.font = io.Fonts->AddFontFromFileTTF(path.c_str(), kBaseFontSize, &config);
            }
        }

        ImFont *selectedMenuFont()
        {
            auto name = Mod::get()->getSavedValue<std::string>("menu-font", "Ubuntu Mono");
            for (auto const &entry : s_menuFonts)
                if (entry.font && name == entry.name)
                    return entry.font;
            return s_fallbackFont;
        }
    }
#endif

#ifndef GEODE_IS_ANDROID
    void beginOpenKeyCapture(std::function<void(cocos2d::enumKeyCodes)> done)
    {
        s_keyCapture = std::move(done);
    }

    void cancelOpenKeyCapture()
    {
        s_keyCapture = nullptr;
    }
#endif

    void registerSettingsMenu()
    {
#ifndef GEODE_IS_ANDROID
        ImGuiCocos::get().setup([] { loadMenuFonts(); }).draw([]
                                            {
                static bool s_wasOpen = false;
                bool opened = s_settingsOpen && !s_wasOpen;
                if (s_settingsOpen != s_wasOpen) {
                    if (s_settingsOpen) {
                        PlatformToolbox::showCursor();
                    } else {
                        auto scene = cocos2d::CCDirector::sharedDirector()->getRunningScene();
                        auto paused = scene && scene->getChildByType<PauseLayer>(0);
                        if (PlayLayer::get() && !paused) PlatformToolbox::hideCursor();
                    }
                    s_wasOpen = s_settingsOpen;
                }
                if (!s_settingsOpen) return;

                auto mod = Mod::get();
                bool changed = opened;
                // The menu is scaled in proportion to the window width.
                constexpr float kBaseWidth = 320.f; // scale 1.0 at this width
                static float s_width = kBaseWidth;  // window width in the previous frame
                // Reset sets this so the window goes back to its default size on the next frame.
                static bool s_resetSize = false;

                float wanted = std::clamp(s_width / kBaseWidth, 0.75f, 2.5f);
                float fontSize = std::round(kBaseFontSize * wanted);
                // Match the layout to the rounded font size.
                float scale = fontSize / kBaseFontSize;

                // Font and style apply to this menu only; both are restored on scope exit.
                struct MenuLook {
                    ImGuiStyle saved = ImGui::GetStyle();
                    MenuLook(float fontSize, float scale) {
                        ImGui::PushFont(selectedMenuFont(), fontSize);
                        ImGui::GetStyle().ScaleAllSizes(scale);
                    }
                    ~MenuLook() {
                        ImGui::GetStyle() = saved;
                        ImGui::PopFont();
                    }
                } menuLook(fontSize, scale);

                ImGui::SetNextWindowSize(ImVec2(kBaseWidth, 0.f), s_resetSize ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
                s_resetSize = false;
                if (!ImGui::Begin("Better Hitbox Trail", &s_settingsOpen, ImGuiWindowFlags_NoScrollbar)) {
                    ImGui::End();
                    return;
                }
                s_width = ImGui::GetWindowWidth();

                // Reset button in the title bar, right next to the title text (the title itself is untouched).
                {
                    auto const& style = ImGui::GetStyle();
                    auto pos = ImGui::GetWindowPos();
                    auto cursor = ImGui::GetCursorScreenPos(); // content start, just below the title bar
                    // The title bar is outside the content clip rect, so widen it for this button.
                    ImGui::PushClipRect(pos, ImVec2(pos.x + ImGui::GetWindowWidth(), cursor.y), false);
                    // The title text starts after the collapse arrow.
                    ImGui::SetCursorScreenPos(ImVec2(pos.x + style.FramePadding.x + ImGui::GetFontSize() + style.ItemInnerSpacing.x +
                                                         ImGui::CalcTextSize("Better Hitbox Trail").x + 12.f * scale,
                                                     pos.y + style.FramePadding.y));
                    if (ImGui::SmallButton("Reset"))
                        ImGui::OpenPopup("Reset Settings");
                    ImGui::PopClipRect();
                    ImGui::SetCursorScreenPos(cursor);
                }

                if (ImGui::BeginPopupModal("Reset Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::TextUnformatted("Reset all settings to their default values?");
                    if (ImGui::Button("Reset")) {
                        trailsettings::resetAll();
                        s_resetSize = true;
                        changed = true;
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel"))
                        ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                }

                ImGui::PushItemWidth(110.f * scale);
                auto checkbox = [&](char const* label, char const* key, bool def) {
                    bool value = mod->getSavedValue<bool>(key, def);
                    if (ImGui::Checkbox(label, &value)) {
                        mod->setSavedValue<bool>(key, value);
                        changed = true;
                    }
                };

                auto colorEdit = [&](char const* label, char const* key) {
                    auto c = trailsettings::loadColor(key);
                    float v[3] = {c.r / 255.f, c.g / 255.f, c.b / 255.f};
                    if (ImGui::ColorEdit3(label, v, ImGuiColorEditFlags_NoInputs)) {
                        trailsettings::saveColor(key, ccColor3B{
                            static_cast<GLubyte>(std::lround(v[0] * 255.f)),
                            static_cast<GLubyte>(std::lround(v[1] * 255.f)),
                            static_cast<GLubyte>(std::lround(v[2] * 255.f))});
                        changed = true;
                    }
                };

                checkbox("Hitbox Trail", "hitbox-trail-enabled", true);
                checkbox("Show on Death", "hitbox-trail-on-death", true);
                checkbox("Always Show", "always-show-hitbox-trail", false);
                ImGui::Separator();

                bool onlyPlayer = mod->getSavedValue<bool>("only-player-enabled", false);
                if (ImGui::Checkbox("Only Player", &onlyPlayer)) {
                    mod->setSavedValue<bool>("only-player-enabled", onlyPlayer);
                    if (onlyPlayer) mod->setSavedValue<bool>("cube-hitbox-enabled", false);
                    changed = true;
                }
                bool cubeHitbox = mod->getSavedValue<bool>("cube-hitbox-enabled", false);
                if (ImGui::Checkbox("Only Cube", &cubeHitbox)) {
                    mod->setSavedValue<bool>("cube-hitbox-enabled", cubeHitbox);
                    if (cubeHitbox) mod->setSavedValue<bool>("only-player-enabled", false);
                    changed = true;
                }
                checkbox("Only Click and Release", "only-click-release-enabled", false);
                ImGui::Separator();

                // The rows are listed in layer order: the top row is drawn on top of the others.
                // Dragging a row moves the whole row (toggle included) up or down in the list.
                auto order = trailsettings::loadLayerOrder();
                constexpr int kLast = static_cast<int>(kHitboxLayerCount) - 1;
                int moveFrom = -1;
                int moveTo = -1;
                auto enabledKeyOf = [](HitboxLayer layer) -> char const* {
                    switch (layer) {
                    case HitboxLayer::Main: return "square-hitbox-enabled";
                    case HitboxLayer::Blue: return "blue-hitbox-enabled";
                    case HitboxLayer::Circle: return "circle-hitbox-enabled";
                    case HitboxLayer::Rotation: return "rotation-hitbox-enabled";
                    }
                    return "";
                };
                auto colorKeyOf = [](HitboxLayer layer) -> char const* {
                    switch (layer) {
                    case HitboxLayer::Main: return "square-hitbox-color";
                    case HitboxLayer::Blue: return "blue-hitbox-color";
                    case HitboxLayer::Circle: return "circle-hitbox-color";
                    case HitboxLayer::Rotation: return "rotation-hitbox-color";
                    }
                    return "";
                };
                // A plain click toggles the hitbox; pressing and dragging the row reorders it.
                static bool s_rowDragged = false;
                auto rowStartY = ImGui::GetCursorScreenPos().y;
                auto rowStep = ImGui::GetFrameHeightWithSpacing();
                for (int pos = kLast; pos >= 0; --pos) {
                    auto label = trailsettings::layerLabel(order[pos]);
                    auto enabledKey = enabledKeyOf(order[pos]);
                    ImGui::PushID(enabledKey);
                    bool value = mod->getSavedValue<bool>(enabledKey, true);
                    bool pressed = ImGui::Checkbox(label, &value);
                    bool active = ImGui::IsItemActive();
                    ImGui::SameLine();
                    colorEdit("##color", colorKeyOf(order[pos]));
                    if (active && ImGui::IsMouseDragging(0, 5.f))
                        s_rowDragged = true;
                    if (active && s_rowDragged) {
                        // The row follows the mouse: find which list slot the cursor is in.
                        auto slot = static_cast<int>(std::floor((ImGui::GetMousePos().y - rowStartY) / rowStep));
                        slot = std::clamp(slot, 0, kLast);
                        int target = kLast - slot; // top slot = top layer
                        if (target != pos) {
                            moveFrom = pos;
                            moveTo = target;
                        }
                    }
                    // Releasing after a drag must not flip the checkbox.
                    if (pressed && !s_rowDragged) {
                        mod->setSavedValue<bool>(enabledKey, value);
                        changed = true;
                    }
                    ImGui::PopID();
                }
                if (!ImGui::IsMouseDown(0)) s_rowDragged = false;
                if (moveFrom >= 0) {
                    auto moved = order[moveFrom];
                    if (moveTo > moveFrom)
                        for (int i = moveFrom; i < moveTo; ++i) order[i] = order[i + 1];
                    else
                        for (int i = moveFrom; i > moveTo; --i) order[i] = order[i - 1];
                    order[moveTo] = moved;
                    trailsettings::saveLayerOrder(order);
                    changed = true;
                }
                ImGui::TextDisabled("Drag to change which hitbox is on top");
                checkbox("Batch Layer Overlap", "batch-layer-overlap", false);
                ImGui::Separator();

                bool noLimit = mod->getSavedValue<bool>("trail-no-limit", false);
                auto length = static_cast<int>(mod->getSavedValue<int64_t>("trail-length", 240));
                ImGui::BeginDisabled(noLimit);
                if (ImGui::InputInt("Length", &length, 1, 10)) {
                    mod->setSavedValue<int64_t>("trail-length", static_cast<int64_t>(std::clamp(length, 1, 1024)));
                    changed = true;
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                checkbox("Unlimited", "trail-no-limit", false);
                auto opacity = static_cast<float>(mod->getSavedValue<double>("opacity", 1.0)) * 100.f;
                if (ImGui::InputFloat("Opacity", &opacity, 1.f, 10.f, "%.0f%%")) {
                    mod->setSavedValue<double>("opacity", static_cast<double>(std::clamp(opacity, 0.f, 100.f)) / 100.0);
                    changed = true;
                }
                auto thickness = static_cast<float>(mod->getSavedValue<double>("thickness", 1.0)) * 100.f;
                if (ImGui::InputFloat("Thickness", &thickness, 1.f, 10.f, "%.0f%%")) {
                    mod->setSavedValue<double>("thickness", static_cast<double>(std::clamp(thickness, 1.f, 200.f)) / 100.0);
                    changed = true;
                }
                ImGui::SameLine();
                ImGui::TextDisabled("min1%%/max200%%");
                ImGui::Separator();

                checkbox("Color Clicks", "color-clicks", true);
                ImGui::SameLine();
                colorEdit("##click", "click-color");
                ImGui::SameLine();
                colorEdit("##release", "release-color");
                ImGui::SameLine();
                checkbox("Draw on Top", "click-release-on-top", false);
                checkbox("Color when Held", "color-when-held", true);
                ImGui::SameLine();
                colorEdit("##hold", "hold-color");
                checkbox("Click Dot", "click-dot", false);
                ImGui::SameLine();
                colorEdit("##dot", "click-dot-color");
                ImGui::SameLine(0.f, 30.f * scale);
                ImGui::BeginDisabled(!mod->getSavedValue<bool>("click-dot", false));
                bool dotCircle = mod->getSavedValue<bool>("click-dot-circle", true);
                bool dotRect = !dotCircle;
                if (ImGui::Checkbox("Circle", &dotCircle)) {
                    mod->setSavedValue<bool>("click-dot-circle", true);
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::Checkbox("Rect", &dotRect)) {
                    mod->setSavedValue<bool>("click-dot-circle", false);
                    changed = true;
                }
                ImGui::EndDisabled();
                ImGui::Separator();

                checkbox("Fill Hitbox", "fill-hitbox", false);
                ImGui::SameLine();
                auto fillOpacity = static_cast<float>(mod->getSavedValue<double>("fill-opacity", 0.25)) * 100.f;
                if (ImGui::InputFloat("Opacity##fill", &fillOpacity, 1.f, 10.f, "%.0f%%")) {
                    mod->setSavedValue<double>("fill-opacity", static_cast<double>(std::clamp(fillOpacity, 0.f, 100.f)) / 100.0);
                    changed = true;
                }
                checkbox("Fade with Age", "fade-with-age", false);
                if (changed) applySettings();
                ImGui::PopItemWidth();
                ImGui::End(); });
#endif
    }
}

#ifndef GEODE_IS_ANDROID
class $modify(OpenKeyDispatcher, CCKeyboardDispatcher)
{
    bool dispatchKeyboardMSG(enumKeyCodes key, bool down, bool repeat, double timestamp)
    {
        if (down && !repeat)
        {
            if (hitboxtrail::s_keyCapture)
            {
                auto done = std::move(hitboxtrail::s_keyCapture);
                hitboxtrail::s_keyCapture = nullptr;
                done(key);
                return true;
            }
            if (key == hitboxtrail::trailsettings::loadOpenMenuKey() &&
                !CCIMEDispatcher::sharedDispatcher()->hasDelegate() &&
                !ImGui::GetIO().WantTextInput)
            {
                hitboxtrail::s_settingsOpen = !hitboxtrail::s_settingsOpen;
                return true;
            }
        }
        return CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, repeat, timestamp);
    }
};
#endif
