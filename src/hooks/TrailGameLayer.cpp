#include <Geode/modify/GJBaseGameLayer.hpp>

#include "../TrailNode.hpp"

using namespace geode::prelude;
using namespace hitboxtrail;

class $modify(TrailGameLayer, GJBaseGameLayer)
{
    struct Fields { TrailNode* trail = nullptr; bool reachedTickEnd = false; };

    bool init() {
        if (!GJBaseGameLayer::init()) return false;
        m_fields->trail = TrailNode::create();
        if (!m_fields->trail) return true;
        m_fields->trail->setID("hitbox-trail"_spr);
        m_fields->trail->setZOrder(m_uiLayer->getZOrder() - 1);
        insertBefore(m_fields->trail, m_uiLayer);
        attachTrail();
        return true;
    }
    void handleButton(bool down, int button, bool isPlayer1) {
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
        if (m_fields->trail && button == static_cast<int>(PlayerButton::Jump)
            && Mod::get()->getSettingValue<bool>("hitbox-between-frames")) {
            m_fields->trail->captureButtonEdge(isPlayer1, m_player1, m_player2);
            if (m_gameState.m_isDualMode) m_fields->trail->captureButtonEdge(!isPlayer1, m_player1, m_player2);
        }
    }
    float getModifiedDelta(float dt) {
        auto modified = GJBaseGameLayer::getModifiedDelta(dt);
        if (m_fields->trail) m_fields->trail->setLastTickDt(modified);
        return modified;
    }
    void checkRepellPlayer() {
        GJBaseGameLayer::checkRepellPlayer();
        attachTrail();
        if (m_fields->trail && ((m_player1 && m_player1->m_isDead) || (m_player2 && m_player2->m_isDead))) {
            m_fields->reachedTickEnd = false;
            m_fields->trail->capture(this);
            return;
        }
        m_fields->reachedTickEnd = true;
    }
    void updateCamera(float dt) {
        GJBaseGameLayer::updateCamera(dt);
        if (m_fields->reachedTickEnd && m_fields->trail) {
            m_fields->reachedTickEnd = false;
            m_fields->trail->capture(this);
        }
    }
    void resetLevelVariables() {
        GJBaseGameLayer::resetLevelVariables();
        if (!typeinfo_cast<LevelEditorLayer*>(this) && m_fields->trail)
            m_fields->trail->resetTrails();
    }
private:
    void attachTrail() {
        if (!m_fields->trail || !m_debugDrawNode) return;
        auto gameplayParent = m_debugDrawNode->getParent();
        if (!gameplayParent || m_fields->trail->getParent() == gameplayParent) return;
        m_fields->trail->retain();
        m_fields->trail->removeFromParentAndCleanup(false);
        gameplayParent->addChild(m_fields->trail, m_debugDrawNode->getZOrder() - 1);
        m_fields->trail->release();
        m_fields->trail->setGameplayXform(true);
    }
};

namespace hitboxtrail {
TrailNode* getTrail(GJBaseGameLayer* layer) {
    if (!layer) return nullptr;
    return base_cast<TrailGameLayer*>(layer)->m_fields->trail;
}
}
