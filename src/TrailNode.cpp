#include "TrailNode.hpp"
#include "ModDetection.hpp"
#include <algorithm>
#include <cstdlib>

using namespace geode::prelude;

namespace hitboxtrail
{
    TrailNode *TrailNode::create()
    {
        auto node = new TrailNode;
        if (node && node->init())
        {
            node->autorelease();
            return node;
        }
        CC_SAFE_DELETE(node);
        return nullptr;
    }

    bool TrailNode::init()
    {
        if (!CCDrawNode::init())
            return false;
        setBlendFunc({GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA});
        setVisible(false);
        return true;
    }

    void TrailNode::setAttached(bool value)
    {
        setVisible(value);
    }

    void TrailNode::resetTrails()
    {
        m_capture.reset();
        m_wasDead = false;
        m_recordedDeathFrame = false;
        m_megaHackIgnoreFrames = 1; // If you die and pause, a hitbox trail is visible for a second upon restarting.
        // this only happens in megahack. It was resolved by adding a 1frame vertex delay upon respawn.
        clear();
        constexpr unsigned int kInitialCapacity = 512; // reset buffer
        if (m_uBufferCapacity > kInitialCapacity)
        {
            auto buffer = static_cast<cocos2d::ccV2F_C4B_T2F *>(
                std::realloc(m_pBuffer, kInitialCapacity * sizeof(cocos2d::ccV2F_C4B_T2F)));
            if (buffer)
            {
                m_pBuffer = buffer;
                m_uBufferCapacity = kInitialCapacity;
            }
        }
    }

    void TrailNode::setLastTickDt(float dt)
    {
        m_capture.setLastTickDt(dt);
    }

    void TrailNode::capture(GJBaseGameLayer *layer)
    {
        auto const &captureSettings = m_settings.cachedCaptureSettings();
        if (!captureSettings.trailEnabled)
        {
            clear();
            return;
        }

        auto p1Dead = layer->m_player1 && layer->m_player1->m_isDead;
        auto p2Dead = layer->m_player2 && layer->m_player2->m_isDead;
        if (p1Dead || p2Dead)
        {
            // Save the final position once, then keep that frozen trail visible after death.
            if (!m_recordedDeathFrame)
            {
                m_capture.record(layer->m_player1, m_capture.player1(), captureSettings, {.allowDead = true});
                if (layer->m_player2 && layer->m_player2->isRunning())
                    m_capture.record(layer->m_player2, m_capture.player2(), captureSettings, {.allowDead = true});
                m_recordedDeathFrame = true;
                if (captureSettings.showOnDeath)
                    drawTrail(layer);
                else
                    clear();
            }
            m_wasDead = true;
            return;
        }

        m_wasDead = false;
        m_recordedDeathFrame = false;

        if (m_megaHackIgnoreFrames > 0)
        {
            --m_megaHackIgnoreFrames;
            if (m_megaHackIgnoreFrames == 0)
                ModDetection::refresh();
        }

        auto showLiveTrail = captureSettings.alwaysShow || shouldShowWhileAlive(layer);
        auto sampleThisTick = m_capture.consumeTick(captureSettings);
        m_capture.record(layer->m_player1, m_capture.player1(), captureSettings, {.sampleThisTick = sampleThisTick});
        if (layer->m_player2 && layer->m_player2->isRunning())
            m_capture.record(layer->m_player2, m_capture.player2(), captureSettings, {.sampleThisTick = sampleThisTick});

        if (showLiveTrail)
            drawTrail(layer);
        else
            clear();
    }

    void TrailNode::refreshDrawing(GJBaseGameLayer *layer)
    {
        auto const &captureSettings = m_settings.cachedCaptureSettings();
        if (!captureSettings.trailEnabled)
        {
            clear();
            return;
        }
        m_capture.trimToMax(m_capture.player1().states, captureSettings);
        m_capture.trimToMax(m_capture.player2().states, captureSettings);
        if (m_wasDead)
        {
            if (captureSettings.showOnDeath)
                drawTrail(layer);
            else
                clear();
            return;
        }
        auto showLiveTrail = captureSettings.alwaysShow || (layer && shouldShowWhileAlive(layer));
        if (showLiveTrail)
            drawTrail(layer);
        else
            clear();
    }

    void TrailNode::invalidateRenderSettings()
    {
        m_settings.invalidate();
    }

    void TrailNode::captureButtonEdge(bool isPlayer1, PlayerObject *player1, PlayerObject *player2)
    {
        auto const &settings = m_settings.cachedCaptureSettings();
        m_capture.captureEdge(isPlayer1, player1, player2, settings);
    }

    // Adapted from thesillydoggo.qolmod
    std::optional<cocos2d::CCRect> TrailNode::visibleRect(GJBaseGameLayer *layer)
    {
        if (typeinfo_cast<LevelEditorLayer *>(layer))
            return std::nullopt;
        auto window = cocos2d::CCDirector::get()->getWinSize();
        auto side = std::max(window.width, window.height) / layer->m_gameState.m_cameraZoom;
        auto outer = side * 0.4f;
        auto camera = layer->m_gameState.m_cameraPosition;
        return cocos2d::CCRect(camera.x - outer, camera.y - outer, side + outer * 2.f, side + outer * 2.f);
    }

    void TrailNode::drawTrail(GJBaseGameLayer *layer)
    {
        clear();
        auto const &settings = m_settings.cachedRenderSettings();
        std::optional<cocos2d::CCRect> visible;
        if (layer && m_settings.cachedCaptureSettings().noLimit)
            visible = visibleRect(layer);
        m_renderer.drawTrail(m_capture.player1().states, m_capture.player2().states, settings, visible,
                             [this](auto a, auto b, auto c, auto col)
                             { drawFlatTriangle(a, b, c, col); });
    }

    bool TrailNode::shouldShowWhileAlive(GJBaseGameLayer *layer)
    {
        auto includeMegaHack = m_megaHackIgnoreFrames <= 0;
        return layer->m_isDebugDrawEnabled || ModDetection::enabled(includeMegaHack);
    }

    void TrailNode::drawFlatTriangle(cocos2d::CCPoint const &a, cocos2d::CCPoint const &b,
                                     cocos2d::CCPoint const &c, cocos2d::ccColor4F color)
    {
        if (color.a <= 0.f)
            return;
        constexpr unsigned int kVertexCount = 3;
        if (m_nBufferCount + kVertexCount > m_uBufferCapacity)
        {
            auto newCapacity = m_uBufferCapacity + std::max(m_uBufferCapacity, kVertexCount);
            auto newBuffer = static_cast<cocos2d::ccV2F_C4B_T2F *>(
                std::realloc(m_pBuffer, newCapacity * sizeof(cocos2d::ccV2F_C4B_T2F)));
            if (!newBuffer)
                return;
            m_pBuffer = newBuffer;
            m_uBufferCapacity = newCapacity;
        }

        auto vertexColor = cocos2d::ccc4BFromccc4F(color);
        auto makeVertex = [&](cocos2d::CCPoint point)
        {
            return cocos2d::ccV2F_C4B_T2F{{point.x, point.y}, vertexColor, {0.f, 0.f}};
        };
        auto triangles = reinterpret_cast<cocos2d::ccV2F_C4B_T2F_Triangle *>(m_pBuffer + m_nBufferCount);
        triangles[0] = {makeVertex(a), makeVertex(b), makeVertex(c)};
        m_nBufferCount += kVertexCount;
        m_bDirty = true;
    }
}
