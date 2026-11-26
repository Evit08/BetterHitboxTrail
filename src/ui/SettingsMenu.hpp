#pragma once

#include <Geode/Geode.hpp>
#include <functional>

namespace hitboxtrail
{
    void registerSettingsMenu();
    void applySettings();
    void showTrailSettings();
#ifndef GEODE_IS_ANDROID
    void beginOpenKeyCapture(std::function<void(cocos2d::enumKeyCodes)> done);
    void cancelOpenKeyCapture();
#endif
}
