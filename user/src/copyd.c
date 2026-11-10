/* copyd -- the background transfer worker.
 *
 * Started by the desktop for every copy or delete (see xfer_proto.h). It receives the job over a
 * message queue, runs it with the same job engine the file manager always used (fs_tree.h), and
 * reports progress back. While it waits for the disk it is asleep and the desktop runs, so a
 * transfer does not slow down the cursor, and the cursor does not slow down the transfer. */
#include "../lib/syscall.h"
#include "fs_tree.h"
#include "xfer_proto.h"

int main(void) {
    XferWorker w;
    xw_init(&w);
    while (xw_poll(&w)) { }
    return 0;
}
