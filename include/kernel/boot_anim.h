#ifndef KERNEL_BOOT_ANIM_H
#define KERNEL_BOOT_ANIM_H

/* Plays the YouOS boot animation once, blocking until it finishes
 * (or until a key/tick timeout, see boot_anim.c). Call this after
 * fb is initialized (post sys_fbinfo()/fb_init()) and before the
 * shell/desktop is launched. */
void boot_anim_run(void);

#endif
