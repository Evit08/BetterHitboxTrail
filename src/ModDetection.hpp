#pragma once

namespace hitboxtrail::ModDetection {
    bool enabled(bool includeMegaHack = true);
    void refresh();
    bool changed();
}
