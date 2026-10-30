# Contributing

Thanks for helping. Testing on your hardware, reports with logs, and fixes are all welcome. Everything in this
repository is in English, so that anyone can follow it.

## The rules that matter most

- **No game material, ever.** Never commit, attach, paste or link to files from the disc or the package, decrypted
  binaries, code lifted or decompiled from the game, extracted assets or movies, saves, keys or firmware. Numbers,
  names, structure layouts and formulas recovered from the game are fine. `tools/check-tree.sh` checks the tree.
- **No circumvention.** No decryption tools, keys or instructions for decrypting game or firmware files.
- **ps3recomp stays pristine.** Never edit `third_party/ps3recomp/`. A change to it is a new patch in
  `port/patches/`, named in its area's number block (`0xxx` general, `1xxx` SPU and SPURS, `2xxx` graphics, `3xxx`
  lifter and system libraries, `4xxx` input and file system, `5xxx` data and trophies) with the next free number, and
  added at the end of `port/patches/series`. To make one: patch a copy of the tree from `tools/ps3recomp-patched.sh`
  (it is plain files), then `git diff --no-index` or a scratch git repository to write the diff.
- **No GPL code.** RPCS3 may be read for behaviour; never copy, translate or closely paraphrase its code.
- **Gameplay stays original by default.** Mark features the PS3 game does not have `ENHANCEMENT:`, keep them optional
  if they change gameplay, and mark unverified values `ASSUMPTION:`.
- **Credit what you absorb** from other ports or pull requests: patch header, `NOTICE.md` and the commit message.

The full list, also for AI coding agents: [AGENTS.md](../AGENTS.md).

## Building and testing

[docs/BUILDING.md](../docs/BUILDING.md). Before a pull request, run what CI runs:

```sh
tools/ps3recomp-patched.sh   # every patch applies
tools/check-tree.sh          # no game files, personal paths, keys or untranslated text
```

and, if you have the game, a run of what you changed: say which level, for how long, with what input, and what the
log or a screenshot showed. Without the game you can still work on tools, docs, the site and patches; say in the pull
request that game runs were not done.

## Reports

Use the issue templates: bug, performance, feature or feedback. Attach the log, with your user name and paths
removed. Never attach game files or saves.

## Commits

One topic per commit, a short plain message that says what changed and why, in English, with credits for anything
absorbed from elsewhere. No tool-generated footers or co-author trailers.
