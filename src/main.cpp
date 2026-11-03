// MacroBot v2.0 - Geode v6 (alpha)
// Menu con pestañas: Bot (macros), Speed (speedhack con slider), Player (noclip/safe mode),
// Visual (hitboxes, overlay de estado).

#include <Geode/Geode.hpp>
#include <Geode/modify/CCScheduler.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

using namespace geode::prelude;

namespace bot {

struct Input {
    int frame;
    int button;
    bool down;
    bool player1;
};

struct Entry {
    std::string key;
    std::string name;
    int count;
};

enum class State { Idle, Recording, Playing };

inline State state = State::Idle;
inline std::vector<Input> macro;
inline size_t nextInput = 0;
inline bool injecting = false;
inline int curFrame = 0;

inline int levelID = 0;
inline std::string levelName = "Unknown";

// opciones
inline float speed = 1.f;
inline bool pitchSync = true;
inline bool noclip = false;
inline bool safeMode = true;
inline bool hitboxes = false;
inline bool hitboxesApplied = false;
inline bool overlay = false;
inline bool autoSave = false;

template <class T>
int toInt(T v) {
    if constexpr (requires { v.value(); }) return static_cast<int>(v.value());
    else return static_cast<int>(v);
}

inline int frameOf(GJBaseGameLayer* layer) {
    return layer->m_gameState.m_currentProgress;
}

inline bool inLevel(GJBaseGameLayer* layer) {
    return layer && layer == static_cast<GJBaseGameLayer*>(PlayLayer::get());
}

inline void notify(std::string const& text, NotificationIcon icon = NotificationIcon::Success) {
    Notification::create(text, icon, 1.5f)->show();
}

inline std::string clean(std::string s) {
    for (auto& c : s) if (c == '\t' || c == '\n' || c == '\r' || c == '|') c = ' ';
    return s;
}

inline std::string levelKey() {
    return std::to_string(levelID) + "|" + levelName;
}

inline void seek(int frame) {
    nextInput = std::lower_bound(
        macro.begin(), macro.end(), frame,
        [](Input const& i, int f) { return i.frame < f; }
    ) - macro.begin();
}

// ---------- velocidad ----------

inline std::string speedText() {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.2fx", speed);
    return buf;
}

// Si esta función da error de compilación, borra su contenido (queda solo la velocidad, sin música).
inline void applyPitch() {
    auto engine = FMODAudioEngine::get();
    if (!engine || !engine->m_system) return;
    FMOD::ChannelGroup* group = nullptr;
    if (engine->m_system->getMasterChannelGroup(&group) == FMOD_OK && group)
        group->setPitch(pitchSync ? speed : 1.f);
}

inline void setSpeed(float s) {
    speed = std::clamp(std::round(s * 20.f) / 20.f, 0.1f, 5.f);
    applyPitch();
}

// ---------- safe mode ----------

inline bool cheating() {
    return noclip || speed != 1.f || state == State::Playing;
}

inline bool safeActive() {
    return safeMode && cheating();
}

// ---------- almacenamiento ----------

inline std::vector<Entry> entries() {
    std::vector<Entry> out;
    std::istringstream in(Mod::get()->getSavedValue<std::string>("index"));
    std::string line;
    while (std::getline(in, line)) {
        auto a = line.find('\t');
        auto b = line.rfind('\t');
        if (a == std::string::npos || a == b) continue;
        out.push_back({ line.substr(0, a), line.substr(a + 1, b - a - 1), std::atoi(line.c_str() + b + 1) });
    }
    return out;
}

inline void writeEntries(std::vector<Entry> const& list) {
    std::ostringstream out;
    for (auto const& e : list) out << e.key << '\t' << e.name << '\t' << e.count << '\n';
    Mod::get()->setSavedValue<std::string>("index", out.str());
}

inline void save() {
    std::ostringstream out;
    out << "MACROBOT2\n";
    for (auto const& i : macro)
        out << i.frame << ' ' << i.button << ' ' << i.down << ' ' << i.player1 << '\n';

    auto key = levelKey();
    Mod::get()->setSavedValue<std::string>("data:" + key, out.str());

    auto list = entries();
    Entry e{ key, levelName, static_cast<int>(macro.size()) };
    auto it = std::find_if(list.begin(), list.end(), [&](Entry const& x) { return x.key == key; });
    if (it != list.end()) *it = e;
    else list.push_back(e);
    writeEntries(list);
}

inline bool load(std::string const& key) {
    auto data = Mod::get()->getSavedValue<std::string>("data:" + key);
    if (data.empty()) return false;

    std::istringstream in(data);
    std::string header;
    std::getline(in, header);
    if (header != "MACROBOT2") return false;

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

inline void deleteMacro(std::string const& key) {
    Mod::get()->setSavedValue<std::string>("data:" + key, "");
    auto list = entries();
    list.erase(std::remove_if(list.begin(), list.end(),
        [&](Entry const& e) { return e.key == key; }), list.end());
    writeEntries(list);
}

// ---------- UI helpers ----------

inline ButtonSprite* button(char const* text, int width, float scale = .55f,
                            char const* tex = "GJ_button_01.png") {
    return ButtonSprite::create(text, width, true, "bigFont.fnt", tex, 25.f, scale);
}

inline CCScale9Sprite* box(char const* file, CCSize size, bool capped) {
    CCScale9Sprite* s = capped ? CCScale9Sprite::create(file, { 0.f, 0.f, 80.f, 80.f })
                               : CCScale9Sprite::create(file);
    if (!s) s = CCScale9Sprite::create("GJ_square01.png");
    if (s) s->setContentSize(size);
    return s;
}

} // namespace bot

// ====================== speedhack ======================

class $modify(BotScheduler, CCScheduler) {
    void update(float dt) {
        if (bot::speed != 1.f && PlayLayer::get()) dt *= bot::speed;
        CCScheduler::update(dt);
    }
};

// ====================== bot / input ======================

class $modify(BotBaseLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        if (bot::inLevel(this) && !bot::injecting) {
            if (bot::state == bot::State::Playing) return; // ignora input real al reproducir
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

// ====================== nivel ======================

class $modify(BotPlayLayer, PlayLayer) {
    struct Fields {
        CCLabelBMFont* label = nullptr;
        std::string last;
    };

    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();

        int id = bot::toInt(m_level->m_levelID);
        std::string name = bot::clean(static_cast<std::string>(m_level->m_levelName));
        if (name.empty()) name = "Unnamed";

        bool changed = (id != bot::levelID) || (name != bot::levelName);
        bot::levelID = id;
        bot::levelName = name;
        if (changed) bot::macro.clear();

        bot::nextInput = 0;
        bot::curFrame = 0;
        bot::state = bot::State::Idle;
        bot::hitboxesApplied = false;

        auto win = CCDirector::get()->getWinSize();
        auto label = CCLabelBMFont::create("", "bigFont.fnt");
        label->setScale(.32f);
        label->setAnchorPoint({ 0.f, 1.f });
        label->setPosition({ 6.f, win.height - 6.f });
        label->setOpacity(170);
        label->setZOrder(1000);
        label->setVisible(false);
        this->addChild(label);
        m_fields->label = label;
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

    // GD 2.2091 solo dibuja hitboxes en practice mode, salvo que se enganche esta función.
    bool shouldDrawDebugHitboxes() {
        if (bot::hitboxes) return true;
        return PlayLayer::shouldDrawDebugHitboxes();
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        if (bot::hitboxes != bot::hitboxesApplied) {
            bot::hitboxesApplied = bot::hitboxes;
            this->updateDebugDrawSettings();
        }

        if (auto label = m_fields->label) {
            if (!bot::overlay) {
                label->setVisible(false);
            } else {
                label->setVisible(true);
                std::string text = "Frame " + std::to_string(bot::frameOf(this));
                if (bot::state == bot::State::Recording) text += " | REC";
                else if (bot::state == bot::State::Playing) text += " | PLAY";
                if (bot::speed != 1.f) text += " | " + bot::speedText();
                if (bot::noclip) text += " | NOCLIP";
                if (bot::safeActive()) text += " | SAFE";
                if (text != m_fields->last) {
                    m_fields->last = text;
                    label->setString(text.c_str());
                }
            }
        }
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (bot::noclip && object != m_anticheatSpike) return;

        if (bot::safeActive()) {
            bool old = m_isTestMode;
            m_isTestMode = true; // GD no guarda progreso en test mode
            PlayLayer::destroyPlayer(player, object);
            m_isTestMode = old;
        } else {
            PlayLayer::destroyPlayer(player, object);
        }
    }

    void levelComplete() {
        if (bot::autoSave && bot::state == bot::State::Recording && !bot::macro.empty()) {
            bot::save();
            bot::notify("Macro auto-saved");
        }

        if (bot::safeActive()) {
            bool old = m_isTestMode;
            m_isTestMode = true;
            PlayLayer::levelComplete();
            m_isTestMode = old;
        } else {
            PlayLayer::levelComplete();
        }
    }

    void onQuit() {
        bot::state = bot::State::Idle;
        bot::speed = 1.f;
        bot::applyPitch();
        PlayLayer::onQuit();
    }
};

// ====================== menú ======================

class $modify(BotPauseLayer, PauseLayer) {
    struct Fields {
        int page = 0;
        int tab = 0;
        CCLabelBMFont* speedLabel = nullptr;
    };

    void customSetup() {
        PauseLayer::customSetup();

        auto win = CCDirector::get()->getWinSize();
        auto menu = CCMenu::create();
        menu->setID("menu"_spr);
        menu->setPosition({ 0.f, 0.f });

        auto open = CCMenuItemSpriteExtra::create(
            bot::button("Bot", 44, .5f), this, menu_selector(BotPauseLayer::onOpen)
        );
        open->setPosition({ 60.f, win.height - 22.f });
        menu->addChild(open);
        this->addChild(menu);
    }

    void setMenusEnabled(bool on) {
        for (auto child : CCArrayExt<CCNode*>(this->getChildren())) {
            if (auto m = typeinfo_cast<CCMenu*>(child)) m->setEnabled(on);
        }
    }

    void buildPanel() {
        if (auto old = this->getChildByID("panel"_spr)) old->removeFromParent();
        m_fields->speedLabel = nullptr;

        auto win = CCDirector::get()->getWinSize();
        float cx = win.width / 2.f;
        float cy = win.height / 2.f;
        float cxc = cx + 58.f; // centro del área de contenido

        auto panel = CCNode::create();
        panel->setID("panel"_spr);
        panel->setZOrder(100);

        panel->addChild(CCLayerColor::create(ccc4(0, 0, 0, 160), win.width, win.height));

        if (auto bg = bot::box("GJ_square01.png", { 470.f, 285.f }, false)) {
            bg->setPosition({ cx, cy });
            panel->addChild(bg);
        }
        if (auto side = bot::box("square02b_001.png", { 110.f, 200.f }, true)) {
            side->setColor({ 0, 0, 0 });
            side->setOpacity(110);
            side->setPosition({ cx - 165.f, cy - 8.f });
            panel->addChild(side);
        }
        if (auto content = bot::box("square02b_001.png", { 330.f, 200.f }, true)) {
            content->setColor({ 0, 0, 0 });
            content->setOpacity(110);
            content->setPosition({ cxc, cy - 8.f });
            panel->addChild(content);
        }

        auto menu = CCMenu::create();
        menu->setPosition({ 0.f, 0.f });
        panel->addChild(menu);

        auto addLabel = [&](std::string const& text, CCPoint pos, float scale,
                            bool left = false, char const* font = "bigFont.fnt") {
            auto l = CCLabelBMFont::create(text.c_str(), font);
            l->setScale(scale);
            if (left) l->setAnchorPoint({ 0.f, .5f });
            l->setPosition(pos);
            panel->addChild(l);
            return l;
        };

        auto addBtn = [&](char const* text, int w, SEL_MenuHandler cb, CCPoint pos,
                          int tag = 0, char const* tex = "GJ_button_01.png") {
            auto item = CCMenuItemSpriteExtra::create(bot::button(text, w, .55f, tex), this, cb);
            item->setPosition(pos);
            item->setTag(tag);
            menu->addChild(item);
            return item;
        };

        auto addToggle = [&](char const* text, CCPoint pos, SEL_MenuHandler cb, bool on) {
            auto t = CCMenuItemToggler::createWithStandardSprites(this, cb, .65f);
            t->toggle(on);
            t->setPosition(pos);
            menu->addChild(t);
            addLabel(text, { pos.x + 22.f, pos.y }, .5f, true);
        };

        // título y cerrar
        addLabel("MacroBot", { cx, cy + 122.f }, .8f, false, "goldFont.fnt");

        auto closeItem = CCMenuItemSpriteExtra::create(
            CCSprite::createWithSpriteFrameName("GJ_closeBtn_001.png"), this,
            menu_selector(BotPauseLayer::onClose)
        );
        closeItem->setPosition({ cx - 213.f, cy + 122.f });
        menu->addChild(closeItem);

        // pestañas
        char const* tabs[] = { "Bot", "Speed", "Player", "Visual" };
        for (int i = 0; i < 4; i++) {
            addBtn(tabs[i], 90, menu_selector(BotPauseLayer::onTab),
                   { cx - 165.f, cy + 62.f - i * 40.f }, i,
                   i == m_fields->tab ? "GJ_button_01.png" : "GJ_button_04.png");
        }

        // ---------------- contenido ----------------
        switch (m_fields->tab) {
        case 0: { // Bot
            addBtn(bot::state == bot::State::Recording ? "Stop Rec" : "Record", 95,
                   menu_selector(BotPauseLayer::onRecord), { cxc - 105.f, cy + 68.f });
            addBtn(bot::state == bot::State::Playing ? "Stop Play" : "Play", 95,
                   menu_selector(BotPauseLayer::onPlay), { cxc, cy + 68.f });
            addBtn("Save", 95, menu_selector(BotPauseLayer::onSave), { cxc + 105.f, cy + 68.f });

            addToggle("Auto-save on complete", { cxc - 150.f, cy + 34.f },
                      menu_selector(BotPauseLayer::onAutoSave), bot::autoSave);

            addLabel("Saved macros", { cxc, cy + 10.f }, .42f);

            auto list = bot::entries();
            int perPage = 3;
            int pages = std::max(1, static_cast<int>((list.size() + perPage - 1) / perPage));
            auto& page = m_fields->page;
            page = std::clamp(page, 0, pages - 1);

            if (list.empty()) addLabel("No macros saved yet", { cxc, cy - 40.f }, .45f);

            for (int i = 0; i < perPage; i++) {
                size_t idx = static_cast<size_t>(page) * perPage + i;
                if (idx >= list.size()) break;
                auto const& e = list[idx];
                std::string label = e.name.size() > 20 ? e.name.substr(0, 19) + "." : e.name;
                float y = cy - 16.f - i * 30.f;
                bool mine = (e.key == bot::levelKey());
                addBtn(label.c_str(), 230, menu_selector(BotPauseLayer::onEntry),
                       { cxc - 30.f, y }, static_cast<int>(idx),
                       mine ? "GJ_button_01.png" : "GJ_button_04.png");
                addBtn("X", 28, menu_selector(BotPauseLayer::onDelete),
                       { cxc + 115.f, y }, static_cast<int>(idx), "GJ_button_06.png");
            }

            float ny = cy - 98.f;
            addBtn("<", 30, menu_selector(BotPauseLayer::onPage), { cxc - 45.f, ny }, -1);
            addBtn(">", 30, menu_selector(BotPauseLayer::onPage), { cxc + 45.f, ny }, 1);
            addLabel(std::to_string(page + 1) + "/" + std::to_string(pages), { cxc, ny }, .5f);
            break;
        }
        case 1: { // Speed
            addLabel("Game speed", { cxc, cy + 74.f }, .5f);
            m_fields->speedLabel = addLabel(bot::speedText(), { cxc, cy + 46.f }, .85f, false, "goldFont.fnt");

            auto slider = Slider::create(this, menu_selector(BotPauseLayer::onSpeedSlider), .85f);
            slider->setPosition({ cxc, cy + 12.f });
            slider->setValue((bot::speed - 0.1f) / 4.9f);
            panel->addChild(slider);

            int presets[] = { 50, 100, 200, 300, 500 };
            char const* names[] = { "0.5x", "1x", "2x", "3x", "5x" };
            for (int i = 0; i < 5; i++) {
                addBtn(names[i], 50, menu_selector(BotPauseLayer::onPreset),
                       { cxc - 120.f + i * 60.f, cy - 28.f }, presets[i]);
            }

            addToggle("Sync music pitch", { cxc - 150.f, cy - 66.f },
                      menu_selector(BotPauseLayer::onPitch), bot::pitchSync);
            addLabel("Works inside levels. 0.1x - 5x", { cxc, cy - 96.f }, .34f);
            break;
        }
        case 2: { // Player
            addToggle("Noclip", { cxc - 150.f, cy + 62.f },
                      menu_selector(BotPauseLayer::onNoclip), bot::noclip);
            addToggle("Safe mode", { cxc - 150.f, cy + 22.f },
                      menu_selector(BotPauseLayer::onSafe), bot::safeMode);
            addLabel("Safe mode: while noclip, speed or", { cxc - 150.f, cy - 18.f }, .34f, true);
            addLabel("replay are active, nothing is saved", { cxc - 150.f, cy - 34.f }, .34f, true);
            addLabel("(no progress, no leaderboards).", { cxc - 150.f, cy - 50.f }, .34f, true);
            break;
        }
        case 3: { // Visual
            addToggle("Show hitboxes", { cxc - 150.f, cy + 62.f },
                      menu_selector(BotPauseLayer::onHitboxes), bot::hitboxes);
            addToggle("Status overlay", { cxc - 150.f, cy + 22.f },
                      menu_selector(BotPauseLayer::onOverlay), bot::overlay);
            addLabel("Overlay: frame, bot state, speed", { cxc - 150.f, cy - 18.f }, .34f, true);
            addLabel("(top-left corner of the level).", { cxc - 150.f, cy - 34.f }, .34f, true);
            break;
        }
        }

        // pie
        std::string st = bot::state == bot::State::Recording ? "REC"
                       : bot::state == bot::State::Playing ? "PLAY" : "idle";
        addLabel("Level: " + bot::levelName + "  |  " + std::to_string(bot::macro.size())
                 + " inputs  |  " + st, { cx, cy - 127.f }, .36f);

        this->addChild(panel);
    }

    void rebuild(float) { this->buildPanel(); }

    void doClose(float) {
        m_fields->speedLabel = nullptr;
        if (auto p = this->getChildByID("panel"_spr)) p->removeFromParent();
        this->setMenusEnabled(true);
    }

    void queueRebuild() {
        this->scheduleOnce(schedule_selector(BotPauseLayer::rebuild), 0.f);
    }

    void onOpen(CCObject*) {
        if (this->getChildByID("panel"_spr)) return;
        m_fields->page = 0;
        this->setMenusEnabled(false);
        this->buildPanel();
    }

    void onClose(CCObject*) {
        this->scheduleOnce(schedule_selector(BotPauseLayer::doClose), 0.f);
    }

    void onTab(CCObject* sender) {
        m_fields->tab = static_cast<CCNode*>(sender)->getTag();
        m_fields->page = 0;
        this->queueRebuild();
    }

    // ----- bot -----

    void onRecord(CCObject*) {
        if (bot::state == bot::State::Recording) {
            bot::state = bot::State::Idle;
            bot::notify("Recording stopped (" + std::to_string(bot::macro.size()) + " inputs)");
        } else {
            bot::state = bot::State::Recording;
            bot::notify("Recording: restart the level to begin");
        }
        this->queueRebuild();
    }

    void onPlay(CCObject*) {
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
        this->queueRebuild();
    }

    void onSave(CCObject*) {
        if (bot::macro.empty()) return bot::notify("Nothing to save", NotificationIcon::Error);
        bot::save();
        bot::notify("Saved: " + bot::levelName);
        this->queueRebuild();
    }

    void onAutoSave(CCObject* sender) {
        bot::autoSave = !static_cast<CCMenuItemToggler*>(sender)->isToggled();
    }

    void onPage(CCObject* sender) {
        m_fields->page += static_cast<CCNode*>(sender)->getTag();
        this->queueRebuild();
    }

    void onEntry(CCObject* sender) {
        auto list = bot::entries();
        size_t idx = static_cast<size_t>(static_cast<CCNode*>(sender)->getTag());
        if (idx >= list.size()) return;

        if (list[idx].key != bot::levelKey())
            return bot::notify("That macro belongs to another level", NotificationIcon::Error);

        if (bot::load(list[idx].key)) bot::notify("Loaded " + std::to_string(bot::macro.size()) + " inputs");
        else bot::notify("Could not load macro", NotificationIcon::Error);
        this->queueRebuild();
    }

    void onDelete(CCObject* sender) {
        auto list = bot::entries();
        size_t idx = static_cast<size_t>(static_cast<CCNode*>(sender)->getTag());
        if (idx >= list.size()) return;
        bot::deleteMacro(list[idx].key);
        bot::notify("Macro deleted");
        this->queueRebuild();
    }

    // ----- speed -----

    void onSpeedSlider(CCObject* sender) {
        float v = static_cast<SliderThumb*>(sender)->getValue();
        bot::setSpeed(0.1f + v * 4.9f);
        if (m_fields->speedLabel) m_fields->speedLabel->setString(bot::speedText().c_str());
    }

    void onPreset(CCObject* sender) {
        bot::setSpeed(static_cast<CCNode*>(sender)->getTag() / 100.f);
        this->queueRebuild();
    }

    void onPitch(CCObject* sender) {
        bot::pitchSync = !static_cast<CCMenuItemToggler*>(sender)->isToggled();
        bot::applyPitch();
    }

    // ----- player / visual -----

    void onNoclip(CCObject* sender) {
        bot::noclip = !static_cast<CCMenuItemToggler*>(sender)->isToggled();
    }

    void onSafe(CCObject* sender) {
        bot::safeMode = !static_cast<CCMenuItemToggler*>(sender)->isToggled();
    }

    void onHitboxes(CCObject* sender) {
        bot::hitboxes = !static_cast<CCMenuItemToggler*>(sender)->isToggled();
    }

    void onOverlay(CCObject* sender) {
        bot::overlay = !static_cast<CCMenuItemToggler*>(sender)->isToggled();
    }
};
