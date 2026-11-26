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
        if (Mod::get()->getSettingValue<bool>("hide-pause-menu-button"))
            return;
        auto sprite = CircleButtonSprite::createWithSprite("trail-icon.png"_spr, 1.2f, CircleBaseColor::Green, CircleBaseSize::Medium); // 0.75f = 아이콘 크기 (작게: 값↓, 크게: 값↑)
        sprite->setScale(.6f); // 크기가 안 맞으면 이 값만 조절 (클수록 커짐)

        auto button = CCMenuItemExt::createSpriteExtra(sprite, [](CCMenuItemSpriteExtra *)
                                                       { showTrailSettings(); });
        button->setID("trail-settings-button"_spr);

        // 오른쪽 버튼 메뉴는 this 가 아니라 m_mainLayer 안에 있음
        auto rightMenu = typeinfo_cast<CCMenu *>(m_mainLayer->getChildByID("right-button-menu"));
        if (rightMenu)
        {
            rightMenu->addChild(button);
            rightMenu->updateLayout(); // 톱니바퀴/숨기기 버튼과 같은 줄·간격으로 자동 정렬
            return;
        }

        // right-button-menu 를 못 찾았을 때의 예비 배치
        auto menu = CCMenu::create();
        menu->addChild(button);
        auto winSize = CCDirector::get()->getWinSize();
        constexpr float kTrailButtonOffsetY = 132.f;
        menu->setPosition({winSize.width - 50.f, winSize.height - kTrailButtonOffsetY});
        addChild(menu);
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
