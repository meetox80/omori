#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "../rpg/audio_manager.h"
#include "../rpg/data.h"
#include "../rpg/game.h"
#include "../rpg/input.h"
#include "../rpg/save.h"
#include "menu_common.h"
#include "scenes.h"
#include "ui_util.h"

namespace ui {
namespace menu {

namespace {

const std::vector<std::vector<const char*>>& actor_names_per_language() {
    static const std::vector<std::vector<const char*>> t = {{"OMORI", "オモリ", "오모리", "OMORI"},
                                                            {"AUBREY", "オーブリー", "오브리", "奥布里"},
                                                            {"KEL", "ケル", "켈", "凯"},
                                                            {"HERO", "ヒロ", "히로", "英雄"}};
    return t;
}

const std::vector<std::vector<const char*>>& chapter_names_per_language() {
    static const std::vector<std::vector<const char*>> t = {{"???", "???", "???", "？？？"},
                                                            {"MOVING DAY", "引越しの日", "이삿날", "搬家当天"},
                                                            {"ONE DAY LEFT...", "あと1日……", "이삿날까지 하루 남음...", "还有一天……"},
                                                            {"TWO DAYS LEFT...", "あと2日……", "이삿날까지 이틀 남음...", "还有两天……"},
                                                            {"THREE DAYS LEFT...", "あと3日……", "이삿날까지 사흘 남음...", "还有三天……"},
                                                            {"PROLOGUE", "プロローグ", "서장", "序章"}};
    return t;
}

const std::vector<std::vector<const char*>>& location_names_per_language() {
    static const std::vector<std::vector<const char*>> t = {
        {"OUTSIDE", "家の外", "바깥", "外面"},
        {"MOM'S ROOM", "お母さんの部屋", "엄마의 방", "妈妈的房间"},
        {"BASIL'S HOUSE", "バジルの家", "바질의 집", "贝瑟尔家"},
        {"FARAWAY PARK", "ハルバル公園", "패러웨이 공원", "遥远公园"},
        {"SECRET LAKE", "秘密の湖", "비밀 호수", "秘密湖泊"},
        {"FOREST PLAYGROUND", "森の遊び場", "숲 놀이터", "森林游乐场"},
        {"VAST FOREST", "ヒロビロ森", "광대한 숲", "辽阔森林"},
        {"PINWHEEL FOREST", "カザグルマ森", "바람개비 숲", "风车森林"},
        {"ORANGE OASIS", "オレンジオアシス", "오렌지 오아시스", "橙子绿"},
        {"RAIN TOWN", "アメフリの町", "비의 마을", "落雨镇"},
        {"OTHERWORLD", "イセカイ", "별세계", "异世界"},
        {"FROZEN LAKE", "ヒエビエ湖", "얼어붙은 호수", "结冰湖"},
        {"SNOWGLOBE MOUNTAIN", "スノードームマウンテン", "스노우블로브 산", "雪花球山"},
        {"JUNKYARD", "廃品投棄場", "쓰레기장", "垃圾场"},
        {"PYREFLY FOREST", "ヒバナ森", "반딧불더미 숲", "焚火虫森林"},
        {"LOST FOREST", "サイゴノ楽園", "잃어버린 숲", "迷失森林"},
        {"SPROUT MOLE VILLAGE", "ナエモグ村", "두더지 새싹 마을", "树苗鼹鼠村"},
        {"SWEETHEART'S CASTLE", "スイートハート城", "스위트하트의 성", "甜心城堡"},
        {"LAST RESORT", "サイゴノ楽園", "최후의 도피처", "最后度假村"},
        {"UNDERWATER HIGHWAY", "水中高速道路", "수중 고속도로", "水下高架"},
        {"DEEPER WELL", "シンエン井戸", "더 깊은 우물", "深井深处"},
        {"HUMPHREY", "ハンフリー", "험프리", "汉弗莱"},
        {"BOSS RUSH", "ボスラッシュ", "보스 러시", "头目反击战"},
        {"THE ABYSS", "ナラク", "심연", "深渊"},
        {"BLACK SPACE", "ブラックスペース", "검은 공간", "黑色空间"},
        {"BASIL'S MEADOW", "バジルの草原", "바질의 꽃밭", "贝瑟尔的草地"},
        {"LOST LIBRARY", "失われし図書館", "잃어버린 도서관", "失落图书馆"},
        {"RECYCULTIST'S HQ", "リサイカルト本部", "재활용교도의 본부", "回收教徒总部"},
        {"BREAVEN", "パンクチュアリ", "빵천국", "面包天堂"},
        {"BACKSTAGE", "バックステージ", "무대 뒤편", "后台"},
        {"MEMORY LANE", "思い出の道", "추억의 뒤안길", "回忆小道"},
        {"NEIGHBOR'S BEDROOM", "オトナリベッドルーム", "이웃의 침실", "邻居的卧室"}};
    return t;
}

std::string default_language() { return data::plugin_param("Text_Language_Processor", "Default Language", "en"); }

std::string convert_name_to_language(const std::vector<std::vector<const char*>>& table, const std::string& name) {
    static const char* langs[] = {"en", "jp", "kr", "sc"};
    std::string lang = default_language();
    int my_index = -1;
    for (int i = 0; i < 4; i++) {
        if (lang == langs[i]) my_index = i;
    }
    if (my_index == -1) return name;
    for (const auto& entry : table) {
        for (size_t language_id = 0; language_id < entry.size(); language_id++) {
            if ((int)language_id == my_index) continue;
            if (name == entry[language_id]) return entry[(size_t)my_index];
        }
    }
    return name;
}

bool load_savefile_info(int id, rpg::save::SlotInfo& info) {
    if (!rpg::save::info(id, info)) return false;
    info.actor_name = convert_name_to_language(actor_names_per_language(), info.actor_name);
    if (!rpg::is_text_in_current_language(info.actor_name)) info.actor_name = data::js_string(blue("Omori_Name_Input")["defaultname"]);
    if (info.chapter.is_str()) info.chapter = js::Val::str(convert_name_to_language(chapter_names_per_language(), info.chapter.to_str()));
    info.location = convert_name_to_language(location_names_per_language(), info.location);
    return true;
}

}

class WindowOmoriFileInformation : public WindowBase {
public:
    void initialize(int index) {
        index_ = index;
        WindowBase::initialize(0, 0, window_width(), window_height());
        create_cursor_sprite();
        refresh();
        deselect();
    }
    void initialize(double x, double y, double w, double h) override { WindowBase::initialize(x, y, w, h); }
    int standard_padding() override { return 4; }
    double window_width() { return 382 + 54; }
    double window_height() { return 142; }

    void create_cursor_sprite() {
        cursor_sprite_ = std::make_shared<SpriteWindowCustomCursor>(-1, "");
        cursor_sprite_->x = 10;
        cursor_sprite_->y = 20;
        add_child(cursor_sprite_);
    }

    void select() {
        cursor_sprite_->visible = true;
        set_contents_opacity(255);
    }

    void deselect() {
        cursor_sprite_->visible = false;
        set_contents_opacity(100);
    }

    void refresh() {
        c().clear();
        std::string color = "rgba(255, 255, 255, 1)";
        int id = index_ + 1;
        rpg::save::SlotInfo info;
        bool valid = load_savefile_info(id, info);
        c().fill_rect(0, 29, c().width(), 3, color);
        for (int i = 0; i < 3; i++) {
            double y = 55 + (i * 25);
            c().fill_rect(113, y, c().width() - 117, 1, color);
        }
        const Value& loc = blue("Window_OmoriFileInformation");
        const Value& text = blue("Omori_Save_Load");
        c().font_size = loc["refresh_contents_fontsize"].as_int();
        const Value& file_pos = loc["file_position"];
        c().draw_text(js_fmt(data::js_string(text["file"]), {num_str(id)}), file_pos[0].num(), file_pos[1].num(), 100, c().font_size, "left");
        if (valid) {
            const Value& chap_pos = loc["refresh_drawText_position"];
            std::string chap;
            bool draw_chap = !info.chapter.is_undef();
            if (draw_chap) {
                std::string key = info.chapter.to_str();
                const Value* named = blue("Chapter_Names").find(key.c_str());
                if (named && named->truthy()) chap = data::js_string(*named);
                else chap = key;
                c().draw_text(chap, chap_pos[0].num(), chap_pos[1].num(), c().width(), c().font_size, "left");
            }
            c().font_size = 28;
            g2d::BitmapPtr back = load_system("faceset_states");
            double width = back->width() / 4.0;
            double height = back->height() / 5.0;
            c().blt(back.get(), 0, 0, width, height, 1, 33);
            draw_face(info.face_name, info.face_index, -2, c().height() - kFaceHeight + 7, kFaceWidth, height - 2);
            c().font_size = 24;
            c().draw_text(info.actor_name, 118, 30, 100, 24, "left");
            const Value& level_pos = loc["level_position"];
            c().draw_text(data::js_string(text["level"]), level_pos[0].num(), level_pos[1].num(), 100, 24, "left");
            c().draw_text(num_str(info.actor_level), level_pos[0].num(), level_pos[1].num(), 70, 24, "right");
            const Value& playtime_pos = loc["playtime_position"];
            c().draw_text(data::js_string(text["playtime"]), 118, 55, 200, 24, "left");
            c().draw_text(info.playtime, playtime_pos[0].num(), playtime_pos[1].num(), 100, 24, "left");
            c().draw_text(data::js_string(text["location"]), 118, 80, 200, 24, "left");
            c().draw_text(info.location, 205, 80, 210, 24, "right");
        }
        c().fill_rect(102, 32, 3, 102, "rgba(255, 255, 255, 1)");
        c().fill_rect(0, 29, 108, 3, "rgba(255, 255, 255, 1)");
    }

private:
    int index_ = 0;
    std::shared_ptr<SpriteWindowCustomCursor> cursor_sprite_;
};

class WindowOmoriFileCommand : public WindowCommand {
public:
    using WindowCommand::initialize;
    void initialize() {
        WindowCommand::initialize(42, 28);
        setup_file(true, true);
    }
    bool is_using_custom_cursor_rect_sprite() override { return true; }
    double line_height() override { return 24; }
    double window_width() override { return 119; }
    double window_height() override { return 64; }
    int standard_padding() override { return 4; }
    int num_visible_rows() override { return 2; }
    int max_cols() override { return 1; }
    double custom_cursor_rect_y_offset() override { return 5; }
    double custom_cursor_rect_text_x_offset() override { return 40; }

    void setup_file(bool save, bool load) {
        can_save_ = save;
        can_load_ = load;
        if (can_save_) select(0);
        else if (can_load_) select(1);
        refresh();
    }

    void make_command_list() override {
        const Value& loc = blue("Omori_Save_Load");
        add_command(data::js_string(loc["save_command"]), "save", can_save_);
        add_command(data::js_string(loc["load_command"]), "load", can_load_);
    }

private:
    bool can_save_ = false;
    bool can_load_ = false;
};

class WindowOmoriFilePrompt : public WindowCommand {
public:
    using WindowCommand::initialize;
    void initialize() {
        WindowCommand::initialize(0, 0);
        x = (640 - width()) / 2;
        y = (480 - height()) / 2;
        create_cover_sprite();
    }

    void create_cover_sprite() {
        g2d::BitmapPtr bitmap = g2d::Bitmap::create(640, 480);
        bitmap->fill_all("rgba(0, 0, 0, 0.5)");
        cover_sprite_ = std::make_shared<g2d::Sprite>(bitmap);
        cover_sprite_->x = -x;
        cover_sprite_->y = -y;
        add_child_at(cover_sprite_, 0);
        cover_sprite_->set_opacity(openness());
    }

    bool is_using_custom_cursor_rect_sprite() override { return true; }
    double line_height() override { return 22; }
    double window_width() override { return 220; }
    double window_height() override { return 70 + 20; }
    int standard_padding() override { return 4; }
    int num_visible_rows() override { return 2; }
    int max_cols() override { return 1; }
    double custom_cursor_rect_x_offset() override { return 50; }
    double custom_cursor_rect_y_offset() override { return 33; }
    double custom_cursor_rect_text_x_offset() override { return 80; }
    double custom_cursor_rect_text_y_offset() override { return 28; }

    void set_prompt_text(const std::string& text) {
        prompt_text_ = text;
        has_prompt_text_ = true;
        refresh();
    }

    void make_command_list() override {
        add_command(data::js_string(lang_data("XX_SYSTEM.message_10")["text"]), "ok");
        add_command(data::js_string(lang_data("XX_SYSTEM.message_11")["text"]), "cancel");
    }

    void refresh() override {
        WindowCommand::refresh();
        if (has_prompt_text_) c().draw_text(prompt_text_, 0, 0, c().width(), 24, "center");
    }

protected:
    void update_transform_hook() override {
        if (cover_sprite_) cover_sprite_->set_opacity(openness());
    }

private:
    std::string prompt_text_;
    bool has_prompt_text_ = false;
    g2d::SpritePtr cover_sprite_;
};

class WindowOmoriFileWaiting : public WindowBase {
public:
    static constexpr int kMaxTime = 3;
    void reset_string() { save_string = system_text("message", "saveWait"); }
    void refresh() {
        if (wait > 0) {
            wait--;
            return;
        }
        save_string += ".";
        c().clear();
        draw_text(save_string, 0, 0, c().width(), "center");
        wait = kMaxTime;
    }
    void clear() {
        reset_string();
        wait = kMaxTime;
        hide();
    }
    void update() override {
        WindowBase::update();
        if (!visible) return;
        refresh();
    }
    std::string save_string;
    int wait = kMaxTime;
};

class SceneOmoriFile : public rpg::Scene {
public:
    const char* name() const override { return "Scene_OmoriFile"; }

    void setup(bool save, bool load) {
        can_save_ = save;
        can_load_ = load;
    }

    void terminate() override {
        rpg::Scene::terminate();
        if (load_success_) rpg::game_system().on_after_load();
    }

    void create() override {
        rpg::Scene::create();
        create_background();
        create_command_window();
        create_file_windows();
        create_prompt_window();
        create_waiting_window();
    }

    void create_background() {
        background_sprite_ = std::make_shared<g2d::TilingSprite>(load_parallax("SAVE_MENU_BG"));
        background_sprite_->move(0, 0, 640, 480);
        add_child(background_sprite_);
    }

    void create_command_window() {
        command_window_ = WindowBase::make<WindowOmoriFileCommand>();
        command_window_->setup_file(can_save_, can_load_);
        command_window_->set_handler("ok", [this] { on_command_window_ok(); });
        command_window_->set_handler("cancel", [this] { on_command_window_cancel(); });
        add_child(command_window_);
    }

    void create_file_windows() {
        file_windows_.clear();
        double sx = command_window_->x + command_window_->width() + 1;
        for (int i = 0; i < 6; i++) {
            auto win = std::shared_ptr<WindowOmoriFileInformation>(new WindowOmoriFileInformation());
            win->initialize(i);
            win->x = sx;
            win->y = 28 + (i * (win->height() + 1));
            file_windows_.push_back(win);
            add_child(win);
        }
    }

    void create_prompt_window() {
        prompt_window_ = WindowBase::make<WindowOmoriFilePrompt>();
        prompt_window_->set_handler("ok", [this] { on_prompt_window_ok(); });
        prompt_window_->set_handler("cancel", [this] { on_prompt_window_cancel(); });
        prompt_window_->close();
        prompt_window_->set_openness(0);
        prompt_window_->deactivate();
        add_child(prompt_window_);
    }

    void create_waiting_window() {
        double ww = std::floor(640 / 3.0);
        double wh = yparam_num("LineHeight", 36) + 16;
        waiting_window_ = WindowBase::make<WindowOmoriFileWaiting>(std::ceil(640 / 2.0 - ww / 2), std::ceil(480 / 2.0 - wh / 2), 0.0, 0.0);
        waiting_window_->hide();
        add_child(waiting_window_);
        waiting_window_->reset_string();
        waiting_window_->wait = WindowOmoriFileWaiting::kMaxTime;
    }

    void update() override {
        rpg::Scene::update();
        update_background();
        if (can_select_) update_select_input();
    }

    int savefile_id() const { return save_index_ + 1; }

    double is_out_of_bounds() {
        int index = save_index_;
        if (index < 0 || index >= (int)file_windows_.size()) return 0;
        auto& win = file_windows_[(size_t)index];
        if (win->y + win->height() > 480) return -28;
        if (index == 0) {
            if (win->y < 28) return 28;
        }
        if (win->y < 0) return 28;
        return 0;
    }

    void update_save_index_cursor() {
        for (size_t i = 0; i < file_windows_.size(); i++) {
            if (save_index_ == (int)i) file_windows_[i]->select();
            else file_windows_[i]->deselect();
        }
    }

    void update_background() { background_sprite_->origin_y += 1; }

    void update_select_input() {
        if (input::is_triggered(input::OK)) {
            on_select_input_ok();
            return;
        }
        if (input::is_triggered(input::CANCEL)) {
            play_cancel();
            on_select_input_cancel();
            return;
        }
        int n = (int)file_windows_.size();
        if (input::is_repeated(input::UP)) {
            play_cursor();
            if (save_index_ == 0) save_index_ = n - 1;
            else save_index_ = (save_index_ - 1) % n;
            update_save_index_cursor();
            return;
        }
        if (input::is_repeated(input::DOWN)) {
            play_cursor();
            save_index_ = (save_index_ + 1) % n;
            update_save_index_cursor();
            return;
        }
        update_placement();
    }

    void update_placement() {
        if (save_index_ < 0) return;
        double bounds = is_out_of_bounds();
        if (bounds == 0) return;
        for (auto& win : file_windows_) win->y += bounds;
    }

    void on_command_window_ok() {
        can_select_ = true;
        int latest_file = can_save_ ? rpg::save::last_accessed() : rpg::save::latest_slot();
        int max_savefiles = 6;
        save_index_ = (latest_file - 1) % max_savefiles;
        update_save_index_cursor();
    }

    void on_command_window_cancel() {
        bool is_title_screen = rpg::scene_manager::is_previous_scene("Scene_OmoriTitleScreen");
        pop_scene();
        if (is_title_screen) rpg::scene_manager::goto_scene(title_scene_prepared(1));
    }

    void on_select_input_ok() {
        int index = command_window_->index();
        int id = savefile_id();
        const Value& loc = blue("Omori_Save_Load");
        if (index == 0) {
            if (rpg::save::exists(id)) {
                show_prompt_window(data::js_string(loc["overwrite_file"]));
                can_select_ = false;
            } else {
                save_game();
            }
        } else {
            if (rpg::save::exists(id)) {
                show_prompt_window(data::js_string(loc["load_file"]));
                can_select_ = false;
            } else {
                play_buzzer();
            }
        }
    }

    void on_select_input_cancel() {
        can_select_ = false;
        save_index_ = -1;
        update_save_index_cursor();
        command_window_->activate();
    }

    void show_prompt_window(const std::string& text) {
        prompt_window_->set_prompt_text(text);
        prompt_window_->open();
        prompt_window_->select(1);
        prompt_window_->activate();
    }

    void on_prompt_window_ok() {
        int index = command_window_->index();
        if (index == 0) {
            save_game();
            prompt_window_->close();
            prompt_window_->deactivate();
            can_select_ = true;
        } else {
            load_game();
        }
    }

    void on_prompt_window_cancel() {
        prompt_window_->close();
        prompt_window_->deactivate();
        can_select_ = true;
    }

    void save_game() {
        rpg::game_system().on_before_save();
        int id = savefile_id();
        std::shared_ptr<WindowOmoriFileInformation> file_window;
        if (save_index_ >= 0 && save_index_ < (int)file_windows_.size()) file_window = file_windows_[(size_t)save_index_];
        prompt_window_->deactivate();
        prompt_window_->close();
        waiting_window_->show();
        if (rpg::save::save_game(id)) {
            play_save();
            rpg::GameSwitches& sw = rpg::game_switches();
            int world;
            if (sw.value(448) && sw.value(447)) world = 449;
            else if (sw.value(448)) world = 448;
            else if (sw.value(447)) world = 447;
            else if (sw.value(446)) world = 446;
            else if (sw.value(445)) world = 445;
            else if (sw.value(444)) world = 444;
            else world = 0;
            rpg::save::write_text("TITLEDATA", std::to_string(world));
            if (file_window) file_window->refresh();
            waiting_window_->clear();
            can_select_ = true;
            update_save_index_cursor();
        } else {
            play_buzzer();
            prompt_window_->deactivate();
            prompt_window_->close();
            can_select_ = true;
            update_save_index_cursor();
        }
    }

    void load_game() {
        if (rpg::save::load_game(savefile_id())) {
            play_load();
            fade_out_all();
            if (rpg::game_system().version_id != data::system()["versionId"].as_int()) {
                rpg::GamePlayer& player = rpg::game_player();
                player.reserve_transfer(rpg::game_map().map_id(), player.x(), player.y(), 0, 2);
                player.request_map_reload();
            }
            rpg::scene_manager::goto_scene(map_scene());
            load_success_ = true;
            prompt_window_->close();
            prompt_window_->deactivate();
        } else {
            play_buzzer();
            prompt_window_->close();
            prompt_window_->deactivate();
            can_select_ = true;
        }
    }

private:
    int save_index_ = -1;
    bool can_select_ = false;
    bool load_success_ = false;
    bool can_save_ = true;
    bool can_load_ = true;
    std::shared_ptr<g2d::TilingSprite> background_sprite_;
    std::shared_ptr<WindowOmoriFileCommand> command_window_;
    std::vector<std::shared_ptr<WindowOmoriFileInformation>> file_windows_;
    std::shared_ptr<WindowOmoriFilePrompt> prompt_window_;
    std::shared_ptr<WindowOmoriFileWaiting> waiting_window_;
};

}

rpg::SceneFactory omori_file_scene(bool save, bool load) {
    return rpg::SceneFactory{"Scene_OmoriFile", [save, load] {
                                 auto* scene = new menu::SceneOmoriFile();
                                 scene->setup(save, load);
                                 return std::unique_ptr<rpg::Scene>(scene);
                             }};
}

static SceneRegistrar r_scene_omori_file("Scene_OmoriFile", omori_file_scene(true, true));

}
