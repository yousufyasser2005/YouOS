/* cliphist.h -- clipboard history: the store, the one-line previews, and the logic of the history panel.
 * No drawing and no system calls (the desktop watches the system clipboard and draws the panel); compiled into
 * the host test (tests/host/cliphist_test.c) and into the desktop. Userspace has no libc, so the loops here are
 * written to never become memcpy/memset calls.
 *
 * STORE   The last CBH_MAX text copies, newest first. Entries live in fixed slots and `order` says which slot is
 *         newest, next newest ... so moving an entry to the top never copies its text. Copying text that is
 *         already in the history moves it to the top instead of adding a duplicate. Copies that are only blanks,
 *         or longer than CBH_ENTRY_MAX, are not recorded (a shortened copy would silently lose text).
 * PANEL   Keys, selection, scrolling, and the positions of rows and buttons are pure functions so the whole
 *         interaction can be tested: see cbp_key(). */
#ifndef CLIPHIST_H
#define CLIPHIST_H

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC push_options
#pragma GCC optimize ("no-tree-loop-distribute-patterns")
#endif

#define CBH_MAX        20
#define CBH_ENTRY_MAX  65536

typedef struct {
    int n;                              /* entries in the history */
    int order[CBH_MAX];                 /* order[0] = slot of the newest entry */
    int len[CBH_MAX];                   /* length of the text in each slot */
    unsigned seq_seen; int have_seq;    /* the system clipboard's sequence number we last looked at */
    char data[CBH_MAX][CBH_ENTRY_MAX];
} ClipHist;

enum { CBH_SKIPPED = 0, CBH_ADDED = 1, CBH_MOVED = 2 };

static void cbh_clear(ClipHist* h) { h->n = 0; }
static void cbh_init(ClipHist* h) { h->n = 0; h->seq_seen = 0; h->have_seq = 0; }
static int cbh_count(const ClipHist* h) { return h->n; }
/* entry i, 0 = newest; NULL if there is none */
static const char* cbh_text(const ClipHist* h, int i) { return (i >= 0 && i < h->n) ? h->data[h->order[i]] : 0; }
static int cbh_len(const ClipHist* h, int i) { return (i >= 0 && i < h->n) ? h->len[h->order[i]] : 0; }

static int cbh_blank(const char* s, int len) {
    for (int i = 0; i < len; i++) if (s[i] != ' ' && s[i] != '\t' && s[i] != '\n' && s[i] != '\r') return 0;
    return 1;
}
static int cbh_same(const ClipHist* h, int slot, const char* s, int len) {
    if (h->len[slot] != len) return 0;
    const char* d = h->data[slot];
    for (int i = 0; i < len; i++) if (d[i] != s[i]) return 0;
    return 1;
}
/* put the entry at position `pos` of `order` at the front */
static void cbh_to_front(ClipHist* h, int pos) {
    int slot = h->order[pos];
    for (int i = pos; i > 0; i--) h->order[i] = h->order[i - 1];
    h->order[0] = slot;
}
/* records a copy; see the rules at the top */
static int cbh_add(ClipHist* h, const char* s, int len) {
    if (len <= 0 || len > CBH_ENTRY_MAX || cbh_blank(s, len)) return CBH_SKIPPED;
    for (int i = 0; i < h->n; i++)
        if (cbh_same(h, h->order[i], s, len)) { if (i > 0) cbh_to_front(h, i); return CBH_MOVED; }
    int slot;
    if (h->n < CBH_MAX) {                         /* first slot nobody uses */
        slot = 0;
        for (;; slot++) { int used = 0; for (int i = 0; i < h->n; i++) if (h->order[i] == slot) used = 1; if (!used) break; }
        h->n++;
    } else slot = h->order[CBH_MAX - 1];           /* full: the oldest entry's slot is reused */
    for (int i = h->n - 1; i > 0; i--) h->order[i] = h->order[i - 1];
    h->order[0] = slot;
    for (int i = 0; i < len; i++) h->data[slot][i] = s[i];
    h->len[slot] = len;
    return CBH_ADDED;
}
static void cbh_remove(ClipHist* h, int i) {
    if (i < 0 || i >= h->n) return;
    for (int k = i; k < h->n - 1; k++) h->order[k] = h->order[k + 1];
    h->n--;
}
/* The desktop calls this once per frame with the system clipboard's (seq, len, type). Returns 1 if the
 * text should now be read and recorded. A change of sequence is remembered either way, so an oversized or
 * non-text clipboard is not looked at again every frame. */
static int cbh_should_read(ClipHist* h, unsigned seq, unsigned len, unsigned type) {
    if (h->have_seq && seq == h->seq_seen) return 0;
    h->have_seq = 1; h->seq_seen = seq;
    return type == 1u && len > 0 && len <= (unsigned)CBH_ENTRY_MAX;
}

/* ------------------------------------------------------------------ previews */
/* A one-line preview of at most `max_chars` characters: leading blanks dropped, runs of blanks become one
 * space, line breaks become " | ", control characters '?', a UTF-8 sequence one '?' (the font is ASCII only).
 * Longer text ends in "...". Returns the length. */
static int cbh_preview(const char* s, int len, char* out, int cap, int max_chars) {
    if (cap < 1) return 0;
    if (max_chars > cap - 1) max_chars = cap - 1;
    if (max_chars > 400) max_chars = 400;           /* tmp below holds 508 characters */
    char tmp[512]; int t = 0;                      /* the whole one-line text (capped), then cut to size */
    int i = 0, pending_sp = 0, pending_nl = 0, started = 0;    /* blanks before the first real character are never output (started == 0) */
    for (; i < len && t < (int)sizeof tmp - 4; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\n' || c == '\r') { pending_nl = 1; continue; }
        if (c == ' ' || c == '\t') { pending_sp = 1; continue; }
        if (c >= 0x80 && c < 0xC0) continue;       /* continuation byte of a UTF-8 sequence */
        if (started && pending_nl) { tmp[t++] = ' '; tmp[t++] = '|'; tmp[t++] = ' '; }
        else if (started && pending_sp) tmp[t++] = ' ';
        pending_nl = pending_sp = 0; started = 1;
        tmp[t++] = (c < 32 || c == 127 || c >= 0xC0) ? '?' : (char)c;
    }
    int more = (i < len);                          /* the text was longer than the buffer */
    int k = 0;
    if (t <= max_chars && !more) { for (int j = 0; j < t; j++) out[k++] = tmp[j]; out[k] = 0; return k; }
    int keep = max_chars >= 4 ? max_chars - 3 : max_chars;      /* room for the dots, if they fit */
    for (int j = 0; j < keep && j < t; j++) out[k++] = tmp[j];
    if (max_chars >= 4) { out[k++] = '.'; out[k++] = '.'; out[k++] = '.'; }
    out[k] = 0; return k;
}
static int cbh_utoa(char* o, int k, unsigned long v) {
    char t[24]; int n = 0; if (!v) t[n++] = '0';
    while (v) { t[n++] = (char)('0' + v % 10); v /= 10; }
    while (n) o[k++] = t[--n];
    return k;
}
/* the size shown at the right of a row: "12 lines", "340 chars" or "1.5 KB" */
static int cbh_label(const char* s, int len, char* out, int cap) {
    if (cap < 16) { if (cap > 0) out[0] = 0; return 0; }
    int lines = 1;
    for (int i = 0; i < len; i++) if (s[i] == '\n' && i < len - 1) lines++;
    int k = 0;
    if (lines > 1) { k = cbh_utoa(out, k, (unsigned long)lines); const char* w = " lines"; while (*w) out[k++] = *w++; }
    else if (len < 1000) { k = cbh_utoa(out, k, (unsigned long)len); const char* w = len == 1 ? " char" : " chars"; while (*w) out[k++] = *w++; }
    else { unsigned long t = ((unsigned long)len * 10 + 512) / 1024; k = cbh_utoa(out, k, t / 10); out[k++] = '.'; out[k++] = (char)('0' + t % 10); const char* w = " KB"; while (*w) out[k++] = *w++; }
    out[k] = 0; return k;
}

/* ------------------------------------------------------------------ the panel */
#define CBP_W       560
#define CBP_HEAD_H  56
#define CBP_ROW_H   40
#define CBP_VIS     8
#define CBP_FOOT_H  34
#define CBP_INSET   10          /* rows are inset from the panel's left / right edges */
#define CBP_DEL_W   22          /* the small delete button at the right end of a row */
#define CBP_CLR_W   72          /* the Clear button in the header */
#define CBP_CLR_H   26

typedef struct { int sel, scroll; } CbpState;
enum { CBA_NONE = 0, CBA_CLOSE, CBA_PICK, CBA_DELETE };

static int cbp_rows(int count) { return count < 1 ? 1 : (count > CBP_VIS ? CBP_VIS : count); }
static int cbp_height(int count) { return CBP_HEAD_H + cbp_rows(count) * CBP_ROW_H + CBP_FOOT_H; }
/* the panel is centred in the work area [0, sw) x [0, sh) */
static void cbp_place(int sw, int sh, int count, int* x, int* y) {
    *x = (sw - CBP_W) / 2; if (*x < 0) *x = 0;
    *y = (sh - cbp_height(count)) / 2; if (*y < 8) *y = 8;
}
static int cbp_clamp_scroll(int count, int scroll) {
    if (scroll > count - CBP_VIS) scroll = count - CBP_VIS;
    if (scroll < 0) scroll = 0;
    return scroll;
}
/* the scroll that keeps entry `sel` on screen */
static int cbp_follow(int sel, int scroll, int count) {
    if (sel < scroll) scroll = sel;
    if (sel >= scroll + CBP_VIS) scroll = sel - CBP_VIS + 1;
    return cbp_clamp_scroll(count, scroll);
}
static int cbp_inside(int x, int y, int count, int mx, int my) {
    return mx >= x && mx < x + CBP_W && my >= y && my < y + cbp_height(count);
}
static int cbp_row_top(int y, int row) { return y + CBP_HEAD_H + row * CBP_ROW_H; }
/* entry under the point (the row's own area, not its delete button), or -1 */
static int cbp_row_at(int x, int y, int count, int scroll, int mx, int my) {
    if (count <= 0) return -1;
    scroll = cbp_clamp_scroll(count, scroll);
    int vis = count - scroll; if (vis > CBP_VIS) vis = CBP_VIS;
    if (mx < x + CBP_INSET || mx >= x + CBP_W - CBP_INSET) return -1;
    for (int r = 0; r < vis; r++) {
        int ry = cbp_row_top(y, r);
        if (my >= ry && my < ry + CBP_ROW_H - 4) return scroll + r;
    }
    return -1;
}
/* x range of the delete button of a row: [cbp_del_x, cbp_del_x + CBP_DEL_W) */
static int cbp_del_x(int x) { return x + CBP_W - CBP_INSET - CBP_DEL_W - 8; }
/* entry whose delete button is under the point, or -1 */
static int cbp_del_at(int x, int y, int count, int scroll, int mx, int my) {
    int i = cbp_row_at(x, y, count, scroll, mx, my);
    if (i < 0) return -1;
    return (mx >= cbp_del_x(x) && mx < cbp_del_x(x) + CBP_DEL_W) ? i : -1;
}
static int cbp_clear_at(int x, int y, int mx, int my) {
    int bx = x + CBP_W - CBP_INSET - CBP_CLR_W, by = y + 14;
    return mx >= bx && mx < bx + CBP_CLR_W && my >= by && my < by + CBP_CLR_H;
}
/* entry for a digit key '1'..'9' (the Nth newest), or -1 */
static int cbp_quick(int ch, int count) { return (ch >= '1' && ch <= '9' && ch - '1' < count) ? ch - '1' : -1; }
/* keep the selection and scroll valid after the list changed */
static void cbp_fix(CbpState* s, int count) {
    if (count <= 0) { s->sel = 0; s->scroll = 0; return; }
    if (s->sel >= count) s->sel = count - 1;
    if (s->sel < 0) s->sel = 0;
    s->scroll = cbp_follow(s->sel, s->scroll, count);
}
static void cbp_wheel(CbpState* s, int count, int notches) { s->scroll = cbp_clamp_scroll(count, s->scroll + notches); }
/* One key press while the panel is open (everything is used up by the panel; the caller never passes it on).
 * Returns what to do: CBA_CLOSE, CBA_PICK (s->sel is the entry to paste), CBA_DELETE (remove s->sel, then call
 * cbp_fix), or CBA_NONE. Codes: 1001 up, 1002 down, 1005 home, 1006 end, 1007 delete, 1008/1009 page up/down. */
static int cbp_key(CbpState* s, int count, int ch) {
    if (ch == 27) return CBA_CLOSE;
    if (count <= 0) return CBA_NONE;
    int q = cbp_quick(ch, count);
    if (q >= 0) { s->sel = q; return CBA_PICK; }
    switch (ch) {
    case '\n': case '\r': cbp_fix(s, count); return CBA_PICK;
    case 1007: cbp_fix(s, count); return CBA_DELETE;
    case 1001: s->sel--; break;
    case 1002: s->sel++; break;
    case 1008: s->sel -= CBP_VIS; break;
    case 1009: s->sel += CBP_VIS; break;
    case 1005: s->sel = 0; break;
    case 1006: s->sel = count - 1; break;
    default: return CBA_NONE;
    }
    cbp_fix(s, count);
    return CBA_NONE;
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC pop_options
#endif
#endif
