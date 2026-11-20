/* notiflist.h -- pure logic for the notification center's right-click menu: which notification a point is on
 * (the same row arithmetic the center draws with), and the text that Copy / Copy All put on the clipboard.
 * No drawing and no system calls; compiled into the host test (tests/host/notiflist_test.c) and the desktop.
 *
 * The center lists the newest notification first. Row r (0 = top) shows notification  count - 1 - scroll - r,
 * at most `max_vis` rows, each card starting at y0 + 90 + r * row_h and being (row_h - 8) tall, inset by 12 px
 * on both sides. The draw code clamps `scroll` to [0, count - max_vis] before drawing; so do we. */
#ifndef NOTIFLIST_H
#define NOTIFLIST_H

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC push_options
#pragma GCC optimize ("no-tree-loop-distribute-patterns")
#endif

#define NL_FIRST_ROW_DY 90          /* top of the first card, below the panel's top edge */
#define NL_CARD_INSET   12          /* left and right margin of a card inside the panel */
#define NL_CARD_GAP     8           /* row_h minus the height of a card */

/* the scroll value the draw code actually uses */
static int nl_clamp_scroll(int count, int scroll, int max_vis) {
    if (scroll > count - max_vis) scroll = count - max_vis;
    if (scroll < 0) scroll = 0;
    return scroll;
}
/* index of the notification whose card contains (mx, my), or -1 */
static int nl_row_at(int x0, int y0, int w, int row_h, int max_vis, int count, int scroll, int mx, int my) {
    if (count <= 0 || row_h <= NL_CARD_GAP) return -1;
    scroll = nl_clamp_scroll(count, scroll, max_vis);
    int visible = count - scroll; if (visible > max_vis) visible = max_vis;
    if (mx < x0 + NL_CARD_INSET || mx >= x0 + w - NL_CARD_INSET) return -1;
    for (int row = 0; row < visible; row++) {
        int ry = y0 + NL_FIRST_ROW_DY + row * row_h;
        if (my >= ry && my < ry + row_h - NL_CARD_GAP) return count - 1 - scroll - row;
    }
    return -1;
}

static int nl_put(char* out, int cap, int k, const char* s) { while (*s && k < cap - 1) out[k++] = *s++; return k; }
/* "Title: message" (or just one of them if the other is empty); returns the length */
static int nl_format_one(const char* title, const char* msg, char* out, int cap) {
    if (cap < 1) return 0;
    int k = nl_put(out, cap, 0, title);
    if (title[0] && msg[0]) k = nl_put(out, cap, k, ": ");
    k = nl_put(out, cap, k, msg);
    out[k] = 0; return k;
}
/* every notification, oldest first, one per line, no trailing newline. titles / msgs are arrays of fixed-size rows. */
static int nl_format_all(const char* titles, int tstride, const char* msgs, int mstride, int count, char* out, int cap) {
    if (cap < 1) return 0;
    int k = 0;
    for (int i = 0; i < count; i++) {
        if (i > 0) k = nl_put(out, cap, k, "\n");
        k = nl_put(out, cap, k, titles + (long)i * tstride);
        if (titles[(long)i * tstride] && msgs[(long)i * mstride]) k = nl_put(out, cap, k, ": ");
        k = nl_put(out, cap, k, msgs + (long)i * mstride);
    }
    out[k] = 0; return k;
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC pop_options
#endif
#endif
