#include "SettingsMenu.hpp"

#include <Geode/Geode.hpp>
#include <imgui-cocos.hpp>
#include <algorithm>
#include <array>
#include <cmath>

#include "../TrailNode.hpp"
#include "../trail/TrailSettings.hpp"

using namespace geode::prelude;

namespace hitboxtrail
{
    namespace
    {
        bool s_settingsOpen = false;

        void refreshTrail()
        {
            if (auto layer = GJBaseGameLayer::get())
                if (auto trail = getTrail(layer))
                {
                    trail->invalidateRenderSettings();
                    trail->refreshDrawing(layer);
                }
        }
    }

    void registerSettingsMenu()
    {
        listenForAllSettingChanges([](std::string_view key, std::shared_ptr<SettingV3>)
                                   {
            if (key == "open-settings") return;
            refreshTrail(); }, Mod::get());

        ImGuiCocos::get().setup([] {}).draw([]
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
                ImGui::SetNextWindowSize(ImVec2(340.f, 0.f), ImGuiCond_FirstUseEver);
                if (!ImGui::Begin("Better Hitbox Trail", &s_settingsOpen)) {
                    ImGui::End();
                    return;
                }
                ImGui::PushItemWidth(110.f);
                auto checkbox = [&](char const* label, char const* key, bool def) {
                    bool value = mod->getSavedValue<bool>(key, def);
                    if (ImGui::Checkbox(label, &value)) {
                        mod->setSavedValue<bool>(key, value);
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
                if (ImGui::SmallButton("Reset")) {
                    trailsettings::saveLayerOrder(trailsettings::kDefaultLayerOrder);
                    changed = true;
                }
                ImGui::SameLine();
                ImGui::TextDisabled("Drag a row to reorder");
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
                checkbox("Draw on Top", "click-release-on-top", false);
                checkbox("Color when Held", "color-when-held", true);
                checkbox("Fade with Age", "fade-with-age", false);
                ImGui::Separator();

                checkbox("Fill Hitbox", "fill-hitbox", false);
                ImGui::SameLine();
                auto fillOpacity = static_cast<float>(mod->getSavedValue<double>("fill-opacity", 0.25)) * 100.f;
                if (ImGui::InputFloat("Opacity##fill", &fillOpacity, 1.f, 10.f, "%.0f%%")) {
                    mod->setSavedValue<double>("fill-opacity", static_cast<double>(std::clamp(fillOpacity, 0.f, 100.f)) / 100.0);
                    changed = true;
                }
                if (changed) refreshTrail();
                ImGui::PopItemWidth();
                ImGui::End(); });

        listenForKeybindSettingPresses("open-settings", [](Keybind const &, bool down, bool repeat, double) -> bool
                                       {
                if (down && !repeat) {
                    s_settingsOpen = !s_settingsOpen;
                    return true;
                }
                return false; });
    }
}
