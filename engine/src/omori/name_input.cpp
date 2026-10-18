#include "name_input.h"

#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../js/vm.h"
#include "../rpg/data.h"
#include "../rpg/game.h"
#include "../rpg/input.h"
#include "../rpg/params.h"
#include "../rpg/party.h"
#include "../rpg/system.h"
#include "menu_common.h"
#include "scene_menu_base.h"
#include "scenes.h"
#include "ui_util.h"

namespace ui {

namespace {

std::u32string utf8_decode(const std::string& s) {
    std::u32string out;
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = (unsigned char)s[i];
        char32_t cp = c;
        size_t len = 1;
        if (c >= 0xF0 && i + 3 < s.size()) {
            cp = ((c & 0x07u) << 18) | (((unsigned char)s[i + 1] & 0x3Fu) << 12) | (((unsigned char)s[i + 2] & 0x3Fu) << 6) | ((unsigned char)s[i + 3] & 0x3Fu);
            len = 4;
        } else if (c >= 0xE0 && i + 2 < s.size()) {
            cp = ((c & 0x0Fu) << 12) | (((unsigned char)s[i + 1] & 0x3Fu) << 6) | ((unsigned char)s[i + 2] & 0x3Fu);
            len = 3;
        } else if (c >= 0xC0 && i + 1 < s.size()) {
            cp = ((c & 0x1Fu) << 6) | ((unsigned char)s[i + 1] & 0x3Fu);
            len = 2;
        }
        out.push_back(cp);
        i += len;
    }
    return out;
}

std::string utf8_encode(char32_t cp) {
    std::string out;
    if (cp < 0x80) {
        out.push_back((char)cp);
    } else if (cp < 0x800) {
        out.push_back((char)(0xC0 | (cp >> 6)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back((char)(0xE0 | (cp >> 12)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else {
        out.push_back((char)(0xF0 | (cp >> 18)));
        out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
    return out;
}

std::string utf8_encode(const std::u32string& s) {
    std::string out;
    for (char32_t c : s) out += utf8_encode(c);
    return out;
}

std::vector<std::string> utf8_chars(const std::string& s) {
    std::vector<std::string> out;
    for (char32_t c : utf8_decode(s)) out.push_back(utf8_encode(c));
    return out;
}

bool js_whitespace(char32_t c) {
    switch (c) {
        case 0x09:
        case 0x0A:
        case 0x0B:
        case 0x0C:
        case 0x0D:
        case 0x20:
        case 0xA0:
        case 0x1680:
        case 0x2028:
        case 0x2029:
        case 0x202F:
        case 0x205F:
        case 0x3000:
        case 0xFEFF:
            return true;
        default:
            return c >= 0x2000 && c <= 0x200A;
    }
}

std::string js_trim(const std::string& s) {
    std::u32string u = utf8_decode(s);
    size_t a = 0, b = u.size();
    while (a < b && js_whitespace(u[a])) a++;
    while (b > a && js_whitespace(u[b - 1])) b--;
    return utf8_encode(u.substr(a, b - a));
}

std::vector<std::string> split_spaces(const std::string& s) {
    std::vector<std::string> out;
    size_t from = 0;
    while (true) {
        size_t at = s.find(' ', from);
        if (at == std::string::npos) {
            out.push_back(s.substr(from));
            break;
        }
        out.push_back(s.substr(from, at - from));
        from = at + 1;
    }
    return out;
}

int name_variable_id() { return (int)rpg::js_number(data::plugin_param("Omori Name Input", "Name Variable ID", "0")); }

const char* const kBadWords[] = {
    "aids",      "anal",     "anus",     "areola",   "arse",     "ass",      "balls",    "bastard ", "beaner",   "berk",     "biatch",
    "bint",      "bitch",    "blow",     "bogan",    "boner",    "boob",     "boom",     "breast",   "butt",     "cancer",   "chav",
    "chink",     "clit",     "cocaine",  "cock",     "coitus",   "commie",   "condom",   "coolie",   "coon",     "coomer",   "cracker",
    "crap",      "crotch",   "cum",      "cunt",     "damn",     "ferro",    "dago",     "dick",     "dildo",    "dilf",     "dong",
    "drug",      "dumb",     "dyke",     "enema",    "erect",    "eskimo",   "fag",      "feck",     "fuck",     "gay",      "gipsy",
    "gfy",       "gook",     "gringo",   "gyp",      "hardon",   "heroin",   "herpes",   "hiv",      "hoe",      "hole",     "homo",
    "honk",      "hooker",   "horny",    "hymen",    "idiot",    "incest",   "injun",    "jap",      "jerkoff",  "jew",      "jigger ",
    "jiz ",      "jizz",     "jock",     "juggalo",  "kaffir",   "kafir",    "kigger",   "killer",   "kink",     "kkk",      "kock",
    "koon",      "kotex",    "krap",     "kum",      "kunt",     "lesbian",  "lesbo",    "loli",     "lsd",      "lube",     "lynch",
    "mammy",     "meth",     "mick",     "milf",     "molest",   "mom",      "mong",     "moron",    "muff",     "mulatto",  "muncher",
    "munt",      "murder",   "muslim",   "nazi",     "negress ", "negro",    "n1g",      "nlg",      "nig",      "nip",      "nlgger",
    "noonan",    "nooner",   "nuke",     "nut",      "oral",     "orga",     "orgies",   "orgy",     "oriental", "paddy",    "pakeha",
    "paki",      "pansies",  "pansy",    "peni5",    "pen15",    "penis",    "perv",     "phuc",     "phuk",     "phuq",     "pi55",
    "pimp",      "piss",     "playboy",  "pocha",    "pocho",    "pohm",     "polack",   "poon",     "poop",     "porn",     "pu55i",
    "pu55y",     "pube",     "puss",     "pygmy",    "quashie",  "queef",    "queer",    "quim",     "racist",   "raghead",  "randy",
    "rape",      "rapist",   "rectum",   "redneck",  "redskin",  "reefer",   "reestie",  "reject",   "retard",   "rigger",   "rimjob",
    "root",      "rump",     "russki",   "savage",   "scag",     "scat",     "schizo",   "schlong",  "screw",    "scrotum",  "scrub",
    "semen",     "sex",      "shag",     "shat",     "shit",     "skank",    "skum",     "slag",     "slant",    "slave",    "slut",
    "smut",      "snatch",   "snot",     "sodom",    "spade",    "spaz",     "sperm",    "spooge",   "spunk",    "squaw",    "strip",
    "stupid",    "suicide",  "syphilis", "tampon",   "tard",     "teat",     "terrorist","teste",    "testicle", "thot",     "tit",
    "toilet",    "tramp",    "trannie",  "tranny",   "trojan",   "turd",     "twat",     "twink",    "urine",    "uterus",   "vagina",
    "vaginal",   "vibrate",  "virgin",   "vomit",    "vulva",    "wank",     "weenie",   "weewee",   "wetback",  "whigger",  "whiskey",
    "whitey",    "whore",    "wigger",   "willie",   "willy",    "wog",      "wop",      "wtf",      "wuss",     "xtc",      "xxx",
    "yank",      "yid",      "zigabo",   "aubrey",   "basil",    "bundy",    "gacy",     "hector",   "hero",     "hitler",   "jawsum",
    "jinping",   "kel",      "kimjong",  "little",   "mari",     "oj",       "osama",    "pluto",    "polly",    "putin",    "ripper",
    "rococo",    "stalin",   "trump",    "zodiac",   "puto",     "puta",     "cuck",     "mierda",   "pendejo",  "Nigga",    "Nibba",
    "trap",      "perra",    "faggot",   "dilldoe",  "blacky",   "pniss",    "biden",    "omori"};

bool contains_bad_word(const std::string& lower) {
    for (const char* w : kBadWords) {
        if (lower.find(w) != std::string::npos) return true;
    }
    return false;
}

namespace hangul {

const int kOffset = 0xAC00;

using Seq = std::vector<char16_t>;

const std::vector<Seq>& cho() {
    static const std::vector<Seq> v = {{u'ㄱ'}, {u'ㄲ'}, {u'ㄴ'}, {u'ㄷ'}, {u'ㄸ'}, {u'ㄹ'}, {u'ㅁ'}, {u'ㅂ'}, {u'ㅃ'}, {u'ㅅ'},
                                       {u'ㅆ'}, {u'ㅇ'}, {u'ㅈ'}, {u'ㅉ'}, {u'ㅊ'}, {u'ㅋ'}, {u'ㅌ'}, {u'ㅍ'}, {u'ㅎ'}};
    return v;
}

const std::vector<Seq>& jung() {
    static const std::vector<Seq> v = {{u'ㅏ'}, {u'ㅐ'}, {u'ㅑ'}, {u'ㅒ'}, {u'ㅓ'}, {u'ㅔ'}, {u'ㅕ'}, {u'ㅖ'}, {u'ㅗ'}, {u'ㅗ', u'ㅏ'}, {u'ㅗ', u'ㅐ'},
                                       {u'ㅗ', u'ㅣ'}, {u'ㅛ'}, {u'ㅜ'}, {u'ㅜ', u'ㅓ'}, {u'ㅜ', u'ㅔ'}, {u'ㅜ', u'ㅣ'}, {u'ㅠ'}, {u'ㅡ'}, {u'ㅡ', u'ㅣ'}, {u'ㅣ'}};
    return v;
}

const std::vector<Seq>& jong() {
    static const std::vector<Seq> v = {{},           {u'ㄱ'},       {u'ㄲ'},       {u'ㄱ', u'ㅅ'}, {u'ㄴ'},       {u'ㄴ', u'ㅈ'}, {u'ㄴ', u'ㅎ'},
                                       {u'ㄷ'},       {u'ㄹ'},       {u'ㄹ', u'ㄱ'}, {u'ㄹ', u'ㅁ'}, {u'ㄹ', u'ㅂ'}, {u'ㄹ', u'ㅅ'}, {u'ㄹ', u'ㅌ'},
                                       {u'ㄹ', u'ㅍ'}, {u'ㄹ', u'ㅎ'}, {u'ㅁ'},       {u'ㅂ'},       {u'ㅂ', u'ㅅ'}, {u'ㅅ'},       {u'ㅆ'},
                                       {u'ㅇ'},       {u'ㅈ'},       {u'ㅊ'},       {u'ㅋ'},       {u'ㅌ'},       {u'ㅍ'},       {u'ㅎ'}};
    return v;
}

using Hash = std::map<int, int>;

Hash make_hash(const std::u16string& chars, int first_index) {
    Hash h;
    h[0] = 0;
    for (size_t i = 0; i < chars.size(); i++) h[(int)chars[i]] = (int)i + first_index;
    return h;
}

const Hash& consonants_hash() {
    static const Hash h = make_hash(u"ㄱㄲㄳㄴㄵㄶㄷㄸㄹㄺㄻㄼㄽㄾㄿㅀㅁㅂㅃㅄㅅㅆㅇㅈㅉㅊㅋㅌㅍㅎ", 0);
    return h;
}

const Hash& cho_hash() {
    static const Hash h = make_hash(u"ㄱㄲㄴㄷㄸㄹㅁㅂㅃㅅㅆㅇㅈㅉㅊㅋㅌㅍㅎ", 0);
    return h;
}

const Hash& jung_hash() {
    static const Hash h = make_hash(u"ㅏㅐㅑㅒㅓㅔㅕㅖㅗㅘㅙㅚㅛㅜㅝㅞㅟㅠㅡㅢㅣ", 0);
    return h;
}

const Hash& jong_hash() {
    static const Hash h = make_hash(u"ㄱㄲㄳㄴㄵㄶㄷㄹㄺㄻㄼㄽㄾㄿㅀㅁㅂㅄㅅㅆㅇㅈㅊㅋㅌㅍㅎ", 1);
    return h;
}

using ComplexHash = std::map<int, std::map<int, int>>;

ComplexHash make_complex_hash(const std::vector<std::u16string>& triples) {
    ComplexHash h;
    for (const std::u16string& t : triples) h[(int)t[0]][(int)t[1]] = (int)t[2];
    return h;
}

const ComplexHash& complex_consonants_hash() {
    static const ComplexHash h =
        make_complex_hash({u"ㄱㅅㄳ", u"ㄴㅈㄵ", u"ㄴㅎㄶ", u"ㄹㄱㄺ", u"ㄹㅁㄻ", u"ㄹㅂㄼ", u"ㄹㅅㄽ", u"ㄹㅌㄾ", u"ㄹㅍㄿ", u"ㄹㅎㅀ", u"ㅂㅅㅄ"});
    return h;
}

const ComplexHash& complex_vowels_hash() {
    static const ComplexHash h = make_complex_hash({u"ㅗㅏㅘ", u"ㅗㅐㅙ", u"ㅗㅣㅚ", u"ㅜㅓㅝ", u"ㅜㅔㅞ", u"ㅜㅣㅟ", u"ㅡㅣㅢ"});
    return h;
}

double lookup(const Hash& h, double c) {
    if (std::isnan(c)) return std::nan("");
    auto it = h.find((int)c);
    return it == h.end() ? std::nan("") : it->second;
}

bool has(const Hash& h, double c) { return !std::isnan(lookup(h, c)); }

bool is_consonant(double c) { return has(consonants_hash(), c); }
bool is_cho(double c) { return has(cho_hash(), c); }
bool is_jung(double c) { return has(jung_hash(), c); }
bool is_jong(double c) { return has(jong_hash(), c); }
bool is_hangul(double c) { return 0xAC00 <= c && c <= 0xD7A3; }

double joinable(const ComplexHash& h, double a, double b) {
    if (std::isnan(a) || std::isnan(b)) return 0;
    auto it = h.find((int)a);
    if (it == h.end()) return 0;
    auto jt = it->second.find((int)b);
    return jt == it->second.end() ? 0 : jt->second;
}

double jung_joinable(double a, double b) { return joinable(complex_vowels_hash(), a, b); }
double jong_joinable(double a, double b) { return joinable(complex_consonants_hash(), a, b); }

char16_t from_char_code(double v) {
    if (std::isnan(v) || std::isinf(v)) return 0;
    long long n = (long long)std::trunc(v);
    return (char16_t)(uint16_t)(n & 0xFFFF);
}

Seq disassemble(const std::u16string& string) {
    Seq result;
    for (char16_t ch : string) {
        double code = ch;
        if (is_hangul(code)) {
            int c = (int)code - kOffset;
            int jg = c % 28;
            int ju = (c - jg) / 28 % 21;
            int ch_i = (c - jg) / 28 / 21;
            for (char16_t x : cho()[(size_t)ch_i]) result.push_back(x);
            for (char16_t x : jung()[(size_t)ju]) result.push_back(x);
            if (jg > 0) {
                for (char16_t x : jong()[(size_t)jg]) result.push_back(x);
            }
        } else if (is_consonant(code)) {
            const Seq& r = is_cho(code) ? cho()[(size_t)lookup(cho_hash(), code)] : jong()[(size_t)lookup(jong_hash(), code)];
            for (char16_t x : r) result.push_back(x);
        } else if (is_jung(code)) {
            for (char16_t x : jung()[(size_t)lookup(jung_hash(), code)]) result.push_back(x);
        } else {
            result.push_back(ch);
        }
    }
    return result;
}

std::u16string assemble(const Seq& array) {
    std::u16string result;
    int complete_index = -1;
    bool jong_joined = false;
    int length = (int)array.size();
    auto at = [&](int i) -> double { return i >= 0 && i < length ? (double)array[(size_t)i] : std::nan(""); };
    auto make_hangul = [&](int index) {
        double cho_c = 0, jung1 = 0, jung2 = 0, jong1 = 0, jong2 = 0;
        std::u16string hangul;
        jong_joined = false;
        if (complete_index + 1 > index) return;
        auto compose = [&]() {
            return from_char_code((lookup(cho_hash(), cho_c) * 21 + lookup(jung_hash(), jung1)) * 28 + lookup(jong_hash(), jong1) + kOffset);
        };
        for (int step = 1;; step++) {
            if (step == 1) {
                cho_c = at(complete_index + step);
                if (is_jung(cho_c)) {
                    if (complete_index + step + 1 <= index && is_jung(jung1 = at(complete_index + step + 1))) {
                        result.push_back(from_char_code(jung_joinable(cho_c, jung1)));
                        complete_index = index;
                        return;
                    }
                    result.push_back((char16_t)array[(size_t)(complete_index + step)]);
                    complete_index = index;
                    return;
                } else if (!is_cho(cho_c)) {
                    result.push_back((char16_t)array[(size_t)(complete_index + step)]);
                    complete_index = index;
                    return;
                }
                hangul = std::u16string(1, (char16_t)array[(size_t)(complete_index + step)]);
            } else if (step == 2) {
                jung1 = at(complete_index + step);
                if (is_cho(jung1)) {
                    cho_c = jong_joinable(cho_c, jung1);
                    hangul = std::u16string(1, from_char_code(cho_c));
                    result += hangul;
                    complete_index = index;
                    return;
                }
                hangul = std::u16string(1, from_char_code((lookup(cho_hash(), cho_c) * 21 + lookup(jung_hash(), jung1)) * 28 + kOffset));
            } else if (step == 3) {
                jung2 = at(complete_index + step);
                if (jung_joinable(jung1, jung2)) jung1 = jung_joinable(jung1, jung2);
                else jong1 = jung2;
                hangul = std::u16string(1, compose());
            } else if (step == 4) {
                jong2 = at(complete_index + step);
                if (jong_joinable(jong1, jong2)) jong1 = jong_joinable(jong1, jong2);
                else jong1 = jong2;
                hangul = std::u16string(1, compose());
            } else if (step == 5) {
                jong2 = at(complete_index + step);
                jong1 = jong_joinable(jong1, jong2);
                hangul = std::u16string(1, compose());
            }
            if (complete_index + step >= index) {
                result += hangul;
                complete_index = index;
                return;
            }
        }
    };
    int stage = 0;
    double previous_code = std::nan("");
    int i = 0;
    for (i = 0; i < length; i++) {
        double code = array[(size_t)i];
        if (!is_cho(code) && !is_jung(code) && !is_jong(code)) {
            make_hangul(i - 1);
            make_hangul(i);
            stage = 0;
            continue;
        }
        if (stage == 0) {
            if (is_cho(code)) stage = 1;
            else if (is_jung(code)) stage = 4;
        } else if (stage == 1) {
            if (is_jung(code)) {
                stage = 2;
            } else {
                if (jong_joinable(previous_code, code)) stage = 5;
                else make_hangul(i - 1);
            }
        } else if (stage == 2) {
            if (is_jong(code)) {
                stage = 3;
            } else if (is_jung(code)) {
                if (!jung_joinable(previous_code, code)) {
                    make_hangul(i - 1);
                    stage = 4;
                }
            } else {
                make_hangul(i - 1);
                stage = 1;
            }
        } else if (stage == 3) {
            if (is_jong(code)) {
                if (!jong_joined && jong_joinable(previous_code, code)) {
                    jong_joined = true;
                } else {
                    make_hangul(i - 1);
                    stage = 1;
                }
            } else if (is_cho(code)) {
                make_hangul(i - 1);
                stage = 1;
            } else if (is_jung(code)) {
                make_hangul(i - 2);
                stage = 2;
            }
        } else if (stage == 4) {
            if (is_jung(code)) {
                if (jung_joinable(previous_code, code)) {
                    make_hangul(i);
                    stage = 0;
                } else {
                    make_hangul(i - 1);
                }
            } else {
                make_hangul(i - 1);
                stage = 1;
            }
        } else if (stage == 5) {
            if (is_jung(code)) {
                make_hangul(i - 2);
                stage = 2;
            } else {
                make_hangul(i - 1);
                stage = 1;
            }
        }
        previous_code = code;
    }
    make_hangul(i - 1);
    return result;
}

std::u16string to_u16(const std::string& s) {
    std::u16string out;
    for (char32_t c : utf8_decode(s)) {
        if (c < 0x10000) {
            out.push_back((char16_t)c);
        } else {
            c -= 0x10000;
            out.push_back((char16_t)(0xD800 + (c >> 10)));
            out.push_back((char16_t)(0xDC00 + (c & 0x3FF)));
        }
    }
    return out;
}

std::string from_u16(const std::u16string& s) {
    std::u32string u;
    for (size_t i = 0; i < s.size(); i++) {
        char32_t c = s[i];
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] < 0xE000) {
            c = 0x10000 + ((c - 0xD800) << 10) + (s[i + 1] - 0xDC00);
            i++;
        }
        u.push_back(c);
    }
    return utf8_encode(u);
}

}

struct KeyboardLayouts {
    std::vector<std::string> normal;
    std::vector<std::string> shift;
};

const std::map<std::string, KeyboardLayouts>& keyboard_layouts() {
    static const std::map<std::string, KeyboardLayouts> layouts = {
        {"EN",
         {{"` 1 2 3 4 5 6 7 8 9 0 - = {bksp}", "q w e r t y u i o p [ ] \\", "{lock} a s d f g h j k l ; '", "z x c v b n m , . / {confirm} {space}"},
          {"~ ! @ # $ % ^ & * ( ) _ + {bksp}", "Q W E R T Y U I O P { } |", "{lock} A S D F G H J K L : \"", "Z X C V B N M < > ? {confirm} {space}"}}},
        {"JP",
         {{"1 2 3 4 5 6 7 8 9 0 - ^ ¥ {bksp}", "た て い す か ん な に ら せ ゛ ゜ む",
           "{lock} ち と し は き く ま の り れ け",
           "つ さ そ ひ こ み も ね る め {confirm} {space}"},
          {"! \" # $ % & ' ( ) ́ = ~ | {bksp}", "た て ぃ す か ん な に ら せ 「 」 む",
           "{lock} ち と し は き く ま の り れ け",
           "っ さ そ ひ こ み も 、 。 ・ {confirm} {space}"}}},
        {"KR",
         {{"` 1 2 3 4 5 6 7 8 9 0 - = {bksp} {rarrow} {larrow}", "ㅂ ㅈ ㄷ ㄱ ㅅ ㅛ ㅕ ㅑ ㅐ ㅔ [ ] ₩", "{lock} ㅁ ㄴ ㅇ ㄹ ㅎ ㅗ ㅓ ㅏ ㅣ ; '",
           "ㅋ ㅌ ㅊ ㅍ ㅠ ㅜ ㅡ , . / {confirm} {space}"},
          {"~ ! @ # $ % ^ & * ( ) _ + {bksp}", "ㅃ ㅉ ㄸ ㄲ ㅆ ㅛ ㅕ ㅑ ㅒ ㅖ { } |", "{lock} ㅁ ㄴ ㅇ ㄹ ㅎ ㅗ ㅓ ㅏ ㅣ : \"",
           "ㅋ ㅌ ㅊ ㅍ ㅠ ㅜ ㅡ < > ? {confirm} {space}"}}}};
    return layouts;
}

const std::map<std::string, std::string>& special_keys() {
    static const std::map<std::string, std::string> keys = {{"{lock}", "⇪"},   {"{confirm}", "OK"},  {"{space}", " "},
                                                            {"{bksp}", "⌫"},   {"{rarrow}", "→"}, {"{larrow}", "←"},
                                                            {"{tradch}", "繁"},     {"{simpch}", "简"}};
    return keys;
}

const std::string* special_key(const std::string& token) {
    auto it = special_keys().find(token);
    return it == special_keys().end() ? nullptr : &it->second;
}

}

void WindowOmoriNameInputName::initialize(int max) {
    max_characters = max;
    WindowBase::initialize(12, 12, window_width(), window_height());
    set_openness(0);
    deactivate();
    clear_name("");
    refresh();
}

void WindowOmoriNameInputName::refresh() {
    c().clear();
    c().font_size = 23;
    c().draw_text(data::js_string(menu::blue("Omori_Name_Input")["nameask"]), 0, 1, c().width(), c().font_size, "center");
    c().fill_rect(0, 32, c().width(), 2, "rgba(255, 255, 255, 1)");
    refresh_text();
}

void WindowOmoriNameInputName::clear_name(const std::string& name) {
    text.clear();
    std::vector<std::string> letters = utf8_chars(name);
    for (int i = 0; i < max_characters; i++) text.push_back(i < (int)letters.size() ? letters[(size_t)i] : std::string(" "));
    text_index = std::max((int)letters.size() - 1, 0);
    refresh_text();
}

std::string WindowOmoriNameInputName::name() const {
    std::string joined;
    for (const std::string& s : text) joined += s;
    return js_trim(joined);
}

bool WindowOmoriNameInputName::add(const std::string& character) {
    if (text_index < max_characters) {
        text[(size_t)text_index] = character;
        text_index = std::min(text_index + 1, max_characters - 1);
        refresh_text();
        return true;
    }
    return false;
}

bool WindowOmoriNameInputName::back() {
    if (text_index > -1) {
        text[(size_t)text_index] = "";
        text_index = std::max(text_index - 1, 0);
        refresh_text();
        return true;
    }
    return false;
}

void WindowOmoriNameInputName::refresh_text() {
    c().clear_rect(0, 34, c().width(), c().height() - 34);
    c().font_size = 28;
    double width = 20;
    for (size_t i = 0; i < text.size(); i++) {
        double x = 6 + (i * (width + 3));
        double y = 34;
        c().draw_text(text[i], x, y, width, c().font_size, "center");
        c().paint_opacity = text_index == (int)i ? 255 : 100;
        c().fill_rect(x, y + c().font_size + 4, width, 2, "rgba(255, 255, 255, 1)");
        c().paint_opacity = 255;
    }
}

bool WindowOmoriNameInputName::add_hangul(const std::string& character) {
    hangul::Seq current = hangul::disassemble(hangul::to_u16(text[(size_t)text_index]));
    for (char16_t c : hangul::to_u16(character)) current.push_back(c);
    std::u16string assembled = hangul::assemble(current);
    bool result = true;
    if (assembled.size() <= 1) {
        text[(size_t)text_index] = hangul::from_u16(assembled);
    } else {
        size_t pos = 0;
        while (pos < assembled.size()) {
            char16_t last = assembled[pos++];
            text[(size_t)text_index] = hangul::from_u16(std::u16string(1, last));
            if (pos < assembled.size()) text_index = text_index + 1;
            if (text_index >= max_characters) {
                text_index = max_characters - 1;
                result = false;
                break;
            }
        }
    }
    refresh_text();
    return result;
}

bool WindowOmoriNameInputName::back_hangul() {
    const std::string& current = text[(size_t)text_index];
    if (current.empty()) return back();
    hangul::Seq dis = hangul::disassemble(hangul::to_u16(current));
    if (!dis.empty()) dis.pop_back();
    text[(size_t)text_index] = hangul::from_u16(hangul::assemble(dis));
    refresh_text();
    return true;
}

void VirtualKeyboard::initialize() {
    double ww = std::floor(640 / 1.2);
    double hh = std::floor(480 / 2.5);
    double x = 640 / 2.0 - ww / 2;
    double y = 480 / 2.0 - hh / 2;
    layout_type_ = "default";
    is_caps_ = false;
    determine_layout_data();
    WindowSelectable::initialize(x, y, ww, hh);
    set_openness(0);
    refresh();
}

std::string VirtualKeyboard::language() const { return rpg::to_upper(data::language()); }

bool VirtualKeyboard::add(const std::string& character) {
    if (language() == "KR") return name_window->add_hangul(character);
    return name_window->add(character);
}

bool VirtualKeyboard::back() {
    if (language() == "KR") return name_window->back_hangul();
    return name_window->back();
}

void VirtualKeyboard::confirm_entry() {
    std::string entry = name_window->name();
    if (!entry.empty()) {
        std::string lower = rpg::to_lower(entry);
        if (lower == rpg::to_lower(data::js_string(menu::blue("OMOCAT")["text"]))) rpg::game_system().unlock_achievement("YOU_THINK_YOU_RE_CLEVER_HUH");
        if (contains_bad_word(lower)) {
            play_buzzer_sound();
            return;
        }
        dispose();
        name_window->close();
        int variable_id = name_variable_id();
        if (variable_id > 0) rpg::game_variables().set_value(variable_id, js::Val::str(entry));
    } else {
        play_buzzer_sound();
    }
}

void VirtualKeyboard::process_ok() {
    std::string ch = character();
    if (special_key(ch)) {
        if (ch == "{lock}" || ch == "{tradch}" || ch == "{simpch}") {
            play_ok();
            is_caps_ = !is_caps_;
            layout_type_ = is_caps_ ? "shift" : "default";
            determine_layout_data();
            refresh();
        } else if (ch == "{confirm}") {
            confirm_entry();
        } else if (ch == "{space}") {
            name_window->add(" ");
        } else if (ch == "{bksp}") {
            if (back()) play_cancel();
        } else if (ch == "{larrow}") {
            play_cursor();
            name_window->text_index = std::max(name_window->text_index - 1, 0);
            name_window->refresh_text();
        } else if (ch == "{rarrow}") {
            play_cursor();
            name_window->text_index = std::min(name_window->text_index + 1, name_window->max_characters - 1);
            name_window->refresh_text();
        }
    } else {
        if (add(ch)) play_ok();
        else play_buzzer();
    }
}

void VirtualKeyboard::setup() {
    activate();
    select(0);
    open();
}

void VirtualKeyboard::dispose() {
    deactivate();
    close();
}

void VirtualKeyboard::refresh() {
    determine_layout_data();
    WindowSelectable::refresh();
}

void VirtualKeyboard::determine_layout_data() {
    current_layout_.clear();
    layout_cols_ = 0;
    auto it = keyboard_layouts().find(language());
    if (it == keyboard_layouts().end()) {
        rpg::script_error("VirtualKeyboard", "no keyboard layout for language " + language());
        return;
    }
    const std::vector<std::string>& rows = layout_type_ == "shift" ? it->second.shift : it->second.normal;
    for (const std::string& row : rows) current_layout_.push_back(split_spaces(row));
    for (const auto& layout : current_layout_) {
        if (layout_cols_ < (int)layout.size()) layout_cols_ = (int)layout.size();
    }
    for (auto& layout : current_layout_) {
        if ((int)layout.size() >= layout_cols_) continue;
        int missing_cols = std::abs((int)layout.size() - layout_cols_);
        std::vector<std::string> back_pad, forward_pad;
        for (int i = 0; i < missing_cols; i++) {
            if (i % 2 == 0) back_pad.push_back(" ");
            else forward_pad.push_back(" ");
        }
        std::vector<std::string> res;
        res.insert(res.end(), back_pad.begin(), back_pad.end());
        res.insert(res.end(), layout.begin(), layout.end());
        res.insert(res.end(), forward_pad.begin(), forward_pad.end());
        layout = res;
    }
}

int VirtualKeyboard::max_items() { return layout_cols_ * (int)current_layout_.size(); }

int VirtualKeyboard::max_cols() { return std::max(layout_cols_, 1); }

std::string VirtualKeyboard::character(int index) {
    if (index < 0 || layout_cols_ <= 0) return "";
    size_t row = (size_t)(index / layout_cols_);
    size_t col = (size_t)(index % layout_cols_);
    if (row >= current_layout_.size() || col >= current_layout_[row].size()) return "";
    return current_layout_[row][col];
}

void VirtualKeyboard::draw_item(int index) {
    ItemRect rect = item_rect(index);
    std::string current = character(index);
    if (const std::string* label = special_key(current)) {
        if (current == "{confirm}") change_text_color(text_color(1));
        current = *label;
    }
    draw_text(current, rect.x, rect.y, rect.width, "center");
    reset_text_color();
}

namespace {

bool locale_starts_with(const char* prefix) {
    std::string locale = data::js_string(data::system()["locale"]);
    return locale.compare(0, std::string(prefix).size(), prefix) == 0;
}

bool is_japanese() { return locale_starts_with("ja"); }
bool is_russian() { return locale_starts_with("ru"); }

using Table = std::vector<std::string>;

Table make_table(std::initializer_list<const char*> chars) {
    Table t;
    for (const char* c : chars) t.push_back(c);
    return t;
}

const std::vector<Table>& name_input_tables() {
    static const Table latin1 = make_table({"A", "B", "C", "D", "E", "a", "b", "c", "d", "e", "F", "G", "H", "I", "J", "f", "g", "h", "i", "j",
                                            "K", "L", "M", "N", "O", "k", "l", "m", "n", "o", "P", "Q", "R", "S", "T", "p", "q", "r", "s", "t",
                                            "U", "V", "W", "X", "Y", "u", "v", "w", "x", "y", "Z", "[", "]", "^", "_", "z", "{", "}", "|", "~",
                                            "0", "1", "2", "3", "4", "!", "#", "$", "%", "&", "5", "6", "7", "8", "9", "(", ")", "*", "+", "-",
                                            "/", "=", "@", "<", ">", ":", ";", " ", "Page", "OK"});
    static const Table latin2 = make_table({"Á", "É", "Í", "Ó", "Ú", "á", "é", "í", "ó", "ú", "À", "È", "Ì", "Ò", "Ù", "à", "è", "ì", "ò", "ù",
                                            "Â", "Ê", "Î", "Ô", "Û", "â", "ê", "î", "ô", "û", "Ä", "Ë", "Ï", "Ö", "Ü", "ä", "ë", "ï", "ö", "ü",
                                            "Ā", "Ē", "Ī", "Ō", "Ū", "ā", "ē", "ī", "ō", "ū", "Ã", "Å", "Æ", "Ç", "Ð", "ã", "å", "æ", "ç", "ð",
                                            "Ñ", "Õ", "Ø", "Š", "Ŵ", "ñ", "õ", "ø", "š", "ŵ", "Ý", "Ŷ", "Ÿ", "Ž", "Þ", "ý", "ÿ", "ŷ", "ž", "þ",
                                            "Ĳ", "Œ", "ĳ", "œ", "ß", "«", "»", " ", "Page", "OK"});
    static const Table russia = make_table({"А", "Б", "В", "Г", "Д", "а", "б", "в", "г", "д", "Е", "Ё", "Ж", "З", "И", "е", "ё", "ж", "з", "и",
                                            "Й", "К", "Л", "М", "Н", "й", "к", "л", "м", "н", "О", "П", "Р", "С", "Т", "о", "п", "р", "с", "т",
                                            "У", "Ф", "Х", "Ц", "Ч", "у", "ф", "х", "ц", "ч", "Ш", "Щ", "Ъ", "Ы", "Ь", "ш", "щ", "ъ", "ы", "ь",
                                            "Э", "Ю", "Я", "^", "_", "э", "ю", "я", "%", "&", "0", "1", "2", "3", "4", "(", ")", "*", "+", "-",
                                            "5", "6", "7", "8", "9", ":", ";", " ", "", "OK"});
    static const Table japan1 = make_table({"あ", "い", "う", "え", "お", "が", "ぎ", "ぐ", "げ", "ご", "か", "き", "く", "け", "こ", "ざ", "じ", "ず", "ぜ", "ぞ",
                                            "さ", "し", "す", "せ", "そ", "だ", "ぢ", "づ", "で", "ど", "た", "ち", "つ", "て", "と", "ば", "び", "ぶ", "べ", "ぼ",
                                            "な", "に", "ぬ", "ね", "の", "ぱ", "ぴ", "ぷ", "ぺ", "ぽ", "は", "ひ", "ふ", "へ", "ほ", "ぁ", "ぃ", "ぅ", "ぇ", "ぉ",
                                            "ま", "み", "む", "め", "も", "っ", "ゃ", "ゅ", "ょ", "ゎ", "や", "ゆ", "よ", "わ", "ん", "ー", "～", "・", "＝", "☆",
                                            "ら", "り", "る", "れ", "ろ", "ゔ", "を", "　", "カナ", "決定"});
    static const Table japan2 = make_table({"ア", "イ", "ウ", "エ", "オ", "ガ", "ギ", "グ", "ゲ", "ゴ", "カ", "キ", "ク", "ケ", "コ", "ザ", "ジ", "ズ", "ゼ", "ゾ",
                                            "サ", "シ", "ス", "セ", "ソ", "ダ", "ヂ", "ヅ", "デ", "ド", "タ", "チ", "ツ", "テ", "ト", "バ", "ビ", "ブ", "ベ", "ボ",
                                            "ナ", "ニ", "ヌ", "ネ", "ノ", "パ", "ピ", "プ", "ペ", "ポ", "ハ", "ヒ", "フ", "ヘ", "ホ", "ァ", "ィ", "ゥ", "ェ", "ォ",
                                            "マ", "ミ", "ム", "メ", "モ", "ッ", "ャ", "ュ", "ョ", "ヮ", "ヤ", "ユ", "ヨ", "ワ", "ン", "ー", "～", "・", "＝", "☆",
                                            "ラ", "リ", "ル", "レ", "ロ", "ヴ", "ヲ", "　", "英数", "決定"});
    static const Table japan3 = make_table({"Ａ", "Ｂ", "Ｃ", "Ｄ", "Ｅ", "ａ", "ｂ", "ｃ", "ｄ", "ｅ", "Ｆ", "Ｇ", "Ｈ", "Ｉ", "Ｊ", "ｆ", "ｇ", "ｈ", "ｉ", "ｊ",
                                            "Ｋ", "Ｌ", "Ｍ", "Ｎ", "Ｏ", "ｋ", "ｌ", "ｍ", "ｎ", "ｏ", "Ｐ", "Ｑ", "Ｒ", "Ｓ", "Ｔ", "ｐ", "ｑ", "ｒ", "ｓ", "ｔ",
                                            "Ｕ", "Ｖ", "Ｗ", "Ｘ", "Ｙ", "ｕ", "ｖ", "ｗ", "ｘ", "ｙ", "Ｚ", "［", "］", "＾", "＿", "ｚ", "｛", "｝", "｜", "～",
                                            "０", "１", "２", "３", "４", "！", "＃", "＄", "％", "＆", "５", "６", "７", "８", "９", "（", "）", "＊", "＋", "－",
                                            "／", "＝", "＠", "＜", "＞", "：", "；", "　", "かな", "決定"});
    static const std::vector<Table> jp = {japan1, japan2, japan3};
    static const std::vector<Table> ru = {russia};
    static const std::vector<Table> latin = {latin1, latin2};
    if (is_japanese()) return jp;
    if (is_russian()) return ru;
    return latin;
}

class WindowNameEdit : public WindowBase {
public:
    void initialize(rpg::GameActor* actor, int max_length) {
        double width = window_width();
        double height = window_height();
        double x = (640 - width) / 2;
        double y = (480 - (height + fitting_height(9) + 8)) / 2;
        WindowBase::initialize(x, y, width, height);
        actor_ = actor;
        name_ = utf8_chars(actor->name());
        index_ = (int)name_.size();
        max_length_ = max_length;
        default_name_ = name_;
        deactivate();
        refresh();
    }
    void initialize(double x, double y, double w, double h) override { WindowBase::initialize(x, y, w, h); }

    double window_width() { return 480; }
    double window_height() { return fitting_height(4); }

    std::string name() const {
        std::string out;
        for (const std::string& s : name_) out += s;
        return out;
    }

    bool restore_default() {
        name_ = default_name_;
        index_ = (int)name_.size();
        refresh();
        return !name_.empty();
    }

    bool add(const std::string& ch) {
        if (index_ < max_length_) {
            name_.push_back(ch);
            index_++;
            refresh();
            return true;
        }
        return false;
    }

    bool back() {
        if (index_ > 0) {
            index_--;
            name_.resize((size_t)index_);
            refresh();
            return true;
        }
        return false;
    }

    double face_width() { return kFaceWidth; }

    double char_width() { return text_width(is_japanese() ? "Ａ" : "A"); }

    double left() {
        double name_center = (contents_width() + face_width()) / 2;
        double name_width = (max_length_ + 1) * char_width();
        return std::min(name_center - name_width / 2, contents_width() - name_width);
    }

    ItemRect item_rect(int index) {
        ItemRect r;
        r.x = left() + index * char_width();
        r.y = 54;
        r.width = char_width();
        r.height = line_height();
        return r;
    }

    ItemRect underline_rect(int index) {
        ItemRect r = item_rect(index);
        r.x++;
        r.y += r.height - 4;
        r.width -= 2;
        r.height = 2;
        return r;
    }

    void draw_underline(int index) {
        ItemRect r = underline_rect(index);
        c().paint_opacity = 48;
        c().fill_rect(r.x, r.y, r.width, r.height, normal_color());
        c().paint_opacity = 255;
    }

    void draw_char(int index) {
        ItemRect r = item_rect(index);
        reset_text_color();
        draw_text(index < (int)name_.size() ? name_[(size_t)index] : std::string(), r.x, r.y, 4294967295.0);
    }

    void refresh() {
        c().clear();
        draw_actor_face(actor_, 0, 0);
        for (int i = 0; i < max_length_; i++) draw_underline(i);
        for (int j = 0; j < (int)name_.size(); j++) draw_char(j);
        ItemRect r = item_rect(index_);
        set_cursor_rect(r.x, r.y, r.width, r.height);
    }

private:
    rpg::GameActor* actor_ = nullptr;
    std::vector<std::string> name_;
    std::vector<std::string> default_name_;
    int index_ = 0;
    int max_length_ = 0;
};

class WindowNameInput : public WindowSelectable {
public:
    void initialize(WindowNameEdit* edit_window) {
        double x = edit_window->x;
        double y = edit_window->y + edit_window->height() + 8;
        double width = edit_window->width();
        double height = window_height();
        WindowSelectable::initialize(x, y, width, height);
        edit_window_ = edit_window;
        page_ = 0;
        index_ = 0;
        refresh();
        update_cursor();
        activate();
    }
    void initialize(double x, double y, double w, double h) override { WindowSelectable::initialize(x, y, w, h); }

    double window_height() { return fitting_height(9); }
    int max_cols() override { return 10; }
    int max_items() override { return 90; }

    std::string character() {
        const std::vector<Table>& t = name_input_tables();
        return index_ < 88 ? t[(size_t)page_][(size_t)index_] : std::string();
    }

    bool is_page_change() { return index_ == 88; }
    bool is_ok() { return index_ == 89; }

    ItemRect item_rect(int index) override {
        ItemRect r;
        r.x = index % 10 * 42 + std::floor(index % 10 / 5) * 24;
        r.y = std::floor(index / 10) * line_height();
        r.width = 42;
        r.height = line_height();
        return r;
    }

    void refresh() override {
        const std::vector<Table>& table = name_input_tables();
        c().clear();
        reset_text_color();
        for (int i = 0; i < 90; i++) {
            ItemRect r = item_rect(i);
            r.x += 3;
            r.width -= 6;
            draw_text(table[(size_t)page_][(size_t)i], r.x, r.y, r.width, "center");
        }
    }

    void update_cursor() override {
        ItemRect r = item_rect(index_);
        set_cursor_rect(r.x, r.y, r.width, r.height);
    }

    bool is_cursor_movable() override { return active; }

    void cursor_down(bool wrap) override {
        if (index_ < 80 || wrap) index_ = (index_ + 10) % 90;
    }

    void cursor_up(bool wrap) override {
        if (index_ >= 10 || wrap) index_ = (index_ + 80) % 90;
    }

    void cursor_right(bool wrap) override {
        if (index_ % 10 < 9) index_++;
        else if (wrap) index_ -= 9;
    }

    void cursor_left(bool wrap) override {
        if (index_ % 10 > 0) index_--;
        else if (wrap) index_ += 9;
    }

    void cursor_pagedown() override {
        page_ = (page_ + 1) % (int)name_input_tables().size();
        refresh();
    }

    void cursor_pageup() override {
        int n = (int)name_input_tables().size();
        page_ = (page_ + n - 1) % n;
        refresh();
    }

    void process_cursor_move() override {
        int last_page = page_;
        WindowSelectable::process_cursor_move();
        update_cursor();
        if (page_ != last_page) play_cursor();
    }

    void process_handling() override {
        if (is_open() && active) {
            if (input::is_triggered(input::SHIFT)) process_jump();
            if (input::is_repeated(input::CANCEL)) process_back();
            if (input::is_repeated(input::OK)) process_ok();
        }
    }

    bool is_cancel_enabled() override { return true; }
    void process_cancel() override { process_back(); }

    void process_jump() {
        if (index_ != 89) {
            index_ = 89;
            play_cursor();
        }
    }

    void process_back() {
        if (edit_window_->back()) play_cancel();
    }

    void process_ok() override {
        if (!character().empty()) {
            on_name_add();
        } else if (is_page_change()) {
            play_ok();
            cursor_pagedown();
        } else if (is_ok()) {
            on_name_ok();
        }
    }

    void on_name_add() {
        if (edit_window_->add(character())) play_ok();
        else play_buzzer();
    }

    void on_name_ok() {
        if (edit_window_->name().empty()) {
            if (edit_window_->restore_default()) play_ok();
            else play_buzzer();
        } else {
            play_ok();
            call_ok_handler();
        }
    }

private:
    WindowNameEdit* edit_window_ = nullptr;
    int page_ = 0;
};

class SceneName : public SceneMenuBase {
public:
    SceneName(int actor_id, int max_length) : actor_id_(actor_id), max_length_(max_length) {}

    const char* name() const override { return "Scene_Name"; }

    void create() override {
        SceneMenuBase::create();
        actor_ = rpg::game_actors().actor(actor_id_);
        create_edit_window();
        create_input_window();
    }

    void start() override {
        SceneMenuBase::start();
        edit_window_->refresh();
    }

private:
    int actor_id_ = 0;
    int max_length_ = 0;
    std::shared_ptr<WindowNameEdit> edit_window_;
    std::shared_ptr<WindowNameInput> input_window_;

    void create_edit_window() {
        edit_window_ = WindowBase::make<WindowNameEdit>(actor_, max_length_);
        add_window(edit_window_);
    }

    void create_input_window() {
        input_window_ = WindowBase::make<WindowNameInput>(edit_window_.get());
        input_window_->set_handler("ok", [this] { on_input_ok(); });
        add_window(input_window_);
    }

    void on_input_ok() {
        actor_->set_name(edit_window_->name());
        pop_scene();
    }
};

}

rpg::SceneFactory name_input_scene(int actor_id, int max_chars) {
    return rpg::SceneFactory{"Scene_Name",
                             [actor_id, max_chars]() -> std::unique_ptr<rpg::Scene> { return std::unique_ptr<rpg::Scene>(new SceneName(actor_id, max_chars)); }};
}

}
