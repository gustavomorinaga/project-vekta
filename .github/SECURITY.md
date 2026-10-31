# Security policy

This project is a work-in-progress PC port; its releases are pre-releases. Security fixes go to the latest `main` and the next release.

## Reporting a vulnerability

Please report privately through GitHub's **"Report a vulnerability"** button on this repository's Security tab.
Do not open a public issue for a security problem. Include what is affected (file, function or build step), how to
reproduce it, and the impact you expect.

## Scope

- In scope: the port's own code and tools in this repository (for example the settings and key-map file parsers, the
  movie player, the build scripts).
- Out of scope: Killzone HD itself, the PlayStation 3 system software, and third-party projects (report those to
  their maintainers; recompiler issues to [ps3recomp](https://github.com/sp00nznet/ps3recomp)).
- The port makes no network connection. Anything that makes it reach the network, or lets a file run code or read
  files it should not, is a vulnerability.

Rights holders: use the same private channel.
