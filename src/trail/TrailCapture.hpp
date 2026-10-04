#pragma once

#include "TrailSettings.hpp"
#include <deque>

namespace hitboxtrail::trailcapture
{
    struct PlayerTrail
    {
        std::deque<TrailState> states;
        bool lastHeld = false; // Jump-button state at the previous sample (regular and between-frame)
        TrailState::Click flashClick = TrailState::Click::None;
        int flashTicks = 0;

        void reset()
        {
            states.clear();
            lastHeld = false;
            flashClick = TrailState::Click::None;
            flashTicks = 0;
        }
    };

    struct SampleOptions
    {
        bool allowDead = false;     // record even if the player is dead (final death frame)
        bool sampleThisTick = true; // false: only record when the button state changed
        bool betweenFrame = false;  // state captured from a button edge between game ticks
    };

    class TrailCapture
    {
    public:
        void reset();
        void setLastTickDt(float dt);
        bool consumeTick(trailsettings::CaptureSettings const &settings);
        void record(PlayerObject *player, PlayerTrail &trail,
                    trailsettings::CaptureSettings const &settings,
                    SampleOptions const &options);
        void captureEdge(bool isPlayer1, PlayerObject *player1, PlayerObject *player2,
                         trailsettings::CaptureSettings const &settings);
        void trimToMax(std::deque<TrailState> &states,
                       trailsettings::CaptureSettings const &settings);

        PlayerTrail &player1();
        PlayerTrail &player2();
        PlayerTrail const &player1() const;
        PlayerTrail const &player2() const;

    private:
        static GameMode currentGameMode(PlayerObject *player);

        PlayerTrail m_player1Trail;
        PlayerTrail m_player2Trail;
        float m_lastTickDt = 1.f / 240.f;
        float m_captureAccumulator = 0.f;
    };
}
