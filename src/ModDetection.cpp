#include "ModDetection.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/Loader.hpp>
#include <Geode/utils/file.hpp>
#include <matjson.hpp>
#include <asp/fs/fs.hpp>
#include <optional>

using namespace geode::prelude;

namespace hitboxtrail::ModDetection
{
    namespace
    {
        struct DrawNodeProbe : cocos2d::CCDrawNode

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
        std::optional<asp::SystemTime> eclTime;

        bool megaHackLoaded()
        {
            static auto const loaded = Loader::get()->isModLoaded("absolllute.megahack");
            return loaded;
        }

        CCNode *findMegaHackHitbox(GJBaseGameLayer *layer)
        {
            static constexpr auto kId = "absolllute.megahack/hitbox";

            if (auto hitboxNode = layer->getChildByID("hitbox-node"))
                if (auto node = hitboxNode->getChildByIDRecursive(kId))
                    return node;

            if (layer->m_debugDrawNode)
                return layer->m_debugDrawNode->getChildByID(kId);

            return nullptr;
        }

        bool mhkShow()
        {
            auto layer = GJBaseGameLayer::get();
            if (!layer)
                return false;
            auto node = findMegaHackHitbox(layer);
            if (!node || !node->isVisible())
                return false;
            auto drawNode = typeinfo_cast<cocos2d::CCDrawNode *>(node);
            return drawNode && DrawNodeProbe::vertexCount(drawNode) > 0;
        }

        void syncFile(asp::fs::PathView path, std::optional<asp::SystemTime> &lastWrite,
                      bool &known, bool &value, std::string const &key)
        {
            auto meta = asp::fs::metadata(path);
            if (!meta)
            {
                value = false;
                known = false;
                return;
            }
            auto writeTime = meta.unwrap().modifiedAt;
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
