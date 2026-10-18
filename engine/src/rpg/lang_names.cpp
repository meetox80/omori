#include "lang_names.h"

namespace rpg {

namespace {

const char* const kActor[][4] = {
    {"OMORI", "オモリ", "오모리", "OMORI"},
    {"AUBREY", "オーブリー", "오브리", "奥布里"},
    {"KEL", "ケル", "켈", "凯"},
    {"HERO", "ヒロ", "히로", "英雄"},
};

const char* const kChapter[][4] = {
    {"???", "???", "???", "？？？"},
    {"MOVING DAY", "引越しの日", "이삿날", "搬家当天"},
    {"ONE DAY LEFT...", "あと1日……", "이삿날까지 하루 남음...", "还有一天……"},
    {"TWO DAYS LEFT...", "あと2日……", "이삿날까지 이틀 남음...", "还有两天……"},
    {"THREE DAYS LEFT...", "あと3日……", "이삿날까지 사흘 남음...", "还有三天……"},
    {"PROLOGUE", "プロローグ", "서장", "序章"},
};

const char* const kLocation[][4] = {
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
    {"NEIGHBOR'S BEDROOM", "オトナリベッドルーム", "이웃의 침실", "邻居的卧室"},
};

const char* const kLanguages[4] = {"en", "jp", "kr", "sc"};

template <size_t N>
std::string convert(const char* const (&table)[N][4], int lang, const std::string& name) {
    for (size_t e = 0; e < N; e++) {
        for (int l = 0; l < 4; l++) {
            if (l == lang) continue;
            if (name == table[e][l]) return table[e][lang];
        }
    }
    return name;
}

}

std::string convert_name_to_language(const std::string& type, const std::string& name, const std::string& language) {
    int lang = -1;
    for (int i = 0; i < 4; i++) {
        if (language == kLanguages[i]) lang = i;
    }
    if (lang < 0) return name;
    if (type == "actor") return convert(kActor, lang, name);
    if (type == "chapter") return convert(kChapter, lang, name);
    return convert(kLocation, lang, name);
}

}
