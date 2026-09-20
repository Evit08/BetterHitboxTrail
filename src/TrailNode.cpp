#include "TrailNode.hpp"
#include "ModDetection.hpp"
#include <utility>
#include <deque>
#include <vector>
#include <array>
#include <optional>
#include <algorithm>
#include <cmath>
#include <cstdlib>

using namespace geode::prelude;

namespace hitboxtrail
{
    class TrailNodeImpl : public TrailNode
    {
    public:
        struct CaptureSettings
        {
            bool trailEnabled;
            bool onlyClickRelease;
            bool onlyPlayer;
            bool onlyCube;
            bool forceSingle;
            size_t length;
            double captureRate;
        };

        static CaptureSettings makeCaptureSettings()
        {
            auto mod = Mod::get();
            CaptureSettings s;
            s.trailEnabled = mod->getSavedValue<bool>("hitbox-trail-enabled", true);
            s.onlyClickRelease = mod->getSavedValue<bool>("only-click-release-enabled", false);
            s.onlyPlayer = mod->getSavedValue<bool>("only-player-enabled", false);
            s.onlyCube = mod->getSavedValue<bool>("cube-hitbox-enabled", false);
            s.forceSingle = s.onlyPlayer || s.onlyCube;
            s.length = static_cast<size_t>(mod->getSettingValue<int64_t>("trail-length"));
            s.captureRate = mod->getSettingValue<double>("trail-capture-rate");
            return s;
        }
        static TrailNode *createImpl()
        {
            auto node = new TrailNodeImpl;
            if (node && node->init())
            {
                node->autorelease();
                return node;
            }
            CC_SAFE_DELETE(node);
            return nullptr;
        }

        static void healThicknessSettings()
        {
            auto mod = Mod::get();
            auto heal = [mod](char const *key, double lo, double hi)
            {
                auto value = mod->getSettingValue<double>(key);
                auto clamped = std::clamp(value, lo, hi);
                if (clamped != value)
                    mod->setSettingValue<double>(key, clamped);
            };
            heal("thickness", 0.01, 2.0);
            for (auto mode : {GameMode::Cube, GameMode::Wave})
            {
                for (auto isMini : {false, true})
                {
                    heal(modeThicknessKey(ThicknessKind::Main, mode, isMini), 0.01, 10.0);
                    heal(modeThicknessKey(ThicknessKind::Blue, mode, isMini), 0.01, 10.0);
                    heal(modeThicknessKey(ThicknessKind::Circle, mode, isMini), 0.01, 10.0);
                    heal(modeThicknessKey(ThicknessKind::Rotation, mode, isMini), 0.01, 10.0);
                }
            }
        }

        bool init() override
        {
            if (!CCDrawNode::init())
                return false;
            setBlendFunc({GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA});
            m_world1 = cocos2d::CCNode::create();
            m_world2 = cocos2d::CCNode::create();
            m_world1->retain();
            m_world2->retain();
            m_world1->addChild(m_world2);
            return true;
        }

        void setGameplayXform(bool value) override
        {
            m_gameplayXform = value;
        }

        ~TrailNodeImpl() override
        {
            CC_SAFE_RELEASE(m_world1);
            CC_SAFE_RELEASE(m_world2);
        }

        void resetTrails() override
        {
            m_states.clear();
            m_states2.clear();
            m_wasDead = false;
            m_recordedDeathFrame = false;
            m_megaHackIgnoreFrames = 1;
            m_lastHeldP1 = false;
            m_lastHeldP2 = false;
            m_flashClickP1 = TrailState::Click::None;
            m_flashClickP2 = TrailState::Click::None;
            m_flashTicksP1 = 0;
            m_flashTicksP2 = 0;
            m_captureAccumulator = 0.f;
            clear();
        }

        void setLastTickDt(float dt) override
        {
            m_lastTickDt = dt;
        }

        bool consumeCaptureTick(CaptureSettings const &settings)
        {
            if (settings.onlyClickRelease)
                return settings.forceSingle;
            if (settings.captureRate <= 0.0)
                return false;
            auto targetInterval = static_cast<float>(1.0 / settings.captureRate);
            m_captureAccumulator += m_lastTickDt;
            if (m_captureAccumulator + 0.00001f < targetInterval)
                return false;
            m_captureAccumulator = std::fmod(m_captureAccumulator, targetInterval);
            return true;
        }

        void capture(GJBaseGameLayer *layer) override
        {
            auto captureSettings = makeCaptureSettings();
            if (!captureSettings.trailEnabled)
            {
                clear();
                return;
            }

            auto p1Dead = layer->m_player1 && layer->m_player1->m_isDead;
            auto p2Dead = layer->m_player2 && layer->m_player2->m_isDead;
            if (p1Dead || p2Dead)
            {
                if (!m_recordedDeathFrame)
                {
                    capturePlayer(layer->m_player1, m_states, captureSettings, false, true);
                    if (layer->m_player2 && layer->m_player2->isRunning())
                        capturePlayer(layer->m_player2, m_states2, captureSettings, true, true);
                    m_recordedDeathFrame = true;
                    if (Mod::get()->getSavedValue<bool>("hitbox-trail-on-death", true))
                        drawTrail(layer, captureSettings.trailEnabled);
                    else
                        clear();
                }
                m_wasDead = true;
                return;
            }

            m_wasDead = false;
            m_recordedDeathFrame = false;
            clear();

            if (m_megaHackIgnoreFrames > 0)
            {
                --m_megaHackIgnoreFrames;
                if (m_megaHackIgnoreFrames == 0)
                    ModDetection::refresh();
            }

            auto showLiveTrail = Mod::get()->getSavedValue<bool>("always-show-hitbox-trail", false) || shouldShowWhileAlive(layer);
            auto sampleThisTick = consumeCaptureTick(captureSettings);
            capturePlayer(layer->m_player1, m_states, captureSettings, false, false, sampleThisTick);
            if (layer->m_player2 && layer->m_player2->isRunning())
                capturePlayer(layer->m_player2, m_states2, captureSettings, true, false, sampleThisTick);

            if (showLiveTrail)
                drawTrail(layer, captureSettings.trailEnabled);
        }

        void refreshDrawing(GJBaseGameLayer *layer) override
        {
            auto captureSettings = makeCaptureSettings();
            if (!captureSettings.trailEnabled)
            {
                clear();
                return;
            }
            trimToMax(m_states, captureSettings);
            trimToMax(m_states2, captureSettings);
            if (m_wasDead)
            {
                if (Mod::get()->getSavedValue<bool>("hitbox-trail-on-death", true))
                    drawTrail(layer, captureSettings.trailEnabled);
                else
                    clear();
                return;
            }
            auto showLiveTrail = Mod::get()->getSavedValue<bool>("always-show-hitbox-trail", false) || (layer && shouldShowWhileAlive(layer));
            if (showLiveTrail)
                drawTrail(layer, captureSettings.trailEnabled);
            else
                clear();
        }

        void invalidateRenderSettings() override
        {
            m_cachedSettings.reset();
        }

        void trimToMax(std::deque<TrailState> &states, CaptureSettings const &settings)
        {
            while (states.size() > settings.length)
                states.pop_front();
        }

        void capturePlayer(PlayerObject *player, std::deque<TrailState> &states, CaptureSettings const &settings,
                           bool isPlayer2 = false, bool allowDead = false,
                           bool sampleThisTick = true)
        {
            if (!player || (player->m_isDead && !allowDead))
                return;

            auto &lastHeld = isPlayer2 ? m_lastHeldP2 : m_lastHeldP1;
            bool held = player->m_holdingButtons[static_cast<int>(PlayerButton::Jump)];
            bool changed = held != lastHeld;
            TrailState::Click click = TrailState::Click::None;
            if (changed)
                click = held ? TrailState::Click::Press : TrailState::Click::Release;
            else if (held)
                click = TrailState::Click::Hold;
            lastHeld = held;

            if (settings.onlyClickRelease && settings.forceSingle)
            {
                auto &flashClick = isPlayer2 ? m_flashClickP2 : m_flashClickP1;
                auto &flashTicks = isPlayer2 ? m_flashTicksP2 : m_flashTicksP1;
                if (changed)
                {
                    flashClick = click;
                    flashTicks = kFlashTicks;
                }
                if (flashTicks > 0)
                {
                    click = flashClick;
                    --flashTicks;
                }
                else
                {
                    click = TrailState::Click::None;
                }
            }

            if (!sampleThisTick && !changed)
                return;

            states.push_back({player->getObjectRect(player->m_vehicleSize, player->m_vehicleSize),
                              player->getObjectRect(0.25f, 0.25f),
                              player->getRotation(),
                              click,
                              currentGameMode(player),
                              player->m_vehicleSize});
            trimToMax(states, settings);
        }

        void captureButtonEdge(bool isPlayer1, PlayerObject *player1, PlayerObject *player2) override
        {
            auto settings = makeCaptureSettings();
            if (!settings.trailEnabled)
                return;
            if (isPlayer1)
                capturePlayer(player1, m_states, settings, false);
            else
                capturePlayer(player2, m_states2, settings, true);
        }

    protected:
        void visit() override
        {
            if (m_gameplayXform)
            {
                CCDrawNode::visit();
                return;
            }

            auto layer = GJBaseGameLayer::get();
            auto parent = layer && layer->m_debugDrawNode ? layer->m_debugDrawNode->getParent() : nullptr;
            if (!parent)
                return;

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
        void drawTrail(GJBaseGameLayer *, bool trailEnabled)
        {
            clear();
            if (!trailEnabled)
                return;
            drawTrailStates(m_states, cachedRenderSettings());
            drawTrailStates(m_states2, cachedRenderSettings());
        }

        struct RenderSettings
        {
            bool squareEnabled, blueEnabled, circleEnabled, rotationEnabled, onlyCube, onlyPlayer, forceSingle;
            bool colorClicks, colorWhenHeld, darkenWithAge, fadeWithAge;
            bool blueColorClicks, circleColorClicks, rotationColorClicks;
            bool fillEnabled;
            bool batchLayerOverlap, onlyClickRelease;
            float opacity, masterThickness, squareOpacity, blueOpacity, circleOpacity, rotationOpacity;
            float fillOpacity;
            cocos2d::ccColor4F mainColor, blueColor, circleColor, rotationColor;
            cocos2d::ccColor4F pressColor, releaseColor, holdColor;
            std::array<float, 3> blueInset;
            std::array<std::array<float, 2>, 2> mainThickness, blueThickness, circleThickness, rotationThickness;
        };

        static size_t insetModeIndex(GameMode mode)
        {
            return mode == GameMode::Spider ? 1 : mode == GameMode::Wave ? 2
                                                                         : 0;
        }

        static size_t thicknessModeIndex(GameMode mode)
        {
            return mode == GameMode::Wave ? 1 : 0;
        }

        static RenderSettings makeRenderSettings()
        {
            auto mod = Mod::get();
            RenderSettings s{
                .squareEnabled = mod->getSavedValue<bool>("square-hitbox-enabled", true),
                .blueEnabled = mod->getSavedValue<bool>("blue-hitbox-enabled", true),
                .circleEnabled = mod->getSavedValue<bool>("circle-hitbox-enabled", true),
                .rotationEnabled = mod->getSavedValue<bool>("player-rotation", true),
                .onlyCube = mod->getSavedValue<bool>("cube-hitbox-enabled", false),
                .onlyPlayer = mod->getSavedValue<bool>("only-player-enabled", false),
                .colorClicks = mod->getSavedValue<bool>("color-clicks", true),
                .colorWhenHeld = mod->getSavedValue<bool>("color-when-held", true),
                .darkenWithAge = mod->getSavedValue<bool>("darken-by-age", false),
                .fadeWithAge = mod->getSavedValue<bool>("fade-with-age", false),
                .blueColorClicks = mod->getSettingValue<bool>("blue-hitbox-color-clicks"),
                .circleColorClicks = mod->getSettingValue<bool>("circle-hitbox-color-clicks"),
                .rotationColorClicks = mod->getSettingValue<bool>("rotation-color-clicks"),
                .fillEnabled = mod->getSavedValue<bool>("fill-hitbox", false),
                .batchLayerOverlap = mod->getSettingValue<bool>("batch-layer-overlap"),
                .onlyClickRelease = mod->getSavedValue<bool>("only-click-release-enabled", false),
                .opacity = static_cast<float>(mod->getSettingValue<double>("opacity")),
                .masterThickness = std::clamp(static_cast<float>(mod->getSettingValue<double>("thickness")), 0.01f, 2.f),
                .squareOpacity = static_cast<float>(mod->getSettingValue<double>("square-hitbox-opacity")),
                .blueOpacity = static_cast<float>(mod->getSettingValue<double>("blue-hitbox-opacity")),
                .circleOpacity = static_cast<float>(mod->getSettingValue<double>("circle-hitbox-opacity")),
                .rotationOpacity = static_cast<float>(mod->getSettingValue<double>("player-rotation-opacity")),
                .fillOpacity = static_cast<float>(mod->getSavedValue<double>("fill-opacity", 0.25)),
                .mainColor = asFloatColor(mod->getSettingValue<cocos2d::ccColor3B>("square-hitbox-color")),
                .blueColor = asFloatColor(mod->getSettingValue<cocos2d::ccColor3B>("blue-hitbox-color")),
                .circleColor = asFloatColor(mod->getSettingValue<cocos2d::ccColor3B>("circle-hitbox-color")),
                .rotationColor = asFloatColor(mod->getSettingValue<cocos2d::ccColor3B>("player-rotation-color")),
                .pressColor = asFloatColor(mod->getSettingValue<cocos2d::ccColor3B>("click-color")),
                .releaseColor = asFloatColor(mod->getSettingValue<cocos2d::ccColor3B>("release-color")),
                .holdColor = asFloatColor(mod->getSettingValue<cocos2d::ccColor3B>("hold-color")),
            };
            s.forceSingle = s.onlyPlayer || s.onlyCube;
            constexpr std::array modes{GameMode::Cube, GameMode::Spider, GameMode::Wave};
            for (size_t mode = 0; mode < modes.size(); ++mode)
                s.blueInset[mode] = modeBlueInsetValue(modes[mode]);
            constexpr std::array thicknessModes{GameMode::Cube, GameMode::Wave};
            for (size_t mode = 0; mode < thicknessModes.size(); ++mode)
                for (size_t mini = 0; mini < 2; ++mini)
                {
                    auto isMini = mini != 0;
                    s.mainThickness[mode][mini] = std::clamp(static_cast<float>(mod->getSettingValue<double>(modeThicknessKey(ThicknessKind::Main, thicknessModes[mode], isMini))), 0.01f, 10.f);
                    s.blueThickness[mode][mini] = std::clamp(static_cast<float>(mod->getSettingValue<double>(modeThicknessKey(ThicknessKind::Blue, thicknessModes[mode], isMini))), 0.01f, 10.f);
                    s.circleThickness[mode][mini] = std::clamp(static_cast<float>(mod->getSettingValue<double>(modeThicknessKey(ThicknessKind::Circle, thicknessModes[mode], isMini))), 0.01f, 10.f);
                    s.rotationThickness[mode][mini] = std::clamp(static_cast<float>(mod->getSettingValue<double>(modeThicknessKey(ThicknessKind::Rotation, thicknessModes[mode], isMini))), 0.01f, 10.f);
                }
            return s;
        }

        RenderSettings const &cachedRenderSettings()
        {
            if (!m_cachedSettings)
                m_cachedSettings = makeRenderSettings();
            return *m_cachedSettings;
        }

        struct BatchItem
        {
            TrailState state;
            float age;
        };

        void drawTrailStates(std::deque<TrailState> const &states, RenderSettings const &settings)
        {
            auto onlyClickRelease = settings.onlyClickRelease;
            auto skip = [&](TrailState const &state)
            {
                return onlyClickRelease && state.click != TrailState::Click::Press && state.click != TrailState::Click::Release;
            };

            if (settings.forceSingle)
            {
                auto window = static_cast<size_t>(kFlashTicks);
                auto begin = states.size() > window ? states.size() - window : size_t{0};
                for (size_t idx = states.size(); idx-- > begin;)
                {
                    auto const &state = states[idx];
                    if (skip(state))
                        continue;
                    drawRotationHitbox(state, 1.f, settings);
                    drawCircleHitbox(state, 1.f, settings);
                    drawMainHitbox(state, 1.f, settings);
                    drawBlueHitbox(state, 1.f, settings);
                    break;
                }
                return;
            }

            auto total = static_cast<float>(states.size());
            auto ageFor = [&](size_t idx)
            {
                return total > 1.f ? static_cast<float>(idx) / (total - 1.f) : 1.f;
            };

            if (settings.batchLayerOverlap)
            {
                m_batchScratch.clear();
                m_batchScratch.reserve(states.size());
                for (size_t idx = 0; idx < states.size(); ++idx)
                {
                    auto const &state = states[idx];
                    if (skip(state))
                        continue;
                    m_batchScratch.push_back({state, ageFor(idx)});
                }
                for (auto const &item : m_batchScratch)
                    drawRotationHitbox(item.state, item.age, settings);
                for (auto const &item : m_batchScratch)
                    drawMainHitbox(item.state, item.age, settings);
                for (auto const &item : m_batchScratch)
                    drawCircleHitbox(item.state, item.age, settings);
                for (auto const &item : m_batchScratch)
                    drawBlueHitbox(item.state, item.age, settings);
            }
            else
            {
                for (size_t idx = 0; idx < states.size(); ++idx)
                {
                    auto const &state = states[idx];
                    if (skip(state))
                        continue;
                    auto age = ageFor(idx);
                    drawRotationHitbox(state, age, settings);
                    drawCircleHitbox(state, age, settings);
                    drawMainHitbox(state, age, settings);
                    drawBlueHitbox(state, age, settings);
                }
            }
        }

        void drawMainHitbox(TrailState const &state, float age, RenderSettings const &s)
        {
            if (!s.squareEnabled || s.onlyCube)
                return;
            auto isMini = state.vehicleSize < 0.99f;
            auto rect = state.rect;
            auto thickness = s.masterThickness * s.mainThickness[thicknessModeIndex(state.mode)][isMini];
            auto color = trailColor(state, age, s.mainColor, s.colorClicks, true, s);
            auto outlineAlpha = s.opacity * s.squareOpacity * ageOpacity(age, s);
            auto fill = color;
            fill.a = s.fillEnabled ? s.fillOpacity * ageOpacity(age, s) : 0.f;
            color.a = outlineAlpha;
            drawHitboxRect(rect, thickness, fill, color);
        }

        void drawBlueHitbox(TrailState const &state, float age, RenderSettings const &s)
        {
            if (!s.blueEnabled || s.onlyCube)
                return;
            auto isMini = state.vehicleSize < 0.99f;
            auto rect = insetRect(state.miniRect, s.blueInset[insetModeIndex(state.mode)]);
            auto thickness = s.masterThickness * s.blueThickness[thicknessModeIndex(state.mode)][isMini];
            auto blue = trailColor(state, age, s.blueColor,
                                   s.colorClicks && s.blueColorClicks, true, s);
            auto outlineAlpha = s.opacity * s.blueOpacity * ageOpacity(age, s);
            auto fill = blue;
            fill.a = s.fillEnabled ? s.fillOpacity * ageOpacity(age, s) : 0.f;
            blue.a = outlineAlpha;
            drawHitboxRect(rect, thickness, fill, blue);
        }

        void drawCircleHitbox(TrailState const &state, float age, RenderSettings const &s)
        {
            if (!s.circleEnabled || s.onlyCube)
                return;
            auto isMini = state.vehicleSize < 0.99f;
            auto circleColor = trailColor(state, age, s.circleColor,
                                          s.colorClicks && s.circleColorClicks, true, s);
            auto circleRect = state.rect;
            auto circleCentre = cocos2d::CCPointMake(circleRect.getMidX(), circleRect.getMidY());
            auto circleRadius = std::min(circleRect.size.width, circleRect.size.height) / 2.f;
            auto circleThickness = s.masterThickness * s.circleThickness[thicknessModeIndex(state.mode)][isMini];
            auto outlineAlpha = s.opacity * s.circleOpacity * ageOpacity(age, s);
            auto fill = circleColor;
            fill.a = s.fillEnabled ? s.fillOpacity * ageOpacity(age, s) : 0.f;
            circleColor.a = outlineAlpha;
            drawHitboxCircle(circleCentre, circleRadius, circleThickness, fill, circleColor, 24);
        }

        void drawRotationHitbox(TrailState const &state, float age, RenderSettings const &s)
        {
            auto shouldDraw = s.rotationEnabled || s.onlyCube;
            if (!shouldDraw)
                return;
            auto isMini = state.vehicleSize < 0.99f;
            auto thickness = s.masterThickness * s.rotationThickness[thicknessModeIndex(state.mode)][isMini];
            auto rotationColor = trailColor(state, age, s.rotationColor,
                                            s.colorClicks && s.rotationColorClicks, true, s);
            auto outlineAlpha = s.opacity * s.rotationOpacity * ageOpacity(age, s);
            auto fill = rotationColor;
            fill.a = s.fillEnabled ? s.fillOpacity * ageOpacity(age, s) : 0.f;
            rotationColor.a = outlineAlpha;
            drawHitboxRect(state.rect, thickness, fill, rotationColor, state.rotation);
        }

        static cocos2d::ccColor4F trailColor(TrailState const &state, float age,
                                             cocos2d::ccColor4F color, bool colorClicks,
                                             bool applyDarken, RenderSettings const &s)
        {
            if (colorClicks)
            {
                switch (state.click)
                {
                case TrailState::Click::Press:
                    color = s.pressColor;
                    break;
                case TrailState::Click::Release:
                    color = s.releaseColor;
                    break;
                case TrailState::Click::Hold:
                    if (s.colorWhenHeld)
                        color = s.holdColor;
                    break;
                default:
                    break;
                }
            }
            if (applyDarken && s.darkenWithAge)
            {
                color.r *= 0.25f + age * 0.75f;
                color.g *= 0.25f + age * 0.75f;
                color.b *= 0.25f + age * 0.75f;
            }
            return color;
        }

        static float ageOpacity(float age, RenderSettings const &s)
        {
            return s.fadeWithAge ? age * age : 1.f;
        }

        static cocos2d::ccColor4F asFloatColor(cocos2d::ccColor3B color)
        {
            return {color.r / 255.f, color.g / 255.f, color.b / 255.f, 1.f};
        }

        static GameMode currentGameMode(PlayerObject *player)
        {
            if (player->m_isShip)
                return GameMode::Ship;
            if (player->m_isBall)
                return GameMode::Ball;
            if (player->m_isBird)
                return GameMode::Ufo;
            if (player->m_isDart)
                return GameMode::Wave;
            if (player->m_isRobot)
                return GameMode::Robot;
            if (player->m_isSpider)
                return GameMode::Spider;
            if (player->m_isSwing)
                return GameMode::Swing;
            return GameMode::Cube;
        }

        enum class ThicknessKind
        {
            Main,
            Circle,
            Rotation,
            Blue
        };

        static char const *modeThicknessKey(ThicknessKind kind, GameMode mode, bool isMini)
        {
            auto isWave = mode == GameMode::Wave;
            switch (kind)
            {
            case ThicknessKind::Main:
                return isWave
                           ? (isMini ? "wave-mini-square-hitbox-thickness" : "wave-square-hitbox-thickness")
                           : (isMini ? "mini-square-hitbox-thickness" : "square-hitbox-thickness");
            case ThicknessKind::Circle:
                return isWave
                           ? (isMini ? "wave-mini-circle-hitbox-thickness" : "wave-circle-hitbox-thickness")
                           : (isMini ? "mini-circle-hitbox-thickness" : "circle-hitbox-thickness");
            case ThicknessKind::Rotation:
                return isWave
                           ? (isMini ? "player-rotation-wave-mini-thickness" : "player-rotation-wave-thickness")
                           : (isMini ? "player-rotation-mini-thickness" : "player-rotation-thickness");
            case ThicknessKind::Blue:
                return isWave
                           ? (isMini ? "wave-mini-blue-hitbox-thickness" : "wave-blue-hitbox-thickness")
                           : (isMini ? "mini-blue-hitbox-thickness" : "blue-hitbox-thickness");
            }
            return "square-hitbox-thickness";
        }

        static constexpr float modeBlueInsetValue(GameMode mode)
        {
            switch (mode)
            {
            case GameMode::Wave:
                return -0.25f;
            case GameMode::Spider:
                return -0.675f;
            default:
                return -0.75f;
            }
        }

        static cocos2d::CCRect insetRect(cocos2d::CCRect rect, float inset)
        {
            inset = std::min(inset, std::min(rect.size.width, rect.size.height) / 2.f);
            rect.origin.x += inset;
            rect.origin.y += inset;
            rect.size.width -= inset * 2.f;
            rect.size.height -= inset * 2.f;
            return rect;
        }

        bool shouldShowWhileAlive(GJBaseGameLayer *layer)
        {
            auto includeMegaHack = m_megaHackIgnoreFrames <= 0;
            return layer->m_isDebugDrawEnabled || ModDetection::enabled(includeMegaHack);
        }

        cocos2d::CCPoint rotatePointAround(cocos2d::CCPoint point, cocos2d::CCPoint centre,
                                           float cosine, float sine)
        {
            auto x = point.x - centre.x;
            auto y = point.y - centre.y;
            return cocos2d::CCPointMake(
                centre.x + x * cosine - y * sine,
                centre.y + x * sine + y * cosine);
        }

        void drawHitboxRect(cocos2d::CCRect rect, float thickness,
                            cocos2d::ccColor4F fill, cocos2d::ccColor4F outline,
                            float rotation = 0.f)
        {
            if (rect.size.width <= 0.f || rect.size.height <= 0.f)
                return;
            if (fill.a <= 0.f && outline.a <= 0.f)
                return;

            thickness = std::clamp(thickness, 0.f,
                                   std::min(rect.size.width, rect.size.height) / 2.f);
            auto edge = outline.a > 0.f ? outline : fill;
            cocos2d::CCPoint verts[4] = {
                {rect.getMinX(), rect.getMinY()},
                {rect.getMaxX(), rect.getMinY()},
                {rect.getMaxX(), rect.getMaxY()},
                {rect.getMinX(), rect.getMaxY()},
            };
            auto centre = cocos2d::CCPointMake(rect.getMidX(), rect.getMidY());
            auto rotated = rotation != 0.f;
            float cosine = 1.f, sine = 0.f;
            if (rotated)
            {
                auto radians = -rotation * 0.01745329251994329577f;
                cosine = std::cos(radians);
                sine = std::sin(radians);
                for (auto &point : verts)
                    point = rotatePointAround(point, centre, cosine, sine);
            }

            if (outline.a <= 0.f)
            {
                drawFlatTriangle(verts[0], verts[1], verts[2], fill);
                drawFlatTriangle(verts[0], verts[2], verts[3], fill);
                return;
            }

            auto inner = insetRect(rect, thickness);
            cocos2d::CCPoint innerVerts[4] = {
                {inner.getMinX(), inner.getMinY()},
                {inner.getMaxX(), inner.getMinY()},
                {inner.getMaxX(), inner.getMaxY()},
                {inner.getMinX(), inner.getMaxY()},
            };
            if (rotated)
            {
                for (auto &point : innerVerts)
                    point = rotatePointAround(point, centre, cosine, sine);
            }

            drawFlatTriangle(innerVerts[0], innerVerts[1], innerVerts[2], fill);
            drawFlatTriangle(innerVerts[0], innerVerts[2], innerVerts[3], fill);

            for (int i = 0; i < 4; ++i)
            {
                int next = (i + 1) % 4;
                drawFlatTriangle(verts[i], verts[next], innerVerts[next], edge);
                drawFlatTriangle(verts[i], innerVerts[next], innerVerts[i], edge);
            }
        }

        void drawHitboxCircle(cocos2d::CCPoint centre, float radius, float thickness,
                              cocos2d::ccColor4F fill, cocos2d::ccColor4F outline,
                              int segments = 16)
        {
            if (radius <= 0.f || (fill.a <= 0.f && outline.a <= 0.f))
                return;
            thickness = std::clamp(thickness, 0.f, radius);
            auto edge = outline.a > 0.f ? outline : fill;
            segments = std::max(segments, 3);
            constexpr float kTau = 6.28318530717958647692f;
            if (outline.a <= 0.f)
            {
                for (int i = 0; i < segments; ++i)
                {
                    auto angle0 = kTau * static_cast<float>(i) / segments;
                    auto angle1 = kTau * static_cast<float>(i + 1) / segments;
                    auto point0 = cocos2d::CCPointMake(
                        centre.x + std::cos(angle0) * radius,
                        centre.y + std::sin(angle0) * radius);
                    auto point1 = cocos2d::CCPointMake(
                        centre.x + std::cos(angle1) * radius,
                        centre.y + std::sin(angle1) * radius);
                    drawFlatTriangle(centre, point0, point1, fill);
                }
                return;
            }

            auto innerRadius = std::max(radius - thickness, 0.f);
            for (int i = 0; i < segments; ++i)
            {
                auto angle0 = kTau * static_cast<float>(i) / segments;
                auto angle1 = kTau * static_cast<float>(i + 1) / segments;
                auto innerPoint0 = cocos2d::CCPointMake(
                    centre.x + std::cos(angle0) * innerRadius,
                    centre.y + std::sin(angle0) * innerRadius);
                auto innerPoint1 = cocos2d::CCPointMake(
                    centre.x + std::cos(angle1) * innerRadius,
                    centre.y + std::sin(angle1) * innerRadius);
                drawFlatTriangle(centre, innerPoint0, innerPoint1, fill);

                auto outerPoint0 = cocos2d::CCPointMake(
                    centre.x + std::cos(angle0) * radius,
                    centre.y + std::sin(angle0) * radius);
                auto outerPoint1 = cocos2d::CCPointMake(
                    centre.x + std::cos(angle1) * radius,
                    centre.y + std::sin(angle1) * radius);
                drawFlatTriangle(outerPoint0, outerPoint1, innerPoint1, edge);
                drawFlatTriangle(outerPoint0, innerPoint1, innerPoint0, edge);
            }
        }

        void drawFlatTriangle(cocos2d::CCPoint const &a, cocos2d::CCPoint const &b,
                              cocos2d::CCPoint const &c, cocos2d::ccColor4F color)
        {
            if (color.a <= 0.f)
                return;
            constexpr unsigned int kVertexCount = 3;
            if (m_nBufferCount + kVertexCount > m_uBufferCapacity)
            {
                m_uBufferCapacity += std::max(m_uBufferCapacity, kVertexCount);
                m_pBuffer = static_cast<cocos2d::ccV2F_C4B_T2F *>(
                    std::realloc(m_pBuffer, m_uBufferCapacity * sizeof(cocos2d::ccV2F_C4B_T2F)));
            }

            auto vertexColor = cocos2d::ccc4BFromccc4F(color);
            auto makeVertex = [&](cocos2d::CCPoint point)
            {
                return cocos2d::ccV2F_C4B_T2F{{point.x, point.y}, vertexColor, {0.f, 0.f}};
            };
            auto triangles = reinterpret_cast<cocos2d::ccV2F_C4B_T2F_Triangle *>(m_pBuffer + m_nBufferCount);
            triangles[0] = {makeVertex(a), makeVertex(b), makeVertex(c)};
            m_nBufferCount += kVertexCount;
            m_bDirty = true;
        }

        std::deque<TrailState> m_states;
        std::deque<TrailState> m_states2;
        cocos2d::CCNode *m_world1 = nullptr;
        cocos2d::CCNode *m_world2 = nullptr;
        std::optional<RenderSettings> m_cachedSettings;
        std::vector<BatchItem> m_batchScratch;
        bool m_wasDead = false;
        bool m_recordedDeathFrame = false;
        int m_megaHackIgnoreFrames = 0;
        bool m_gameplayXform = false;
        bool m_lastHeldP1 = false;
        bool m_lastHeldP2 = false;
        static constexpr int kFlashTicks = 2;
        TrailState::Click m_flashClickP1 = TrailState::Click::None;
        TrailState::Click m_flashClickP2 = TrailState::Click::None;
        int m_flashTicksP1 = 0;
        int m_flashTicksP2 = 0;
        float m_lastTickDt = 1.f / 240.f;
        float m_captureAccumulator = 0.f;
    };

    TrailNode *TrailNode::create()
    {
        return TrailNodeImpl::createImpl();
    }

    void TrailNode::healThicknessSettings()
    {
        TrailNodeImpl::healThicknessSettings();
    }
}
