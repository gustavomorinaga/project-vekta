// The Trophies page's data (docs/dev/trophies.md, stage 3): the 59 rows in the TRP's order, their words in the game's
// language, and the Lua that hands them to the page (pcoptions.cpp). ENHANCEMENT: the PS3 game has no such page (the
// console's system software listed the trophies). Header-only and free of the game, so a host test checks it
// (tools/tests/test_trophy_list.cpp).
#pragma once

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace tl {

// The grades as sceNpTrophy numbers them (SCE_NP_TROPHY_GRADE_*), so this header needs none of the HLE's.
enum { kPlatinum = 1, kGold = 2, kSilver = 3, kBronze = 4 };

// The words the notice and the page show, in the language the game is told (PS3_LANG, a CELL_SYSUTIL_LANG_* number
// or its code, as sceNpTrophy picks the TRP's names), English for the others. Escapes keep the source ASCII.
struct Words {
    const char *trophy, *trophies, *grade[4];   // grade: bronze, silver, gold, platinum
    const char *locked, *hidden, *page;   // page: the pause menu's item
};

inline const Words& words_for(const char* lang)
{
    static const Words en = { "TROPHY", "TROPHIES", { "BRONZE", "SILVER", "GOLD", "PLATINUM" }, "Locked",
                              "Hidden trophy", "Trophies" },
        fr = { "TROPH\xC3\x89""E", "TROPH\xC3\x89""ES", { "BRONZE", "ARGENT", "OR", "PLATINE" },
               "Verrouill\xC3\xA9", "Troph\xC3\xA9""e cach\xC3\xA9""e", "Troph\xC3\xA9""es" },
        es = { "TROFEO", "TROFEOS", { "BRONCE", "PLATA", "ORO", "PLATINO" }, "Bloqueado", "Trofeo oculto", "Trofeos" },
        de = { "TROPH\xC3\x84""E", "TROPH\xC3\x84""EN", { "BRONZE", "SILBER", "GOLD", "PLATIN" }, "Gesperrt",
               "Versteckte Troph\xC3\xA4""e", "Troph\xC3\xA4""en" },
        it = { "TROFEO", "TROFEI", { "BRONZO", "ARGENTO", "ORO", "PLATINO" }, "Bloccato", "Trofeo nascosto", "Trofei" },
        nl = { "TROFEE", "TROFEE\xC3\x8BN", { "BRONS", "ZILVER", "GOUD", "PLATINA" }, "Vergrendeld",
               "Verborgen trofee", "Trofee\xC3\xABn" };
    static const Words* const by_id[7] = { &en, &en, &fr, &es, &de, &it, &nl };   // 2 fr ... 6 nl
    static const char* const codes[7] = { "ja", "en", "fr", "es", "de", "it", "nl" };
    if (!lang || !*lang) return en;
    char* end = nullptr;
    const long n = std::strtol(lang, &end, 10);
    if (end && !*end) return n >= 0 && n < 7 ? *by_id[n] : en;
    for (int i = 2; i < 7; i++) {
        const char* a = lang;
        const char* b = codes[i];
        while (*a && *b && (*a | 0x20) == *b) { a++; b++; }
        if (!*a && !*b) return *by_id[i];
    }
    return en;
}

inline const Words& words() { return words_for(std::getenv("PS3_LANG")); }

inline int grade_index(int g) { return g == kPlatinum ? 3 : g == kGold ? 2 : g == kSilver ? 1 : 0; }

// One trophy as the page needs it (the HLE's ps3_trophy_def, copied by the caller).
struct Def {
    int id, grade;
    bool hidden, unlocked;
    uint64_t timestamp;   // UNIX seconds, 0 = locked
    std::string name, detail;
};

struct Row {
    std::string label;    // "BRONZE   Burn Baby Burn"
    std::string value;    // the unlock date, or "Locked"
    std::string detail;   // the description (the hidden word for a hidden one still locked)
};

struct Page {
    int got = 0, total = 0;
    std::vector<Row> rows;   // in the TRP's order (by id)
};

// date(t) writes the unlock day for UNIX seconds t (the port passes the local day; the test UTC)
template <class DateFn>
Page build(const std::vector<Def>& defs, const Words& w, DateFn&& date)
{
    Page p;
    p.total = (int)defs.size();
    for (const Def& d : defs) {
        p.got += d.unlocked;
        Row r;
        const bool masked = d.hidden && !d.unlocked;
        // three spaces between grade and name: the menu's font has no middle dot (it showed nothing)
        r.label = std::string(w.grade[grade_index(d.grade)]) + "   " + (masked ? w.hidden : d.name);
        r.value = d.unlocked && d.timestamp ? date(d.timestamp) : d.unlocked ? std::string("-") : std::string(w.locked);
        r.detail = masked ? std::string() : d.detail;
        p.rows.push_back(r);
    }
    return p;
}

// The game's menu text is one byte per letter, Latin-1 (UTF-8's two bytes showed as two letters): the TRP's UTF-8
// and these words converted, a letter beyond Latin-1 as '?'.
inline std::string latin1(const std::string& s)
{
    std::string o;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = (unsigned char)s[i];
        unsigned cp = c, n = 0;
        if (c >= 0xF0) cp = c & 7, n = 3;
        else if (c >= 0xE0) cp = c & 15, n = 2;
        else if (c >= 0xC0) cp = c & 31, n = 1;
        else if (c >= 0x80) n = 0, cp = '?';   // a stray continuation byte
        i++;
        for (; n && i < s.size() && ((unsigned char)s[i] & 0xC0) == 0x80; n--, i++) cp = cp << 6 | (s[i] & 63);
        o += n || cp > 0xFF ? '?' : (char)cp;
    }
    return o;
}

// A Lua string literal between single quotes, in Latin-1 (latin1). The option list of a row splits on '|' and ';', so those become ','.
inline std::string lua_str(const std::string& s, bool option = false)
{
    std::string o = "'";
    for (char c : latin1(s)) {
        if (c == '\\' || c == '\'') { o += '\\'; o += c; }
        else if (c == '\n') o += "\\n";
        else if (c == '\r') {}
        else if (option && (c == '|' || c == ';')) o += ',';
        else o += c;
    }
    return o + "'";
}

// The Lua that hands the page to a state: KZTRDATA = { n, got, title, [i] = { l, v, d } }, in pieces of at most
// max_bytes (the port runs each from a hook, on the guest's stack). The page's own script (pcoptions.cpp) reads it.
inline std::vector<std::string> lua_chunks(const Page& p, const Words& w, size_t max_bytes)
{
    std::vector<std::string> out;
    std::string cur = "KZTRDATA = { n = " + std::to_string(p.rows.size()) + ", got = " + std::to_string(p.got) +
                      ", title = " + lua_str(std::string(w.trophies) + "  " + std::to_string(p.got) + " / " +
                                             std::to_string(p.total)) + " }\n";
    for (size_t i = 0; i < p.rows.size(); i++) {
        const Row& r = p.rows[i];
        const std::string line = "KZTRDATA[" + std::to_string(i + 1) + "] = { l = " + lua_str(r.label, true) +
                                 ", v = " + lua_str(r.value, true) + ", d = " + lua_str(r.detail) + " }\n";
        if (cur.size() + line.size() > max_bytes) { out.push_back(cur); cur.clear(); }
        cur += line;
    }
    out.push_back(cur);
    return out;
}

}  // namespace tl
