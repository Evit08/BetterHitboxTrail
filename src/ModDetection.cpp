#include "ModDetection.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/Loader.hpp>
#include <Geode/utils/file.hpp>
#include <matjson.hpp>
#include <asp/fs/fs.hpp>
#include <algorithm>
#include <optional>

using namespace geode::prelude;

// 1 = MegaHack 감지용 디버그 로그 켜기 ([MHDBG] 로 검색), 0 = 끄기
#define HITBOXTRAIL_MH_DEBUG 1

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

#if HITBOXTRAIL_MH_DEBUG
        std::string mhDebugLastSignature;
        size_t mhDebugTick = 0;
        bool mhDebugModLogged = false;

        // 노드 트리를 문자열로 덤프 (구조 변화 감지용이라 vertex 수는 기본적으로 제외)
        void mhDebugDump(CCNode *node, int depth, int maxDepth, std::string &out, bool withVerts)
        {
            if (!node)
                return;
            int verts = -1;
            if (auto dn = typeinfo_cast<CCDrawNode *>(node))
                verts = DrawNodeProbe::vertexCount(dn);
            out += fmt::format("{}- [{}] id='{}' vis={} z={} children={}",
                               std::string(depth * 2, ' '),
                               geode::cocos::getObjectName(node),
                               std::string(node->getID()),
                               node->isVisible(),
                               node->getZOrder(),
                               node->getChildrenCount());
            if (withVerts && verts >= 0)
                out += fmt::format(" verts={}", verts);
            out += "\n";
            if (depth >= maxDepth || !node->getChildren())
                return;
            for (auto child : CCArrayExt<CCNode *>(node->getChildren()))
                mhDebugDump(child, depth + 1, maxDepth, out, withVerts);
        }

        // 레이어 전체에서 CCDrawNode 이거나 ID에 mega/hack/hitbox 가 들어간 노드만 찾기
        void mhDebugFind(CCNode *node, int depth, int maxDepth, std::string &out, std::string const &path)
        {
            if (!node || depth > maxDepth)
                return;
            auto id = std::string(node->getID());
            auto drawNode = typeinfo_cast<CCDrawNode *>(node);
            auto lower = id;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            bool idMatch = lower.find("mega") != std::string::npos ||
                           lower.find("hack") != std::string::npos ||
                           lower.find("hitbox") != std::string::npos;
            if (drawNode || idMatch)
            {
                out += fmt::format("  {}/[{}] id='{}' vis={} verts={}\n",
                                   path,
                                   geode::cocos::getObjectName(node),
                                   id,
                                   node->isVisible(),
                                   drawNode ? DrawNodeProbe::vertexCount(drawNode) : -1);
            }
            if (!node->getChildren())
                return;
            int i = 0;
            for (auto child : CCArrayExt<CCNode *>(node->getChildren()))
                mhDebugFind(child, depth + 1, maxDepth, out, fmt::format("{}/{}", path, i++));
        }

        void mhDebug()
        {
            // 240tps 기준 약 1초마다 실행
            if (++mhDebugTick < 240)
                return;
            mhDebugTick = 0;

            if (!mhDebugModLogged)
            {
                mhDebugModLogged = true;
                for (auto mod : Loader::get()->getAllMods())
                {
                    auto id = mod->getID();
                    if (id.find("mega") != std::string::npos || id.find("hack") != std::string::npos)
                        log::info("[MHDBG] mod id='{}' loaded={} enabled={} version={}",
                                  id, mod->isLoaded(), mod->isOrWillBeEnabled(),
                                  mod->getVersion().toVString());
                }
                log::info("[MHDBG] isModLoaded('absolllute.megahack') = {}",
                          Loader::get()->isModLoaded("absolllute.megahack"));
            }

            auto layer = GJBaseGameLayer::get();
            if (!layer)
                return;

            // 1초마다 한 줄 요약
            auto dd = layer->m_debugDrawNode;
            log::info("[MHDBG] m_isDebugDrawEnabled={} debugDrawNode={} vis={} ownVerts={} children={}",
                      layer->m_isDebugDrawEnabled,
                      static_cast<void *>(dd),
                      dd ? dd->isVisible() : false,
                      dd ? DrawNodeProbe::vertexCount(dd) : -1,
                      dd ? dd->getChildrenCount() : 0);

            // 구조가 바뀌었을 때만 전체 덤프
            std::string structure;
            if (dd)
            {
                structure += "[debugDrawNode subtree]\n";
                mhDebugDump(dd, 0, 3, structure, false);
            }
            structure += "[layer: DrawNode / mega / hack / hitbox nodes, depth<=4]\n";
            mhDebugFind(layer, 0, 4, structure, "layer");

            if (structure != mhDebugLastSignature)
            {
                mhDebugLastSignature = structure;
                log::info("[MHDBG] structure changed:\n{}", structure);
            }
        }
#endif

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
#if HITBOXTRAIL_MH_DEBUG
        mhDebug();
#endif
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
