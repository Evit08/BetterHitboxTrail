#include "ModDetection.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/Loader.hpp>
#include <Geode/utils/file.hpp>
#include <matjson.hpp>
#include <filesystem>

using namespace geode::prelude;

namespace hitboxtrail::ModDetection
{
    namespace
    {
        struct DrawNodeProbe : cocos2d::CCDrawNode
        // vertexcount method is only situation to detect mhk i think
        // so even if MH Show Hitboxes is enabled may automatically disable itself if MH has no hitboxes of its own to display on screen.

        {
            static int vertexCount(cocos2d::CCDrawNode *node)
            {
                if (!node)
                    return 0;
                return static_cast<int>(static_cast<DrawNodeProbe *>(node)->m_nBufferCount);
            }
        };

        size_t tick = 30;
        bool qolCached = false;
        bool megaHackCached = false;
        bool eclipseCached = false;
        bool eclKnown = false;
        bool eclShow = false;
        std::filesystem::file_time_type eclTime{};

        bool megaHackLoaded()
        {
            static auto const loaded = Loader::get()->isModLoaded("absolllute.megahack");
            return loaded;
        }

        bool mhkShow()
        {
            auto layer = GJBaseGameLayer::get();
            if (!layer || !layer->m_debugDrawNode)
                return false;
            auto node = layer->m_debugDrawNode->getChildByID("absolllute.megahack/hitbox");
            if (!node || !node->isVisible())
                return false;
            auto drawNode = typeinfo_cast<cocos2d::CCDrawNode *>(node);
            return drawNode && DrawNodeProbe::vertexCount(drawNode) > 0;
        }

        void syncFile(std::filesystem::path const &path, std::filesystem::file_time_type &lastWrite,
                      bool &known, bool &value, std::string const &key)
        {
            std::error_code error;
            auto writeTime = std::filesystem::last_write_time(path, error);
            if (error)
            {
                value = false;
                known = false;
                return;
            }
            if (known && writeTime == lastWrite)
                return;
            auto json = utils::file::readJson(path);
            if (!json)
                return;
            auto root = json.unwrap();
            value = root.isObject() && root.contains(key) && root[key].asBool().unwrapOr(false);
            lastWrite = writeTime;
            known = true;
        }
    }

    bool enabled(bool includeMegaHack)
    {
        megaHackCached = megaHackLoaded() && mhkShow(); // 240tps to show hitboxes on noclip death delay

        if (++tick >= 30)
        {
            tick = 0;
            qolCached = false;
            if (auto qolmod = Loader::get()->getLoadedMod("thesillydoggo.qolmod"))
                qolCached = qolmod->getSavedValue<bool>("show-hitboxes_enabled", false);
            auto eclipseMod = Loader::get()->getLoadedMod("eclipse.eclipse-menu"); // trail activates when the eclipse menu is closed
            if (eclipseMod)
            {
                syncFile(eclipseMod->getSaveDir() / "config.json", eclTime, eclKnown,
                         eclShow, "level.showhitboxes");
            }
            eclipseCached = eclipseMod && eclShow;
        }
        return qolCached || (includeMegaHack && megaHackCached) || eclipseCached;
    }

    void refresh() { tick = 30; }

    bool changed()
    {
        auto before = qolCached || megaHackCached || eclipseCached;
        refresh();
        return enabled() != before;
    }
}
