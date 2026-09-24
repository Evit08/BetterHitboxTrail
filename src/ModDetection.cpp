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

        bool readBool(std::string const &contents, std::string const &key)
        {
            auto parsed = matjson::parse(contents);
            if (!parsed)
                return false;
            auto root = parsed.unwrap();
            if (!root.isObject() || !root.contains(key))
                return false;
            return root[key].asBool().unwrapOr(false);
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
            auto contents = utils::file::readString(path).unwrapOr("");
            value = readBool(contents, key);
            lastWrite = writeTime;
            known = true;
        }
    }

    bool enabled(bool includeMegaHack)
    {
        megaHackCached = mhkShow();

        if (++tick >= 30)
        {
            tick = 0;
            qolCached = false;
            if (auto qolmod = Loader::get()->getLoadedMod("thesillydoggo.qolmod"))
                qolCached = qolmod->getSavedValue<bool>("show-hitboxes_enabled", false);
            auto eclipseLoaded = Loader::get()->getLoadedMod("eclipse.eclipse-menu") != nullptr;
            if (eclipseLoaded)
            {
                auto modsDir = Mod::get()->getSaveDir().parent_path();
                auto eclipsePath = modsDir / "eclipse.eclipse-menu" / "config.json";
                syncFile(eclipsePath, eclTime, eclKnown,
                         eclShow, "level.showhitboxes");
            }
            eclipseCached = eclipseLoaded && eclShow;
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
