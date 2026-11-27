// The Trophies page's data (port/kz1hd/trophy_list.h): the rows in the TRP's order, grade and state words, the
// count, a hidden trophy kept hidden until unlocked, the words in each language, and the Lua that carries them
// (quotes escaped, cut in pieces that fit); with no trophy set, a title without "0 / 0" and the message that says so.
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

#include "trophy_list.h"

static int fails;
#define EXPECT(c, what) do { if (!(c)) { std::fprintf(stderr, "FAIL: %s\n", what); fails++; } else std::printf("ok   %s\n", what); } while (0)

static std::string utc_day(uint64_t t)
{
    const time_t tt = (time_t)t;
    char b[16];
    std::strftime(b, sizeof b, "%Y-%m-%d", std::gmtime(&tt));
    return b;
}

int main()
{
    std::vector<tl::Def> defs;
    for (int id = 0; id < 59; id++)
        defs.push_back({ id, id == 0 ? tl::kPlatinum : id == 3 ? tl::kGold : id == 2 ? tl::kSilver : tl::kBronze,
                         false, false, 0, "Name " + std::to_string(id), "Detail " + std::to_string(id) });
    defs[30].unlocked = true; defs[30].timestamp = 1795651200;   // 2026-11-26 00:00 UTC
    defs[30].name = "Burn Baby Burn";
    defs[34].unlocked = true; defs[34].timestamp = 1795737599;   // 2026-11-26 23:59:59 UTC
    defs[34].name = "They're On Our Side Now";
    defs[40].hidden = true;
    defs[41].hidden = true; defs[41].unlocked = true; defs[41].timestamp = 1795651200;
    defs[5].name = "A|B;C";

    const tl::Words& en = tl::words_for(nullptr);
    const tl::Page p = tl::build(defs, en, utc_day);
    EXPECT(p.total == 59 && p.got == 3 && p.rows.size() == 59, "59 rows, 3 unlocked");
    EXPECT(p.rows[0].label == "PLATINUM   Name 0", "row 0 is the platinum, in the TRP's order");
    EXPECT(p.rows[2].label.rfind("SILVER", 0) == 0 && p.rows[3].label.rfind("GOLD", 0) == 0, "grade words");
    EXPECT(p.rows[30].label == "BRONZE   Burn Baby Burn" && p.rows[30].value == "2026-11-26",
           "an unlocked one shows its day");
    EXPECT(p.rows[34].value == "2026-11-26", "the day of the unlock, not the next");
    EXPECT(p.rows[1].value == "Locked" && p.rows[1].detail == "Detail 1", "a locked one says so and keeps its detail");
    EXPECT(p.rows[40].label == "BRONZE   Hidden trophy" && p.rows[40].detail.empty(),
           "a hidden one stays hidden while locked");
    EXPECT(p.rows[41].label == "BRONZE   Name 41" && p.rows[41].detail == "Detail 41",
           "a hidden one shows once unlocked");

    const tl::Words& fr = tl::words_for("2");
    EXPECT(std::string(fr.locked) == "Verrouill\xC3\xA9" && std::string(fr.grade[0]) == "BRONZE",
           "PS3_LANG=2 is French");
    EXPECT(std::string(tl::words_for("nl").page) == "Trofee\xC3\xABn" && std::string(en.page) == "Trophies",
           "the menu item's word");
    EXPECT(&tl::words_for("de") == &tl::words_for("4") && std::string(tl::words_for("DE").trophies) == "TROPH\xC3\x84""EN",
           "a code or its number, any case");
    EXPECT(&tl::words_for("ja") == &en && &tl::words_for("99") == &en && &tl::words_for("xx") == &en,
           "English for the others");
    const tl::Page pf = tl::build(defs, fr, utc_day);
    EXPECT(pf.rows[1].value == "Verrouill\xC3\xA9" && pf.rows[0].label.rfind("PLATINE", 0) == 0, "French rows");

    EXPECT(tl::lua_str("They're \\ on") == "'They\\'re \\\\ on'", "quotes and backslashes escaped");
    EXPECT(tl::lua_str("a\nb") == "'a\\nb'", "a new line escaped");
    EXPECT(tl::latin1("Verrouill\xC3\xA9 \xC2\xB7 \xE2\x80\xA6") == "Verrouill\xE9 \xB7 ?", "UTF-8 to Latin-1, the rest '?'");

    const std::vector<std::string> c = tl::lua_chunks(p, en, 1200);
    std::string all;
    for (const std::string& s : c) all += s;
    bool fit = true;
    for (const std::string& s : c) fit = fit && s.size() <= 1200;
    EXPECT(c.size() > 1 && fit, "the Lua comes in pieces that fit");
    EXPECT(all.rfind("KZTRDATA = { n = 59, got = 3, title = 'TROPHIES  3 / 59' }\n", 0) == 0, "the header first");
    EXPECT(all.find("KZTRDATA[35] = { l = 'BRONZE   They\\'re On Our Side Now', v = '2026-11-26', d = 'Detail 34' }\n") !=
           std::string::npos, "row 35 (id 34), escaped");
    EXPECT(all.find("l = 'BRONZE   A|B;C'") != std::string::npos, "a row is text: its words kept as they are");
    EXPECT(all.find("KZTRDATA[59] = ") != std::string::npos && all.find("KZTRDATA[60]") == std::string::npos,
           "every row, once");

    const tl::Page none = tl::build({}, en, utc_day);
    const std::vector<std::string> cn = tl::lua_chunks(none, en, 1200);
    EXPECT(cn.size() == 1 && cn[0] == "KZTRDATA = { n = 0, got = 0, title = 'TROPHIES', missing = 'Trophy set not found: "
                                      "see GETTING-FILES' }\n", "no trophy set: no count, the message");
    const std::vector<std::string> cf = tl::lua_chunks(none, fr, 1200);
    EXPECT(cf[0].find("missing = 'Liste des troph\xE9""es introuvable : voir GETTING-FILES'") != std::string::npos,
           "the message in the game's language, in Latin-1");
    bool all_missing = true;
    for (const char* l : { "en", "fr", "es", "de", "it", "nl" }) all_missing = all_missing && tl::words_for(l).missing;
    EXPECT(all_missing, "the message in every language");
    EXPECT(all.find("missing") == std::string::npos, "a set with rows has no message");

    // the menu script in whole blocks: nothing lost, every piece fits, each starts a block
    std::string script;
    for (int k = 0; k < 40; k++)
        script += "if not KZ" + std::to_string(k) + " then\n KZ" + std::to_string(k) + " = 1\n if x then y() end\n" +
                  std::string(k == 7 ? 900 : 150, ' ') + "\nend\n";
    const std::vector<std::string> bl = tl::lua_blocks(script, 600);
    std::string back;
    bool fits = true, starts = true;
    for (const std::string& s : bl) {
        back += s;
        fits = fits && (s.size() <= 600 || s.find("\nif ", 1) == std::string::npos);
        starts = starts && s.rfind("if not KZ", 0) == 0;
    }
    EXPECT(back == script && bl.size() > 5, "the script's pieces add up to it");
    EXPECT(fits && starts, "each piece is whole blocks that fit (a longer block alone)");
    EXPECT(tl::lua_blocks("if a then end\n", 4096).size() == 1, "a short script is one piece");

    if (fails) return 1;
    std::puts("test_trophy_list: ok");
    return 0;
}
