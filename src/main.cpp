#include <Geode/Geode.hpp>

#include "trail/TrailSettings.hpp"
#include "ui/SettingsMenu.hpp"

using namespace geode::prelude;

$on_mod(Loaded)
{
    hitboxtrail::trailsettings::healThicknessSettings();
    hitboxtrail::registerSettingsMenu();
}
