// MacroBot - simple input recorder / replayer for Geode v6 (alpha).
// NOTE: written against the v5 bindings; if a signature below fails to compile,
// open the matching header in $GEODE_SDK/bindings and copy the exact signature.

#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace geode::prelude;

namespace bot {

struct Input {
    int frame;
    int button;
    bool down;
    bool player1;
};

enum class State { Idle, Recording, Playing };

inline State state = State::Idle;
inline std::vector<Input> macro;
inline size_t nextInput = 0;   // playback cursor
inline bool injecting = false; // true while we feed inputs ourselves
inline int curFrame = 0;       // frame captured at the start of processCommands
inline int levelID = 0;

// m_levelID is an int or a wrapper with .value() depending on bindings version.
template <class T>
int toInt(T const& v) {
    if constexpr (requires { v.value(); }) return static_cast<int>(v.value());
    else return static_cast<int>(v);
}

// If GD changes where the step counter lives, this is the only line to fix.
inline int frameOf(GJBaseGameLayer* layer) {
    return layer->m_gameState.m_currentProgress;
}

inline bool inLevel(GJBaseGameLayer* layer) {
    return layer && layer == static_cast<GJBaseGameLayer*>(PlayLayer::get());
}

inline void notify(std::string const& text, NotificationIcon icon = NotificationIcon::Success) {
    Notification::create(text, icon, 1.5f)->show();
}

inline void seek(int frame) {
    nextInput = std::lower_bound(
        macro.begin(), macro.end(), frame,
        [](Input const& i, int f) { return i.frame < f; }
    ) - macro.begin();
}

inline std::filesystem::path macroPath() {
    auto dir = Mod::get()->getSaveDir() / "macros";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir / (std::to_string(levelID) + ".macro");
}

inline bool save() {
    std::ofstream out(macroPath(), std::ios::trunc);
    if (!out) return false;
    out << "MACROBOT1\n";
    for (auto const& i : macro)
        out << i.frame << ' ' << i.button << ' ' << i.down << ' ' << i.player1 << '\n';
    return static_cast<bool>(out);
}

inline bool load() {
    std::ifstream in(macroPath());
    if (!in) return false;
    std::string header;
    std::getline(in, header);
    if (header != "MACROBOT1") return false;

    std::vector<Input> loaded;
    int f, b, d, p;
    while (in >> f >> b >> d >> p)
        loaded.push_back({ f, b, d != 0, p != 0 });

    std::stable_sort(loaded.begin(), loaded.end(),
        [](Input const& a, Input const& c) { return a.frame < c.frame; });
    macro = std::move(loaded);
    nextInput = 0;
    return true;
}

inline ButtonSprite* button(char const* text) {
    return ButtonSprite::create(text, 90, true, "bigFont.fnt", "GJ_button_01.png", 30.f, .6f);
}

inline void setLabel(CCNode* item, char const* text) {
    if (auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(item))
        if (auto spr = typeinfo_cast<ButtonSprite*>(btn->getNormalImage()))
            spr->setString(text);
}

inline void refresh(CCNode* menu) {
    if (!menu) return;
    setLabel(menu->getChildByID("record"_spr), state == State::Recording ? "Stop Rec" : "Record");
    setLabel(menu->getChildByID("play"_spr), state == State::Playing ? "Stop Play" : "Play");
}

} // namespace bot

class $modify(BotBaseLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        if (bot::inLevel(this) && !bot::injecting) {
            if (bot::state == bot::State::Playing) return; // ignore real input while replaying
            if (bot::state == bot::State::Recording)
                bot::macro.push_back({ bot::curFrame, button, down, isPlayer1 });
        }
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }

    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        if (bot::state != bot::State::Idle && bot::inLevel(this)) {
            bot::curFrame = bot::frameOf(this);

            if (bot::state == bot::State::Playing) {
                bot::injecting = true;
                while (bot::nextInput < bot::macro.size()
                       && bot::macro[bot::nextInput].frame <= bot::curFrame) {
                    auto in = bot::macro[bot::nextInput++];
                    this->handleButton(in.down, in.button, in.player1);
                }
                bot::injecting = false;
            }
        }
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
    }
};

class $modify(BotPlayLayer, PlayLayer) {
    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();
        int id = bot::toInt(m_level->m_levelID);
        if (id != bot::levelID) {
            bot::levelID = id;
            bot::macro.clear();
        }
        bot::nextInput = 0;
        bot::curFrame = 0;
        bot::state = bot::State::Idle;
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        int frame = bot::frameOf(this);
        bot::curFrame = frame;

        if (bot::state == bot::State::Recording) {
            if (frame <= 0) bot::macro.clear();
            else std::erase_if(bot::macro, [&](bot::Input const& i) { return i.frame >= frame; });
        } else if (bot::state == bot::State::Playing) {
            bot::seek(frame);
        }
    }

    void onQuit() {
        bot::state = bot::State::Idle;
        PlayLayer::onQuit();
    }
};

class $modify(BotPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        auto winSize = CCDirector::get()->getWinSize();
        auto menu = CCMenu::create();
        menu->setID("menu"_spr);
        menu->setPosition({ 0.f, 0.f });

        float x = 70.f;
        float y = winSize.height / 2.f + 60.f;
        float step = 36.f;

        auto rec = CCMenuItemSpriteExtra::create(bot::button("Record"), this, menu_selector(BotPauseLayer::onRecord));
        rec->setID("record"_spr);
        rec->setPosition({ x, y });

        auto play = CCMenuItemSpriteExtra::create(bot::button("Play"), this, menu_selector(BotPauseLayer::onPlay));
        play->setID("play"_spr);
        play->setPosition({ x, y - step });

        auto save = CCMenuItemSpriteExtra::create(bot::button("Save"), this, menu_selector(BotPauseLayer::onSave));
        save->setID("save"_spr);
        save->setPosition({ x, y - step * 2 });

        auto load = CCMenuItemSpriteExtra::create(bot::button("Load"), this, menu_selector(BotPauseLayer::onLoad));
        load->setID("load"_spr);
        load->setPosition({ x, y - step * 3 });

        menu->addChild(rec);
        menu->addChild(play);
        menu->addChild(save);
        menu->addChild(load);
        this->addChild(menu);

        bot::refresh(menu);
    }

    void onRecord(CCObject* sender) {
        auto menu = static_cast<CCNode*>(sender)->getParent();
        if (bot::state == bot::State::Recording) {
            bot::state = bot::State::Idle;
            bot::notify("Recording stopped (" + std::to_string(bot::macro.size()) + " inputs)");
        } else {
            bot::state = bot::State::Recording;
            bot::notify("Recording: restart the level to begin");
        }
        bot::refresh(menu);
    }

    void onPlay(CCObject* sender) {
        auto menu = static_cast<CCNode*>(sender)->getParent();
        if (bot::state == bot::State::Playing) {
            bot::state = bot::State::Idle;
            bot::notify("Playback stopped");
        } else if (bot::macro.empty()) {
            bot::notify("No macro loaded or recorded", NotificationIcon::Error);
        } else {
            bot::state = bot::State::Playing;
            bot::nextInput = 0;
            bot::notify("Playing: restart the level to begin");
        }
        bot::refresh(menu);
    }

    void onSave(CCObject*) {
        if (bot::macro.empty()) return bot::notify("Nothing to save", NotificationIcon::Error);
        if (bot::save()) bot::notify("Macro saved");
        else bot::notify("Failed to save macro", NotificationIcon::Error);
    }

    void onLoad(CCObject*) {
        if (bot::load()) bot::notify("Loaded " + std::to_string(bot::macro.size()) + " inputs");
        else bot::notify("No macro found for this level", NotificationIcon::Error);
    }
};
