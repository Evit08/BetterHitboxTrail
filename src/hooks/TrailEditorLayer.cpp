#include <Geode/modify/LevelEditorLayer.hpp>

#include "../TrailNode.hpp"

using namespace geode::prelude;
using namespace hitboxtrail;


//Adapted from thesillydoggo.qolmod
class $modify(TrailEditorLayer, LevelEditorLayer)
{
    void updateEditor(float dt)
    {
        LevelEditorLayer::updateEditor(dt);
        restoreTrail(this);
    }
    void onPlaytest()
    {
        LevelEditorLayer::onPlaytest();
        if (auto trail = getTrail(this))
            trail->resetTrails();
    }
};
