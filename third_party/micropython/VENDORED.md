# Vendored MicroPython subset

Source: https://github.com/micropython/micropython (main branch)
Vendored: 2026-08-15

Trimmed to only what the YouOS port's build actually uses:
- `py/` — the core interpreter (unmodified upstream)
- `shared/runtime/` — portable helpers (gchelper_generic.c for GC stack
  scanning, used unmodified)
- `ports/youos/` — the YouOS-specific port layer (NOT upstream code --
  written for this project, see handoff doc)
- `LICENSE` — upstream MIT license, kept for attribution

Everything else from the original clone (other hardware ports, docs,
examples, tests, CI workflows, tools/) was removed as unused. The
original clone's .git history was also stripped before this note was
added, so there's no pinned commit hash recorded here -- if exact
version pinning ever matters, re-clone upstream and diff against `py/`
to find the point-in-time match.
