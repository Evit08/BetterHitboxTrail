#pragma once

#include <Geode/Geode.hpp>

namespace hitboxtrail {
enum class GameMode { Cube, Ship, Ball, Ufo, Wave, Robot, Spider, Swing };

struct TrailState {
    cocos2d::CCRect rect;
    cocos2d::CCRect miniRect;
    float rotation = 0.f;
    enum class Click { None, Press, Release, Hold } click = Click::None;
    GameMode mode = GameMode::Cube;
    float vehicleSize = 1.f;
};

class TrailNode : public cocos2d::CCDrawNode {
public:
    static TrailNode* create();
    static void healThicknessSettings();
    ~TrailNode() override = default;

    virtual bool init() override = 0;
    virtual void setGameplayXform(bool value) = 0;
    virtual void resetTrails() = 0;
    virtual void setLastTickDt(float dt) = 0;
    virtual void capture(GJBaseGameLayer* layer) = 0;
    virtual void refreshDrawing(GJBaseGameLayer* layer) = 0;
    virtual void invalidateRenderSettings() = 0;
    virtual void captureButtonEdge(bool isPlayer1, PlayerObject* player1, PlayerObject* player2) = 0;
};

TrailNode* getTrail(GJBaseGameLayer* layer);
}
