// mphalport.h — YouOS userspace port
//
// The one function that matters: mp_hal_stdout_tx_strn_cooked, wired to
// the real sys_write(1, ...) syscall (see main.c). This is the exact slot
// the sandbox's host-stdout stub filled -- same shape, real backend now.

#include <stddef.h>

void mp_hal_stdout_tx_strn_cooked(const char *str, size_t len);
