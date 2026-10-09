#pragma once

#include "TrailGeometry.hpp"
#include "TrailSettings.hpp"
#include <array>
#include <deque>
#include <optional>
#include <vector>

namespace hitboxtrail::trailrenderer
{
    enum class DrawPass
    {
        All,
        NonClick,
        ClickOnly
    };

    class TrailRenderer
    {
    public:
        template <class Emit>
        void drawTrail(std::deque<TrailState> const &player1States,
                       std::deque<TrailState> const &player2States,
                       trailsettings::RenderSettings const &settings,
                       std::optional<cocos2d::CCRect> const &visible, Emit &&emit)
        {
            auto split = settings.clickOnTop && !settings.onlyClickRelease;
            auto basePass = split ? DrawPass::NonClick : DrawPass::All;
            drawStates(player1States, settings, basePass, visible, emit);
            drawStates(player2States, settings, basePass, visible, emit);
            if (split)
            {
                drawStates(player1States, settings, DrawPass::ClickOnly, visible, emit);
                drawStates(player2States, settings, DrawPass::ClickOnly, visible, emit);
            }
        }

    private:
        using Layer = HitboxLayer;

        struct LayerColors
        {
            cocos2d::ccColor4F fill;
            cocos2d::ccColor4F outline;
        };

        struct BatchItem
        {
            TrailState const *state;
            float age;
        };

        static cocos2d::ccColor4F trailColor(TrailState const &state, cocos2d::ccColor4F base,
                                             bool colorClicks, trailsettings::RenderSettings const &settings,
                                             bool hitboxColors = true);
        static float ageOpacity(float age, trailsettings::RenderSettings const &settings);
        static LayerColors layerColors(TrailState const &state, cocos2d::ccColor4F base,
                                       bool colorClicks, float age,
                                       trailsettings::RenderSettings const &settings,
                                       bool hitboxColors = true);

        template <class Emit>
        void drawStates(std::deque<TrailState> const &states,
                        trailsettings::RenderSettings const &settings, DrawPass pass,
                        std::optional<cocos2d::CCRect> const &visible, Emit &&emit)
        {
            auto onlyClickRelease = settings.onlyClickRelease;
            auto showBetweenFrames = settings.showBetweenFrames;
            auto isClick = [](TrailState const &state)
            {
                return state.click == TrailState::Click::Press || state.click == TrailState::Click::Release;
            };
            auto skipBase = [&](TrailState const &state)
            {
                if (state.betweenFrame && !showBetweenFrames)
                    return true;
                return onlyClickRelease && !isClick(state);
            };
            auto inPass = [&](TrailState const &state)
            {
                switch (pass)
                {
                case DrawPass::NonClick:
                    return !isClick(state);
                case DrawPass::ClickOnly:
                    return isClick(state);
                case DrawPass::All:
                    break;
                }
                return true;
            };
            // Adapted from thesillydoggo.qolmod
            auto offscreen = [&](TrailState const &state)
            {
                if (!visible)
                    return false;
                auto view = *visible;
                auto rect = state.rect;
                return rect.getMaxX() < view.getMinX() || rect.getMinX() > view.getMaxX() ||
                       rect.getMaxY() < view.getMinY() || rect.getMinY() > view.getMaxY();
            };
            auto skip = [&](TrailState const &state)
            {
                return skipBase(state) || !inPass(state) || offscreen(state);
            };

            if (settings.forceSingle)
            {
                // Enable Only Player or Only Cube alongside Only Click and Release
                // displaying the hitbox state only at the moment of the click or release
                auto window = static_cast<size_t>(kFlashTicks);
                auto begin = states.size() > window ? states.size() - window : size_t{0};
                for (size_t idx = states.size(); idx-- > begin;)
                {
                    auto const &state = states[idx];
                    if (skipBase(state))
                        continue;
                    if (inPass(state))
                        drawAll(state, 1.f, settings, emit);
                    break;
                }
                return;
            }

            auto total = static_cast<float>(states.size());
            auto ageFor = [&](size_t idx)
            {
                return total > 1.f ? static_cast<float>(idx) / (total - 1.f) : 1.f;
            };

            if (settings.batchLayers)
            {
                m_batch.clear();
                for (size_t idx = 0; idx < states.size(); ++idx)
                {
                    auto const &state = states[idx];
                    if (skip(state))
                        continue;
                    m_batch.push_back({&state, ageFor(idx)});
                }
                for (auto layer : settings.layerOrder)
                    for (auto const &item : m_batch)
                        drawLayer(layer, *item.state, item.age, settings, emit);
            }
            else
            {
                for (size_t idx = 0; idx < states.size(); ++idx)
                {
                    auto const &state = states[idx];
                    if (skip(state))
                        continue;
                    drawAll(state, ageFor(idx), settings, emit);
                }
            }
        }

        template <class Emit>
        static void drawAll(TrailState const &state, float age,
                            trailsettings::RenderSettings const &settings, Emit &&emit)
        {
            for (auto layer : settings.layerOrder)
                drawLayer(layer, state, age, settings, emit);
        }

        template <class Emit>
        static void drawLayer(Layer layer, TrailState const &state, float age,
                              trailsettings::RenderSettings const &settings, Emit &&emit)
        {
            auto isMini = state.vehicleSize < 0.99f;
            switch (layer)
            {
            case Layer::Main:
            {
                if (!settings.squareOn || settings.onlyCube)
                    return;
                auto thickness = settings.masterThick * settings.mainThick[trailsettings::thickIndex(state.mode)][isMini];
                auto colors = layerColors(state, settings.mainCol, settings.colorClicks, age, settings);
                trailgeometry::appendRect(emit, state.rect, thickness, colors.fill, colors.outline);
                return;
            }
            case Layer::Blue:
            {
                if (!settings.blueOn || settings.onlyCube)
                    return;
                auto rect = trailgeometry::insetRect(state.miniRect, trailsettings::blueInsetFor(state.mode));
                auto thickness = settings.masterThick * settings.blueThick[trailsettings::thickIndex(state.mode)][isMini];
                auto colors = layerColors(state, settings.blueCol,
                                          settings.colorClicks && settings.blueClicks,
                                          age, settings, settings.blueClicks);
                trailgeometry::appendRect(emit, rect, thickness, colors.fill, colors.outline);
                return;
            }
            case Layer::Circle:
            {
                if (!settings.circleOn || settings.onlyCube)
                    return;
                auto cr = state.rect;
                auto ctr = cocos2d::CCPointMake(cr.getMidX(), cr.getMidY());
                auto rad = std::min(cr.size.width, cr.size.height) / 2.f;
                auto circleThick = settings.masterThick * settings.circleThick[trailsettings::thickIndex(state.mode)][isMini];
                auto colors = layerColors(state, settings.circleCol, settings.colorClicks, age, settings);
                trailgeometry::appendCircle(emit, ctr, rad, circleThick,
                                            colors.fill, colors.outline);
                return;
            }
            case Layer::Rotation:
            {
                auto shouldDraw = settings.rotOn || settings.onlyCube;
                if (!shouldDraw)
                    return;
                auto thickness = settings.masterThick * settings.rotThick[trailsettings::thickIndex(state.mode)][isMini];
                auto colors = layerColors(state, settings.rotCol, settings.colorClicks, age, settings);
                trailgeometry::appendRect(emit, state.rect, thickness, colors.fill, colors.outline, state.rotation);
                return;
            }
            }
        }

        std::vector<BatchItem> m_batch;
    };
}
