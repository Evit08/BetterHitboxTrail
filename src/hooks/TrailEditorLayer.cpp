#include <Geode/modify/LevelEditorLayer.hpp>

#include "../TrailNode.hpp"

using namespace geode::prelude;
using namespace hitboxtrail;

class $modify(TrailEditorLayer, LevelEditorLayer)
{
    void onPlaytest() {
        LevelEditorLayer::onPlaytest();
        if (auto trail = getTrail(this)) trail->resetTrails();
    }
};

namespace hitboxtrail { void registerTrailEditorLayer() {} }
