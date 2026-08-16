#pragma once
// Freestanding: no libc abort()/stderr to report an assertion failure
// through. MicroPython's own MP_UNREACHABLE / mp_raise mechanisms are
// the real invariant-checking tools used throughout this codebase --
// assert() itself is a deliberate no-op here, same as building any other
// port with NDEBUG.
#define assert(x) ((void)0)
