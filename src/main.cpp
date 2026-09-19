#include <Geode/Geode.hpp>

#include "TrailNode.hpp"
#include "ui/SettingsMenu.hpp"

using namespace geode::prelude;

namespace hitboxtrail {
void registerTrailGameLayer();
void registerTrailEditorLayer();
void registerTrailPauseLayer();
}

$on_mod(Loaded)
{
    hitboxtrail::TrailNode::healThicknessSettings();
    hitboxtrail::registerTrailGameLayer();
    hitboxtrail::registerTrailEditorLayer();
    hitboxtrail::registerTrailPauseLayer();
    hitboxtrail::registerSettingsMenu();
}
