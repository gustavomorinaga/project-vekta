/* KZ_NATIVE_VERIFY with the guest store journal: see verify.h. */
#include "verify.h"
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <unordered_map>
#include <vector>
#ifdef PS3RECOMP_STORE_JOURNAL
#include "ppu_store_journal.h"
#endif

kz_native_probe_words kz_native_probe_words_fn;
int kz_native_psgl = !getenv("KZ_NATIVE_PSGL") || atoi(getenv("KZ_NATIVE_PSGL")) != 0;

#ifndef PS3RECOMP_STORE_JOURNAL
void kz_native_run(const char* name, ppu_context* ctx, kz_native_body native, void (*lifted)(ppu_context*),
                   kz_native_probe)
{
    if (kz_native_verify) {
        static bool said;
        if (!said) { said = true; fprintf(stderr, "[kz-native] KZ_NATIVE_VERIFY for %s needs a KZ_STORE_JOURNAL build; not checked\n", name); }
    }
    if (!native(ctx)) lifted(ctx);
}
#else
namespace {

/* one journal's room: generous for a single PSGL call (the draw path stores a few hundred words) */
constexpr uint32_t kEntries = 1u << 16, kOld = 1u << 22;
struct Journal {
    std::vector<uint32_t> ea, n, off;
    std::vector<uint8_t> old;
    ppu_store_journal j;
    Journal() : ea(kEntries), n(kEntries), off(kEntries), old(kOld)
    { j = { ea.data(), n.data(), off.data(), kEntries, 0, old.data(), kOld, 0, 0 }; }
};
thread_local Journal* t_a;
/* inside a check: a native function the checked bodies call runs its lifted
 * body, so the lifted side is all lifted and journals do not nest */
thread_local int t_depth;
thread_local Journal* t_b;

/* the registers a callee keeps (r1, r2, r13..r31) */
bool kept_equal(const ppu_context& x, const ppu_context& y)
{
    if (x.gpr[1] != y.gpr[1] || x.gpr[2] != y.gpr[2]) return false;
    for (int i = 13; i < 32; i++) if (x.gpr[i] != y.gpr[i]) return false;
    return true;
}

}  // namespace

void kz_native_run(const char* name, ppu_context* ctx, kz_native_body native, void (*lifted)(ppu_context*),
                   kz_native_probe probe)
{
    if (!kz_native_verify) { if (!native(ctx)) lifted(ctx); return; }
    if (t_depth) { lifted(ctx); return; }
    t_depth++;
    if (!t_a) { t_a = new Journal; t_b = new Journal; }
    const uint32_t sp = (uint32_t)ctx->gpr[1];
    const ppu_context in = *ctx;
    const uint64_t probe_a = probe ? probe(ctx) : 0;
    const uint64_t foreign0 = g_store_journal_foreign;
    /* the probed words at entry (diagnostics only) */
    static thread_local std::vector<uint32_t> pw_ea(16384), pw_v0(16384);
    uint32_t pw_n = 0;
    if (kz_native_probe_words_fn) {
        kz_native_probe_words_fn(ctx, pw_ea.data(), &pw_n, (uint32_t)pw_ea.size());
        for (uint32_t i = 0; i < pw_n; i++) pw_v0[i] = vm_read32(pw_ea[i]);
    }

    ppu_journal_begin(&t_a->j);
    /* KZ_NATIVE_VERIFY_SELF=1: the lifted body against itself, a check of the
     * check: what differs then is not the native code's (memory the journal
     * cannot undo, another thread) */
    static const bool self = getenv("KZ_NATIVE_VERIFY_SELF") && atoi(getenv("KZ_NATIVE_VERIFY_SELF"));
    const bool took = self ? (lifted(ctx), true) : native(ctx);
    ppu_journal_end();
    if (!took) {
        ppu_journal_undo(&t_a->j);   /* by contract nothing; undone in case */
        *ctx = in;
        kz_native_fallback(name);
        t_depth--;   /* the lifted body alone, outside the check: what it calls may be checked */
        lifted(ctx);
        return;
    }
    /* the native side's final bytes, before they are undone */
    std::unordered_map<uint32_t, uint8_t> mine;
    for (uint32_t i = 0; i < t_a->j.count; i++)
        for (uint32_t k = 0; k < t_a->j.n[i]; k++) { const uint32_t a = t_a->j.ea[i] + k; mine[a] = vm_base[a]; }
    const ppu_context out_native = *ctx;
    ppu_journal_undo(&t_a->j);
    *ctx = in;
    const uint64_t foreign1 = g_store_journal_foreign;
    const uint64_t probe_u = probe ? probe(ctx) : 0;   /* right after the undo */
    /* which entry words differ after the undo, and did the first run's journal note them */
    char pw_report[1024] = "";
    if (probe && probe_u != probe_a && kz_native_probe_words_fn) {
        size_t o = 0;
        for (uint32_t i = 0; i < pw_n && o < sizeof pw_report - 96; i++) {
            const uint32_t now = vm_read32(pw_ea[i]);
            if (now == pw_v0[i]) continue;
            o += (size_t)snprintf(pw_report + o, sizeof pw_report - o, " [0x%08X %08x->%08x %s]", pw_ea[i], pw_v0[i], now,
                                  ppu_journal_find(&t_a->j, pw_ea[i], 4) >= 0 ? "in journal" : "NOT in journal");
        }
        if (!o) snprintf(pw_report, sizeof pw_report, " (the words probed at entry are equal: the walk itself reached other blocks)");
    }

    const uint64_t probe_b = probe ? probe(ctx) : 0;
    ppu_journal_begin(&t_b->j);
    lifted(ctx);
    ppu_journal_end();
    t_depth--;
    const uint64_t foreign2 = g_store_journal_foreign;

    /* each byte either side stored, above the entry stack pointer */
    std::unordered_map<uint32_t, uint8_t> orig;   /* where only the lifted side stored: the native side left it */
    for (uint32_t i = t_b->j.count; i-- > 0;)     /* oldest note last, so it wins */
        for (uint32_t k = 0; k < t_b->j.n[i]; k++) orig[t_b->j.ea[i] + k] = t_b->j.old[t_b->j.off[i] + k];
    uint32_t bad = 0, first = 0;
    uint8_t want = 0, got = 0;
    struct Diff { uint32_t a; uint8_t mine, now; bool by_a, by_b; };
    std::vector<Diff> diffs;
    auto check = [&](uint32_t a) {
        /* dead after the call: the callee frames below the entry stack pointer, and
         * the save slots the ABI gives a callee in its caller's frame header
         * (sp+8 CR, sp+16 LR), which a native body has no use for */
        if (a < sp && a >= sp - 0x100000u) return;
        if (a >= sp + 8 && a < sp + 24) return;
        const auto m = mine.find(a);
        const uint8_t n = m != mine.end() ? m->second : orig[a];
        if (n != vm_base[a]) {
            if (!bad++) { first = a; want = n; got = vm_base[a]; }
            if (diffs.size() < 24) diffs.push_back({a, n, vm_base[a], mine.count(a) != 0, orig.count(a) != 0});
        }
    };
    for (const auto& kv : mine) check(kv.first);
    for (const auto& kv : orig) if (!mine.count(kv.first)) check(kv.first);
    /* The inputs changed during the check, by a store neither journal saw: another
     * PPU thread storing from a function it entered before the journal began (its
     * stores bypass the slow path), or SPU DMA. Measured 2026-11-10 on PSGL's heap:
     * every lifted-against-lifted difference was a list head unlinked by another
     * thread between the first entry and the undo, the words not in the journal.
     * Such a call is counted apart, not compared. */
    if (probe && (probe_u != probe_a || probe_b != probe_u)) { kz_native_skip(name); return; }
    const bool regs = out_native.gpr[3] == ctx->gpr[3] && kept_equal(out_native, *ctx);
    const bool overflow = t_a->j.overflow || t_b->j.overflow;
    /* kz_native_check counts the call; a mismatch passes it two different bytes */
    const uint8_t r[2] = { (uint8_t)(bad || !regs || overflow), 0 };
    kz_native_check(name, r, r + 1, 1);
    if ((bad || !regs || overflow)) {
        static unsigned said;
        /* with a probe: were the inputs the same at both entries? */
        static unsigned long in_changed, in_same;
        const bool changed = probe && probe_a != probe_b;
        (changed ? in_changed : in_same)++;
        if (said++ < 40) {
            fprintf(stderr, "[kz-native] %s: %u byte(s) differ (first 0x%08X: native %02x, lifted %02x); r3 native %016llx "
                    "lifted %016llx%s%s%s [mismatches so far: inputs changed between the runs %lu, inputs equal %lu]\n",
                    name, bad, first, want, got, (unsigned long long)out_native.gpr[3],
                    (unsigned long long)ctx->gpr[3], kept_equal(out_native, *ctx) ? "" : "; kept registers differ",
                    overflow ? "; journal overflow (not comparable)" : "",
                    !probe ? "" : changed ? "; INPUTS CHANGED between the runs" : "; inputs equal at both entries",
                    in_changed, in_same);
            fprintf(stderr, "[kz-native]   inputs right after the undo: %s; other threads' stores: %llu during the first run, "
                    "%llu during the second\n", !probe ? "-" : probe_u == probe_a ? "as at the first entry" : "ALREADY CHANGED",
                    (unsigned long long)(foreign1 - foreign0), (unsigned long long)(foreign2 - foreign1));
            if (pw_report[0]) fprintf(stderr, "[kz-native]   words changed by the undo point:%s\n", pw_report);
            /* the words only one side stored (aligned to 4), to see which paths differed */
            std::vector<uint32_t> only_a, only_b;
            for (const auto& kv : mine) if (!orig.count(kv.first) && (kv.first & 3) == 0 && only_a.size() < 12) only_a.push_back(kv.first);
            for (const auto& kv : orig) if (!mine.count(kv.first) && (kv.first & 3) == 0 && only_b.size() < 12) only_b.push_back(kv.first);
            std::sort(only_a.begin(), only_a.end()); std::sort(only_b.begin(), only_b.end());
            fprintf(stderr, "[kz-native]   words only the first run stored:");
            for (uint32_t a2 : only_a) fprintf(stderr, " 0x%08X", a2);
            fprintf(stderr, "; only the second:");
            for (uint32_t a2 : only_b) fprintf(stderr, " 0x%08X", a2);
            fprintf(stderr, " (entry sp 0x%08X)\n", sp);
            for (const Diff& d : diffs)
                fprintf(stderr, "[kz-native]   0x%08X: first run %02x, second run %02x, stored by %s%s\n", d.a, d.mine, d.now,
                        d.by_a ? "first" : "", d.by_a && d.by_b ? "+second" : d.by_b ? "second" : "");
        } else if ((said & (said - 1)) == 0)
            fprintf(stderr, "[kz-native] %s: mismatches so far: inputs changed %lu, inputs equal %lu\n", name, in_changed, in_same);
    }
}
#endif
