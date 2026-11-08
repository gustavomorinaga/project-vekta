/*
 * KZ_DEBUG_GIVE (testing): weapons given to the player through the game's
 * own script call, PLAYER:AddWeapon(name, primary ammo, secondary ammo), the
 * one the mission scripts use (e.g. PLAYER:AddWeapon('M82', 180, 0)), in the
 * order given; the last is then selected (PLAYER:SelectWeapon), so put the
 * weapon to hold last (grenades given after a gun take the hand). pcoptions.cpp runs the
 * snippet in each Lua state that has a PLAYER, once (KZGIVEN).
 *
 *   KZ_DEBUG_GIVE="HandGrenade:3:0,ChainGun:731:0"
 *
 * Names the scripts use: M82, Pistol, SMG, ChainGun (Rico's M224-A3), H_LAR,
 * H_Pistol, H_SniperRifle, H_MRL, Knife, HandGrenade, H_ObserverDesignator.
 * Header-only so a host test can check it (tools/tests/test_debug_give.cpp).
 */
#ifndef KZ_DEBUG_GIVE_H
#define KZ_DEBUG_GIVE_H
#include <cctype>
#include <cstdlib>
#include <string>

/* The Lua for `spec`; empty when nothing in it is usable. Names other than
 * [A-Za-z0-9_] are skipped (nothing else reaches the script). */
static inline std::string debug_give_lua(const char* spec)
{
    if (!spec) return {};
    std::string body, first;
    const std::string s(spec);
    size_t i = 0;
    while (i <= s.size()) {
        size_t j = s.find(',', i);
        if (j == std::string::npos) j = s.size();
        const std::string item = s.substr(i, j - i);
        i = j + 1;
        const size_t c1 = item.find(':');
        const std::string name = item.substr(0, c1);
        bool ok = !name.empty() && name.size() <= 48;
        for (char ch : name) if (!isalnum((unsigned char)ch) && ch != '_') ok = false;
        if (!ok) continue;
        long ammo = 999, sec = 0;
        if (c1 != std::string::npos) {
            const size_t c2 = item.find(':', c1 + 1);
            ammo = strtol(item.substr(c1 + 1, c2 == std::string::npos ? std::string::npos : c2 - c1 - 1).c_str(), nullptr, 10);
            if (c2 != std::string::npos) sec = strtol(item.substr(c2 + 1).c_str(), nullptr, 10);
        }
        if (ammo < 0) ammo = 0;
        if (sec < 0) sec = 0;
        body += " PLAYER:AddWeapon('" + name + "', " + std::to_string(ammo) + ", " + std::to_string(sec) + ")";
        first = name;   /* the last one */
    }
    if (body.empty()) return {};
    return "if PLAYER and not KZGIVEN then KZGIVEN = 1" + body + " PLAYER:SelectWeapon('" + first + "') end";
}
#endif
