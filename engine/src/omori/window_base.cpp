#include "window_base.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "../core/regex.h"
#include "../gfx2d/text.h"
#include "../js/compile.h"
#include "../js/random.h"
#include "../js/vm.h"
#include "../rpg/data.h"
#include "../rpg/enemy.h"
#include "../rpg/evalctx.h"
#include "../rpg/game.h"
#include "../rpg/interpreter.h"
#include "../rpg/params.h"
#include "../rpg/party.h"
#include "../rpg/scene.h"
#include "ui_util.h"

namespace ui {

namespace {

const char ESC = '\x1b';

std::string esc_pattern(const char* body) { return std::string("\x1b") + body; }

struct Macro {
    std::string lower_code;
    std::string text;
};

const std::vector<Macro>& macros() {
    static std::vector<Macro> list;
    static bool built = false;
    if (built) return list;
    built = true;
    const Value& texts = data::root()["yanfly"]["MsgMacro"];
    static const struct {
        const char* plugin;
        int first, last;
    } ranges[] = {{"YEP_X_MessageMacros1", 1, 100},
                  {"YEP_X_MessageMacros2", 101, 200},
                  {"YEP_X_MessageMacros3", 201, 300},
                  {"YEP_X_MessageMacros4", 301, 400},
                  {"YEP_X_MessageMacros5", 401, 500}};
    for (auto& r : ranges) {
        if (data::plugin(r.plugin).is_null()) continue;
        for (int i = r.first; i <= r.last; i++) {
            std::string key = "Macro " + std::to_string(i) + " Name";
            const Value& pv = data::plugin(r.plugin)[key.c_str()];
            std::string name = pv.is_null() ? std::string("undefined") : data::js_string(pv);
            if ((int)list.size() <= i) list.resize((size_t)i + 1);
            list[(size_t)i].lower_code = rpg::to_lower(std::string(1, ESC) + name);
            list[(size_t)i].text = data::js_string(texts[std::to_string(i).c_str()]);
        }
    }
    return list;
}

std::string replace_ci_all(const std::string& text, const std::string& lower_needle, const std::string& with) {
    if (lower_needle.empty()) return text;
    std::string lower = rpg::to_lower(text);
    size_t pos = lower.find(lower_needle);
    if (pos == std::string::npos) return text;
    std::string out;
    size_t from = 0;
    while (pos != std::string::npos) {
        out += text.substr(from, pos - from);
        out += with;
        from = pos + lower_needle.size();
        pos = lower.find(lower_needle, from);
    }
    out += text.substr(from);
    return out;
}

js::Val window_js(WindowBase* w) {
    js::Val o = js::make_object();
    js::vm().put(o, js::atom("groupDigits"), js::make_native([](js::Vm&, const js::Val&, std::vector<js::Val>& a) {
                     return js::Val::str(group_digits(js::arg_num(a, 0)));
                 }));
    js::vm().put(o, js::atom("contents"), js::make_object());
    (void)w;
    return o;
}

std::string lunatic(WindowBase* w, const char* key, std::initializer_list<std::pair<const char*, js::Val>> vars) {
    const Value& src = data::root()["yanfly"]["Lunatic"]["Msg"][key];
    if (!src.is_str()) return "";
    auto prog = js::compile_cached(src.string());
    if (!prog) return "";
    rpg::JsEval e(window_js(w));
    for (auto& v : vars) e.set(v.first, v.second);
    e.set("text", js::Val::str(""));
    e.set("Imported", js::make_object());
    if (!e.run(prog.get(), "MESSAGE LUNATIC CODE")) return "";
    return e.get("text").to_str();
}

std::string first_actor_id_value() {
    auto m = rpg::game_party().members();
    if (m.empty()) js::vm().throw_type_error("Cannot read property 'actorId' of undefined");
    return js::vm().get(m[0]->to_js(), js::atom("actorId")).to_str();
}

}

WindowBase::WindowBase() { create_letter_effect_container(); }

WindowBase::~WindowBase() = default;

void WindowBase::initialize(double x, double y, double w, double h) {
    init_letter_effects();
    openness_type = standard_openness_type();
    load_windowskin();
    move(x, y, w, h);
    update_padding();
    update_back_opacity();
    update_tone();
    create_contents();
    opening_ = false;
    closing_ = false;
    dimmer_sprite_.reset();
}

double WindowBase::line_height() { return yparam_num("LineHeight", 36); }

std::string WindowBase::standard_font_face() { return yparam_str("DefaultFont"); }

int WindowBase::standard_font_size() { return (int)yparam_num("FontSize", 28); }
int WindowBase::standard_padding() { return (int)yparam_num("WindowPadding", 18); }
int WindowBase::text_padding() { return (int)yparam_num("TextPadding", 6); }
int WindowBase::standard_back_opacity() { return 255; }

void WindowBase::load_windowskin() { set_windowskin(load_system("Window")); }
void WindowBase::update_padding() { set_padding(standard_padding()); }
void WindowBase::update_back_opacity() { set_back_opacity(standard_back_opacity()); }
double WindowBase::contents_width() { return width() - standard_padding() * 2; }
double WindowBase::contents_height() { return height() - standard_padding() * 2; }
double WindowBase::fitting_height(int lines) { return lines * line_height() + standard_padding() * 2; }

void WindowBase::update_tone() {
    rpg::GameSystem& s = rpg::game_system();
    set_tone((int)s.window_tone(0), (int)s.window_tone(1), (int)s.window_tone(2));
}

void WindowBase::create_contents() {
    set_contents(g2d::Bitmap::create((int)std::max(0.0, contents_width()), (int)std::max(0.0, contents_height())));
    reset_font_settings();
}

void WindowBase::reset_font_settings() {
    g2d::Bitmap& b = c();
    b.font_face = standard_font_face();
    b.font_size = standard_font_size();
    reset_text_color();
    b.font_bold = false;
    b.font_italic = false;
    b.outline_color = "rgba(0, 0, 0, 0.5)";
    b.outline_width = rpg::game_objects_exist() ? rpg::game_system().msg_font_outline : 4;
}

void WindowBase::reset_text_color() { change_text_color(normal_color()); }

void WindowBase::update() {
    g2d::Window::update();
    update_tone();
    update_open();
    update_close();
    update_background_dimmer();
    update_letter_effects();
}

void WindowBase::update_open() {
    if (opening_) {
        set_openness(openness() + open_speed());
        if (is_open()) opening_ = false;
    }
}

void WindowBase::update_close() {
    if (closing_) {
        set_openness(openness() - close_speed());
        if (is_closed()) closing_ = false;
    }
}

void WindowBase::open() {
    if (!is_open()) opening_ = true;
    closing_ = false;
}

void WindowBase::close() {
    if (!is_closed()) closing_ = true;
    opening_ = false;
}

std::string WindowBase::text_color(int n) {
    g2d::Bitmap* skin = windowskin().get();
    if (skin != color_cache_skin_) {
        color_cache_.clear();
        color_cache_skin_ = skin;
    }
    auto it = color_cache_.find(n);
    if (it != color_cache_.end()) return it->second;
    int px = 96 + (n % 8) * 12 + 6;
    int py = 144 + (int)std::floor(n / 8.0) * 12 + 6;
    std::string c = skin ? skin->get_pixel(px, py) : std::string("#ffffff");
    color_cache_[n] = c;
    return c;
}

std::string WindowBase::normal_color() { return text_color((int)yparam_num("ColorNormal", 0)); }
std::string WindowBase::system_color() { return text_color((int)yparam_num("ColorSystem", 16)); }
std::string WindowBase::crisis_color() { return text_color((int)yparam_num("ColorCrisis", 17)); }
std::string WindowBase::death_color() { return text_color((int)yparam_num("ColorDeath", 18)); }
std::string WindowBase::gauge_back_color() { return text_color((int)yparam_num("ColorGaugeBack", 19)); }
std::string WindowBase::hp_gauge_color1() { return text_color((int)yparam_num("ColorHpGauge1", 20)); }
std::string WindowBase::hp_gauge_color2() { return text_color((int)yparam_num("ColorHpGauge2", 21)); }
std::string WindowBase::mp_gauge_color1() { return text_color((int)yparam_num("ColorMpGauge1", 22)); }
std::string WindowBase::mp_gauge_color2() { return text_color((int)yparam_num("ColorMpGauge2", 23)); }
std::string WindowBase::mp_cost_color() { return text_color((int)yparam_num("ColorMpCost", 23)); }
std::string WindowBase::power_up_color() { return text_color((int)yparam_num("ColorPowerUp", 24)); }
std::string WindowBase::power_down_color() { return text_color((int)yparam_num("ColorPowerDown", 25)); }
std::string WindowBase::tp_gauge_color1() { return text_color((int)yparam_num("ColorTpGauge1", 28)); }
std::string WindowBase::tp_gauge_color2() { return text_color((int)yparam_num("ColorTpGauge2", 29)); }
std::string WindowBase::tp_cost_color() { return text_color((int)yparam_num("ColorTpCost", 29)); }

std::string WindowBase::pending_color() {
    g2d::Bitmap* skin = windowskin().get();
    return skin ? skin->get_pixel(120, 120) : std::string("#ffffff");
}

void WindowBase::change_text_color(const std::string& col) { c().text_color = col; }
void WindowBase::change_paint_opacity(bool enabled) { c().paint_opacity = enabled ? 255 : translucent_opacity(); }

void WindowBase::draw_text(const std::string& text, double x, double y, double max_width, const std::string& align) {
    c().draw_text(text, x, y, max_width, line_height(), align);
}

double WindowBase::text_width(const std::string& text) { return c().measure_text_width(text); }

double WindowBase::draw_text_ex(const std::string& text_in, double x, double y) {
    if (text_in.empty()) return base_draw_text_ex(text_in, x, y);
    return base_draw_text_ex(replace_database_codes(text_in), x, y);
}

double WindowBase::base_draw_text_ex(const std::string& text, double x, double y) {
    if (text.empty()) return 0;
    reset_font_settings();
    TextState ts;
    ts.x = x;
    ts.y = y;
    ts.left = x;
    ts.text = convert_escape_characters(text);
    ts.height = calc_text_height(ts, false);
    while (ts.index < ts.text.size()) process_character(ts);
    return ts.x - x;
}

std::string WindowBase::convert_macro_text(std::string text) {
    const Value& mtexts = data::root()["yanfly"]["MsgMacro"];
    text = re::replace_fn("\\\\M\\[(\\d+)\\]", "gi", text, [&](const re::Groups& g) { return data::js_string(mtexts[g[1].c_str()]); });
    text = re::replace_fn("\\\\M\\[(.*?)\\]", "gi", text, [&](const re::Groups& g) {
        const Value& ref = data::root()["yanfly"]["MsgMacroRef"][rpg::to_upper(g[1]).c_str()];
        if (ref.is_null()) return std::string("undefined");
        return data::js_string(mtexts[data::js_string(ref).c_str()]);
    });
    for (auto& ch : text) {
        if (ch == '\\') ch = ESC;
    }
    const std::string placeholder = "YANFLYMACROTESTREVERSEDOUBLESLASH";
    std::string dbl(2, ESC);
    std::string tmp;
    for (size_t i = 0; i < text.size();) {
        if (text.compare(i, 2, dbl) == 0) {
            tmp += placeholder;
            i += 2;
        } else {
            tmp += text[i++];
        }
    }
    text = tmp;
    if (text.find(ESC) != std::string::npos) {
        const auto& list = macros();
        std::string lower;
        bool after_esc[256];
        auto rescan = [&] {
            lower = rpg::to_lower(text);
            std::fill(after_esc, after_esc + 256, false);
            for (size_t p = lower.find(ESC); p != std::string::npos && p + 1 < lower.size(); p = lower.find(ESC, p + 1))
                after_esc[(unsigned char)lower[p + 1]] = true;
        };
        rescan();
        for (size_t i = 1; i < list.size(); i++) {
            const std::string& code = list[i].lower_code;
            if (code.empty() || (code.size() > 1 && !after_esc[(unsigned char)code[1]]) || lower.find(code) == std::string::npos) continue;
            text = replace_ci_all(text, code, list[i].text);
            rescan();
        }
    }
    size_t p;
    while ((p = text.find(placeholder)) != std::string::npos) text.replace(p, placeholder.size(), "\\\\");
    return text;
}

std::string WindowBase::convert_escape_characters(std::string text) {
    auto var_value = [](const re::Groups& g) { return rpg::game_variables().value(g.num(1)).to_str(); };
    text = re::replace_fn("\\\\V\\[(\\d+)\\]", "gi", text, var_value);
    text = re::replace_fn("\\\\V\\[(\\d+)\\]", "gi", text, var_value);
    text = re::replace_fn("\\\\N\\[(\\d+)\\]", "gi", text, [this](const re::Groups& g) { return actor_name(g.num(1)); });
    text = re::replace_fn("\\\\P\\[(\\d+)\\]", "gi", text, [this](const re::Groups& g) { return party_member_name(g.num(1)); });
    text = convert_macro_text(text);
    text = set_word_wrap(text);
    for (auto& ch : text) {
        if (ch == '\\') ch = ESC;
    }
    {
        std::string out;
        for (size_t i = 0; i < text.size();) {
            if (text[i] == ESC && i + 1 < text.size() && text[i + 1] == ESC) {
                out += '\\';
                i += 2;
            } else {
                out += text[i++];
            }
        }
        text = out;
    }
    text = re::replace_fn((esc_pattern("V\\[(\\d+)\\]")).c_str(), "gi", text, var_value);
    text = re::replace_fn((esc_pattern("V\\[(\\d+)\\]")).c_str(), "gi", text, var_value);
    text = re::replace_fn((esc_pattern("SV\\[(\\d+)\\]")).c_str(), "gi", text, [](const re::Groups& g) {
        js::Val v = rpg::game_map().interpreter->self_variable_value(js::Val::num(g.num(1)));
        return v.truthy() ? v.to_str() : std::string();
    });
    text = re::replace_fn((esc_pattern("N\\[(\\d+)\\]")).c_str(), "gi", text, [this](const re::Groups& g) { return actor_name(g.num(1)); });
    text = re::replace_fn((esc_pattern("P\\[(\\d+)\\]")).c_str(), "gi", text, [this](const re::Groups& g) { return party_member_name(g.num(1)); });
    text = re::replace_all(esc_pattern("G").c_str(), "gi", text, data::system()["currencyUnit"].string());
    text = convert_extra_escape_characters(text);
    rpg::ItemRef item = rpg::game_party().last_gained_item();
    if (item.valid()) {
        double amount = rpg::game_party().last_item_data.amount;
        std::string name = item.data()["name"].string();
        std::string rep;
        if (amount > 0) rep = amount == 1 ? name : name + " x" + number_text(amount);
        else rep = amount < -1 ? name + " x" + number_text(std::abs(amount)) : name;
        text = re::replace_all(esc_pattern("itemget").c_str(), "gi", text, rep);
    } else {
        text = re::replace_all(esc_pattern("itemget").c_str(), "gi", text, "NULL ID #");
    }
    return text;
}

std::string WindowBase::convert_extra_escape_characters(std::string text) {
    auto colored = [this](const Value& item, const rpg::ItemRef& ref) {
        if (!item.is_obj()) js::vm().throw_type_error("Cannot read property 'name' of undefined");
        return std::string(1, ESC) + "C[" + std::to_string(item_color_index(ref)) + "]" + item["name"].string() + ESC + "c[0]";
    };
    text = re::replace_fn(esc_pattern("NI\\[(\\d+)\\]").c_str(), "gi", text,
                          [&](const re::Groups& g) { return colored(data::items()[g.num(1)], rpg::ItemRef::item(g.num(1))); });
    text = re::replace_fn(esc_pattern("NW\\[(\\d+)\\]").c_str(), "gi", text,
                          [&](const re::Groups& g) { return colored(data::weapons()[g.num(1)], rpg::ItemRef::weapon(g.num(1))); });
    text = re::replace_fn(esc_pattern("NA\\[(\\d+)\\]").c_str(), "gi", text,
                          [&](const re::Groups& g) { return colored(data::armors()[g.num(1)], rpg::ItemRef::armor(g.num(1))); });
    static const struct {
        const char* code;
        const char* key;
        bool enemy;
    } lunatic_codes[] = {{"QI", "TcQI", false},       {"QW", "TcQW", false},       {"QA", "TcQA", false},       {"ALVL", "TcALvl", false},
                         {"AMHP", "TcAMhp", false},   {"AHP", "TcAHp", false},     {"AHP%", "TcAHpp", false},   {"AMMP", "TcAMmp", false},
                         {"AMP", "TcAMp", false},     {"AMP%", "TcAMpp", false},   {"AMTP", "TcATmp", false},   {"ATP", "TcATp", false},
                         {"ATP%", "TcATpp", false},   {"AATK", "TcAatk", false},   {"ADEF", "TcAdef", false},   {"AMAT", "TcAmat", false},
                         {"AMDF", "TcAmdf", false},   {"AAGI", "TcAagi", false},   {"ALUK", "TcAluk", false},   {"AHIT", "TcAhit", false},
                         {"AEVA", "TcAeva", false},   {"ACRI", "TcAcri", false},   {"ACEV", "TcAcev", false},   {"AMEV", "TcAmev", false},
                         {"AMRF", "TcAmrf", false},   {"ACNT", "TcAcnt", false},   {"AHRG", "TcAhrg", false},   {"AMRG", "TcAmrg", false},
                         {"ATRG", "TcAtrg", false},   {"ATGR", "TcAtgr", false},   {"AGRD", "TcAgrd", false},   {"AREC", "TcArec", false},
                         {"APHA", "TcApha", false},   {"AMCR", "TcAmcr", false},   {"ATCR", "TcAtcr", false},   {"APDR", "TcApdr", false},
                         {"AMDR", "TcAmdr", false},   {"AFDR", "TcAfdr", false},   {"AEXR", "TcAexr", false},   {"ELVL", "TcELvl", true},
                         {"EMHP", "TcEMhp", true},    {"EHP", "TcEHp", true},      {"EHP%", "TcEHpp", true},    {"EMMP", "TcEMmp", true},
                         {"EMP", "TcEMp", true},      {"EMP%", "TcEMpp", true},    {"EMTP", "TcETmp", true},    {"ETP", "TcETp", true},
                         {"ETP%", "TcETpp", true},    {"EATK", "TcEatk", true},    {"EDEF", "TcEdef", true},    {"EMAT", "TcEmat", true},
                         {"EMDF", "TcEmdf", true},    {"EAGI", "TcEagi", true},    {"ELUK", "TcEluk", true},    {"EEXP", "TcEexp", true},
                         {"EGOLD", "TcEgold", true},  {"EHIT", "TcEhit", true},    {"EEVA", "TcEeva", true},    {"ECRI", "TcEcri", true},
                         {"ECEV", "TcEcev", true},    {"EMEV", "TcEmev", true},    {"EMRF", "TcEmrf", true},    {"ECNT", "TcEcnt", true},
                         {"EHRG", "TcEhrg", true},    {"EMRG", "TcEmrg", true},    {"ETRG", "TcEtrg", true},    {"ETGR", "TcEtgr", true},
                         {"EGRD", "TcEgrd", true},    {"EREC", "TcErec", true},    {"EPHA", "TcEpha", true},    {"EMCR", "TcEmcr", true},
                         {"ETCR", "TcEtcr", true},    {"EPDR", "TcEpdr", true},    {"EMDR", "TcEmdr", true},    {"EFDR", "TcEfdr", true},
                         {"EEXR", "TcEexr", true}};
    static const std::vector<std::string> needles = [] {
        std::vector<std::string> out;
        for (auto& lc : lunatic_codes) out.push_back(rpg::to_lower(std::string(1, ESC) + lc.code + "["));
        return out;
    }();
    bool any_esc = text.find(ESC) != std::string::npos;
    std::string lower;
    bool lower_stale = true;
    auto run_codes = [&](bool enemy, int from, int to) {
        if (!any_esc) return;
        if (enemy && !rpg::party_in_battle()) return;
        for (int i = from; i < to; i++) {
            auto& lc = lunatic_codes[i];
            if (lower_stale) {
                lower = rpg::to_lower(text);
                lower_stale = false;
            }
            if (lower.find(needles[(size_t)i]) == std::string::npos) continue;
            lower_stale = true;
            std::string pat = std::string(1, ESC) + lc.code + "\\[(\\d+)\\]";
            text = re::replace_fn(pat.c_str(), "gi", text, [&](const re::Groups& g) {
                js::Val x;
                if (lc.enemy) {
                    double n = js::string_to_number(g[1]) - 1;
                    double maxi = (double)rpg::game_troop().members().size() - 1;
                    x = js::Val::num(std::min(std::max(n, 0.0), maxi));
                } else if (i < 3) {
                    x = js::string_to_number(g[1]) <= 0 ? js::Val::num(1) : js::Val::str(g[1]);
                } else {
                    x = js::string_to_number(g[1]) <= 0 ? js::Val::str(first_actor_id_value()) : js::Val::str(g[1]);
                }
                return lunatic(this, lc.key, {{"x", x}});
            });
        }
    };
    run_codes(false, 0, 3);
    run_codes(false, 3, 39);
    run_codes(true, 39, (int)(sizeof(lunatic_codes) / sizeof(lunatic_codes[0])));
    text = re::replace_fn(esc_pattern("PLAYTIME").c_str(), "gi", text, [](const re::Groups&) { return rpg::game_system().playtime_text(); });
    text = re::replace_fn(esc_pattern("MAP\\[(\\d+)\\]").c_str(), "gi", text, [](const re::Groups& g) {
        int id = g.num(1);
        if (id <= 0) id = rpg::game_map().map_id();
        return data::map_infos()[id]["name"].string();
    });
    text = re::replace_fn(esc_pattern("EN\\[(\\d+)\\]").c_str(), "gi", text, [](const re::Groups& g) {
        int id = g.num(1);
        if (id <= 0) return std::string();
        return data::enemies()[id]["name"].string();
    });
    text = re::replace_fn(esc_pattern("ET\\[(\\d+)\\]").c_str(), "gi", text, [](const re::Groups& g) {
        int index = std::max(1, g.num(1) - 1);
        auto all = rpg::game_troop().unit_all_members();
        return index < (int)all.size() && all[(size_t)index] ? all[(size_t)index]->name() : std::string();
    });
    text = re::replace_all(esc_pattern("FR").c_str(), "gi", text, std::string(1, ESC) + "MSGCORE[0]");
    text = re::replace_all(esc_pattern("FB").c_str(), "gi", text, std::string(1, ESC) + "MSGCORE[1]");
    text = re::replace_all(esc_pattern("FI").c_str(), "gi", text, std::string(1, ESC) + "MSGCORE[2]");
    auto actor_at = [](int n) -> rpg::GameActor* { return n >= 1 ? rpg::game_actors().actor(n) : nullptr; };
    auto party_at = [](int n) -> rpg::GameActor* {
        auto m = rpg::game_party().members();
        return n >= 1 && n - 1 < (int)m.size() ? m[(size_t)(n - 1)] : nullptr;
    };
    text = re::replace_fn(esc_pattern("AC\\[(\\d+)\\]").c_str(), "gi", text, [&](const re::Groups& g) {
        rpg::GameActor* a = actor_at(g.num(1));
        return a ? a->current_class()["name"].string() : std::string();
    });
    text = re::replace_fn(esc_pattern("AN\\[(\\d+)\\]").c_str(), "gi", text, [&](const re::Groups& g) {
        rpg::GameActor* a = actor_at(g.num(1));
        return a ? a->nickname() : std::string();
    });
    text = re::replace_fn(esc_pattern("PC\\[(\\d+)\\]").c_str(), "gi", text, [&](const re::Groups& g) {
        rpg::GameActor* a = party_at(g.num(1));
        return a ? a->current_class()["name"].string() : std::string();
    });
    text = re::replace_fn(esc_pattern("PN\\[(\\d+)\\]").c_str(), "gi", text, [&](const re::Groups& g) {
        rpg::GameActor* a = party_at(g.num(1));
        return a ? a->nickname() : std::string();
    });
    auto name_of = [](const Value& db, int id) {
        const Value& v = db[id];
        if (!v.is_obj()) js::vm().throw_type_error("Cannot read property 'name' of null");
        return v["name"].string();
    };
    text = re::replace_fn(esc_pattern("NC\\[(\\d+)\\]").c_str(), "gi", text, [&](const re::Groups& g) { return name_of(data::classes(), g.num(1)); });
    text = re::replace_fn(esc_pattern("NI\\[(\\d+)\\]").c_str(), "gi", text, [&](const re::Groups& g) { return name_of(data::items(), g.num(1)); });
    text = re::replace_fn(esc_pattern("NW\\[(\\d+)\\]").c_str(), "gi", text, [&](const re::Groups& g) { return name_of(data::weapons(), g.num(1)); });
    text = re::replace_fn(esc_pattern("NA\\[(\\d+)\\]").c_str(), "gi", text, [&](const re::Groups& g) { return name_of(data::armors(), g.num(1)); });
    text = re::replace_fn(esc_pattern("NE\\[(\\d+)\\]").c_str(), "gi", text, [&](const re::Groups& g) { return name_of(data::enemies(), g.num(1)); });
    text = re::replace_fn(esc_pattern("NS\\[(\\d+)\\]").c_str(), "gi", text, [&](const re::Groups& g) { return name_of(data::skills(), g.num(1)); });
    text = re::replace_fn(esc_pattern("NT\\[(\\d+)\\]").c_str(), "gi", text, [&](const re::Groups& g) { return name_of(data::states(), g.num(1)); });
    auto icon_item = [&](const Value& db, int id) {
        const Value& v = db[id];
        if (!v.is_obj()) js::vm().throw_type_error("Cannot read property 'iconIndex' of null");
        return std::string(1, ESC) + "I[" + data::js_string(v["iconIndex"]) + "]" + v["name"].string();
    };
    text = re::replace_fn(esc_pattern("II\\[(\\d+)\\]").c_str(), "gi", text, [&](const re::Groups& g) { return icon_item(data::items(), g.num(1)); });
    text = re::replace_fn(esc_pattern("IW\\[(\\d+)\\]").c_str(), "gi", text, [&](const re::Groups& g) { return icon_item(data::weapons(), g.num(1)); });
    text = re::replace_fn(esc_pattern("IA\\[(\\d+)\\]").c_str(), "gi", text, [&](const re::Groups& g) { return icon_item(data::armors(), g.num(1)); });
    text = re::replace_fn(esc_pattern("IS\\[(\\d+)\\]").c_str(), "gi", text, [&](const re::Groups& g) { return icon_item(data::skills(), g.num(1)); });
    text = re::replace_fn(esc_pattern("IT\\[(\\d+)\\]").c_str(), "gi", text, [&](const re::Groups& g) { return icon_item(data::states(), g.num(1)); });
    text = re::replace_fn(esc_pattern("DG\\[(\\d+)\\]").c_str(), "gi", text, [](const re::Groups& g) { return group_digits(rpg::js_parse_int(g[1])); });
    if (any_esc) {
        static const char* cm_keys[] = {"TcCm0", "TcCm1", "TcCm2", "TcCm3", "TcCm4", "TcCm5", "TcCm6", "TcCm7", "TcCm8", "TcCm9"};
        for (int k = 0; k < 10; k++) {
            std::string pat = std::string(1, ESC) + "COMPARE" + (k == 0 ? std::string() : std::to_string(k)) + "\\<(.*?):(.*?)\\>";
            text = re::replace_fn(pat.c_str(), "gi", text, [&](const re::Groups& g) {
                return lunatic(this, cm_keys[k], {{"x", js::Val::num(js::string_to_number(g[1]))}, {"y", js::Val::num(js::string_to_number(g[2]))}});
            });
        }
        text = re::replace_fn(esc_pattern("CASESWITCH\\{(.*?)\\?(.*?):(.*?)\\}").c_str(), "gi", text, [&](const re::Groups& g) {
            bool s = rpg::game_switches().value((int)js::string_to_number(g[1]));
            return lunatic(this, "TcCSwitch", {{"s", js::Val::boolean(s)}, {"x", js::Val::str(g[2])}, {"y", js::Val::str(g[3])}});
        });
        text = re::replace_fn(esc_pattern("CASEEVAL\\{(.*?)\\?(.*?):(.*?)\\}").c_str(), "gi", text, [&](const re::Groups& g) {
            js::Val e;
            auto prog = js::compile_cached(g[1]);
            if (prog) {
                rpg::JsEval ev(window_js(this));
                if (ev.run(prog.get(), "CASEEVAL")) e = ev.completion();
            }
            return lunatic(this, "TcCaseEval", {{"e", e}, {"x", js::Val::str(g[2])}, {"y", js::Val::str(g[3])}});
        });
    }
    return text;
}

std::string WindowBase::actor_name(int n) {
    rpg::GameActor* a = n >= 1 ? rpg::game_actors().actor(n) : nullptr;
    return a ? a->name() : std::string();
}

std::string WindowBase::party_member_name(int n) {
    auto m = rpg::game_party().members();
    rpg::GameActor* a = n >= 1 && n - 1 < (int)m.size() ? m[(size_t)(n - 1)] : nullptr;
    return a ? a->name() : std::string();
}

void WindowBase::process_character(TextState& ts) {
    char ch = ts.text[ts.index];
    if (ch == '\n') process_new_line(ts);
    else if (ch == '\f') process_new_page(ts);
    else if (ch == ESC) process_escape_character(obtain_escape_code(ts), ts);
    else process_normal_character(ts);
}

void WindowBase::process_normal_character(TextState& ts) {
    if (can_create_letter_effect_sprite()) {
        create_letter_effect_sprite(ts);
        c().text_blocked = true;
    }
    if (check_word_wrap(ts)) {
        process_new_line(ts);
        return;
    }
    base_process_normal_character(ts);
}

void WindowBase::base_process_normal_character(TextState& ts) {
    size_t i = ts.index;
    g2d::decode_utf8(ts.text, i);
    std::string ch = ts.text.substr(ts.index, i - ts.index);
    ts.index = i;
    double w = text_width(ch);
    c().draw_text(ch, ts.x, ts.y, w * 2, ts.height, "left");
    ts.x += w;
}

void WindowBase::process_new_line(TextState& ts) {
    ts.x = ts.left;
    ts.y += ts.height;
    ts.height = calc_text_height(ts, false);
    ts.index++;
}

void WindowBase::process_new_page(TextState& ts) { ts.index++; }

std::string WindowBase::obtain_escape_code(TextState& ts) {
    ts.index++;
    if (ts.index >= ts.text.size()) return "";
    char ch = ts.text[ts.index];
    if (std::strchr("$.|^!><{}\\", ch) && ch != 0) {
        ts.index++;
        return std::string(1, ch);
    }
    size_t j = ts.index;
    while (j < ts.text.size() && std::isalpha((unsigned char)ts.text[j])) j++;
    std::string code = ts.text.substr(ts.index, j - ts.index);
    ts.index = j;
    return rpg::to_upper(code);
}

js::Val WindowBase::obtain_escape_param(TextState& ts) {
    re::Groups g;
    std::string rest = ts.text.substr(ts.index);
    if (re::search("^\\[\\d+\\]", "", rest, &g)) {
        ts.index += g.length;
        return js::Val::num(rpg::js_parse_int(g[0].substr(1)));
    }
    return js::Val::str("");
}

int WindowBase::obtain_escape_param_int(TextState& ts) { return obtain_escape_param(ts).to_int(); }

std::string WindowBase::obtain_escape_string(TextState& ts) {
    re::Groups g;
    std::string rest = ts.text.substr(ts.index);
    if (re::search("^\\<(.*?)\\>", "", rest, &g)) {
        ts.index += g.length;
        return g[0].substr(1, g[0].size() - 2);
    }
    return "";
}

std::string WindowBase::obtain_color_string(TextState& ts) {
    re::Groups g;
    std::string rest = ts.text.substr(ts.index);
    if (re::search("^\\[(.*?)\\]", "", rest, &g)) {
        ts.index += g.length;
        return "#" + g[0].substr(1, g[0].size() - 2);
    }
    return "#ffffff";
}

std::vector<std::string> WindowBase::obtain_escape_params_array(TextState& ts) {
    re::Groups g;
    std::string rest = ts.text.substr(ts.index);
    std::vector<std::string> out;
    if (re::search("^\\[(\\d+,\\d+)]", "", rest, &g)) {
        ts.index += g.length;
        std::string s = g[1];
        size_t comma = s.find(',');
        out.push_back(s.substr(0, comma));
        out.push_back(s.substr(comma + 1));
    }
    return out;
}

void WindowBase::process_escape_character(const std::string& code, TextState& ts) {
    if (!is_ignoring_letter_effect_escape_codes()) {
        if (code == "LETAG") {
            letter_effect_tag_ = obtain_escape_param_int(ts);
        } else if (code == "LESTP") {
            int tag = obtain_escape_param_int(ts);
            for (auto& e : letter_effects_) {
                if (e.tag == tag) e.active = false;
            }
        } else if (code == "LESYNCH") {
            has_letter_effect_synch_ = true;
            letter_effect_synch_index_ = obtain_escape_param_int(ts);
        } else if (code == "LETMR") {
            int time = obtain_escape_param_int(ts);
            if (time == 0) has_letter_effect_timer_ = false;
            else {
                has_letter_effect_timer_ = true;
                letter_effect_timer_ = time;
            }
        } else if (code == "LECLEAR") {
            for (auto& e : letter_effects_) {
                if (e.end_index == kNoEnd) e.end_index = ts.index;
            }
            ts.index++;
        } else if (code == "SINV" || code == "SINH" || code == "RAINBOW") {
            setup_letter_effect(code, obtain_escape_param_int(ts), ts);
        } else if (code == "QUAKE") {
            setup_letter_effect("SHAKE", obtain_escape_param_int(ts), ts);
        }
    }
    if (code == "HC") {
        change_text_color(rpg::to_lower(obtain_color_string(ts)));
    } else if (code == "MSGCORE") {
        int id = obtain_escape_param_int(ts);
        if (id == 0) {
            rpg::GameSystem& s = rpg::game_system();
            s.msg_font_name = yparam_str("MSGFontName");
            s.msg_font_size = (int)yparam_num("MSGFontSize", 28);
            s.msg_font_outline = (int)yparam_num("MSGFontOutline", 4);
            reset_font_settings();
        }
        if (id == 1) c().font_bold = !c().font_bold;
        if (id == 2) c().font_italic = !c().font_italic;
    } else if (code == "FS") {
        int size = obtain_escape_param_int(ts);
        c().font_size = size;
        if (yparam_bool("MSGFontMaintain")) rpg::game_system().msg_font_size = size;
    } else if (code == "FN") {
        std::string name = obtain_escape_string(ts);
        c().font_face = name;
        if (yparam_bool("MSGFontMaintain")) rpg::game_system().msg_font_name = name;
    } else if (code == "OC") {
        c().outline_color = text_color(obtain_escape_param_int(ts));
    } else if (code == "OW") {
        c().outline_width = obtain_escape_param_int(ts);
    } else if (code == "PX") {
        ts.x = obtain_escape_param_int(ts);
    } else if (code == "PY") {
        ts.y = obtain_escape_param_int(ts);
    } else {
        base_process_escape_character(code, ts);
    }
    if (code == "DII") {
        re::Groups g;
        std::string rest = ts.text.substr(ts.index);
        if (!re::search("^\\[(.+)\\]", "", rest, &g)) js::vm().throw_type_error("Cannot read property '0' of null");
        ts.index += g[0].size();
        process_draw_input_icon(g[1], ts);
    }
}

void WindowBase::base_process_escape_character(const std::string& code, TextState& ts) {
    if (code == "C") change_text_color(text_color(obtain_escape_param_int(ts)));
    else if (code == "I") process_draw_icon(obtain_escape_param_int(ts), ts);
    else if (code == "{") make_font_bigger();
    else if (code == "}") make_font_smaller();
}

void WindowBase::process_draw_icon(int icon_index, TextState& ts) {
    const Value& off = lang_data("XX_BLUE.Window_Base")["processDrawIconOffset"];
    draw_icon(icon_index, ts.x + 2 + off[0].num(), ts.y + 2 + off[1].num());
    ts.x += kIconWidth + 4;
}

void WindowBase::process_draw_input_icon(const std::string& input, TextState& ts) {
    const Value& off = lang_data("XX_BLUE.Window_Base")["processDrawInputIconOffset"];
    std::string key = input_key_code(input);
    IconRect up, down;
    key_icon_rects(key, up, down);
    ts.x += 4;
    draw_aligned_key_icon(&c(), key, ts.x + off[0].num(), ts.y + off[1].num(), up.w, ts.height);
    ts.x += up.w + 4;
}

void WindowBase::make_font_bigger() {
    double change = yparam_num("MSGFontSizeChange", 12);
    c().font_size = (int)std::min(c().font_size + change, yparam_num("MSGFontChangeMax", 96));
}

void WindowBase::make_font_smaller() {
    double change = yparam_num("MSGFontSizeChange", 12);
    c().font_size = (int)std::max(c().font_size - change, yparam_num("MSGFontChangeMin", 12));
}

double WindowBase::calc_text_height(const TextState& ts, bool all) {
    int last = c().font_size;
    double h = 0;
    std::string rest = ts.text.substr(std::min(ts.index, ts.text.size()));
    std::vector<std::string> lines;
    size_t start = 0;
    while (true) {
        size_t nl = rest.find('\n', start);
        lines.push_back(rest.substr(start, nl == std::string::npos ? std::string::npos : nl - start));
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    size_t max_lines = all ? lines.size() : 1;
    for (size_t i = 0; i < max_lines; i++) {
        int max_size = c().font_size;
        const std::string& line = lines[i];
        for (size_t k = 0; k + 1 < line.size(); k++) {
            if (line[k] != ESC) continue;
            if (line[k + 1] == '{') make_font_bigger();
            else if (line[k + 1] == '}') make_font_smaller();
            else continue;
            max_size = std::max(max_size, c().font_size);
        }
        h += max_size + 8;
    }
    c().font_size = last;
    return h;
}

void WindowBase::prefetch_text_fonts(const std::string& text) {
    int size = c().font_size;
    g2d::prefetch_font(c().font_face, size, c().outline_width > 0);
    for (size_t k = 0; k + 1 < text.size(); k++) {
        if (text[k] != ESC) continue;
        char n = text[k + 1];
        if (n == '{') {
            make_font_bigger();
        } else if (n == '}') {
            make_font_smaller();
        } else if (k + 3 < text.size() && (n == 'F' || n == 'f') && (text[k + 2] == 'S' || text[k + 2] == 's') && text[k + 3] == '[') {
            c().font_size = std::atoi(text.c_str() + k + 4);
        } else {
            continue;
        }
        g2d::prefetch_font(c().font_face, c().font_size, c().outline_width > 0);
    }
    c().font_size = size;
}

void WindowBase::draw_icon(int icon_index, double x, double y) {
    g2d::BitmapPtr bmp = load_system("IconSet");
    double sx = (icon_index % 16) * kIconWidth;
    double sy = std::floor(icon_index / 16.0) * kIconHeight;
    c().blt(bmp.get(), sx, sy, kIconWidth, kIconHeight, x, y);
}

void WindowBase::draw_face(const std::string& face_name, int face_index, double x, double y, double width, double height) {
    if (width == 0) width = kFaceWidth;
    if (height == 0) height = kFaceHeight;
    g2d::BitmapPtr bmp = load_face(face_name);
    double pw = kFaceWidth, ph = kFaceHeight;
    double sw = std::min(width, pw), sh = std::min(height, ph);
    double dx = std::floor(x + std::max(width - pw, 0.0) / 2);
    double dy = std::floor(y + std::max(height - ph, 0.0) / 2);
    double sx = (face_index % 4) * pw + (pw - sw) / 2;
    double sy = std::floor(face_index / 4.0) * ph + (ph - sh) / 2;
    c().blt(bmp.get(), sx, sy, sw, sh, dx, dy);
}

void WindowBase::draw_character(const std::string& name, int index, double x, double y) {
    g2d::BitmapPtr bmp = load_character(name);
    bool big = is_big_character(name);
    double pw = bmp->width() / (big ? 3.0 : 12.0);
    double ph = bmp->height() / (big ? 4.0 : 8.0);
    double sx = ((index % 4) * 3 + 1) * pw;
    double sy = std::floor(index / 4.0) * 4 * ph;
    c().blt(bmp.get(), sx, sy, pw, ph, x - pw / 2, y - ph);
}

double WindowBase::gauge_height() { return yparam_num("GaugeHeight", 18); }

void WindowBase::draw_gauge(double dx, double dy, double dw, double rate, const std::string& c1, const std::string& c2) {
    std::string c3 = gauge_back_color();
    double fill_w = std::min(std::max(std::floor(dw * rate), 0.0), dw);
    double gh = gauge_height();
    double gy = dy + line_height() - gh - 2;
    if (yparam_bool("GaugeOutline")) {
        c().fill_rect(dx, gy - 1, dw, gh, c3);
        fill_w = std::max(fill_w - 2, 0.0);
        gh -= 2;
        dx += 1;
    } else {
        fill_w = std::floor(dw * rate);
        c().fill_rect(dx, gy, dw, gh, c3);
    }
    c().gradient_fill_rect(dx, gy, fill_w, gh, c1, c2, false);
}

std::string WindowBase::hp_color(rpg::GameBattler* a) {
    if (a->is_dead()) return death_color();
    if (a->is_dying()) return crisis_color();
    return normal_color();
}

std::string WindowBase::mp_color(rpg::GameBattler*) { return normal_color(); }
std::string WindowBase::tp_color(rpg::GameBattler*) { return normal_color(); }

void WindowBase::draw_actor_character(rpg::GameActor* a, double x, double y) { draw_character(a->character_name(), a->character_index(), x, y); }

void WindowBase::draw_actor_face(rpg::GameActor* a, double x, double y, double w, double h) { draw_face(a->face_name(), a->face_index(), x, y, w, h); }

void WindowBase::draw_actor_name(rpg::GameBattler* a, double x, double y, double w) {
    if (w == 0) w = 168;
    change_text_color(hp_color(a));
    draw_text(replace_database_codes(a->name()), x, y, w);
}

void WindowBase::draw_actor_class(rpg::GameActor* a, double x, double y, double w) {
    if (w == 0) w = 168;
    reset_text_color();
    draw_text(replace_database_codes(a->current_class()["name"].string()), x, y, w);
}

void WindowBase::draw_actor_nickname(rpg::GameActor* a, double x, double y, double w) {
    if (w == 0) w = 270;
    reset_text_color();
    draw_text(replace_database_codes(a->nickname()), x, y, w);
}

void WindowBase::draw_actor_level(rpg::GameActor* a, double x, double y) {
    change_text_color(system_color());
    std::string level_a = data::system()["terms"]["basic"][1].string();
    double dw1 = text_width(level_a);
    draw_text(level_a, x, y, dw1);
    reset_text_color();
    std::string level = group_digits(a->level);
    double dw2 = text_width(group_digits(a->max_level()));
    draw_text(level, x + dw1, y, dw2, "right");
}

void WindowBase::draw_actor_icons(rpg::GameBattler* a, double x, double y, double w) {
    if (w == 0) w = 144;
    auto icons = a->all_icons();
    size_t n = std::min(icons.size(), (size_t)std::floor(w / kIconWidth));
    for (size_t i = 0; i < n; i++) draw_icon(icons[i], x + kIconWidth * (double)i, y + 2);
    draw_actor_icons_turns(a, x, y, w);
}

void WindowBase::draw_current_and_max(double current, double max, double x, double y, double width, const std::string& c1, const std::string& c2) {
    double label_w = text_width("HP");
    double value_w = text_width(group_digits(max));
    double slash_w = text_width("/");
    double x1 = x + width - value_w;
    double x2 = x1 - slash_w;
    double x3 = x2 - value_w;
    if (x3 >= x + label_w) {
        change_text_color(c1);
        draw_text(group_digits(current), x3, y, value_w, "right");
        change_text_color(c2);
        draw_text("/", x2, y, slash_w, "right");
        draw_text(group_digits(max), x1, y, value_w, "right");
    } else {
        change_text_color(c1);
        draw_text(group_digits(current), x1, y, value_w, "right");
    }
}

void WindowBase::draw_actor_hp(rpg::GameBattler* a, double x, double y, double w) {
    std::string g = a->gauge(1).to_str();
    if (g == "MP") {
        draw_actor_mp(a, x, y, w);
        return;
    }
    if (g == "TP") {
        draw_actor_tp(a, x, y, w);
        return;
    }
    if (g != "HP") return;
    if (w == 0) w = 186;
    draw_gauge(x, y, w, a->hp_rate(), hp_gauge_color1(), hp_gauge_color2());
    change_text_color(system_color());
    draw_text(data::system()["terms"]["basic"][3].string(), x, y, 44);
    draw_current_and_max(a->hp(), a->mhp(), x, y, w, hp_color(a), normal_color());
}

void WindowBase::draw_actor_mp(rpg::GameBattler* a, double x, double y, double w) {
    if (w == 0) w = 186;
    draw_gauge(x, y, w, a->mp_rate(), mp_gauge_color1(), mp_gauge_color2());
    change_text_color(system_color());
    draw_text(data::system()["terms"]["basic"][5].string(), x, y, 44);
    draw_current_and_max(a->mp(), a->mmp(), x, y, w, mp_color(a), normal_color());
}

void WindowBase::draw_actor_tp(rpg::GameBattler* a, double x, double y, double w) {
    if (w == 0) w = 96;
    draw_gauge(x, y, w, a->tp_rate(), tp_gauge_color1(), tp_gauge_color2());
    change_text_color(system_color());
    draw_text(data::system()["terms"]["basic"][7].string(), x, y, 44);
    change_text_color(tp_color(a));
    draw_text(group_digits(a->tp()), x + w - 64, y, 64, "right");
}

void WindowBase::draw_actor_simple_status(rpg::GameActor* a, double x, double y, double w) {
    double lh = line_height();
    double xpad = kFaceWidth + 2 * yparam_num("TextPadding", 6);
    double x2 = x + xpad;
    double w2 = std::max(180.0, w - xpad - text_padding());
    draw_actor_name(a, x, y);
    draw_actor_level(a, x, y + lh);
    draw_actor_icons(a, x, y + lh * 2);
    draw_actor_class(a, x2, y, w2);
    draw_actor_hp(a, x2, y + lh, w2);
    draw_actor_mp(a, x2, y + lh * 2, w2);
    if (yparam_bool("MenuTpGauge")) draw_actor_tp(a, x2, y + lh * 3, w2);
}

void WindowBase::draw_item_name(const rpg::ItemRef& item, double x, double y, double w) {
    if (w == 0) w = 312;
    if (!item.valid()) return;
    double icon_box = kIconWidth + 4;
    reset_text_color();
    draw_icon(item.data()["iconIndex"].as_int(), x + 2, y + 2);
    draw_text(replace_database_codes(item.data()["name"].string()), x + icon_box, y, w - icon_box);
}

void WindowBase::draw_currency_value(double value, const std::string& unit, double wx, double wy, double ww) {
    reset_text_color();
    c().font_size = (int)yparam_num("GoldFontSize", 20);
    bool icon = using_gold_icon(unit);
    double cx = icon ? kIconWidth : text_width(unit);
    std::string text = group_digits(value);
    if (text_width(text) > ww - cx) text = yparam_str("GoldOverlap");
    draw_text(text, wx, wy, ww - cx - 4, "right");
    if (icon) {
        draw_icon(world_gold_icon(), wx + ww - kIconWidth, wy + 2);
    } else {
        change_text_color(system_color());
        draw_text(unit, wx, wy, ww, "right");
    }
    reset_font_settings();
}

std::string WindowBase::param_change_text_color(double change) {
    if (change > 0) return power_up_color();
    if (change < 0) return power_down_color();
    return normal_color();
}

void WindowBase::set_background_type(int type) {
    set_window_opacity(type == 0 ? 255 : 0);
    if (type == 1) show_background_dimmer();
    else hide_background_dimmer();
}

void WindowBase::show_background_dimmer() {
    if (!dimmer_sprite_) {
        dimmer_sprite_ = std::make_shared<g2d::Sprite>(g2d::Bitmap::create(0, 0));
        add_child_to_back(dimmer_sprite_);
    }
    g2d::BitmapPtr b = dimmer_sprite_->bitmap();
    if (!b || b->width() != (int)width() || b->height() != (int)height()) refresh_dimmer_bitmap();
    dimmer_sprite_->visible = true;
    update_background_dimmer();
}

void WindowBase::hide_background_dimmer() {
    if (dimmer_sprite_) dimmer_sprite_->visible = false;
}

void WindowBase::update_background_dimmer() {
    if (dimmer_sprite_) dimmer_sprite_->set_opacity(openness());
}

void WindowBase::refresh_dimmer_bitmap() {
    if (!dimmer_sprite_) return;
    double w = width(), h = height(), m = padding();
    g2d::BitmapPtr b = g2d::Bitmap::create((int)w, (int)h);
    std::string c1 = "rgba(0, 0, 0, 0.6)", c2 = "rgba(0, 0, 0, 0)";
    b->gradient_fill_rect(0, 0, w, m, c2, c1, true);
    b->fill_rect(0, m, w, h - m * 2, c1);
    b->gradient_fill_rect(0, h - m, w, m, c1, c2, true);
    dimmer_sprite_->set_bitmap(b);
    dimmer_sprite_->set_frame(0, 0, w, h);
}

double WindowBase::text_width_ex(const std::string& text) { return draw_text_ex(text, 0, c().height() + line_height()); }

bool WindowBase::using_gold_icon(const std::string& unit) {
    if (unit != data::system()["currencyUnit"].string()) return false;
    const Value& gold = data::root()["yanfly"]["Icon"]["Gold"];
    return gold.num() > 0 && world_gold_icon() > 0;
}

void WindowBase::draw_actor_icons_turns(rpg::GameBattler* a, double wx, double wy, double ww) {
    double iw = kIconWidth;
    int shown_max = (int)std::floor(ww / iw);
    for (const Value* st : a->states()) {
        if (shown_max <= 0) break;
        const Value& state = *st;
        if (state["iconIndex"].as_int() <= 0) continue;
        if (state["autoRemovalTiming"].as_int() > 0) draw_state_turns(a, state, wx, wy);
        draw_state_counter(a, state, wx, wy);
        wx += iw;
        --shown_max;
    }
    for (int i = 0; i < 8; ++i) {
        if (shown_max <= 0) break;
        if (a->buffs_[i] == 0) continue;
        draw_buff_turns(a, i, wx, wy);
        if (yparam_bool("BSCShowBuffRate")) draw_buff_rate(a, i, wx, wy);
        wx += iw;
        --shown_max;
    }
    reset_font_settings();
    reset_text_color();
}

void WindowBase::draw_state_turns(rpg::GameBattler* a, const Value& state, double wx, double wy) {
    if (!state["showTurns"].truthy()) return;
    double turns = a->state_turns_of(state["id"].as_int());
    if (std::isnan(turns)) return;
    std::string t = group_digits(std::ceil(turns));
    wx += state["turnBufferX"].num();
    wy += state["turnBufferY"].num();
    change_paint_opacity(true);
    change_text_color(text_color(state["turnColor"].as_int()));
    c().font_size = state["turnFontSize"].as_int(16);
    draw_text(t, wx, wy, kIconWidth, state["turnAlign"].string());
    reset_font_settings();
    reset_text_color();
}

void WindowBase::draw_state_counter(rpg::GameBattler* a, const Value& state, double wx, double wy) {
    js::Val value = a->get_state_counter(state["id"].as_int());
    if (value.is_undef()) return;
    const Value& s = state["stateCounterSettings"];
    std::string text = value.is_num() ? group_digits(value.n) : value.to_str();
    wx += s["bufferX"].num();
    wy += s["bufferY"].num();
    change_paint_opacity(true);
    change_text_color(text_color(s["color"].as_int()));
    c().font_size = s["size"].as_int(16);
    draw_text(text, wx, wy, kIconWidth, s["align"].string());
    reset_font_settings();
    reset_text_color();
}

void WindowBase::draw_buff_turns(rpg::GameBattler* a, int param_id, double wx, double wy) {
    if (!yparam_bool("BSCShowTurns")) return;
    std::string turns = group_digits(std::ceil(a->buff_turns_of(param_id)));
    wx += yparam_num("BSCTurnBufferX");
    wy += yparam_num("BSCTurnBufferY");
    change_paint_opacity(true);
    c().font_size = (int)yparam_num("BSCFontSize", 16);
    change_text_color(text_color((int)yparam_num(a->is_buff_affected(param_id) ? "BSCBuffColor" : "BSCDebuffColor")));
    draw_text(turns, wx, wy, kIconWidth, yparam_str("BSCTurnAlign"));
    reset_font_settings();
    reset_text_color();
}

void WindowBase::draw_buff_rate(rpg::GameBattler* a, int param_id, double wx, double wy) {
    double value = a->param_buff_rate(param_id);
    std::string text = number_text(std::floor(value * 100)) + "%";
    double size = yparam_num("BSCCounterSize", 0);
    c().font_size = (int)((size ? size : 16) * 0.75);
    double bx = yparam_num("BSCCounterBufferX", 0), by = yparam_num("BSCCounterBufferY", 0);
    wx += bx;
    wy += by ? by : 8;
    change_paint_opacity(true);
    draw_text(text, wx, wy, kIconWidth, "center");
    reset_font_settings();
    reset_text_color();
}

std::string WindowBase::set_word_wrap(std::string text) {
    word_wrap_ = false;
    if (re::search("<(?:WordWrap)>", "i", text)) {
        word_wrap_ = true;
        text = re::replace_all("<(?:WordWrap)>", "gi", text, "");
    }
    if (word_wrap_) {
        std::string rep = yparam_bool("MSGWrapSpace") ? " " : "";
        text = re::replace_all("[\\n\\r]+", "g", text, rep);
        text = re::replace_all("<(?:BR|line break)>", "gi", text, "\n");
    } else {
        text = re::replace_all("<(?:BR|line break)>", "gi", text, "");
    }
    return text;
}

bool WindowBase::check_word_wrap(TextState& ts) {
    if (!is_eastern_locale()) {
        if (!word_wrap_) return false;
        double size = NAN;
        if (ts.index < ts.text.size() && ts.text[ts.index] == ' ') {
            size_t next_space = ts.text.find(' ', ts.index + 1);
            size_t next_break = ts.text.find('\n', ts.index + 1);
            long ns = next_space == std::string::npos ? (long)ts.text.size() + 1 : (long)next_space;
            if (next_break != std::string::npos && next_break > 0) ns = std::min(ns, (long)next_break);
            std::string word = ts.text.substr(ts.index, (size_t)std::max(0L, ns - (long)ts.index));
            size = text_width_ex_check(word);
        }
        return size + ts.x > wordwrap_width();
    }
    if (ts.index == 0) return false;
    if (!word_wrap_) return false;
    std::string next = ts.text.substr(ts.index, 1);
    double scale = (double)c().font_size / standard_font_size();
    double size = text_width_ex_check(next) * scale;
    if (ts.x + size > wordwrap_width()) return !can_line_break(0, next);
    next = ts.text.substr(ts.index, 2);
    size = text_width_ex_check(next) * scale;
    if (ts.x + size > wordwrap_width()) return can_line_break(1, next);
    return false;
}

double WindowBase::wordwrap_width() { return c().width(); }

void WindowBase::save_current_window_settings() {
    g2d::Bitmap& b = c();
    saved_font_.saved = true;
    saved_font_.face = b.font_face;
    saved_font_.size = b.font_size;
    saved_font_.color = b.text_color;
    saved_font_.bold = b.font_bold;
    saved_font_.italic = b.font_italic;
    saved_font_.outline_color = b.outline_color;
    saved_font_.outline_width = b.outline_width;
}

void WindowBase::restore_current_window_settings() {
    g2d::Bitmap& b = c();
    b.font_face = saved_font_.face;
    b.font_size = saved_font_.size;
    b.text_color = saved_font_.color;
    b.font_bold = saved_font_.bold;
    b.font_italic = saved_font_.italic;
    b.outline_color = saved_font_.outline_color;
    b.outline_width = saved_font_.outline_width;
}

double WindowBase::text_width_ex_check(const std::string& text) {
    bool setting = word_wrap_;
    word_wrap_ = false;
    save_current_window_settings();
    check_word_wrap_mode_ = true;
    double value = draw_text_ex(text, 0, c().height());
    check_word_wrap_mode_ = false;
    restore_current_window_settings();
    saved_font_.saved = false;
    word_wrap_ = setting;
    return value;
}

double WindowBase::text_height_ex(const std::string& text) { return get_text_ex_height(text, 0, 0); }

double WindowBase::get_text_ex_height(const std::string& text, double x, double y) {
    if (text.empty()) return 0;
    TextState ts;
    ts.x = x;
    ts.y = y;
    ts.left = x;
    ts.text = convert_escape_characters(text);
    ts.height = calc_text_height(ts, true);
    return ts.height;
}

void WindowBase::draw_black_letter_text(const std::string& text, double x, double y, double space, double offset) {
    g2d::BitmapPtr bmp = load_system("Blackletters_menu");
    double sx = x;
    for (char ch : text) {
        int code = std::toupper((unsigned char)ch);
        int index = (code > 64 && code < 91) ? code - 65 : -1;
        if (index > -1) {
            double bx = (index % 5) * 32, by = std::floor(index / 5.0) * 32;
            c().blt(bmp.get(), bx, by, 32, 32, sx, y);
            sx += space + offset;
        } else {
            sx += space;
        }
    }
}

int WindowBase::item_color_index(const rpg::ItemRef& item) {
    if (item.is_item() && rpg::is_consumable_item(item.data())) return 3;
    if (item.is_weapon()) return 2;
    return 0;
}

void WindowBase::draw_hp_icon(double x, double y) {
    g2d::BitmapPtr icon = load_system("hp_icon");
    c().blt(icon.get(), 0, 0, icon->width(), icon->height(), x, y);
}

void WindowBase::draw_mp_icon(double x, double y) {
    g2d::BitmapPtr icon = load_system("mp_icon");
    c().blt(icon.get(), 0, 0, icon->width(), icon->height(), x, y);
}

void WindowBase::draw_short_actor_hp(rpg::GameBattler* a, double x, double y) {
    g2d::BitmapPtr icon = load_system("hp_icon");
    c().blt(icon.get(), 0, 0, icon->width(), icon->height(), x, y);
    c().font_size = lang_data("XX_BLUE.Window_Base")["drawShortActorHP_contents_fontsize"].as_int(20);
    change_text_color(hp_color(a));
    draw_text(number_text(a->hp()), x + icon->width() + 4, y - 12, 100);
    reset_font_settings();
}

void WindowBase::draw_short_actor_mp(rpg::GameBattler* a, double x, double y) {
    g2d::BitmapPtr icon = load_system("mp_icon");
    c().blt(icon.get(), 0, 0, icon->width(), icon->height(), x, y);
    c().font_size = 20;
    change_text_color(mp_color(a));
    draw_text(number_text(a->mp()), x + icon->width() + 4, y - 12, 100);
    reset_font_settings();
}

void WindowBase::draw_item_icon(const rpg::ItemRef& item, double x, double y, double rate) {
    const Value& d = item.data();
    std::string name;
    int rows = 1, columns = 1, index = 0;
    const Value& meta = d["meta"];
    const Value* icon_index = meta.find("IconIndex");
    if (icon_index && icon_index->truthy()) index = (int)js::string_to_number(data::js_string(*icon_index));
    if (item.is_item() && rpg::is_key_item(d)) {
        name = "itemImportant";
        rows = 15;
        columns = 8;
    } else if (item.is_item() && rpg::is_consumable_item(d)) {
        name = "itemConsumables";
        rows = 12;
        columns = 8;
    } else if (item.is_item() && rpg::is_toy_item(d)) {
        name = "itemConsumables";
        rows = 12;
        columns = 8;
    } else if (item.is_weapon()) {
        name = "itemWeapons";
        rows = 5;
        columns = 9;
    } else if (item.is_armor()) {
        name = "itemCharms";
        rows = 13;
        columns = 7;
    }
    g2d::BitmapPtr bmp = load_system(name);
    double w = bmp->width() / (double)columns;
    double h = bmp->height() / (double)rows;
    double sx = (index % columns) * w;
    double sy = std::floor(index / (double)columns) * h;
    c().blt(bmp.get(), sx, sy, w, h, x, y, w * rate, h * rate);
}

int WindowBase::world_gold_icon() {
    switch (current_world()) {
    case 1: return 12;
    case 2: return 13;
    }
    return 12;
}

std::string WindowBase::world_currency_unit() {
    const Value& d = lang_data("XX_BLUE.World_Currency_Unit");
    switch (current_world()) {
    case 1: return d["clams"].string();
    case 2: return d["dollars"].string();
    }
    return d["clams"].string();
}

bool WindowBase::can_line_break(int type, const std::string& ch) {
    static const std::string before =
        ",)]｝、〕〉》」』】〙〗〟’”｠»ゝゞーァィゥェォッャュョヮヵヶぁぃぅぇぉっゃゅょゎゕゖㇰㇱㇲㇳㇴㇵㇶㇷㇸㇹㇷ゚ㇺㇻㇼㇽㇾㇿ々〻‐゠–〜～?!‼⁇⁈⁉・:;/。.";
    static const std::string after = "([｛〔〈《「『【〘〖〝‘“｟«";
    return (type == 0 ? before : after).find(ch) != std::string::npos;
}

bool WindowBase::is_eastern_locale() { return false; }

void WindowBase::init_letter_effects() {
    clear_letter_effect_sprites();
    letter_sprites_.clear();
    letter_effects_.clear();
    letter_effects_active_ = false;
    letter_effect_tag_ = 0;
    has_letter_effect_timer_ = false;
    has_letter_effect_synch_ = false;
}

void WindowBase::create_letter_effect_container() {
    letter_effect_container_ = std::make_shared<g2d::Sprite>();
    add_child(letter_effect_container_);
}

bool WindowBase::can_create_letter_effect_sprite() {
    if (check_word_wrap_mode_) return false;
    return letter_effects_active_;
}

void WindowBase::clear_letter_effect_sprites() {
    for (auto& ls : letter_sprites_) letter_effect_container_->remove_child(ls.sprite.get());
}

static const std::vector<LetterEffect>& letter_effect_presets(const std::string& name) {
    static std::map<std::string, std::vector<LetterEffect>> cache;
    auto it = cache.find(name);
    if (it != cache.end()) return it->second;
    std::vector<LetterEffect>& list = cache[name];
    std::string raw = data::plugin_param("TDS Text Effects", name.c_str(), "[]");
    js::Val arr = js::json_parse_text(raw);
    if (arr.is_array()) {
        for (auto& entry : arr.o->items) {
            js::Val obj = js::json_parse_text(entry.to_str());
            LetterEffect e;
            e.name = name;
            if (obj.is_obj()) {
                std::vector<uint32_t> keys;
                js::ordered_keys(obj.o, keys);
                for (uint32_t k : keys) {
                    const js::Val* v = obj.o->find(k);
                    if (!v) continue;
                    js::Val parsed = js::json_parse_text(v->to_str());
                    const std::string& kn = js::atom_name(k);
                    if (parsed.is_bool()) e.flag[kn] = parsed.b;
                    else e.num[kn] = parsed.to_num();
                }
            }
            list.push_back(e);
        }
    }
    return list;
}

void WindowBase::setup_letter_effect(const std::string& code, int index, TextState& ts) {
    if (index == 0) {
        set_letter_effect_end_index(code, ts);
    } else {
        const auto& presets = letter_effect_presets(code);
        LetterEffect e;
        if (index - 1 >= 0 && index - 1 < (int)presets.size()) e = presets[(size_t)(index - 1)];
        else js::vm().throw_type_error("Cannot convert undefined or null to object");
        e.name = code;
        e.active = true;
        e.tag = letter_effect_tag_;
        e.start_index = ts.index;
        e.end_index = kNoEnd;
        if (has_letter_effect_timer_) {
            e.has_timer = true;
            e.timer = 60;
        }
        letter_effects_.push_back(e);
    }
    if (!letter_effects_.empty()) {
        bool active = false;
        for (auto& e : letter_effects_) {
            if (ts.index < e.end_index) active = true;
        }
        letter_effects_active_ = active;
        c().text_blocked = active;
    }
}

void WindowBase::set_letter_effect_end_index(const std::string& name, TextState& ts) {
    for (auto& e : letter_effects_) {
        if (e.name == name && e.end_index == kNoEnd) e.end_index = ts.index;
    }
}

void WindowBase::create_letter_effect_sprite(TextState& ts) {
    size_t i = ts.index;
    g2d::decode_utf8(ts.text, i);
    std::string ch = ts.text.substr(ts.index, i - ts.index);
    double w = std::round(text_width(ch));
    if (ch == " ") return;
    g2d::BitmapPtr bmp = g2d::Bitmap::create((int)w, (int)(ts.height + 10));
    bmp->font_face = c().font_face;
    bmp->font_size = c().font_size;
    bmp->text_color = c().text_color;
    bmp->draw_text(ch, 0, 0, bmp->width(), bmp->height(), "center");
    auto sprite = std::make_shared<g2d::Sprite>(bmp);
    double pad = standard_padding();
    sprite->x = ts.x + pad;
    sprite->y = ts.y + pad - 5;
    LetterSprite ls;
    ls.sprite = sprite;
    ls.origin_x = sprite->x;
    ls.origin_y = sprite->y;
    ls.index = ts.index;
    ls.position = letter_sprites_.size();
    ls.tag = letter_effect_tag_;
    letter_sprites_.push_back(ls);
    letter_effect_container_->add_child(sprite);
    if (has_letter_effect_synch_) {
        for (auto& e : letter_effects_) {
            if (e.tag == letter_effect_synch_index_) e.synch_sprite = sprite.get();
        }
        has_letter_effect_synch_ = false;
    }
}

void WindowBase::update_letter_effects() {
    letter_effect_container_->visible = is_open();
    if (letter_sprites_.empty()) return;
    std::vector<size_t> finished;
    for (size_t ei = 0; ei < letter_effects_.size(); ei++) {
        LetterEffect& effect = letter_effects_[ei];
        g2d::Sprite* synch = effect.synch_sprite;
        LetterSprite* synch_ls = nullptr;
        if (synch) {
            for (auto& ls : letter_sprites_) {
                if (ls.sprite.get() == synch) synch_ls = &ls;
            }
        }
        for (auto& ls : letter_sprites_) {
            if (synch) {
                if (ls.sprite.get() == synch) {
                    apply_letter_effect_to_sprite(effect, ls);
                } else if (synch_ls && ls.tag == effect.tag) {
                    ls.sprite->anchor_x = synch->anchor_x;
                    ls.sprite->anchor_y = synch->anchor_y;
                    ls.sprite->x = ls.origin_x + synch_ls->offset_x;
                    ls.sprite->y = ls.origin_y + synch_ls->offset_y;
                    ls.sprite->rotation = synch->rotation;
                    ls.sprite->set_blend_color(synch->blend_color());
                }
            } else {
                apply_letter_effect_to_sprite(effect, ls);
            }
        }
        if (effect.has_timer && effect.timer != 0 && effect.end_index != kNoEnd) {
            effect.timer--;
            if (effect.timer <= 0) effect.active = false;
        }
        if (!effect.active) finished.push_back(ei);
    }
    if (!finished.empty()) {
        for (size_t k = finished.size(); k-- > 0;) letter_effects_.erase(letter_effects_.begin() + (long)finished[k]);
        static const double zero[4] = {0, 0, 0, 0};
        for (auto& ls : letter_sprites_) {
            ls.sprite->x = ls.origin_x;
            ls.sprite->y = ls.origin_y;
            ls.sprite->set_blend_color(zero);
        }
    }
}

void WindowBase::apply_letter_effect_to_sprite(LetterEffect& effect, LetterSprite& ls) {
    size_t index = ls.index;
    if (!effect.active) return;
    if (index < effect.start_index || index >= effect.end_index) return;
    double fc = (double)rpg::frame_count();
    double idx = (double)index;
    if (effect.name == "SINH") {
        double v = std::sin((fc + idx * effect.n("spacing")) * effect.n("speed")) * effect.n("amplitude");
        if (effect.f("anchor")) {
            ls.sprite->anchor_y = v;
        } else {
            ls.offset_x = std::round(v);
            ls.sprite->x = ls.origin_x + ls.offset_x;
        }
    } else if (effect.name == "SINV") {
        ls.sprite->anchor_y = std::sin((fc + idx * effect.n("spacing")) * effect.n("speed")) * effect.n("amplitude");
    } else if (effect.name == "SHAKE") {
        double timing = effect.n("timing");
        if (timing != 0 && std::fmod(fc, timing) == 0) {
            auto rint = [](double max) { return std::floor(rng::next() * max); };
            if (effect.n("powerX") > 0) ls.offset_x = std::max(rint(effect.n("powerX")), 1.0) * (rint(100) > 50 ? -1 : 1);
            if (effect.n("powerY") > 0) ls.offset_y = std::max(rint(effect.n("powerY")), 1.0) * (rint(100) > 50 ? -1 : 1);
            ls.sprite->x = ls.origin_x + ls.offset_x;
            ls.sprite->y = ls.origin_y + ls.offset_y;
        }
    } else if (effect.name == "RAINBOW") {
        double frame = fc + idx;
        double sp = effect.n("speed"), amp = effect.n("amplitude"), center = effect.n("center");
        double col[4];
        col[0] = std::sin(sp * frame + effect.n("redPhase")) * amp + center;
        col[1] = std::sin(sp * frame + effect.n("greenPhase")) * amp + center;
        col[2] = std::sin(sp * frame + effect.n("bluePhase")) * amp + center;
        col[3] = effect.f("useWaveOpacity") ? std::sin(sp * frame) * amp + center : effect.n("opacity");
        ls.sprite->set_blend_color(col);
    }
}

void WindowBase::set_frame_opacity(double v) { set_window_opacity(v); }

}
