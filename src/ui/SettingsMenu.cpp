#include "SettingsMenu.hpp"

#include <Geode/Geode.hpp>
#include <imgui-cocos.hpp>
#include <algorithm>

#include "../TrailNode.hpp"

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
        static auto *s_listener = listenForAllSettingChanges([](std::string_view key, std::shared_ptr<SettingV3>)
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

        checkbox("Square Hitbox", "square-hitbox-enabled", true);
        checkbox("Blue Hitbox", "blue-hitbox-enabled", true);
        checkbox("Circle Hitbox", "circle-hitbox-enabled", true);
        checkbox("Rotation Hitbox", "player-rotation", true);
        ImGui::Separator();

        auto length = static_cast<int>(mod->getSettingValue<int64_t>("trail-length"));
        if (ImGui::InputInt("Length", &length, 1, 10)) {
            mod->setSettingValue<int64_t>("trail-length", static_cast<int64_t>(std::clamp(length, 1, 1024)));
        }
        auto opacity = static_cast<float>(mod->getSettingValue<double>("opacity")) * 100.f;
        if (ImGui::InputFloat("Opacity", &opacity, 1.f, 10.f, "%.0f%%")) {
            mod->setSettingValue<double>("opacity", static_cast<double>(std::clamp(opacity, 0.f, 100.f)) / 100.0);
        }
        auto thickness = static_cast<float>(mod->getSettingValue<double>("thickness")) * 100.f;
        if (ImGui::InputFloat("Thickness", &thickness, 1.f, 10.f, "%.0f%%")) {
            mod->setSettingValue<double>("thickness", static_cast<double>(std::clamp(thickness, 1.f, 200.f)) / 100.0);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("min1%%/max200%%");
        ImGui::Separator();

        checkbox("Color Clicks", "color-clicks", true);
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

        static auto s_keybindListener = listenForKeybindSettingPresses("open-settings", [](Keybind const &, bool down, bool repeat, double) -> bool
                                                                       {
        if (down && !repeat) {
            s_settingsOpen = !s_settingsOpen;
            return true;
        }
        return false; });
    }
}
