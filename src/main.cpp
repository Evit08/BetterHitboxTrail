#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>
#include <Geode/loader/Loader.hpp>
#include <Geode/utils/file.hpp>
#include <eclipse.eclipse-menu/include/modules.hpp>
#include <eclipse.eclipse-menu/include/config.hpp>
#include <imgui-cocos.hpp>
#include <utility>
#include <deque>
#include <array>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <Windows.h>

using namespace geode::prelude;

namespace {
    // Cube covers both actual Cube mode and the implicit "none of the
    // flags below are set" fallback case.
    enum class GameMode { Cube, Ship, Ball, Ufo, Wave, Robot, Spider, Swing };

    // Fill clearances are fractions of their outline thickness. These are
    // renderer-calibrated constants, not user settings: Blue needs a tiny
    // per-mode adjustment to land on the same pixel boundary everywhere.
    constexpr float kSquareFillInset = 0.99996f;
    constexpr float kCircleFillInset = 1.00863f;
    constexpr float kRotationFillInset = 0.99996f;

    // drawHitboxRect's polygon-border outline (Square/Blue/Rotation) and
    // drawHitboxCircle's native CCDrawNode circle stroke render the same
    // "thickness" value at very different visual weights - purely a side
    // effect of how each primitive rasterizes its border, not a deliberate
    // design difference. This correction keeps Master Thickness = 1
    // producing roughly the matching visual line weight on both: the
    // three polygon-border shapes get scaled down, the circle guide is
    // left alone (i.e. still multiplied by 1 - see drawCircleHitbox).
    constexpr float kRectThicknessCorrection = 0.5f;

    struct TrailState {
        cocos2d::CCRect rect;
        cocos2d::CCRect miniRect;
        float rotation = 0.f;
        enum class Click { None, Press, Release, Hold } click = Click::None;
        // Per-mode inset lookup happens at draw time, once the PlayerObject
        // is gone - so mode is captured here instead.
        GameMode mode = GameMode::Cube;
        // getObjectRect()'s scale (1.f normally, smaller in Mini Mode) -
        // scales per-mode insets down instead of needing a mini setting.
        float vehicleSize = 1.f;
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

        static void disableCompetingTrailsNow() {
            if (auto qolmod = Loader::get()->getLoadedMod("thesillydoggo.qolmod")) {
                constexpr auto key = "hitbox-trail_enabled";
                if (qolmod->hasSetting(key)) {
                    qolmod->setSettingValue<bool>(key, false);
                }

            }

            // Eclipse is an optional dependency. Never enter its API unless
            // the mod is actually loaded; otherwise an absent extension may
            // leave its config vtable unavailable.
            if (Loader::get()->getLoadedMod("eclipse.eclipse-menu")) {
                s_eclipseTrailConfirmedOff = false;
                eclipse::config::set<bool>("level.showhitboxes.traillength.toggle", false);
                eclipse::config::setInternal<bool>("level.showhitboxes.traillength.toggle", false);
            }

            using SetHackEnabled = void(__cdecl*)(std::string const&, bool);
            static auto setHackEnabled = [] {
                auto module = GetModuleHandleA("absolllute.megahack.dll");
                return module ? reinterpret_cast<SetHackEnabled>(GetProcAddress(
                module,
                "?setHackEnabled@extensions@hackpro@@YAXAEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@_N@Z"
                )) : nullptr;
            }();
            if (setHackEnabled)
                setHackEnabled("LEVEL/SHOW_HITBOXES_TRAIL/HACK_SHOW_HITBOXES_TRAIL", false);
        }

        static void healThicknessSettings() {
            // Earlier builds could leave "thickness" (the master
            // percent-style multiplier, meant to stay in [0.01, 2.0] i.e.
            // 1%-200%) or any per-mode thickness setting (meant to stay
            // in [0.25, 10]) holding a value outside that range.
            // makeRenderSettings() has clamped both at render time since
            // the last fix, but the stored value itself was never
            // corrected - it just sat pinned at whatever the render
            // clamp's boundary was (e.g. 200%) instead of settling back
            // to something sane, so the popup's displayed number and the
            // actual rendered thickness could still disagree even after
            // that fix. Correct the stored values themselves here, once
            // on mod load (see $on_mod(Loaded)), reusing this class's own
            // key-name functions so there's no risk of a key drifting out
            // of sync between this and makeRenderSettings().
            auto mod = Mod::get();
            auto heal = [mod](char const* key, double lo, double hi) {
                auto value = mod->getSettingValue<double>(key);
                auto clamped = std::clamp(value, lo, hi);
                if (clamped != value)
                    mod->setSettingValue<double>(key, clamped);
            };
            heal("thickness", 0.01, 2.0);
            for (auto mode : { GameMode::Cube, GameMode::Wave }) {
                for (auto isMini : { false, true }) {
                    heal(modeThicknessKey(mode, isMini), 0.25, 10.0);
                    heal(modeBlueThicknessKey(mode, isMini), 0.25, 10.0);
                    heal(modeCircleThicknessKey(mode, isMini), 0.25, 10.0);
                    heal(modeRotationThicknessKey(mode, isMini), 0.25, 10.0);
                }
            }
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

        void setUsesGameplayTransform(bool value) {
            m_usesGameplayTransform = value;
        }

        ~ClickTrailNode() override {
            CC_SAFE_RELEASE(m_world1);
            CC_SAFE_RELEASE(m_world2);
        }

        void resetTrails() {
            m_states.clear();
            m_states2.clear();
            m_wasDead = false;
            m_recordedDeathFrame = false;
            m_lastHeldP1 = false;
            m_lastHeldP2 = false;
            m_flashClickP1 = TrailState::Click::None;
            m_flashClickP2 = TrailState::Click::None;
            m_flashTicksP1 = 0;
            m_flashTicksP2 = 0;
            m_captureAccumulator = 0.f;
            clear();
        }

        void setLastTickDt(float dt) {
            m_lastTickDt = dt;
        }

        // Trail Capture Rate (Hz): how many *regular* (Hold/None) samples
        // per second the trail records. Click/release edges always bypass
        // this and get recorded immediately regardless (see
        // capturePlayer()) - this only throttles the steady samples taken
        // while nothing changed. 0 is a special case: no regular sample is
        // ever recorded, so only the click/release edges show at all -
        // this is why the setting's minimum is 0, not some small positive
        // rate. Only Click and Release (see isOnlyClickReleaseEnabled())
        // forces this same 0 behaviour without touching the saved rate
        // itself, so turning it back off restores whatever rate the user
        // actually had configured.
        //
        // Uses the real per-tick dt from getModifiedDelta (set via
        // setLastTickDt every tick) rather than wall-clock time: several
        // physics substeps can land within the same rendered frame, and
        // measuring real elapsed time between those would make samples
        // clump together unevenly even though each substep still covers
        // an even slice of game time.
        bool consumeCaptureTick() {
            if (isOnlyClickReleaseEnabled()) {
                // Only Player/Only Cube each cap history at a single
                // state (trimToMax()). If capture kept being skipped here
                // on every tick that isn't a press/release edge, that one
                // stored state would only ever update at the instant of a
                // click/release and then sit frozen there - detached from
                // the player's actual current position the rest of the
                // time (the reported bug: hitbox left behind at the last
                // click/release point instead of following the player).
                // Capturing every tick instead keeps that single state
                // always at the player's current position; drawTrailStates()
                // is what still only shows it during an actual press/
                // release/hold, not this.
                if (isOnlyPlayerEnabled() || isCubeHitboxEnabled())
                    return true;
                return false;
            }
            auto captureRate = Mod::get()->getSettingValue<double>("trail-capture-rate");
            if (captureRate <= 0.0)
                return false;
            auto targetInterval = static_cast<float>(1.0 / captureRate);
            m_captureAccumulator += m_lastTickDt;
            if (m_captureAccumulator + 0.00001f < targetInterval)
                return false;
            m_captureAccumulator = std::fmod(m_captureAccumulator, targetInterval);
            return true;
        }

        void capture(GJBaseGameLayer* layer) {
            handleCompetingTrails();
            // Resolve competing-trail state once per capture tick. The same
            // result is passed to drawTrail below, avoiding a second QOLMod /
            // MegaHack check in the same tick.
            auto trailEnabled = isTrailEnabled();
            if (!trailEnabled) {
                clear();
                return;
            }

            auto p1Dead = layer->m_player1 && layer->m_player1->m_isDead;
            auto p2Dead = layer->m_player2 && layer->m_player2->m_isDead;
            if (p1Dead || p2Dead) {
                // Save the exact collision frame once, then keep redrawing
                // the frozen trail (the old loop skipped saving a dead
                // player, losing that frame).
                if (!m_recordedDeathFrame) {
                    capturePlayer(layer->m_player1, m_states, false, true);
                    if (layer->m_player2 && layer->m_player2->isRunning())
                        capturePlayer(layer->m_player2, m_states2, true, true);
                    m_recordedDeathFrame = true;
                    // Camera-following geometry stays in the CCDrawNode, so
                    // a dead attempt never rebuilds its trail.
                    if (Mod::get()->getSavedValue<bool>("hitbox-trail-on-death", true))
                        drawTrail(layer, trailEnabled);
                    else
                        clear();
                }
                m_wasDead = true;
                return;
            }

            // History only resets via resetTrails() (actual Retry) now -
            // "Reset Positions on Death" was removed.
            m_wasDead = false;
            m_recordedDeathFrame = false;
            clear();

            auto showLiveTrail = Mod::get()->getSavedValue<bool>("always-show-hitbox-trail", false)
            || shouldShowWhileAlive(layer);
            auto sampleThisTick = consumeCaptureTick();
            capturePlayer(layer->m_player1, m_states, false, false, sampleThisTick);
            if (layer->m_player2 && layer->m_player2->isRunning())
                capturePlayer(layer->m_player2, m_states2, true, false, sampleThisTick);

            if (showLiveTrail)
                drawTrail(layer, trailEnabled);
        }

        // Lets ImGui changes apply immediately instead of waiting for a
        // physics tick - reruns capture()'s draw-or-not logic against the
        // already-stored states without capturing anything new (useful
        // while paused, when capture() never runs).
        void refreshDrawing(GJBaseGameLayer* layer) {
            auto trailEnabled = isTrailEnabled();
            if (!trailEnabled) {
                clear();
                return;
            }
            // Re-applies capturePlayer()'s trimming so changing Stored
            // Positions/Only Cube/Only Player from the popup works
            // immediately even while paused.
            trimToMax(m_states);
            trimToMax(m_states2);
            if (m_wasDead) {
                if (Mod::get()->getSavedValue<bool>("hitbox-trail-on-death", true))
                    drawTrail(layer, trailEnabled);
                else
                    clear();
                return;
            }
            auto showLiveTrail = Mod::get()->getSavedValue<bool>("always-show-hitbox-trail", false)
            || (layer && shouldShowWhileAlive(layer));
            if (showLiveTrail)
                drawTrail(layer, trailEnabled);
            else
                clear();
        }

        // Only Cube/Only Player cap history at 1 - see
        // isCubeHitboxEnabled()/isOnlyPlayerEnabled(). Called from
        // capturePlayer() and refreshDrawing() so popup changes apply
        // immediately even while paused.
        void trimToMax(std::deque<TrailState>& states) {
            auto forceSingle = isCubeHitboxEnabled() || isOnlyPlayerEnabled();
            auto max = forceSingle
            ? size_t{1}
            : static_cast<size_t>(Mod::get()->getSettingValue<int64_t>("stored-positions"));
            // while, not if: toggling Only Cube on or lowering Stored
            // Positions mid-trail can drop max far below the current size
            // in one step - if would only trim one element per call.
            while (states.size() > max)
                states.pop_front();
        }

        // See ClickTrailGameLayer::handleButton for why capture also
        // happens outside the once-per-tick capture() below.
        void capturePlayer(PlayerObject* player, std::deque<TrailState>& states,
        bool isPlayer2 = false, bool allowDead = false,
        bool sampleThisTick = true) {
            if (!player || (player->m_isDead && !allowDead)) return;

            auto& lastHeld = isPlayer2 ? m_lastHeldP2 : m_lastHeldP1;
            bool held = player->m_holdingButtons[static_cast<int>(PlayerButton::Jump)];
            bool changed = held != lastHeld;
            TrailState::Click click = TrailState::Click::None;
            if (changed)
                click = held ? TrailState::Click::Press : TrailState::Click::Release;
            else if (held)
                click = TrailState::Click::Hold;
            lastHeld = held;

            // Only Player/Only Cube force a single, constantly-overwritten
            // state (trimToMax() caps history at 1) that now also gets
            // re-captured every tick when combined with Only Click and
            // Release (see consumeCaptureTick()), so the box always tracks
            // the player's live position. The cost is that Press/Release
            // is only ever true on the exact tick the edge happens - the
            // very next tick overwrites it with Hold/None, so at normal
            // tick rates that colour was only ever on screen for about
            // 1/240s, in practice invisible (confirmed: it only became
            // visible when the game was paused right on that tick, since
            // pausing is what stopped it from being overwritten again).
            // Stretch it out over a real, visible window instead: latch
            // whichever edge just happened and keep reporting that colour
            // for kFlashTicks ticks, independent of what actually happens
            // to held during that window, before letting it fall back to
            // None (Only Click and Release's own draw-time filter in
            // drawTrailStates() is what hides it outside Press/Release,
            // same as it always did - this only controls how long an edge
            // keeps counting as "just happened" before that filter would
            // otherwise drop it).
            if (isOnlyClickReleaseEnabled() && (isOnlyPlayerEnabled() || isCubeHitboxEnabled())) {
                constexpr int kFlashTicks = 2;
                auto& flashClick = isPlayer2 ? m_flashClickP2 : m_flashClickP1;
                auto& flashTicks = isPlayer2 ? m_flashTicksP2 : m_flashTicksP1;
                if (changed) {
                    flashClick = click;
                    flashTicks = kFlashTicks;
                }
                if (flashTicks > 0) {
                    click = flashClick;
                    --flashTicks;
                } else {
                    click = TrailState::Click::None;
                }
            }

            // Trail Capture Rate only throttles steady Hold/None samples -
            // press/release edges always get through immediately.
            if (!sampleThisTick && !changed)
                return;

            states.push_back({
                player->getObjectRect(player->m_vehicleSize, player->m_vehicleSize),
                player->getObjectRect(0.25f, 0.25f),
                player->getRotation(),
                click,
                currentGameMode(player),
                player->m_vehicleSize
            });
            trimToMax(states);
        }

        // Public entry point called from ClickTrailGameLayer::handleButton.
        void captureButtonEdge(bool isPlayer1, PlayerObject* player1, PlayerObject* player2) {
            if (isPlayer1)
                capturePlayer(player1, m_states, false);
            else
                capturePlayer(player2, m_states2, true);
        }

    protected:
        void visit() override {
            if (m_usesGameplayTransform) {
                // This node is a sibling of m_debugDrawNode, so it inherits
                // the gameplay camera transform and remains below all UI.
                CCDrawNode::visit();
                return;
            }

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
        void drawTrail(GJBaseGameLayer*, bool trailEnabled) {
            clear();
            if (!trailEnabled)
                return;
            // Settings do not vary between trail points. Snapshot them once
            // per geometry rebuild so a 1024-position dual trail does not
            // perform thousands of string-key setting lookups every frame.
            auto settings = makeRenderSettings();
            drawTrailStates(m_states, settings);
            drawTrailStates(m_states2, settings);
        }

        static bool isCompetingTrailEnabled() {
            auto qolTrail = false;
            if (auto qolmod = Loader::get()->getLoadedMod("thesillydoggo.qolmod"))
                qolTrail = qolmod->getSavedValue<bool>("hitbox-trail_enabled", false);

            auto megaHackTrail = megaHackRuntimeOption(
            "LEVEL/SHOW_HITBOXES_TRAIL/HACK_SHOW_HITBOXES_TRAIL"
            ).value_or(false);

            auto eclipseLoaded = Loader::get()->getLoadedMod("eclipse.eclipse-menu") != nullptr;
            if (eclipseLoaded && ++s_eclipseTrailCheckTicks >= 15) {
                s_eclipseTrailCheckTicks = 0;
                auto modsDir = Mod::get()->getSaveDir().parent_path();
                auto path = modsDir / "eclipse.eclipse-menu" / "config.json";
                std::error_code error;
                auto writeTime = std::filesystem::last_write_time(path, error);
                if (!error && (!s_eclipseTrailFileKnown || writeTime != s_eclipseTrailLastWrite)) {
                    auto contents = utils::file::readString(path).unwrapOr("");
                    s_eclipseTrailFileToggle = savedOptionIsTrue(contents, "level.showhitboxes.traillength.toggle");
                    s_eclipseTrailLastWrite = writeTime;
                    s_eclipseTrailFileKnown = true;
                    if (!s_eclipseTrailFileToggle)
                        s_eclipseTrailConfirmedOff = true;
                }
            }

            auto eclipseTrail = eclipseLoaded && s_eclipseTrailConfirmedOff && s_eclipseTrailFileToggle;

            return qolTrail || megaHackTrail || eclipseTrail;
        }

        static bool isTrailEnabled() {
            return Mod::get()->getSavedValue<bool>("hitbox-trail-enabled", true) && !isCompetingTrailEnabled();
        }

        // Shows only a single, non-trailed rotation outline - suppresses
        // main/blue/circle and the trail history; rotation draws even if
        // Rotation Hitbox is off since it's the only thing left to show.
        static bool isCubeHitboxEnabled() {
            return Mod::get()->getSavedValue<bool>("cube-hitbox-enabled", false);
        }

        // Unlike Only Cube, doesn't suppress any outline - just caps
        // history at 1 (same mechanism as Only Cube), so whatever's
        // enabled still draws, just without a trail behind it.
        static bool isOnlyPlayerEnabled() {
            return Mod::get()->getSavedValue<bool>("only-player-enabled", false);
        }

        // Forces consumeCaptureTick() to behave as if Trail Capture Rate
        // were 0 - only Press/Release edges get recorded, no regular
        // Hold/None samples - without touching the actual saved
        // trail-capture-rate setting, same reasoning as Only Cube/Only
        // Player leaving their own settings untouched: turning this back
        // off should restore whatever rate the user actually had
        // configured, not some remembered/default value.
        static bool isOnlyClickReleaseEnabled() {
            return Mod::get()->getSavedValue<bool>("only-click-release-enabled", false);
        }

        void handleCompetingTrails() {
            if (!Mod::get()->getSavedValue<bool>("hitbox-trail-enabled", true)) {
                m_competingTrailsInitialised = false;
                m_disableRetryFrames = 0;
                return;
            }

            if (!m_competingTrailsInitialised) {
                disableCompetingTrailsNow();
                m_competingTrailsInitialised = true;

                m_disableRetryFrames = 60;
                return;
            }

            if (m_disableRetryFrames > 0) {
                --m_disableRetryFrames;
                disableCompetingTrailsNow();
            }
        }

        struct RenderSettings {
            bool squareEnabled, blueEnabled, circleEnabled, rotationEnabled, onlyCube;
            bool colorClicks, colorWhenHeld, darkenWithAge, fadeWithAge;
            bool circleColorClicks, rotationColorClicks;
            bool fillEnabled;
            float opacity, masterThickness, squareOpacity, blueOpacity, circleOpacity, circleInset, rotationOpacity;
            float fillOpacity;
            cocos2d::ccColor4F mainColor, blueColor, circleColor, rotationColor;
            cocos2d::ccColor4F pressColor, releaseColor, holdColor;
            std::array<std::array<float, 2>, 3> mainInset, blueInset;
            std::array<std::array<float, 2>, 2> mainThickness, blueThickness, circleThickness, rotationThickness;
        };

        static size_t insetModeIndex(GameMode mode) {
            return mode == GameMode::Spider ? 1 : mode == GameMode::Wave ? 2 : 0;
        }

        static size_t thicknessModeIndex(GameMode mode) {
            return mode == GameMode::Wave ? 1 : 0;
        }

        // Pixel-calibrated Blue fill inset for each collision-size family.
        // Standard covers Cube, Ship, Ball, UFO, Robot and Swing.
        static float blueFillInset(GameMode mode, bool isMini) {
            switch (mode) {
            case GameMode::Spider: return isMini ? 1.f : 0.99998f;
            case GameMode::Wave:   return isMini ? 0.99995f : 1.00003f;
            default:                return 1.f;
            }
        }

        static RenderSettings makeRenderSettings() {
            auto mod = Mod::get();
            RenderSettings s {
                .squareEnabled = mod->getSavedValue<bool>("square-hitbox-enabled", true),
                .blueEnabled = mod->getSavedValue<bool>("blue-hitbox-enabled", true),
                .circleEnabled = mod->getSavedValue<bool>("circle-hitbox-enabled", true),
                .rotationEnabled = mod->getSavedValue<bool>("player-rotation", true),
                .onlyCube = mod->getSavedValue<bool>("cube-hitbox-enabled", false),
                .colorClicks = mod->getSavedValue<bool>("color-clicks", true),
                .colorWhenHeld = mod->getSavedValue<bool>("color-when-held", true),
                .darkenWithAge = mod->getSavedValue<bool>("darken-by-age", false),
                .fadeWithAge = mod->getSavedValue<bool>("fade-with-age", false),
                .circleColorClicks = mod->getSettingValue<bool>("circle-hitbox-color-clicks"),
                .rotationColorClicks = mod->getSettingValue<bool>("rotation-color-clicks"),
                .fillEnabled = mod->getSavedValue<bool>("fill-hitbox", false),
                .opacity = static_cast<float>(mod->getSettingValue<double>("opacity")),
                // Clamped here too, not just in the ImGui edit callback
                // below - that clamp only ever runs when the field is
                // actively typed into, so a value already stored outside
                // 1%-200% (e.g. left over from before this was a percent-
                // style multiplier, or set some other way) would otherwise
                // get used completely unclamped forever, capable of
                // making every hitbox many times thicker than intended
                // even with a perfectly normal per-mode Thickness value.
                .masterThickness = std::clamp(static_cast<float>(mod->getSettingValue<double>("thickness")), 0.01f, 2.f),
                .squareOpacity = static_cast<float>(mod->getSettingValue<double>("square-hitbox-opacity")),
                .blueOpacity = static_cast<float>(mod->getSettingValue<double>("blue-hitbox-opacity")),
                .circleOpacity = static_cast<float>(mod->getSettingValue<double>("circle-hitbox-opacity")),
                .circleInset = static_cast<float>(mod->getSettingValue<double>("circle-hitbox-inset")),
                .rotationOpacity = static_cast<float>(mod->getSettingValue<double>("player-rotation-opacity")),
                .fillOpacity = static_cast<float>(mod->getSavedValue<double>("fill-opacity", 0.35)),
                .mainColor = asFloatColor(mod->getSettingValue<cocos2d::ccColor3B>("hitbox-trail-color")),
                .blueColor = asFloatColor(mod->getSettingValue<cocos2d::ccColor3B>("blue-hitbox-color")),
                .circleColor = asFloatColor(mod->getSettingValue<cocos2d::ccColor3B>("circle-hitbox-color")),
                .rotationColor = asFloatColor(mod->getSettingValue<cocos2d::ccColor3B>("player-rotation-color")),
                .pressColor = asFloatColor(mod->getSettingValue<cocos2d::ccColor3B>("click-color")),
                .releaseColor = asFloatColor(mod->getSettingValue<cocos2d::ccColor3B>("release-color")),
                .holdColor = asFloatColor(mod->getSettingValue<cocos2d::ccColor3B>("hold-color")),
            };
            constexpr std::array modes { GameMode::Cube, GameMode::Spider, GameMode::Wave };
            for (size_t mode = 0; mode < modes.size(); ++mode)
                for (size_t mini = 0; mini < 2; ++mini) {
                    auto isMini = mini != 0;
                    s.mainInset[mode][mini] = static_cast<float>(mod->getSettingValue<double>(modeInsetKey(modes[mode], isMini)));
                    s.blueInset[mode][mini] = static_cast<float>(mod->getSettingValue<double>(modeBlueInsetKey(modes[mode], isMini)));
                }
            constexpr std::array thicknessModes { GameMode::Cube, GameMode::Wave };
            for (size_t mode = 0; mode < thicknessModes.size(); ++mode)
                for (size_t mini = 0; mini < 2; ++mini) {
                    auto isMini = mini != 0;
                    // Same reasoning as masterThickness above - clamped
                    // here too so a stored value outside the ImGui field's
                    // own [0.25, 10] range can't slip through unclamped.
                    s.mainThickness[mode][mini] = std::clamp(static_cast<float>(mod->getSettingValue<double>(modeThicknessKey(thicknessModes[mode], isMini))), 0.25f, 10.f);
                    s.blueThickness[mode][mini] = std::clamp(static_cast<float>(mod->getSettingValue<double>(modeBlueThicknessKey(thicknessModes[mode], isMini))), 0.25f, 10.f);
                    s.circleThickness[mode][mini] = std::clamp(static_cast<float>(mod->getSettingValue<double>(modeCircleThicknessKey(thicknessModes[mode], isMini))), 0.25f, 10.f);
                    s.rotationThickness[mode][mini] = std::clamp(static_cast<float>(mod->getSettingValue<double>(modeRotationThicknessKey(thicknessModes[mode], isMini))), 0.25f, 10.f);
                }
            return s;
        }

        void drawTrailStates(std::deque<TrailState> const& states, RenderSettings const& settings) {
            // Overlap priority: blue > main > circle > rotation, same
            // order for every state so each state's 4 layers stay grouped.
            // Outer loop runs oldest->newest, so newer states still draw
            // on top of older ones as a whole.
            //
            // Only Click and Release is applied here (at draw time), not
            // just at capture time, so it takes effect immediately on
            // every already-captured state too - including while paused,
            // when no new capture ever runs to make consumeCaptureTick()'s
            // throttle relevant. Filtering here instead of pruning the
            // deque also means turning it back off instantly shows
            // everything again, nothing was actually discarded.
            //
            // Only Press/Release ever show here, in every case - including
            // Only Player/Only Cube, where consumeCaptureTick() still
            // captures every tick underneath this (so the single stored
            // state's position always tracks the player, even while
            // Hold/None is what's currently being drawn-filtered-out) -
            // that's what keeps the box glued to the player instead of
            // stuck at the last click/release position, without also
            // needing to keep it visible while just held.
            auto onlyClickRelease = isOnlyClickReleaseEnabled();
            auto total = static_cast<float>(states.size());
            for (size_t idx = 0; idx < states.size(); ++idx) {
                // Map the oldest and newest points exactly to 0 and 1.
                // A one-point trail is necessarily the newest point.
                auto age = total > 1.f ? static_cast<float>(idx) / (total - 1.f) : 1.f;
                auto const& state = states[idx];
                if (onlyClickRelease
                    && state.click != TrailState::Click::Press
                    && state.click != TrailState::Click::Release)
                    continue;
                drawRotationHitbox(state, age, settings);
                drawCircleHitbox(state, age, settings);
                drawMainHitbox(state, age, settings);
                drawBlueHitbox(state, age, settings);
            }
        }

        void drawMainHitbox(TrailState const& state, float age, RenderSettings const& s) {
            if (!s.squareEnabled || s.onlyCube)
                return;
            auto isMini = state.vehicleSize < 0.99f;
            auto rect = insetRect(state.rect, s.mainInset[insetModeIndex(state.mode)][isMini]);
            auto thickness = s.masterThickness * kRectThicknessCorrection * s.mainThickness[thicknessModeIndex(state.mode)][isMini];
            // true: main trail always supports Darken with Age (whether it's
            // currently applied is trailColor's own darkenWithAge check).
            auto color = trailColor(state, age, s.mainColor, s.colorClicks, true, s);
            auto outlineAlpha = s.opacity * s.squareOpacity * ageOpacity(age, s);
            auto fill = color;
            fill.a = s.fillEnabled ? s.fillOpacity * ageOpacity(age, s) : 0.f;
            color.a = outlineAlpha;
            drawHitboxRect(rect, thickness, fill, color, kSquareFillInset);
        }

        void drawBlueHitbox(TrailState const& state, float age, RenderSettings const& s) {
            if (!s.blueEnabled || s.onlyCube)
                return;
            auto isMini = state.vehicleSize < 0.99f;
            auto rect = insetRect(state.miniRect, s.blueInset[insetModeIndex(state.mode)][isMini]);
            auto thickness = s.masterThickness * kRectThicknessCorrection * s.blueThickness[thicknessModeIndex(state.mode)][isMini];
            auto blue = s.blueColor;
            if (s.darkenWithAge) {
                auto fade = 0.25f + age * 0.75f;
                blue.r *= fade; blue.g *= fade; blue.b *= fade;
            }
            auto outlineAlpha = s.opacity * s.blueOpacity * ageOpacity(age, s);
            auto fill = blue;
            fill.a = s.fillEnabled ? s.fillOpacity * ageOpacity(age, s) : 0.f;
            blue.a = outlineAlpha;
            drawHitboxRect(rect, thickness, fill, blue, blueFillInset(state.mode, isMini));
        }

        // Approximates the round collision check used against circular
        // hazards - a circle inscribed in the main hitbox's outer edge.
        void drawCircleHitbox(TrailState const& state, float age, RenderSettings const& s) {
            if (!s.circleEnabled || s.onlyCube)
                return;
            auto isMini = state.vehicleSize < 0.99f;
            // true: circle guide always supports Darken with Age too.
            auto circleColor = trailColor(state, age, s.circleColor,
            s.colorClicks && s.circleColorClicks, true, s);
            auto circleRect = insetRect(state.rect, s.mainInset[insetModeIndex(state.mode)][isMini]);
            auto circleCentre = cocos2d::CCPointMake(circleRect.getMidX(), circleRect.getMidY());
            auto circleRadius = std::min(circleRect.size.width, circleRect.size.height) / 2.f;
            circleRadius = std::max(circleRadius - s.circleInset, 0.f);
            // Deliberately not scaled by kRectThicknessCorrection - the
            // native circle stroke used here doesn't need it. See that
            // constant's comment.
            auto circleThickness = s.masterThickness * s.circleThickness[thicknessModeIndex(state.mode)][isMini];
            auto outlineAlpha = s.opacity * s.circleOpacity * ageOpacity(age, s);
            auto fill = circleColor;
            fill.a = s.fillEnabled ? s.fillOpacity * ageOpacity(age, s) : 0.f;
            circleColor.a = outlineAlpha;
            drawHitboxCircle(circleCentre, circleRadius, circleThickness, fill, circleColor, kCircleFillInset, 24);
        }

        // Draws only the main player box - the blue collision hitbox stays
        // exclusive to the non-rotated trail above.
        void drawRotationHitbox(TrailState const& state, float age, RenderSettings const& s) {
            // Only Cube exception: still draws with Rotation Hitbox off,
            // since it's the only outline left once Only Cube suppresses
            // everything else.
            if (!s.rotationEnabled && !s.onlyCube)
                return;
            auto isMini = state.vehicleSize < 0.99f;
            auto thickness = s.masterThickness * kRectThicknessCorrection * s.rotationThickness[thicknessModeIndex(state.mode)][isMini];
            // Now takes the real per-state age and darken support (true)
            // like every other layer - it used to be hardcoded off (age=0,
            // applyDarken=false), so Darken with Age never affected it.
            auto rotationColor = trailColor(state, age, s.rotationColor,
            s.colorClicks && s.rotationColorClicks, true, s);
            auto outlineAlpha = s.opacity * s.rotationOpacity * ageOpacity(age, s);
            auto fill = rotationColor;
            fill.a = s.fillEnabled ? s.fillOpacity * ageOpacity(age, s) : 0.f;
            // Rotation already receives the same Darken with Age path above;
            // apply the matching Fade with Age alpha curve as well.
            rotationColor.a = outlineAlpha;
            drawHitboxRect(state.rect, thickness, fill, rotationColor, kRotationFillInset, state.rotation);
        }

        // Applies Click/Release/Hold colours and Darken with Age.
        // colorClicks/applyDarken are plain capability flags passed in by
        // the caller (every layer type now supports both) rather than
        // being re-derived from settings here.
        static cocos2d::ccColor4F trailColor(TrailState const& state, float age,
        cocos2d::ccColor4F color, bool colorClicks,
        bool applyDarken, RenderSettings const& s) {
            if (colorClicks) {
                switch (state.click) {
                case TrailState::Click::Press:   color = s.pressColor; break;
                case TrailState::Click::Release: color = s.releaseColor; break;
                case TrailState::Click::Hold:
                    if (s.colorWhenHeld) color = s.holdColor;
                    break;
                default: break;
                }
            }
            if (applyDarken && s.darkenWithAge) {
                color.r *= 0.25f + age * 0.75f;
                color.g *= 0.25f + age * 0.75f;
                color.b *= 0.25f + age * 0.75f;
            }
            return color;
        }

        // Fade more aggressively than Darken: the oldest point is invisible
        // and opacity rises quadratically to 100% at the newest point.
        static float ageOpacity(float age, RenderSettings const& s) {
            return s.fadeWithAge ? age * age : 1.f;
        }

        static cocos2d::ccColor4F asFloatColor(cocos2d::ccColor3B color) {
            return { color.r / 255.f, color.g / 255.f, color.b / 255.f, 1.f };
        }

        // getObjectRect() already accounts for each mode's real collision
        // size, so only the mode itself needs detecting here.
        static GameMode currentGameMode(PlayerObject* player) {
            if (player->m_isShip) return GameMode::Ship;
            if (player->m_isBall) return GameMode::Ball;
            if (player->m_isBird) return GameMode::Ufo;
            if (player->m_isDart) return GameMode::Wave;
            if (player->m_isRobot) return GameMode::Robot;
            if (player->m_isSpider) return GameMode::Spider;
            if (player->m_isSwing) return GameMode::Swing;
            return GameMode::Cube;
        }

        // Cube/Ship/Ball/UFO/Robot/Swing share one hitbox size ("standard-
        // inset"); Spider/Wave are noticeably smaller and get their own
        // settings, each with a Mini variant.
        static char const* modeInsetKey(GameMode mode, bool isMini) {
            switch (mode) {
            case GameMode::Spider: return isMini ? "spider-mini-inset" : "spider-inset";
            case GameMode::Wave:   return isMini ? "wave-mini-inset" : "wave-inset";
            default:               return isMini ? "standard-mini-inset" : "standard-inset";
            }
        }

        // Same idea, but for thickness - only Wave gets its own pair here
        // (Spider's hitbox is close enough to standard size to share).
        static char const* modeThicknessKey(GameMode mode, bool isMini) {
            switch (mode) {
            case GameMode::Wave: return isMini ? "wave-mini-thickness" : "wave-thickness";
            default:             return isMini ? "mini-thickness" : "normal-thickness";
            }
        }

        // Same Wave/default split, for the circle guide's thickness.
        static char const* modeCircleThicknessKey(GameMode mode, bool isMini) {
            switch (mode) {
            case GameMode::Wave: return isMini ? "wave-mini-circle-hitbox-thickness" : "wave-circle-hitbox-thickness";
            default:             return isMini ? "mini-circle-hitbox-thickness" : "circle-hitbox-thickness";
            }
        }

        static char const* modeRotationThicknessKey(GameMode mode, bool isMini) {
            switch (mode) {
            case GameMode::Wave: return isMini ? "player-rotation-wave-mini-thickness" : "player-rotation-wave-thickness";
            default:             return isMini ? "player-rotation-mini-thickness" : "player-rotation-thickness";
            }
        }

        static char const* modeBlueInsetKey(GameMode mode, bool isMini) {
            switch (mode) {
            case GameMode::Spider: return isMini ? "spider-mini-blue-inset" : "spider-blue-inset";
            case GameMode::Wave:   return isMini ? "wave-mini-blue-inset" : "wave-blue-inset";
            default:               return isMini ? "standard-mini-blue-inset" : "standard-blue-inset";
            }
        }

        // Same Wave/default split, for the blue collision-box thickness.
        static char const* modeBlueThicknessKey(GameMode mode, bool isMini) {
            switch (mode) {
            case GameMode::Wave: return isMini ? "wave-mini-blue-hitbox-thickness" : "wave-blue-hitbox-thickness";
            default:             return isMini ? "mini-blue-hitbox-thickness" : "blue-hitbox-thickness";
            }
        }

        // Shrinks (or, if negative, grows) a rect symmetrically before
        // hitbox geometry is generated.
        static cocos2d::CCRect insetRect(cocos2d::CCRect rect, float inset) {
            // Clamp so a large positive inset can't flip the rect inside out.
            inset = std::min(inset, std::min(rect.size.width, rect.size.height) / 2.f);
            rect.origin.x += inset;
            rect.origin.y += inset;
            rect.size.width -= inset * 2.f;
            rect.size.height -= inset * 2.f;
            return rect;
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
            // Hacks are stored as IDs in the selected PROFILE array, not booleans.
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

        // Used by shouldShowWhileAlive() below to mirror another mod's
        // own live hitbox display - if QOLMod/MegaHack/Eclipse are
        // already showing their own hitboxes live, this mod's trail
        // shows live too instead of only appearing after death.
        // Deliberately excludes GD's own debug draw (layer->
        // m_isDebugDrawEnabled, checked separately in shouldShowWhileAlive
        // below) - that's not "another mod", so it's left out here.
        bool isOtherModShowHitboxesEnabled() const {
            auto qol = false;
            if (auto qolmod = Loader::get()->getLoadedMod("thesillydoggo.qolmod"))
                qol = qolmod->getSavedValue<bool>("show-hitboxes_enabled", false);

            auto megaHackEnabled = megaHackRuntimeOption(
            "LEVEL/SHOW_HITBOXES/HACK_SHOW_HITBOXES"
            );

            auto eclipseLoaded = Loader::get()->getLoadedMod("eclipse.eclipse-menu") != nullptr;

            if (++m_liveCheckTicks >= 30) {
                m_liveCheckTicks = 0;
                auto modsDir = Mod::get()->getSaveDir().parent_path();
                if (!megaHackEnabled) {
                    auto megaHackPath = modsDir / "absolllute.megahack" / "v9" / "hackpro.json";
                    reloadWhenModified(megaHackPath, m_megaHackLastWriteTime, m_megaHackFileKnown, m_megaHackFileShowHitboxes,
                    "LEVEL/SHOW_HITBOXES/HACK_SHOW_HITBOXES", true);
                }
                if (eclipseLoaded) {
                    auto eclipsePath = modsDir / "eclipse.eclipse-menu" / "config.json";
                    reloadWhenModified(eclipsePath, m_eclipseLastWriteTime, m_eclipseFileKnown, m_eclipseFileShowHitboxes,
                    "level.showhitboxes", false);
                }
            }
            auto megaHackFinal = megaHackEnabled.value_or(m_megaHackFileShowHitboxes);
            auto eclipseFinal = eclipseLoaded && m_eclipseFileShowHitboxes;

            return qol || megaHackFinal || eclipseFinal;
        }

        bool shouldShowWhileAlive(GJBaseGameLayer* layer) const {
            return layer->m_isDebugDrawEnabled || isOtherModShowHitboxesEnabled();
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

        cocos2d::CCPoint rotatePointAround(cocos2d::CCPoint point, cocos2d::CCPoint centre,
        float cosine, float sine) {
            auto x = point.x - centre.x;
            auto y = point.y - centre.y;
            return cocos2d::CCPointMake(
            centre.x + x * cosine - y * sine,
            centre.y + x * sine + y * cosine
            );
        }

        // QOLMod uses this exact path: one polygon with its border aligned
        // inside. Unlike a fill polygon plus four independently blended
        // segments, a boundary pixel is emitted once. When the user hides an
        // outline, its inside border is coloured as the fill solely to cover
        // CCDrawNode's AA fringe without changing the requested opacity.
        void drawHitboxRect(cocos2d::CCRect rect, float thickness,
        cocos2d::ccColor4F fill, cocos2d::ccColor4F outline,
        float fillInset, float rotation = 0.f) {
            if (rect.size.width <= 0.f || rect.size.height <= 0.f) return;
            if (fill.a <= 0.f && outline.a <= 0.f) return;

            thickness = std::clamp(thickness, 0.f,
            std::min(rect.size.width, rect.size.height) / 2.f);
            auto edge = outline.a > 0.f ? outline : fill;
            cocos2d::CCPoint verts[4] = {
                { rect.getMinX(), rect.getMinY() }, { rect.getMaxX(), rect.getMinY() },
                { rect.getMaxX(), rect.getMaxY() }, { rect.getMinX(), rect.getMaxY() },
            };
            if (rotation != 0.f) {
                auto centre = cocos2d::CCPointMake(rect.getMidX(), rect.getMidY());
                auto radians = -rotation * 0.01745329251994329577f;
                auto cosine = std::cos(radians);
                auto sine = std::sin(radians);
                for (auto& point : verts)
                    point = rotatePointAround(point, centre, cosine, sine);
            }

            // With no requested outline, every pixel is written exactly once.
            if (outline.a <= 0.f) {
                drawFlatTriangle(verts[0], verts[1], verts[2], fill);
                drawFlatTriangle(verts[0], verts[2], verts[3], fill);
                return;
            }

            // BorderAlignment::Inside places the whole outline inside the
            // supplied rect. Its anti-aliased edge lands fractionally inside
            // the mathematical edge, so leave a fill clearance proportional
            // to this thickness beyond it (see RenderSettings::fillInset).
            auto interior = insetRect(rect, thickness * (1.f + std::max(fillInset, 0.f)));
            cocos2d::CCPoint interiorVerts[4] = {
                { interior.getMinX(), interior.getMinY() }, { interior.getMaxX(), interior.getMinY() },
                { interior.getMaxX(), interior.getMaxY() }, { interior.getMinX(), interior.getMaxY() },
            };
            if (rotation != 0.f) {
                auto centre = cocos2d::CCPointMake(rect.getMidX(), rect.getMidY());
                auto radians = -rotation * 0.01745329251994329577f;
                auto cosine = std::cos(radians);
                auto sine = std::sin(radians);
                for (auto& point : interiorVerts)
                    point = rotatePointAround(point, centre, cosine, sine);
            }
            drawFlatTriangle(interiorVerts[0], interiorVerts[1], interiorVerts[2], fill);
            drawFlatTriangle(interiorVerts[0], interiorVerts[2], interiorVerts[3], fill);
            cocos2d::ccColor4F transparentFill{ outline.r, outline.g, outline.b, 0.f };
            drawPolygon(verts, 4, transparentFill, thickness, edge, cocos2d::BorderAlignment::Inside);
        }

        void drawHitboxCircle(cocos2d::CCPoint centre, float radius, float thickness,
        cocos2d::ccColor4F fill, cocos2d::ccColor4F outline,
        float fillInset, int segments = 16) {
            if (radius <= 0.f || (fill.a <= 0.f && outline.a <= 0.f)) return;
            thickness = std::clamp(thickness, 0.f, radius);
            auto edge = outline.a > 0.f ? outline : fill;
            segments = std::max(segments, 3);
            constexpr float kTau = 6.28318530717958647692f;
            if (outline.a <= 0.f) {
                // With no outline there is no inner edge to reserve: fill
                // the hitbox radius directly.
                auto fillRadius = radius;
                for (int i = 0; i < segments; ++i) {
                    auto angle0 = kTau * static_cast<float>(i) / segments;
                    auto angle1 = kTau * static_cast<float>(i + 1) / segments;
                    auto point0 = cocos2d::CCPointMake(
                    centre.x + std::cos(angle0) * fillRadius,
                    centre.y + std::sin(angle0) * fillRadius
                    );
                    auto point1 = cocos2d::CCPointMake(
                    centre.x + std::cos(angle1) * fillRadius,
                    centre.y + std::sin(angle1) * fillRadius
                    );
                    drawFlatTriangle(centre, point0, point1, fill);
                }
                return;
            }

            // Keep CCDrawNode's native circle path. Its stroke's radius
            // convention differs from polygon outlines, so the fill radius
            // is computed directly rather than via insetRect - but uses the
            // same thickness-proportional clearance as drawHitboxRect (see
            // RenderSettings::fillInset) so both shapes look consistent.
            auto outlineRadius = std::max(radius - thickness, 0.f);
            // outlineRadius is the native circle outline's inner boundary.
            // Do not apply a circumradius/apothem expansion here: that makes
            // triangle vertices enter the outline band. Fill Inset is the
            // only extra distance between this boundary and the fill.
            auto fillRadius = std::max(outlineRadius - thickness * std::max(fillInset, 0.f), 0.f);
            for (int i = 0; i < segments; ++i) {
                auto angle0 = kTau * static_cast<float>(i) / segments;
                auto angle1 = kTau * static_cast<float>(i + 1) / segments;
                auto point0 = cocos2d::CCPointMake(
                centre.x + std::cos(angle0) * fillRadius,
                centre.y + std::sin(angle0) * fillRadius
                );
                auto point1 = cocos2d::CCPointMake(
                centre.x + std::cos(angle1) * fillRadius,
                centre.y + std::sin(angle1) * fillRadius
                );
                drawFlatTriangle(centre, point0, point1, fill);
            }
            cocos2d::ccColor4F transparentFill{ outline.r, outline.g, outline.b, 0.f };
            drawCircle(centre, outlineRadius, transparentFill, thickness, edge, segments);
        }

        // CCDrawNode exposes the underlying triangle buffer to subclasses,
        // although GD 2.2081 does not expose a drawTriangle helper. This is
        // the same buffer format used by CCDrawNode's own draw primitives.
        void drawFlatTriangle(cocos2d::CCPoint const& a, cocos2d::CCPoint const& b,
        cocos2d::CCPoint const& c, cocos2d::ccColor4F color) {
            if (color.a <= 0.f) return;
            constexpr unsigned int kVertexCount = 3;
            if (m_nBufferCount + kVertexCount > m_uBufferCapacity) {
                m_uBufferCapacity += std::max(m_uBufferCapacity, kVertexCount);
                m_pBuffer = static_cast<cocos2d::ccV2F_C4B_T2F*>(
                std::realloc(m_pBuffer, m_uBufferCapacity * sizeof(cocos2d::ccV2F_C4B_T2F))
                );
            }

            auto vertexColor = cocos2d::ccc4BFromccc4F(color);
            auto makeVertex = [&](cocos2d::CCPoint point) {
                return cocos2d::ccV2F_C4B_T2F{ { point.x, point.y }, vertexColor, { 0.f, 0.f } };
            };
            auto triangles = reinterpret_cast<cocos2d::ccV2F_C4B_T2F_Triangle*>(m_pBuffer + m_nBufferCount);
            triangles[0] = { makeVertex(a), makeVertex(b), makeVertex(c) };
            m_nBufferCount += kVertexCount;
            m_bDirty = true;
        }

        // deque: removing the oldest state is O(1), unlike a vector.
        std::deque<TrailState> m_states;
        std::deque<TrailState> m_states2;
        cocos2d::CCNode* m_world1 = nullptr;
        cocos2d::CCNode* m_world2 = nullptr;
        mutable size_t m_liveCheckTicks = 30;
        mutable bool m_megaHackFileKnown = false;
        mutable bool m_megaHackFileShowHitboxes = false;
        mutable bool m_eclipseFileKnown = false;
        mutable bool m_eclipseFileShowHitboxes = false;
        mutable std::filesystem::file_time_type m_megaHackLastWriteTime {};
        mutable std::filesystem::file_time_type m_eclipseLastWriteTime {};
        bool m_wasDead = false;
        bool m_recordedDeathFrame = false;
        bool m_usesGameplayTransform = false;
        bool m_competingTrailsInitialised = false;
        int m_disableRetryFrames = 0;
        // Jump-held state from the previous capturePlayer() call, per
        // player - diffed each tick in capturePlayer().
        bool m_lastHeldP1 = false;
        bool m_lastHeldP2 = false;
        // See the Only Player/Only Cube + Only Click and Release flash
        // logic in capturePlayer().
        TrailState::Click m_flashClickP1 = TrailState::Click::None;
        TrailState::Click m_flashClickP2 = TrailState::Click::None;
        int m_flashTicksP1 = 0;
        int m_flashTicksP2 = 0;
        // See consumeCaptureTick()/setLastTickDt().
        float m_lastTickDt = 1.f / 240.f;
        float m_captureAccumulator = 0.f;

        inline static bool s_eclipseTrailFileKnown = false;
        inline static bool s_eclipseTrailFileToggle = false;
        inline static std::filesystem::file_time_type s_eclipseTrailLastWrite{};
        inline static size_t s_eclipseTrailCheckTicks = 15;
        inline static bool s_eclipseTrailConfirmedOff = false;
    };
}

class $modify(ClickTrailGameLayer, GJBaseGameLayer) {
    // Matches QOLMod's tick boundary: checkRepellPlayer runs at tick end,
    // then updateCamera is the safe point to snapshot player state.
    struct Fields {
        ClickTrailNode* trail = nullptr;
        bool reachedTickEnd = false;
    };

    bool init() {
        if (!GJBaseGameLayer::init()) return false;
        m_fields->trail = ClickTrailNode::create();
        // Fallback until m_debugDrawNode exists; attachTrail moves it into
        // the gameplay hierarchy on the first physics tick.
        m_fields->trail->setZOrder(m_uiLayer->getZOrder() - 1);
        insertBefore(m_fields->trail, m_uiLayer);
        attachTrail();
        return true;
    }

    // Captures here too (not just the once-per-tick capture() below) so
    // the click/release marker lands on the exact position/timing GD
    // applies the input at, rather than waiting for the next tick.
    void handleButton(bool down, int button, bool isPlayer1) {
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
        if (m_fields->trail && button == static_cast<int>(PlayerButton::Jump))
            m_fields->trail->captureButtonEdge(isPlayer1, m_player1, m_player2);
    }

    // Feeds ClickTrailNode::consumeCaptureTick() a real per-tick dt so
    // Trail Capture Rate reflects actual elapsed tick time rather than a
    // fixed guess - matters if the physics tick rate itself changes
    // (TPS-changing mods, time warp portals, etc).
    float getModifiedDelta(float dt) {
        auto modified = GJBaseGameLayer::getModifiedDelta(dt);
        if (m_fields->trail)
            m_fields->trail->setLastTickDt(modified);
        return modified;
    }

    void checkRepellPlayer() {
        GJBaseGameLayer::checkRepellPlayer();
        attachTrail();

        // A dead player can be displaced by its death animation during
        // updateCamera - record right after collision detection instead.
        if (m_fields->trail &&
        ((m_player1 && m_player1->m_isDead) || (m_player2 && m_player2->m_isDead))) {
            m_fields->reachedTickEnd = false;
            m_fields->trail->capture(this);
            return;
        }
        m_fields->reachedTickEnd = true;
    }

    void updateCamera(float dt) {
        GJBaseGameLayer::updateCamera(dt);
        if (m_fields->reachedTickEnd && m_fields->trail) {
            m_fields->reachedTickEnd = false;
            m_fields->trail->capture(this);
        }
    }

    void resetLevelVariables() {
        GJBaseGameLayer::resetLevelVariables();

        if (!typeinfo_cast<LevelEditorLayer*>(this)) {
            if (m_fields->trail) {
                m_fields->trail->resetTrails();
            }
        }
    }

private:
    void attachTrail() {
        if (!m_fields->trail || !m_debugDrawNode)
            return;

        auto gameplayParent = m_debugDrawNode->getParent();
        if (!gameplayParent || m_fields->trail->getParent() == gameplayParent)
            return;

        // Keep a temporary reference across the move so the autoreleased
        // trail isn't deleted before addChild receives it.
        m_fields->trail->retain();
        m_fields->trail->removeFromParentAndCleanup(false);
        // Parenting under the camera-transformed gameplay container keeps
        // the trail outside all UI subtrees.
        gameplayParent->addChild(m_fields->trail, m_debugDrawNode->getZOrder() - 1);
        m_fields->trail->release();
        m_fields->trail->setUsesGameplayTransform(true);
    }

};

// Resets the trail on editor playtest. LevelEditorLayer extends
// GJBaseGameLayer so it shares ClickTrailGameLayer's m_fields, but
// attachTrail() reparents the trail node away from being a direct child
// of this - base_cast still finds the right trail regardless.
class $modify(ClickTrailEditorLayer, LevelEditorLayer) {
    void onPlaytest() {
        LevelEditorLayer::onPlaytest();

        if (auto trail = base_cast<ClickTrailGameLayer*>(this)->m_fields->trail) {
            trail->resetTrails();
        }
    }
};

static bool s_settingsOpen = false;

$on_mod(Loaded) {
    ClickTrailNode::healThicknessSettings();

    ImGuiCocos::get()
    .setup([] {})
    .draw([] {
        static bool s_lastSettingsOpen = false;
        bool justOpened = s_settingsOpen && !s_lastSettingsOpen;
        if (s_settingsOpen != s_lastSettingsOpen) {
            if (s_settingsOpen) {
                PlatformToolbox::showCursor();
            } else {
                // Hiding it back is only correct while actually in
                // gameplay - not paused (PauseLayer needs the cursor
                // for its own buttons), and not the main menu/editor,
                // where GD already expects a visible cursor on its own.
                auto scene = cocos2d::CCDirector::sharedDirector()->getRunningScene();
                auto paused = scene && scene->getChildByType<PauseLayer>(0);
                if (PlayLayer::get() && !paused)
                    PlatformToolbox::hideCursor();
            }
            s_lastSettingsOpen = s_settingsOpen;
        }

        if (!s_settingsOpen) return;

        auto mod = Mod::get();

        // drawTrail() rebuilds full geometry every frame regardless of
        // changes (tanked FPS from ~2200-2500 to ~280-300 with the window
        // open) - only refresh when a control below was actually edited.
        bool changed = justOpened;

        ImGui::SetNextWindowSize(ImVec2(340.f, 0.f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Click Hitbox Trail", &s_settingsOpen)) {
            ImGui::End();
            return;
        }

        // Fixed width so stretching the window doesn't just grow the
        // input boxes while leaving their labels fixed-width.
        ImGui::PushItemWidth(110.f);

        bool trailEnabled = mod->getSavedValue<bool>("hitbox-trail-enabled", true);
        if (ImGui::Checkbox("Hitbox Trail", &trailEnabled)) {
            mod->setSavedValue<bool>("hitbox-trail-enabled", trailEnabled);
            changed = true;
        }

        bool onDeath = mod->getSavedValue<bool>("hitbox-trail-on-death", true);
        if (ImGui::Checkbox("Show on Death", &onDeath)) {
            mod->setSavedValue<bool>("hitbox-trail-on-death", onDeath);
            changed = true;
        }

        bool alwaysShow = mod->getSavedValue<bool>("always-show-hitbox-trail", false);
        if (ImGui::Checkbox("Always Show", &alwaysShow)) {
            mod->setSavedValue<bool>("always-show-hitbox-trail", alwaysShow);
            changed = true;
        }

        ImGui::Separator();

        bool onlyPlayer = mod->getSavedValue<bool>("only-player-enabled", false);
        if (ImGui::Checkbox("Only Player", &onlyPlayer)) {
            mod->setSavedValue<bool>("only-player-enabled", onlyPlayer);
            if (onlyPlayer)
                mod->setSavedValue<bool>("cube-hitbox-enabled", false);
            changed = true;
        }

        bool cubeHitbox = mod->getSavedValue<bool>("cube-hitbox-enabled", false);
        if (ImGui::Checkbox("Only Cube", &cubeHitbox)) {
            mod->setSavedValue<bool>("cube-hitbox-enabled", cubeHitbox);
            if (cubeHitbox)
                mod->setSavedValue<bool>("only-player-enabled", false);
            changed = true;
        }

        // Not a mod.json setting - just forces consumeCaptureTick() to act
        // like Trail Capture Rate is 0 without overwriting that setting,
        // see isOnlyClickReleaseEnabled().
        bool onlyClickRelease = mod->getSavedValue<bool>("only-click-release-enabled", false);
        if (ImGui::Checkbox("Only Click and Release", &onlyClickRelease)) {
            mod->setSavedValue<bool>("only-click-release-enabled", onlyClickRelease);
            changed = true;
        }

        ImGui::Separator();

        // Not mod.json settings - getSavedValue reads/writes save data
        // directly without a matching json key.
        bool squareHitbox = mod->getSavedValue<bool>("square-hitbox-enabled", true);
        if (ImGui::Checkbox("Square Hitbox", &squareHitbox)) {
            mod->setSavedValue<bool>("square-hitbox-enabled", squareHitbox);
            changed = true;
        }

        bool blueHitbox = mod->getSavedValue<bool>("blue-hitbox-enabled", true);
        if (ImGui::Checkbox("Blue Hitbox", &blueHitbox)) {
            mod->setSavedValue<bool>("blue-hitbox-enabled", blueHitbox);
            changed = true;
        }

        bool circleHitbox = mod->getSavedValue<bool>("circle-hitbox-enabled", true);
        if (ImGui::Checkbox("Circle Hitbox", &circleHitbox)) {
            mod->setSavedValue<bool>("circle-hitbox-enabled", circleHitbox);
            changed = true;
        }

        bool playerRotation = mod->getSavedValue<bool>("player-rotation", true);
        if (ImGui::Checkbox("Rotation Hitbox", &playerRotation)) {
            mod->setSavedValue<bool>("player-rotation", playerRotation);
            changed = true;
        }

        ImGui::Separator();

        auto storedPositions = static_cast<int>(mod->getSettingValue<int64_t>("stored-positions"));
        // Plain typed input instead of drag/slider - clamp manually
        // afterward since InputInt/InputFloat don't take a min/max
        // themselves (unlike Slider/DragInt/DragFloat, which did).
        if (ImGui::InputInt("Length", &storedPositions, 1, 10)) {
            mod->setSettingValue<int64_t>("stored-positions", static_cast<int64_t>(std::clamp(storedPositions, 1, 1024)));
            changed = true;
        }

        auto opacityPct = static_cast<float>(mod->getSettingValue<double>("opacity")) * 100.f;
        if (ImGui::InputFloat("Opacity", &opacityPct, 1.f, 10.f, "%.0f%%")) {
            mod->setSettingValue<double>("opacity", static_cast<double>(std::clamp(opacityPct, 0.f, 100.f)) / 100.0);
            changed = true;
        }

        // Same master-multiplier pattern as Opacity above - see
        // drawMainHitbox.
        auto thicknessPct = static_cast<float>(mod->getSettingValue<double>("thickness")) * 100.f;
        if (ImGui::InputFloat("Thickness", &thicknessPct, 1.f, 10.f, "%.0f%%")) {
            mod->setSettingValue<double>("thickness", static_cast<double>(std::clamp(thicknessPct, 1.f, 200.f)) / 100.0);
            changed = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("min1%%/max200%%");

        ImGui::Separator();

        // Not mod.json settings - getSavedValue reads/writes save data,
        // same as the other popup-only toggles above.
        bool colorClicks = mod->getSavedValue<bool>("color-clicks", true);
        if (ImGui::Checkbox("Color Clicks", &colorClicks)) {
            mod->setSavedValue<bool>("color-clicks", colorClicks);
            changed = true;
        }

        bool colorWhenHeld = mod->getSavedValue<bool>("color-when-held", true);
        if (ImGui::Checkbox("Color When Held", &colorWhenHeld)) {
            mod->setSavedValue<bool>("color-when-held", colorWhenHeld);
            changed = true;
        }

        bool darkenWithAge = mod->getSavedValue<bool>("darken-by-age", false);
        if (ImGui::Checkbox("Darken with Age", &darkenWithAge)) {
            mod->setSavedValue<bool>("darken-by-age", darkenWithAge);
            changed = true;
        }

        bool fadeWithAge = mod->getSavedValue<bool>("fade-with-age", false);
        if (ImGui::Checkbox("Fade with Age", &fadeWithAge)) {
            mod->setSavedValue<bool>("fade-with-age", fadeWithAge);
            changed = true;
        }

        ImGui::Separator();

        bool fillHitbox = mod->getSavedValue<bool>("fill-hitbox", false);
        if (ImGui::Checkbox("Fill Hitbox", &fillHitbox)) {
            mod->setSavedValue<bool>("fill-hitbox", fillHitbox);
            changed = true;
        }
        ImGui::SameLine();
        // "##fill" keeps this InputFloat's id distinct from the main
        // Opacity control above - ImGui derives widget ids from the
        // label text, so two visible "Opacity" labels would otherwise
        // collide.
        auto fillOpacityPct = static_cast<float>(mod->getSavedValue<double>("fill-opacity", 0.35)) * 100.f;
        if (ImGui::InputFloat("Opacity##fill", &fillOpacityPct, 1.f, 10.f, "%.0f%%")) {
            mod->setSavedValue<double>("fill-opacity", static_cast<double>(std::clamp(fillOpacityPct, 0.f, 100.f)) / 100.0);
            changed = true;
        }

        if (changed) {
            if (auto layer = GJBaseGameLayer::get()) {
                if (auto trail = base_cast<ClickTrailGameLayer*>(layer)->m_fields->trail) {
                    trail->refreshDrawing(layer);
                }
            }
        }

        ImGui::PopItemWidth();
        ImGui::End();
    });

    // Geode handles rebinding and conflict management for this setting. The
    // regular ModSettingsPopup works in both gameplay and the editor.
    static auto s_openSettingsListener = listenForKeybindSettingPresses("open-settings", [](
    Keybind const&, bool down, bool repeat, double
    ) -> bool {
        if (down && !repeat) {
            s_settingsOpen = !s_settingsOpen;
            // Do not pass F7 to the active gameplay layer. Passing it on
            // can make GD restore its hidden gameplay cursor after the
            // window has already made the cursor visible.
            return true;
        }
        return false;
    });
}
