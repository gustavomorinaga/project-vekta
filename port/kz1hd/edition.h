/*
 * Killzone HD's editions: the guest addresses the port names (always the US EBOOT's, NPUA80856) and where
 * they are in the edition this build runs (KZ_EDITION_EU=1: the EU EBOOT, NPEA00418). Written by
 * tools/edition_map.py from re/eu/address_map.tsv; never edit it by hand (the host tests check it).
 *
 * KZ_GA(a): the address a (a constant from the US EBOOT's code or rodata) in this edition. Data and bss
 * addresses (0x5C0000 and up) are the same in both and need no KZ_GA. An address inside code that differs
 * between the editions and that no site row proves gives KZ_GA_NONE, which no guest address equals
 * (tools/tests/test_edition_map.py fails on any such use). Function names (func_XXXXXXXX) stay the US ones.
 */
#ifndef KZ_EDITION_H
#define KZ_EDITION_H
#include <stdint.h>

#ifndef KZ_EDITION_EU
#define KZ_EDITION_EU 0
#endif
#define KZ_GA_NONE 0xFFFFFFFFu

#if KZ_EDITION_EU
#define KZ_GA(a) KZ_GA_EU_((uint32_t)(a))
#define KZ_GA_EU_(a) ( \
    (a) == 0x002793A0u ? 0x00279300u : \
    (a) == 0x004CBBB4u ? 0x004CBB14u : \
    (a) == 0x004CC740u ? 0x004CC67Cu : \
    (a) == 0x004CD834u ? 0x004CD74Cu : \
    (a) == 0x004CD90Cu ? 0x004CD824u : \
    (a) == 0x004CD9D4u ? 0x004CD8ECu : \
    (a) == 0x004CDC0Cu ? 0x004CDB24u : \
    (a) == 0x004CDCC0u ? 0x004CDBD8u : \
    (a) == 0x004CE4BCu ? 0x004CE3D4u : \
    (a) == 0x000517B0u ? 0x000517B0u : \
    (a) == 0x00054FFCu ? 0x00054FFCu : \
    (a) == 0x00086768u ? 0x00086760u : \
    (a) == 0x00221838u ? 0x002217E8u : \
    (a) == 0x002263F8u ? 0x00226388u : \
    (a) == 0x00278888u ? 0x002787F0u : \
    (a) == 0x003D2E38u ? 0x003D2D98u : \
    (a) == 0x003D3098u ? 0x003D2FF8u : \
    (a) == 0x003D3134u ? 0x003D3094u : \
    (a) == 0x004C7A28u ? 0x004C7988u : \
    (a) == 0x004CB798u ? 0x004CB6F8u : \
    (a) >= 0x000517B0u && (a) < 0x0005193Cu ? KZ_GA_NONE : \
    (a) >= 0x00054FFCu && (a) < 0x000553D8u ? KZ_GA_NONE : \
    (a) >= 0x00086750u && (a) < 0x00086758u ? KZ_GA_NONE : \
    (a) >= 0x00086768u && (a) < 0x00086A28u ? KZ_GA_NONE : \
    (a) >= 0x00221838u && (a) < 0x00221B20u ? KZ_GA_NONE : \
    (a) >= 0x002263F8u && (a) < 0x00226A4Cu ? KZ_GA_NONE : \
    (a) >= 0x00278858u && (a) < 0x00278888u ? KZ_GA_NONE : \
    (a) >= 0x00278888u && (a) < 0x00279E9Cu ? KZ_GA_NONE : \
    (a) >= 0x003D2E38u && (a) < 0x003D2FA8u ? KZ_GA_NONE : \
    (a) >= 0x003D3098u && (a) < 0x003D3134u ? KZ_GA_NONE : \
    (a) >= 0x003D3134u && (a) < 0x003D31A0u ? KZ_GA_NONE : \
    (a) >= 0x004C7A28u && (a) < 0x004C8980u ? KZ_GA_NONE : \
    (a) >= 0x004CB6BCu && (a) < 0x004CB6D0u ? KZ_GA_NONE : \
    (a) >= 0x004CB798u && (a) < 0x004CEC70u ? KZ_GA_NONE : \
    (a) >= 0x00565538u && (a) < 0x0056580Cu ? KZ_GA_NONE : \
    (a) >= 0x0057EFD0u && (a) < 0x0057EFE4u ? KZ_GA_NONE : \
    (a) < 0x00010000u ? (a) : \
    (a) < 0x00086758u ? (a) : \
    (a) < 0x0008689Cu ? (a) - 8u : \
    (a) < 0x00221B1Cu ? (a) - 80u : \
    (a) < 0x0022662Cu ? (a) - 112u : \
    (a) < 0x0022672Cu ? (a) - 136u : \
    (a) < 0x00226B20u ? (a) - 148u : \
    (a) < 0x00278888u ? (a) - 144u : \
    (a) < 0x00278AD8u ? (a) - 152u : \
    (a) < 0x004CC3E8u ? (a) - 160u : \
    (a) < 0x004CCC88u ? KZ_GA_NONE : \
    (a) < 0x0052FF40u ? (a) - 232u : \
    (a) < 0x0052FF80u ? KZ_GA_NONE : \
    (a) < 0x005BC3E8u ? (a) - 256u : \
    (a) < 0x005C0000u ? KZ_GA_NONE : \
    (a) < 0x0137FE98u ? (a) : \
    (a))
#else
#define KZ_GA(a) ((uint32_t)(a))
#endif
#endif
