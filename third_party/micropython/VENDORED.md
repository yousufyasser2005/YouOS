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
- `extmod/virtpin.h`, `extmod/modplatform.h` — small, dependency-free
  upstream headers that py/ core needs unconditionally (mphal.h's
  virtual-pin fallback, modsys.c's platform-string macros). Found the
  hard way: the first trim pass deleted extmod/ entirely, which broke a
  from-scratch build (worked the first time only because build/ objects
  from before the trim were still cached). Their actual functions are
  never called on YouOS (no pin/GPIO concept here) -- same
  declare-but-never-invoke situation as the stub headers in
  ports/youos/include/, except these are real upstream files rather than
  reinventions, matching how bare-arm/unix (upstream's own minimal
  ports) handle the same dependency.

Everything else from the original clone (other hardware ports, docs,
examples, tests, CI workflows, tools/) was removed as unused. The
original clone's .git history was also stripped before this note was
added, so there's no pinned commit hash recorded here -- if exact
version pinning ever matters, re-clone upstream and diff against `py/`
to find the point-in-time match.
