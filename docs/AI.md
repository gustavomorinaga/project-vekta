# How AI was used

This project is developed by one person with AI coding agents (Claude Code). The agents did most of the work of
reading the game's binary and reference material, writing and testing patches, tools and documentation, and running
the builds. The maintainer sets the goals, plays and checks the game, reviews what the agents produce, and decides
what is published and is responsible for it.

- **Rules for agents:** the same as for people, written in [AGENTS.md](../AGENTS.md): no game material, no
  circumvention, ps3recomp changed only through numbered patches, no GPL code, credits for what is absorbed.
- **Checking:** a runtime change is called working only after it ran in the game, with the level, the input and
  what the log or a screenshot showed. Host tests run in CI on every pull request.
- **Findings are dated.** An `ASSUMPTION:` in the code or the docs is not fact.

If you find a statement here that was not verified, please [open an issue](https://github.com/gustavomorinaga/project-vekta/issues).
