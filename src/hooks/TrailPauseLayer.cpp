#include <Geode/modify/PauseLayer.hpp>

#include "../ModDetection.hpp"
#include "../TrailNode.hpp"

using namespace geode::prelude;
using namespace hitboxtrail;

class $modify(TrailPauseLayer, PauseLayer)
{
    void customSetup() {
        PauseLayer::customSetup();
        refreshTrail();
        schedule(schedule_selector(TrailPauseLayer::refreshTrailTick), 0.1f);
    }
    void onExit() {
        unschedule(schedule_selector(TrailPauseLayer::refreshTrailTick));
        PauseLayer::onExit();
    }
    void refreshTrailTick(float) {
        if (auto layer = GJBaseGameLayer::get())
            if (auto trail = getTrail(layer))
                if (ModDetection::changed()) trail->refreshDrawing(layer);
    }
    void refreshTrail() {
        if (auto layer = GJBaseGameLayer::get())
            if (auto trail = getTrail(layer)) {
                ModDetection::refresh();
                trail->refreshDrawing(layer);
            }
    }
};

namespace hitboxtrail { void registerTrailPauseLayer() {} }
