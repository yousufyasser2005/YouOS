/* Deliberately crashes with a genuine, guaranteed-invalid instruction
 * (UD2 -- Intel's own "always #UD" opcode, not a real bug in disguise).
 * Exists solely to test crash.c's real_exit-aware recovery path: a
 * real process_create()-spawned child faulting should be caught,
 * marked DEAD, and handed back to its parent via process_exit(),
 * without taking down the rest of the OS. TEMPORARY -- remove once
 * that recovery path is confirmed working in QEMU. */
int main(void) {
    __asm__ volatile("ud2");
    return 0;  /* unreachable */
}
