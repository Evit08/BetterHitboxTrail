#include "TrailRenderer.hpp"

namespace hitboxtrail::trailrenderer
{
    cocos2d::ccColor4F TrailRenderer::trailColor(TrailState const &state, cocos2d::ccColor4F base,
                                                 bool colorClicks, trailsettings::RenderSettings const &settings,
                                                 bool hitboxColors)
    {
        auto color = base;
        if (colorClicks)
        {
            switch (state.click)
            {
            case TrailState::Click::Press:
                color = settings.pressCol;
                break;
            case TrailState::Click::Release:
                color = settings.releaseCol;
                break;
            default:
                break;
            }
        }
        if (state.click == TrailState::Click::Hold && hitboxColors && settings.colorHeld)
            color = settings.holdCol;
        return color;
    }

    float TrailRenderer::ageOpacity(float age, trailsettings::RenderSettings const &settings)
    {
        return settings.fadeWithAge ? age * age : 1.f;
    }

    TrailRenderer::LayerColors TrailRenderer::layerColors(TrailState const &state, cocos2d::ccColor4F base,
                                                          bool colorClicks, float age,
                                                          trailsettings::RenderSettings const &settings,
                                                          bool hitboxColors)
    {
        auto outline = trailColor(state, base, colorClicks, settings, hitboxColors);
        auto fill = outline;
        auto fade = ageOpacity(age, settings);
        fill.a = settings.fillOn ? settings.fillAlpha * fade * fade * fade : 0.f;
        outline.a = settings.opacity * fade;
        return {fill, outline};
    }
}
