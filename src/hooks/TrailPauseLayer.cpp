#include <Geode/modify/PauseLayer.hpp>

#include "../ModDetection.hpp"
#include "../TrailNode.hpp"
#include "../ui/SettingsMenu.hpp"

using namespace geode::prelude;
using namespace hitboxtrail;

class $modify(TrailPauseLayer, PauseLayer)
{
    void customSetup()
    {
        PauseLayer::customSetup();
        refreshTrail();
        schedule(schedule_selector(TrailPauseLayer::refreshTrailTick), 0.1f);
#ifdef GEODE_IS_ANDROID
        auto sprite = ButtonSprite::create("Trail");
        sprite->setScale(0.6f);
        auto button = CCMenuItemExt::createSpriteExtra(sprite, [](CCMenuItemSpriteExtra *)
                                                       { showTrailSettings(); });
        auto menu = CCMenu::create();
        menu->addChild(button);
        auto winSize = CCDirector::get()->getWinSize();
        // 오른쪽 위 버튼들(숨기기 버튼 등)과 겹치지 않도록 그 아래쪽에 배치.
        // 위치가 마음에 안 들면 kTrailButtonOffsetY 값만 조절하면 됨 (클수록 더 아래로).
        constexpr float kTrailButtonOffsetY = 108.f;
        menu->setPosition({winSize.width - 50.f, winSize.height - kTrailButtonOffsetY});
        addChild(menu);
#endif
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
