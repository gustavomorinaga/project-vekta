## What changed and why

## How it was checked

<!-- What you ran: tools/ps3recomp-patched.sh, tools/check-tree.sh, a build, a game run (which level, how long, what
the log or a screenshot showed). Say plainly what was not run. -->

## Checklist

- [ ] No game files, decrypted binaries, generated code, keys, saves or personal paths (`tools/check-tree.sh` passes)
- [ ] A change to ps3recomp is a new patch in its area's number block, added at the end of `port/patches/series`
- [ ] Credits for anything absorbed from elsewhere are in the patch header, `NOTICE.md` and the commit message
- [ ] Docs updated where the change touches them (BUILDING, GETTING-FILES, FEATURES, KNOWN_ISSUES, CHANGELOG)
