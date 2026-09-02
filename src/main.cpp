#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/loader/Loader.hpp>
#include <Geode/utils/file.hpp>
#include <eclipse.eclipse-menu/include/modules.hpp>
#include <deque>
#include <filesystem>
#include <Windows.h>

using namespace geode::prelude;

namespace {
    struct TrailState {
        cocos2d::CCRect rect;
        cocos2d::CCRect miniRect;
        enum class Click { None, Press, Release, Hold } click = Click::None;
    };

    class ClickTrailNode : public cocos2d::CCDrawNode {
    public:
        static ClickTrailNode* create() {
            auto node = new ClickTrailNode;
            if (node && node->init()) {
                node->autorelease();
                return node;
            }
            CC_SAFE_DELETE(node);
            return nullptr;
        }

        bool init() override {
            if (!CCDrawNode::init()) return false;
            setBlendFunc({ GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA });
            m_world1 = cocos2d::CCNode::create();
            m_world2 = cocos2d::CCNode::create();
            m_world1->retain();
            m_world2->retain();
            m_world1->addChild(m_world2);
            return true;
        }

        ~ClickTrailNode() override {
            CC_SAFE_RELEASE(m_world1);
            CC_SAFE_RELEASE(m_world2);
        }

        void capture(GJBaseGameLayer* layer) {
            // Checkpoints reuse the same attempt, whereas Retry increments
            // this counter. Detect it here instead of hooking resetLevel:
            // resetLevel is inline on this GD version and cannot be hooked.
            if (m_lastAttempt >= 0 && layer->m_attempts > m_lastAttempt)
                resetForNewAttempt();
            m_lastAttempt = layer->m_attempts;

            auto p1Dead = layer->m_player1 && layer->m_player1->m_isDead;
            auto p2Dead = layer->m_player2 && layer->m_player2->m_isDead;
            if (p1Dead || p2Dead) {
                // The original loop stopped before saving a dead player,
                // which omitted the exact collision frame. Save it once,
                // then keep redrawing the frozen trail on later frames.
                if (!m_recordedDeathFrame) {
                    // Save the final collision frame before freezing the
                    // trail. History is always capped by Stored Positions.
                    capturePlayer(layer->m_player1, m_states, m_wasHeld, true);
                    if (layer->m_player2 && layer->m_player2->isRunning())
                        capturePlayer(layer->m_player2, m_states2, m_wasHeld2, true);
                    m_recordedDeathFrame = true;
                    // Geometry remains in CCDrawNode after this call. The
                    // visit transform continues following the camera, so a
                    // dead attempt never needs to rebuild its whole trail.
                    drawTrail(layer);
                }
                m_wasDead = true;
                return;
            }

            if (m_wasDead && Mod::get()->getSettingValue<bool>("reset-on-death")) {
                m_states.clear();
                m_states2.clear();
            }
            m_wasDead = false;
            m_recordedDeathFrame = false;
            clear();

            auto showLiveTrail = shouldShowWhileAlive(layer);
            capturePlayer(layer->m_player1, m_states, m_wasHeld);
            if (layer->m_player2 && layer->m_player2->isRunning())
                capturePlayer(layer->m_player2, m_states2, m_wasHeld2);

            // When QOLMod's Show Hitboxes is on, mirror its live visibility
            // and show the same colour-coded input history before death too.
            if (showLiveTrail)
                drawTrail(layer);
        }

        void capturePlayer(PlayerObject* player, std::deque<TrailState>& states, bool& wasHeld, bool allowDead = false) {
            if (!player || (player->m_isDead && !allowDead)) return;
            bool held = player->m_holdingButtons[static_cast<int>(PlayerButton::Jump)];
            TrailState::Click click = TrailState::Click::None;
            if (held != wasHeld)
                click = held ? TrailState::Click::Press : TrailState::Click::Release;
            else if (held)
                click = TrailState::Click::Hold;
            wasHeld = held;

            states.push_back({
                player->getObjectRect(player->m_vehicleSize, player->m_vehicleSize),
                player->getObjectRect(0.25f, 0.25f),
                click
            });
            auto max = static_cast<size_t>(Mod::get()->getSettingValue<int64_t>("stored-positions"));
            if (states.size() > max)
                states.pop_front();
        }

        void resetForNewAttempt() {
            m_states.clear();
            m_states2.clear();
            m_wasHeld = false;
            m_wasHeld2 = false;
            m_wasDead = false;
            m_recordedDeathFrame = false;
            clear();
        }

    protected:
        void visit() override {
            auto layer = GJBaseGameLayer::get();
            auto parent = layer && layer->m_debugDrawNode ? layer->m_debugDrawNode->getParent() : nullptr;
            if (!parent) return;

            auto window = cocos2d::CCDirector::get()->getWinSize();
            m_world1->setPosition(window / 2);
            m_world1->setRotation(layer->m_gameState.m_cameraAngle);
            if (layer->m_gameState.m_cameraAngle == 0)
                m_world2->setPosition(parent->getPosition() - m_world1->getPosition());
            else
                m_world2->setPosition(parent->getPosition());
            m_world2->setScaleX(parent->getScaleX());
            m_world2->setScaleY(parent->getScaleY());

            kmGLPushMatrix();
            m_world1->transform();
            m_world2->transform();
            CCDrawNode::draw();
            kmGLPopMatrix();
        }

    private:
        void drawTrail(GJBaseGameLayer*) {
            clear();
            drawTrailStates(m_states);
            drawTrailStates(m_states2);
        }

        void drawTrailStates(std::deque<TrailState> const& states) {
            auto total = static_cast<float>(states.size());
            for (size_t i = 0; i < states.size(); ++i) {
                auto const& state = states[i];
                auto age = total > 0 ? static_cast<float>(i) / total : 1.f;
                cocos2d::ccColor4F colour = { 1.f, 0.f, 0.f, 1.f };
                if (Mod::get()->getSettingValue<bool>("colour-clicks")) {
                    switch (state.click) {
                        case TrailState::Click::Press:   colour = asFloatColor(Mod::get()->getSettingValue<cocos2d::ccColor3B>("click-colour")); break;
                        case TrailState::Click::Release: colour = asFloatColor(Mod::get()->getSettingValue<cocos2d::ccColor3B>("release-colour")); break;
                        case TrailState::Click::Hold:
                            if (Mod::get()->getSettingValue<bool>("colour-when-held"))
                                colour = asFloatColor(Mod::get()->getSettingValue<cocos2d::ccColor3B>("hold-colour"));
                            break;
                        default: break;
                    }
                }
                if (Mod::get()->getSettingValue<bool>("darken-by-age")) {
                    colour.r *= 0.25f + age * 0.75f;
                    colour.g *= 0.25f + age * 0.75f;
                    colour.b *= 0.25f + age * 0.75f;
                }
                colour.a = static_cast<float>(Mod::get()->getSettingValue<double>("opacity"));

                // QOLMod's normal outline is 0.35. With direct segments the
                // radius is especially noticeable on a mini player, so scale
                // it with the recorded player hitbox.
                // Keep QOLMod's original 0.35 outline in normal mode. The
                // mini player uses half that value to avoid the four edges
                // visually merging at its reduced hitbox size.
                auto mainThickness = state.rect.size.width < 25.f
                    ? static_cast<float>(Mod::get()->getSettingValue<double>("mini-thickness"))
                    : static_cast<float>(Mod::get()->getSettingValue<double>("normal-thickness"));
                drawInsideRect(state.rect, mainThickness, colour);

                cocos2d::ccColor4F blue = { 0.f, 0.f, 1.f, 1.f };
                if (Mod::get()->getSettingValue<bool>("darken-by-age"))
                    blue.b *= 0.25f + age * 0.75f;
                blue.a = colour.a;
                auto miniThickness = state.rect.size.width < 25.f
                    ? static_cast<float>(Mod::get()->getSettingValue<double>("mini-blue-hitbox-thickness"))
                    : static_cast<float>(Mod::get()->getSettingValue<double>("blue-hitbox-thickness"));
                drawInsideRect(state.miniRect, miniThickness, blue);

            }
        }

        static cocos2d::ccColor4F asFloatColor(cocos2d::ccColor3B color) {
            return { color.r / 255.f, color.g / 255.f, color.b / 255.f, 1.f };
        }

        static bool savedOptionIsTrue(std::string const& contents, std::string const& key) {
            auto quotedKey = "\"" + key + "\"";
            auto keyPos = contents.find(quotedKey);
            if (keyPos == std::string::npos) return false;
            auto colon = contents.find(':', keyPos + quotedKey.size());
            if (colon == std::string::npos) return false;
            auto value = contents.find_first_not_of(" \t\r\n", colon + 1);
            return value != std::string::npos && contents.compare(value, 4, "true") == 0;
        }

        static bool megaHackOptionIsEnabled(std::string const& contents, std::string const& option) {
            // Mega Hack does not store primary hacks as true/false values.
            // It stores the active hack IDs in the currently selected
            // PROFILE array, so only a member of that array is enabled.
            constexpr auto profileTag = "\"PROFILE\"";
            auto profileKey = contents.find(profileTag);
            if (profileKey == std::string::npos) return false;
            auto profileValue = contents.find('"', contents.find(':', profileKey) + 1);
            if (profileValue == std::string::npos) return false;
            auto profileEnd = contents.find('"', profileValue + 1);
            if (profileEnd == std::string::npos) return false;
            auto profile = contents.substr(profileValue + 1, profileEnd - profileValue - 1);

            auto profileArray = contents.find("\"" + profile + "\"", profileEnd);
            if (profileArray == std::string::npos) return false;
            auto arrayStart = contents.find('[', profileArray);
            auto arrayEnd = arrayStart == std::string::npos ? std::string::npos : contents.find(']', arrayStart);
            if (arrayEnd == std::string::npos) return false;
            return contents.substr(arrayStart, arrayEnd - arrayStart).find("\"" + option + "\"") != std::string::npos;
        }

        static std::optional<bool> megaHackRuntimeOption(std::string const& option) {
            using IsHackEnabled = bool(__cdecl*)(std::string const&);
            // Mega Hack loads before this mod. Resolve its exported query
            // once; every later check is an in-memory call with no file I/O.
            static auto function = [] {
                auto module = GetModuleHandleA("absolllute.megahack.dll");
                return module ? reinterpret_cast<IsHackEnabled>(GetProcAddress(
                    module,
                    "?isHackEnabled@extensions@hackpro@@YA_NAEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@Z"
                )) : nullptr;
            }();
            if (!function) return std::nullopt;
            return function(option);
        }

        std::optional<bool> eclipseRuntimeOption() const {
            auto const& vtable = eclipse::__internal__::getVTable();
            if (!vtable.GetHackingModules) return std::nullopt;

            // Eclipse exposes the live list of enabled hacks. This reflects
            // the menu toggle immediately, unlike config::get which can lag
            // behind until the menu serializes its session state.
            if (++m_eclipseCheckTicks >= 4) {
                m_eclipseCheckTicks = 0;
                m_eclipseRuntimeShowHitboxes = false;
                for (auto const& module : eclipse::getEnabledCheats()) {
                    // Eclipse can retain a module in its attempt/cheat list
                    // after it has been switched off. Only its Enabled state
                    // represents a currently active toggle.
                    if (module.state == eclipse::HackingModule::State::Enabled &&
                        (module.name == "level.showhitboxes" || module.name == "Show Hitboxes")) {
                        m_eclipseRuntimeShowHitboxes = true;
                        break;
                    }
                }
            }
            return m_eclipseRuntimeShowHitboxes;
        }

        bool shouldShowWhileAlive(GJBaseGameLayer* layer) const {
            if (layer->m_isDebugDrawEnabled)
                return true;
            if (auto qolmod = Loader::get()->getLoadedMod("thesillydoggo.qolmod"))
                if (qolmod->getSavedValue<bool>("show-hitboxes_enabled", false))
                    return true;

            // Mega Hack is queried directly from its live state on every
            // physics tick. Eclipse has no installed runtime API here, so
            // only its metadata is polled; its JSON is read only when the
            // file timestamp changes.
            auto megaHackEnabled = megaHackRuntimeOption(
                "LEVEL/SHOW_HITBOXES/HACK_SHOW_HITBOXES"
            );
            auto eclipseEnabled = eclipseRuntimeOption();

            if (++m_liveCheckTicks >= 30) {
                m_liveCheckTicks = 0;
                auto modsDir = Mod::get()->getSaveDir().parent_path();
                if (!megaHackEnabled) {
                    auto megaHackPath = modsDir / "absolllute.megahack" / "v9" / "hackpro.json";
                    reloadWhenModified(megaHackPath, m_megaHackLastWriteTime, m_megaHackFileKnown, m_megaHackFileShowHitboxes,
                        "LEVEL/SHOW_HITBOXES/HACK_SHOW_HITBOXES", true);
                }
                if (!eclipseEnabled) {
                    auto eclipsePath = modsDir / "eclipse.eclipse-menu" / "config.json";
                    reloadWhenModified(eclipsePath, m_eclipseLastWriteTime, m_eclipseFileKnown, m_eclipseFileShowHitboxes,
                        "level.showhitboxes", false);
                }
            }
            return megaHackEnabled.value_or(m_megaHackFileShowHitboxes) ||
                eclipseEnabled.value_or(m_eclipseFileShowHitboxes);
        }

        void reloadWhenModified(std::filesystem::path const& path,
                                std::filesystem::file_time_type& lastWrite,
                                bool& known,
                                bool& value,
                                std::string const& key,
                                bool isMegaHack) const {
            std::error_code error;
            auto writeTime = std::filesystem::last_write_time(path, error);
            if (error || (known && writeTime == lastWrite)) return;

            auto contents = utils::file::readString(path).unwrapOr("");
            value = isMegaHack ? megaHackOptionIsEnabled(contents, key) : savedOptionIsTrue(contents, key);
            lastWrite = writeTime;
            known = true;
        }

        void drawInsideRect(cocos2d::CCRect rect, float thickness, cocos2d::ccColor4F colour) {
            // BorderAlignment::Inside가 바깥쪽까지 두께에 따라 흔들리는 문제가 있어서,
            // 바깥 테두리 좌표(rect)를 고정하고 안쪽 경계만 thickness만큼 움직이는
            // 4개의 띠(위/아래/좌/우)를 직접 그리는 방식으로 대체.
            thickness = std::min(thickness, std::min(rect.size.width, rect.size.height) / 2.f);
            if (thickness <= 0.f) return;

            float left = rect.getMinX();
            float right = rect.getMaxX();
            float bottom = rect.getMinY();
            float top = rect.getMaxY();
            float innerLeft = left + thickness;
            float innerRight = right - thickness;
            float innerBottom = bottom + thickness;
            float innerTop = top - thickness;

            cocos2d::CCPoint bottomStrip[4] = {
                {left, bottom}, {right, bottom}, {right, innerBottom}, {left, innerBottom}
            };
            cocos2d::CCPoint topStrip[4] = {
                {left, innerTop}, {right, innerTop}, {right, top}, {left, top}
            };
            cocos2d::CCPoint leftStrip[4] = {
                {left, innerBottom}, {innerLeft, innerBottom}, {innerLeft, innerTop}, {left, innerTop}
            };
            cocos2d::CCPoint rightStrip[4] = {
                {innerRight, innerBottom}, {right, innerBottom}, {right, innerTop}, {innerRight, innerTop}
            };

            drawPolygon(bottomStrip, 4, colour, 0.f, colour);
            drawPolygon(topStrip, 4, colour, 0.f, colour);
            drawPolygon(leftStrip, 4, colour, 0.f, colour);
            drawPolygon(rightStrip, 4, colour, 0.f, colour);
        }
        // QOLMod uses a deque for hitbox history. Removing the oldest state
        // is O(1), unlike erasing the first element of a vector each tick.
        std::deque<TrailState> m_states;
        std::deque<TrailState> m_states2;
        cocos2d::CCNode* m_world1 = nullptr;
        cocos2d::CCNode* m_world2 = nullptr;
        mutable size_t m_liveCheckTicks = 30;
        mutable size_t m_eclipseCheckTicks = 4;
        mutable bool m_megaHackFileKnown = false;
        mutable bool m_megaHackFileShowHitboxes = false;
        mutable bool m_eclipseFileKnown = false;
        mutable bool m_eclipseFileShowHitboxes = false;
        mutable bool m_eclipseRuntimeShowHitboxes = false;
        mutable std::filesystem::file_time_type m_megaHackLastWriteTime {};
        mutable std::filesystem::file_time_type m_eclipseLastWriteTime {};
        int m_lastAttempt = -1;
        bool m_wasHeld = false;
        bool m_wasHeld2 = false;
        bool m_wasDead = false;
        bool m_recordedDeathFrame = false;
    };
}

class $modify(ClickTrailGameLayer, GJBaseGameLayer) {
    // Match QOLMod's tick boundary: checkRepellPlayer runs at the end of a
    // physics tick, then the following camera update is the safe point to
    // snapshot the fully-updated player hitbox and input state.
    struct Fields { ClickTrailNode* trail = nullptr; bool reachedTickEnd = false; };

    bool init() {
        if (!GJBaseGameLayer::init()) return false;
        m_fields->trail = ClickTrailNode::create();
        m_fields->trail->setZOrder(m_uiLayer->getZOrder() - 1);
        insertBefore(m_fields->trail, m_uiLayer);
        return true;
    }

    void checkRepellPlayer() {
        GJBaseGameLayer::checkRepellPlayer();
        m_fields->reachedTickEnd = true;
    }

    void updateCamera(float dt) {
        GJBaseGameLayer::updateCamera(dt);
        if (m_fields->reachedTickEnd && m_fields->trail) {
            m_fields->reachedTickEnd = false;
            m_fields->trail->capture(this);
        }
    }

};
