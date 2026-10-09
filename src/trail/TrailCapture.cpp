#include "TrailCapture.hpp"

#include <cmath>

namespace hitboxtrail::trailcapture
{
    void TrailCapture::reset()
    {
        m_player1Trail.reset();
        m_player2Trail.reset();
        m_captureAccumulator = 0.f;
    }

    void TrailCapture::setLastTickDt(float dt)
    {
        m_lastTickDt = dt;
    }

    bool TrailCapture::consumeTick(trailsettings::CaptureSettings const &settings)
    {
        if (settings.onlyClickRelease)
            return settings.forceSingle;
        if (settings.captureRate <= 0.0)
            return false;
        auto targetInterval = static_cast<float>(1.0 / settings.captureRate);
        m_captureAccumulator += m_lastTickDt;
        if (m_captureAccumulator < targetInterval)
            return false;
        // Keep the leftover time so irregular frame durations do not drift the capture rate.
        m_captureAccumulator = std::fmod(m_captureAccumulator, targetInterval);
        return true;
    }

    // Adapted from thesillydoggo.qolmod
    void TrailCapture::record(PlayerObject *player, PlayerTrail &trail,
                              trailsettings::CaptureSettings const &settings,
                              SampleOptions const &options)
    {
        if (!player || (player->m_isDead && !options.allowDead))
            return;

        bool held = player->m_holdingButtons[static_cast<int>(PlayerButton::Jump)];
        bool changed = held != trail.lastHeld;
        TrailState::Click click = TrailState::Click::None;
        if (changed)
            click = held ? TrailState::Click::Press : TrailState::Click::Release;
        else if (held)
            click = TrailState::Click::Hold;
        trail.lastHeld = held;

        if (!options.sampleThisTick && !changed)
            return;

        if (settings.onlyClickRelease && settings.forceSingle)
        {
            if (changed)
            {
                trail.flashClick = click;
                trail.flashTicks = kFlashTicks;
            }
            if (trail.flashTicks > 0)
            {
                click = trail.flashClick;
                --trail.flashTicks;
            }
            else
            {
                click = TrailState::Click::None;
            }
        }

        trail.states.push_back({player->getObjectRect(player->m_vehicleSize, player->m_vehicleSize),
                                player->getObjectRect(0.25f, 0.25f),
                                player->getRotation(),
                                click,
                                currentGameMode(player),
                                player->m_vehicleSize,
                                options.betweenFrame});
        trimToMax(trail.states, settings);
    }

    void TrailCapture::captureEdge(bool isPlayer1, PlayerObject *player1, PlayerObject *player2,
                                   trailsettings::CaptureSettings const &settings)
    {
        if (!settings.trailEnabled || !settings.showBetweenFrames)
            return;
        if (isPlayer1)
            record(player1, m_player1Trail, settings, {.sampleThisTick = false, .betweenFrame = true});
        else
            record(player2, m_player2Trail, settings, {.sampleThisTick = false, .betweenFrame = true});
    }

    void TrailCapture::trimToMax(std::deque<TrailState> &states,
                                 trailsettings::CaptureSettings const &settings)
    {
        while (states.size() > settings.length)
            states.pop_front();
    }

    PlayerTrail &TrailCapture::player1()
    {
        return m_player1Trail;
    }

    PlayerTrail &TrailCapture::player2()
    {
        return m_player2Trail;
    }

    PlayerTrail const &TrailCapture::player1() const
    {
        return m_player1Trail;
    }

    PlayerTrail const &TrailCapture::player2() const
    {
        return m_player2Trail;
    }

    GameMode TrailCapture::currentGameMode(PlayerObject *player)
    {
        if (player->m_isShip)
            return GameMode::Ship;
        if (player->m_isBall)
            return GameMode::Ball;
        if (player->m_isBird)
            return GameMode::Ufo;
        if (player->m_isDart)
            return GameMode::Wave;
        if (player->m_isRobot)
            return GameMode::Robot;
        if (player->m_isSpider)
            return GameMode::Spider;
        if (player->m_isSwing)
            return GameMode::Swing;
        return GameMode::Cube;
    }
}
