#pragma once
// Every #include <unistd.h> in py/ core is there "for ssize_t" only
// (confirmed by grep -n across py/*.c) -- nothing else from this header
// is used. No POSIX functions declared here on purpose: if something
// ever tries to call read()/write()/etc. from this header, that's a bug
// to catch, not paper over -- YouOS's own sys_read/sys_write (via
// user/lib/syscall.h) are the real I/O path.
typedef long ssize_t;
