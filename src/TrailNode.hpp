#pragma once

#include <Geode/Geode.hpp>
#include "trail/TrailCapture.hpp"
#include "trail/TrailRenderer.hpp"
#include "trail/TrailSettings.hpp"

namespace hitboxtrail
{
    class TrailNode : public cocos2d::CCDrawNode
    {
    public:
        static TrailNode *create();
        bool init() override;

        void setAttached(bool value);
        void resetTrails();
        void setLastTickDt(float dt);
        void capture(GJBaseGameLayer *layer);
        void refreshDrawing(GJBaseGameLayer *layer);
        void invalidateRenderSettings();
        void captureButtonEdge(bool isPlayer1, PlayerObject *player1, PlayerObject *player2);

    private:
        static std::optional<cocos2d::CCRect> visibleRect(GJBaseGameLayer *layer);
        void drawTrail(GJBaseGameLayer *layer);
        bool shouldShowWhileAlive(GJBaseGameLayer *layer);
        void drawFlatTriangle(cocos2d::CCPoint const &a, cocos2d::CCPoint const &b,
                              cocos2d::CCPoint const &c, cocos2d::ccColor4F color);

        trailcapture::TrailCapture m_capture;
        trailsettings::TrailSettings m_settings;
        trailrenderer::TrailRenderer m_renderer;
        bool m_wasDead = false;
        bool m_recordedDeathFrame = false;
        int m_megaHackIgnoreFrames = 0;
    };

    TrailNode *getTrail(GJBaseGameLayer *layer);
}
