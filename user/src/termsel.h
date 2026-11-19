/* termsel.h -- the pure logic behind clipboard support in the two terminal kinds (the desktop's own
 * command window and the process terminals): mouse selection of output text, word / line selection,
 * copying a selection out, sanitising pasted text, feeding a paste to a terminal at a safe pace, and
 * deciding what a Ctrl key combination means in a terminal. No drawing and no system calls, so it is
 * compiled into the host test (tests/host/termsel_test.c) and into the desktop.
 *
 * Both terminals keep their text as `char lines[N][128]` (NUL terminated rows). A TermView describes
 * where those rows are on screen; a selection is two CARET positions (row, column) -- the column is the
 * gap between characters, exactly like a text editor -- in absolute line numbers. When the terminal
 * scrolls (its oldest line is dropped) the owner calls ts_scrolled() so the selection follows the text.
 *
 * Terminal shortcuts (agreed design):  Ctrl+Shift+C copies, Ctrl+Shift+V pastes, Ctrl+C is the
 * interrupt (control code 3) and every other Ctrl+letter becomes its control code (Ctrl+A = 1 ...). */
#ifndef TERMSEL_H
#define TERMSEL_H

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC push_options
#pragma GCC optimize ("no-tree-loop-distribute-patterns")
#endif

#define TS_COLS 128                 /* row stride of the terminals' line arrays (127 characters + NUL) */
#define TS_CW 8                     /* character cell, pixels */
#define TS_CH 16
#define TS_DBLCLICK_TICKS 100       /* 100 Hz ticks between presses; same window as the file manager and the Notepad. Measured on the real machine: a relaxed triple-click is 76 and 80 ticks apart */
#define TS_PASTE_MAX 65536          /* a paste is cut to this many characters */

typedef struct {
    int active;                     /* there is a selection (anchor != cursor) */
    int ar, ac, cr, cc;             /* anchor and cursor: row, caret column */
    int drag;                       /* the left button is down and the cursor follows the pointer */
    int click_n, click_x, click_y; long long click_tick;
} TermSel;

typedef struct {
    const char* lines;              /* nlines rows of TS_COLS bytes */
    int nlines;
    int cx, cy;                     /* pixel position of the first visible row's first column */
    int first;                      /* index of the first visible line */
    int rows, cols;                 /* visible rows / columns */
} TermView;

static void ts_clear(TermSel* s) { s->active = 0; s->ar = s->ac = s->cr = s->cc = 0; s->drag = 0; }
static void ts_init(TermSel* s) { ts_clear(s); s->click_n = 0; s->click_tick = 0; s->click_x = s->click_y = 0; }
static const char* ts_line(const TermView* v, int row) { return v->lines + (long)row * TS_COLS; }
static int ts_len(const TermView* v, int row) {
    if (row < 0 || row >= v->nlines) return 0;
    const char* l = ts_line(v, row); int n = 0; while (n < TS_COLS - 1 && l[n]) n++; return n;
}

/* pixel -> (row, column). cell != 0: the character under the pointer (for word selection);
 * cell == 0: the nearest caret position. A pointer above the first visible row goes to its start, one
 * below the last visible row goes to the end of that row. Returns 0 if the terminal has no text. */
static int ts_hit(const TermView* v, int mx, int my, int cell, int* row, int* col) {
    if (v->nlines <= 0 || v->rows <= 0) return 0;
    int last = v->first + v->rows - 1; if (last > v->nlines - 1) last = v->nlines - 1;
    int above = my < v->cy, r;
    if (above) r = v->first; else r = v->first + (my - v->cy) / TS_CH;
    int below = r > last;
    if (r > last) r = last;
    if (r < v->first) r = v->first;
    if (r < 0) r = 0;
    int len = ts_len(v, r), x = mx - v->cx, c;
    if (above || x < 0) c = 0; else c = cell ? x / TS_CW : (x + TS_CW / 2) / TS_CW;
    if (below) c = len;
    if (c > len) c = len;
    if (c > v->cols) c = v->cols;
    *row = r; *col = c; return 1;
}

/* class of a character for double-click: 0 blank, 1 "word" (letters, digits and path characters), 2 other */
static int ts_cls(char c) {
    if (c == ' ' || c == '\t') return 0;
    if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '-' || c == '.' || c == '/' || c == '~') return 1;
    return 2;
}
static void ts_word_bounds(const char* line, int len, int col, int* a, int* b) {
    if (len <= 0) { *a = *b = 0; return; }
    if (col >= len) col = len - 1;
    if (col < 0) col = 0;
    int k = ts_cls(line[col]);
    if (k == 2) { *a = col; *b = col + 1; return; }
    int s = col, e = col + 1;
    while (s > 0 && ts_cls(line[s - 1]) == k) s--;
    while (e < len && ts_cls(line[e]) == k) e++;
    *a = s; *b = e;
}

/* 1 single, 2 double, 3 triple; a click continues the series if it is soon after the last and within 4 px */
static int ts_click_count(const TermSel* s, long long now, int x, int y) {
    int dx = x - s->click_x, dy = y - s->click_y; if (dx < 0) dx = -dx; if (dy < 0) dy = -dy;
    if (s->click_n > 0 && now - s->click_tick <= TS_DBLCLICK_TICKS && dx <= 4 && dy <= 4) return s->click_n >= 3 ? 1 : s->click_n + 1;
    return 1;
}

static void ts_mouse_down(TermSel* s, const TermView* v, int mx, int my, long long now) {
    int n = ts_click_count(s, now, mx, my);
    s->click_n = n; s->click_tick = now; s->click_x = mx; s->click_y = my;
    int r, c;
    if (!ts_hit(v, mx, my, 1, &r, &c)) { ts_clear(s); return; }
    if (n == 2) {
        int a, b; ts_word_bounds(ts_line(v, r), ts_len(v, r), c, &a, &b);
        s->ar = r; s->ac = a; s->cr = r; s->cc = b; s->active = b > a; s->drag = 0; return;
    }
    if (n == 3) {
        int len = ts_len(v, r);
        s->ar = r; s->ac = 0; s->cr = r; s->cc = len; s->active = len > 0; s->drag = 0; return;
    }
    ts_hit(v, mx, my, 0, &r, &c);
    s->ar = s->cr = r; s->ac = s->cc = c; s->active = 0; s->drag = 1;
}
static void ts_mouse_drag(TermSel* s, const TermView* v, int mx, int my) {
    if (!s->drag) return;
    int r, c; if (!ts_hit(v, mx, my, 0, &r, &c)) return;
    s->cr = r; s->cc = c; s->active = (s->ar != s->cr || s->ac != s->cc);
}
static void ts_mouse_up(TermSel* s) { s->drag = 0; }

static void ts_ordered(const TermSel* s, int* r1, int* c1, int* r2, int* c2) {
    if (s->ar < s->cr || (s->ar == s->cr && s->ac <= s->cc)) { *r1 = s->ar; *c1 = s->ac; *r2 = s->cr; *c2 = s->cc; }
    else { *r1 = s->cr; *c1 = s->cc; *r2 = s->ar; *c2 = s->ac; }
}
/* the selected columns [a, b) of one row; returns 1 if there are any */
static int ts_row_range(const TermSel* s, const TermView* v, int row, int* a, int* b) {
    *a = *b = 0; if (!s->active) return 0;
    int r1, c1, r2, c2; ts_ordered(s, &r1, &c1, &r2, &c2);
    if (row < r1 || row > r2) return 0;
    int len = ts_len(v, row);
    int x = (row == r1) ? c1 : 0, y = (row == r2) ? c2 : len;
    if (x > len) x = len;
    if (y > len) y = len;
    *a = x; *b = y; return y > x;
}
/* the selected text, rows joined with '\n'; returns its length (0 and an empty string if nothing is selected) */
static int ts_extract(const TermSel* s, const TermView* v, char* out, int cap) {
    if (cap < 1) return 0;
    out[0] = 0; if (!s->active) return 0;
    int r1, c1, r2, c2; ts_ordered(s, &r1, &c1, &r2, &c2);
    int o = 0;
    for (int r = r1; r <= r2 && r < v->nlines; r++) {
        if (r < 0) continue;
        int a, b; ts_row_range(s, v, r, &a, &b);
        const char* l = ts_line(v, r);
        for (int i = a; i < b && o < cap - 1; i++) out[o++] = l[i];
        if (r < r2 && o < cap - 1) out[o++] = '\n';
    }
    out[o] = 0; return o;
}
/* the terminal dropped its n oldest lines: every row number moves up by n */
static void ts_scrolled(TermSel* s, int n) {
    if (n <= 0 || (!s->active && !s->drag)) return;
    s->ar -= n; s->cr -= n;
    if (s->ar < 0 && s->cr < 0) { ts_clear(s); return; }
    if (s->ar < 0) { s->ar = 0; s->ac = 0; }
    if (s->cr < 0) { s->cr = 0; s->cc = 0; }
    s->active = (s->ar != s->cr || s->ac != s->cc);
}

/* ------------------------------------------------------------------ paste */
typedef struct {
    char buf[TS_PASTE_MAX]; int len, pos;
    int active;                    /* a paste is in progress */
    int target; long long pid;     /* the window slot and process it is going to */
    int truncated;                 /* the text was longer than TS_PASTE_MAX */
} TermPaste;

static void tp_cancel(TermPaste* p) { p->active = 0; p->len = p->pos = 0; p->target = -1; p->pid = -1; p->truncated = 0; }
/* Loads clipboard text for a terminal: CRLF and CR become '\n'; other control characters (including ESC,
 * so a paste can never carry terminal escape sequences) are dropped; a UTF-8 sequence becomes one '?'
 * (the font is ASCII only); tabs become a space if tabs_to_space. Returns the length. */
static int tp_load(TermPaste* p, int target, long long pid, const char* src, int n, int tabs_to_space) {
    p->len = 0; p->pos = 0; p->truncated = 0;
    for (int i = 0; i < n; i++) {
        unsigned char c = (unsigned char)src[i]; char o;
        if (c == '\r') { if (i + 1 < n && src[i + 1] == '\n') continue; o = '\n'; }
        else if (c == '\n') o = '\n';
        else if (c == '\t') o = tabs_to_space ? ' ' : '\t';
        else if (c < 32 || c == 127) continue;
        else if (c >= 0xC0) o = '?';
        else if (c >= 0x80) continue;            /* UTF-8 continuation byte: part of the '?' already written */
        else o = (char)c;
        if (p->len >= TS_PASTE_MAX) { p->truncated = 1; break; }
        p->buf[p->len++] = o;
    }
    p->active = p->len > 0; p->target = target; p->pid = pid;
    return p->len;
}
/* sink: 1 = taken, 0 = refused (queue full: try again next frame), 2 = taken, stop for this frame */
typedef int (*tp_sink)(void* ctx, char c);
static int tp_feed(TermPaste* p, int maxn, tp_sink f, void* ctx) {
    int sent = 0;
    while (p->active && p->pos < p->len && sent < maxn) {
        int r = f(ctx, p->buf[p->pos]);
        if (r == 0) break;
        p->pos++; sent++;
        if (r == 2) break;
    }
    if (p->pos >= p->len) p->active = 0;
    return sent;
}

/* ------------------------------------------------------------------ keys */
enum { TS_KEY_NONE = 0, TS_KEY_COPY, TS_KEY_PASTE, TS_KEY_INTERRUPT, TS_KEY_CODE };
/* What does Ctrl+<ch> (with or without Shift) mean in a terminal? *code is set for INTERRUPT / CODE. */
static int ts_classify_ctrl(int ch, int shift, int* code) {
    *code = 0;
    if (ch >= 'A' && ch <= 'Z') ch += 32;
    if (ch < 'a' || ch > 'z') return TS_KEY_NONE;
    if (shift) return ch == 'c' ? TS_KEY_COPY : ch == 'v' ? TS_KEY_PASTE : TS_KEY_NONE;
    *code = ch - 'a' + 1;
    return ch == 'c' ? TS_KEY_INTERRUPT : TS_KEY_CODE;
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC pop_options
#endif
#endif
