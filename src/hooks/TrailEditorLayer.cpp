#include <Geode/modify/LevelEditorLayer.hpp>

#include "../TrailNode.hpp"

using namespace geode::prelude;
using namespace hitboxtrail;

class $modify(TrailEditorLayer, LevelEditorLayer)
{
    // Adapted from thesillydoggo.qolmod, used with permission from the developer.
    void onPlaytest()
    {
        LevelEditorLayer::onPlaytest();
        if (auto trail = getTrail(this))
            trail->resetTrails();
    }
};
