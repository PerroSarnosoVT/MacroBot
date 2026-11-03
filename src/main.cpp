// MacroBot v1.1 - Geode v6 (alpha)
// Panel con: grabar, reproducir, guardar, lista de macros por nivel, speedhack, hitboxes.

#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>

#include <algorithm>
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

inline bool hitboxes = false;
inline bool hitboxesApplied = false;

inline constexpr float speeds[] = { 0.25f, 0.5f, 0.75f, 1.f, 1.25f, 1.5f, 2.f, 3.f };
inline constexpr int speedCount = 8;
inline int speedIdx = 3;

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

inline void applySpeed() {
    CCDirector::get()->getScheduler()->setTimeScale(speeds[speedIdx]);
}

inline std::string speedLabel() {
    char buf[32];
    std::snprintf(buf, sizeof buf, "Speed %.2gx", speeds[speedIdx]);
    return buf;
}

// ---------- almacenamiento (datos guardados del mod) ----------

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

inline ButtonSprite* button(char const* text, int width, float scale = .6f) {
    return ButtonSprite::create(text, width, true, "bigFont.fnt", "GJ_button_01.png", 25.f, scale);
}

} // namespace bot

// ====================== hooks de juego ======================

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

class $modify(BotPlayLayer, PlayLayer) {
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

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (bot::hitboxes) {
            m_isDebugDrawEnabled = true;
            this->updateDebugDraw();
            bot::hitboxesApplied = true;
        } else if (bot::hitboxesApplied) {
            m_isDebugDrawEnabled = false;
            this->updateDebugDraw();
            bot::hitboxesApplied = false;
        }
    }

    void onQuit() {
        bot::state = bot::State::Idle;
        bot::speedIdx = 3;
        bot::applySpeed();
        PlayLayer::onQuit();
    }
};

// ====================== interfaz ======================

class $modify(BotPauseLayer, PauseLayer) {
    struct Fields {
        int page = 0;
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

        auto win = CCDirector::get()->getWinSize();
        float cx = win.width / 2.f;
        float cy = win.height / 2.f;

        auto panel = CCNode::create();
        panel->setID("panel"_spr);
        panel->setZOrder(100);

        panel->addChild(CCLayerColor::create(ccc4(0, 0, 0, 150), win.width, win.height));

        auto bg = CCScale9Sprite::create("GJ_square01.png");
        bg->setContentSize({ 430.f, 250.f });
        bg->setPosition({ cx, cy });
        panel->addChild(bg);

        auto title = CCLabelBMFont::create("MacroBot", "goldFont.fnt");
        title->setScale(.8f);
        title->setPosition({ cx, cy + 108.f });
        panel->addChild(title);

        auto menu = CCMenu::create();
        menu->setPosition({ 0.f, 0.f });
        panel->addChild(menu);

        auto addBtn = [&](char const* text, int w, SEL_MenuHandler cb, CCPoint pos, int tag = 0) {
            auto item = CCMenuItemSpriteExtra::create(bot::button(text, w), this, cb);
            item->setPosition(pos);
            item->setTag(tag);
            menu->addChild(item);
            return item;
        };

        auto addLabel = [&](std::string const& text, CCPoint pos, float scale, bool left = false) {
            auto l = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
            l->setScale(scale);
            if (left) l->setAnchorPoint({ 0.f, .5f });
            l->setPosition(pos);
            panel->addChild(l);
        };

        // cerrar
        auto closeItem = CCMenuItemSpriteExtra::create(
            CCSprite::createWithSpriteFrameName("GJ_closeBtn_001.png"), this,
            menu_selector(BotPauseLayer::onClose)
        );
        closeItem->setPosition({ cx - 200.f, cy + 108.f });
        menu->addChild(closeItem);

        // ----- columna izquierda: herramientas -----
        float lx = cx - 115.f;
        addBtn(bot::state == bot::State::Recording ? "Stop Rec" : "Record", 120,
               menu_selector(BotPauseLayer::onRecord), { lx, cy + 80.f });
        addBtn(bot::state == bot::State::Playing ? "Stop Play" : "Play", 120,
               menu_selector(BotPauseLayer::onPlay), { lx, cy + 45.f });
        addBtn("Save", 120, menu_selector(BotPauseLayer::onSave), { lx, cy + 10.f });
        addBtn(bot::speedLabel().c_str(), 120, menu_selector(BotPauseLayer::onSpeed), { lx, cy - 25.f });

        auto tog = CCMenuItemToggler::createWithStandardSprites(
            this, menu_selector(BotPauseLayer::onHitboxes), .6f
        );
        tog->toggle(bot::hitboxes);
        tog->setPosition({ lx - 50.f, cy - 62.f });
        menu->addChild(tog);
        addLabel("Hitboxes", { lx - 30.f, cy - 62.f }, .45f, true);

        addLabel("TPS: 240 (GD default)", { lx, cy - 92.f }, .35f);

        // ----- columna derecha: macros guardadas -----
        float rx = cx + 70.f;
        addLabel("Saved macros", { rx + 10.f, cy + 92.f }, .5f);

        auto list = bot::entries();
        int perPage = 4;
        int pages = std::max(1, static_cast<int>((list.size() + perPage - 1) / perPage));
        auto& page = m_fields->page;
        page = std::clamp(page, 0, pages - 1);

        if (list.empty()) {
            addLabel("No macros saved", { rx + 10.f, cy + 20.f }, .45f);
        }
        for (int i = 0; i < perPage; i++) {
            size_t idx = static_cast<size_t>(page) * perPage + i;
            if (idx >= list.size()) break;

            auto const& e = list[idx];
            std::string label = e.name.size() > 14 ? e.name.substr(0, 13) + "." : e.name;
            float y = cy + 62.f - i * 34.f;
            addBtn(label.c_str(), 140, menu_selector(BotPauseLayer::onEntry), { rx, y }, static_cast<int>(idx));
            addBtn("X", 28, menu_selector(BotPauseLayer::onDelete), { rx + 95.f, y }, static_cast<int>(idx));
        }

        float ny = cy - 78.f;
        addBtn("<", 30, menu_selector(BotPauseLayer::onPage), { rx - 45.f, ny }, -1);
        addBtn(">", 30, menu_selector(BotPauseLayer::onPage), { rx + 65.f, ny }, 1);
        addLabel(std::to_string(page + 1) + "/" + std::to_string(pages), { rx + 10.f, ny }, .5f);

        // pie
        addLabel(
            "Level: " + bot::levelName + "  |  " + std::to_string(bot::macro.size()) + " inputs",
            { cx, cy - 112.f }, .38f
        );

        this->addChild(panel);
    }

    void rebuild(float) { this->buildPanel(); }

    void doClose(float) {
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

    void onSpeed(CCObject*) {
        bot::speedIdx = (bot::speedIdx + 1) % bot::speedCount;
        bot::applySpeed();
        this->queueRebuild();
    }

    void onHitboxes(CCObject* sender) {
        bot::hitboxes = !static_cast<CCMenuItemToggler*>(sender)->isToggled();
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
};
