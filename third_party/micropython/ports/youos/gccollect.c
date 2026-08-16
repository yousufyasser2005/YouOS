// gccollect.c — YouOS sandbox port
//
// Same pattern as ports/unix/gccollect.c: gc_collect() = scan registers +
// C stack conservatively via the portable shared/runtime gchelper. This
// is exactly the mechanism the real YouOS port will also need (against
// YouOS's real stack, not the host's) — see handoff doc, step 3, "GC
// root scanning against the real stack layout".

#include "py/mpstate.h"
#include "py/gc.h"
#include "shared/runtime/gchelper.h"

#if MICROPY_ENABLE_GC

void gc_collect(void) {
    gc_collect_start();
    gc_helper_collect_regs_and_stack();
    gc_collect_end();
}

#endif
