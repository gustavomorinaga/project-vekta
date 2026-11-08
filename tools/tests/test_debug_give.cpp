// KZ_DEBUG_GIVE's script (port/kz1hd/debug_give.h): the game's own PLAYER:AddWeapon for each weapon named, then
// PLAYER:PickupAmmo for grenades, once per state; names that are not plain identifiers never reach the script.
#include <cstdio>
#include <string>
#include "debug_give.h"

static int fails;
static void expect(const char* spec, const std::string& want)
{
    const std::string got = debug_give_lua(spec);
    if (got != want) { fprintf(stderr, "FAIL: %s\n  got  %s\n  want %s\n", spec ? spec : "(null)", got.c_str(), want.c_str()); fails++; }
    else printf("ok   %s\n", spec ? spec : "(null)");
}

int main()
{
    expect("ChainGun:731:0,HandGrenade:3", "if PLAYER and not KZGIVEN then KZGIVEN = 1 PLAYER:AddWeapon('ChainGun', 731, 0) PLAYER:PickupAmmo('HandGrenade', 3, 0) end");
    expect("HandGrenade:3,H_LAR:300:1,Pistol", "if PLAYER and not KZGIVEN then KZGIVEN = 1 PLAYER:AddWeapon('H_LAR', 300, 1) PLAYER:AddWeapon('Pistol', 999, 0) PLAYER:PickupAmmo('HandGrenade', 3, 0) end");
    expect("M82:-5", "if PLAYER and not KZGIVEN then KZGIVEN = 1 PLAYER:AddWeapon('M82', 0, 0) end");
    expect("x') os.exit() --,SMG", "if PLAYER and not KZGIVEN then KZGIVEN = 1 PLAYER:AddWeapon('SMG', 999, 0) end");
    expect("", "");
    expect(nullptr, "");
    expect("bad name", "");
    if (fails) return 1;
    puts("test_debug_give: ok");
    return 0;
}
