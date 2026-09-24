#include <Geode/Geode.hpp>

#include "TrailNode.hpp"
#include "ui/SettingsMenu.hpp"

using namespace geode::prelude;

$on_mod(Loaded)
{
    hitboxtrail::TrailNode::healThicknessSettings();
    hitboxtrail::registerSettingsMenu();
}
