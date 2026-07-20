#pragma once
#include <stdint.h>

/* Records which uid/gid "owns" the currently-loaded CR3. Populated once
 * by sys_set_session_uid (called from desktop.c's login flow right after
 * auth_verify_password succeeds), consulted by every YCFS permission
 * check. NOT a real security boundary — any code that knows the syscall
 * number could call this itself. It exists to make the permission model
 * functionally correct for the one trusted login path, not to defend
 * against malicious userspace; that's a later problem (capabilities /
 * code signing), not a stage-3 one. */

void session_set_uid(uint32_t uid, uint32_t gid);

/* Returns 0 and fills uid_out/gid_out if the calling CR3 has a known
 * session. Returns -1 if unknown (e.g. a sys_exec'd process like shell
 * that never called sys_set_session_uid itself) — callers should treat
 * that as "no owner/group match possible," i.e. only world/other
 * permission bits apply. */
int session_lookup(uint32_t* uid_out, uint32_t* gid_out);

/* "youdo" — session-scoped elevation (sudo-shaped, not su-shaped). The
 * calling CR3's uid/gid never change; this is a separate flag checked
 * as an additional bypass in ycfs_perm_check, alongside the uid==0
 * check. Persists until explicitly cleared (logout), NOT on lock,
 * since lock preserves the whole session including this. */
void session_set_elevated(int on);
int  session_is_elevated(void);
