#include <Geode/modify/PauseLayer.hpp>

#include "../ModDetection.hpp"
#include "../TrailNode.hpp"
#include "../ui/SettingsMenu.hpp"

using namespace geode::prelude;
using namespace hitboxtrail;

namespace
{
    void updateTrailButton(PauseLayer *layer, bool hidden)
    {
        if (!layer || !layer->m_mainLayer)
            return;

        auto existing = layer->getChildByIDRecursive("trail-settings-button"_spr);

        if (hidden)
        {
            if (!existing)
                return;
            auto parent = existing->getParent();
            existing->removeFromParent();
            if (parent)
            {
                if (parent->getID() == "trail-settings-fallback-menu"_spr)
                    parent->removeFromParent();
                else
                    parent->updateLayout();
            }
            return;
        }

        if (existing)
            return;

        auto sprite = CircleButtonSprite::createWithSprite("trail-icon.png"_spr, 1.2f, CircleBaseColor::Green, CircleBaseSize::Medium);
        sprite->setScale(.6f);

        auto button = CCMenuItemExt::createSpriteExtra(sprite, [](CCMenuItemSpriteExtra *)
                                                       { showTrailSettings(); });
        button->setID("trail-settings-button"_spr);

        auto rightMenu = typeinfo_cast<CCMenu *>(layer->m_mainLayer->getChildByID("right-button-menu"));
        if (rightMenu)
        {
            rightMenu->addChild(button);
            rightMenu->updateLayout();
            return;
        }

        auto menu = CCMenu::create();
        menu->setID("trail-settings-fallback-menu"_spr);
        menu->addChild(button);
        auto winSize = CCDirector::get()->getWinSize();
        constexpr float kTrailButtonOffsetY = 132.f;
        menu->setPosition({winSize.width - 50.f, winSize.height - kTrailButtonOffsetY});
        layer->addChild(menu);
    }
}

#ifndef GEODE_IS_MOBILE
$execute
{
    static auto s_hideButtonListener = listenForSettingChanges<bool>("hide-pause-menu-button", [](bool hidden)
    {
        auto scene = CCDirector::get()->getRunningScene();
        if (!scene)
            return;
        updateTrailButton(scene->getChildByType<PauseLayer>(0), hidden);
    });
}
#endif

class $modify(TrailPauseLayer, PauseLayer)
{
    void customSetup()
    {
        PauseLayer::customSetup();
        refreshTrail();
        schedule(schedule_selector(TrailPauseLayer::refreshTrailTick), 0.1f);

        bool hidden = false;
#ifndef GEODE_IS_MOBILE
        hidden = Mod::get()->getSettingValue<bool>("hide-pause-menu-button");
#endif
        updateTrailButton(this, hidden);
    }
    void onEdit(CCObject *sender)
    {
        if (auto layer = GJBaseGameLayer::get())
            s_carry = getTrail(layer);
        PauseLayer::onEdit(sender);
    }
    void onExit()
    {
        unschedule(schedule_selector(TrailPauseLayer::refreshTrailTick));
        PauseLayer::onExit();
    }
    void refreshTrailTick(float)
    {
        if (auto layer = GJBaseGameLayer::get())
            if (auto trail = getTrail(layer))
                if (ModDetection::changed())
                    trail->refreshDrawing(layer);
    }
    void refreshTrail()
    {
        if (auto layer = GJBaseGameLayer::get())
            if (auto trail = getTrail(layer))
            {
                ModDetection::refresh();
                trail->refreshDrawing(layer);
            }
    }
};
