/* TEMPORARY (but kept permanently, like crashtest.c/exectest.c) --
 * regression check for Phase 1 of true concurrent multi-program
 * execution. Runs quietly for a few seconds (no console output at all,
 * deliberately, to avoid fighting with whatever spawned it over the
 * single shared text console before per-process I/O redirection
 * exists) so a caller that spawns it via sys_spawn() (non-blocking)
 * instead of sys_exec() (blocking) can prove its own code keeps running
 * the whole time -- e.g. desktop's own UI should keep animating/
 * responding while this sleeps, something a blocking sys_exec() could
 * never allow. */
#include "../lib/syscall.h"

int main(void) {
    /* ~5 seconds total at the scheduler's 100Hz tick rate, done in
     * smaller chunks rather than one large sleep -- not load-bearing
     * for correctness, just avoids assuming a single huge sleep value
     * behaves identically to several smaller ones on every timer
     * implementation. */
    for (int i = 0; i < 10; i++) {
        sys_sleep(50);
    }
    return 0;
}
