/* lineedit.h -- single-line text field engine: file name boxes, rename boxes, search boxes and
 * PASSWORD boxes (lock screen, youdo). Header-only, no drawing; compiled into the host test.
 *
 * Clipboard access goes through two macros the includer provides:
 *     LE_CLIP_SET(buf, len)     replace the system clipboard with len bytes
 *     LE_CLIP_GET(buf, max)     copy up to max bytes out, return the length copied (0 = empty)
 * so the same code runs against the kernel clipboard in the desktop and a fake one in the test.
 *
 * PASSWORD FIELDS (le->password != 0) NEVER put anything on the clipboard: copy and cut do nothing
 * (and cut does not delete either, so it cannot be used to peek at the text). Pasting INTO them
 * is allowed (password managers). The text is only ever drawn as bullets (le_display_char). */
#ifndef LINEEDIT_H
#define LINEEDIT_H

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC push_options
#pragma GCC optimize ("no-tree-loop-distribute-patterns")
#endif

typedef struct {
    char* buf; int cap;          /* caller's buffer; holds at most cap-1 characters plus the NUL */
    int len, cursor, anchor;     /* selection = [min(cursor,anchor), max) */
    int password;
} LineEdit;

static void le_set(LineEdit* e, const char* s) {
    int n = 0; while (s && s[n] && n < e->cap - 1) { e->buf[n] = s[n]; n++; }
    e->buf[n] = 0; e->len = n; e->cursor = e->anchor = n;
}
static void le_init(LineEdit* e, char* buf, int cap, int password) {
    e->buf = buf; e->cap = cap; e->password = password; e->len = 0; e->cursor = e->anchor = 0; if (cap > 0) buf[0] = 0;
}
static int le_has_sel(const LineEdit* e) { return e->cursor != e->anchor; }
static void le_sel(const LineEdit* e, int* a, int* b) {
    if (e->anchor < e->cursor) { *a = e->anchor; *b = e->cursor; } else { *a = e->cursor; *b = e->anchor; }
}
static void le_select_all(LineEdit* e) { e->anchor = 0; e->cursor = e->len; }

/* Replace the selection (or insert at the cursor) with the printable ASCII characters of s; anything
 * else (control characters, newlines, bytes >= 127) is skipped. Returns how many were inserted. */
static int le_insert(LineEdit* e, const char* s, int n) {
    int a, b; le_sel(e, &a, &b);
    int room = (e->cap - 1) - (e->len - (b - a));
    int ins = 0;
    char tmp[256];                                   /* filter in chunks so no large temporary is needed */
    int pos = a;
    if (b > a) {                                     /* remove the selection first */
        for (int i = b; i <= e->len; i++) e->buf[i - (b - a)] = e->buf[i];
        e->len -= (b - a);
    }
    for (int i = 0; i < n && ins < room; ) {
        int k = 0;
        while (i < n && k < (int)sizeof tmp && ins + k < room) {
            unsigned char c = (unsigned char)s[i++];
            if (c >= 32 && c < 127) tmp[k++] = (char)c;
        }
        if (k == 0) break;
        for (int j = e->len; j >= pos; j--) e->buf[j + k] = e->buf[j];     /* open a gap incl. the NUL */
        for (int j = 0; j < k; j++) e->buf[pos + j] = tmp[j];
        e->len += k; pos += k; ins += k;
    }
    e->cursor = e->anchor = pos;
    return ins;
}
static int le_type(LineEdit* e, char c) { return le_insert(e, &c, 1); }
static void le_backspace(LineEdit* e) {
    if (le_has_sel(e)) { le_insert(e, "", 0); return; }
    if (e->cursor <= 0) return;
    for (int i = e->cursor; i <= e->len; i++) e->buf[i - 1] = e->buf[i];
    e->len--; e->cursor--; e->anchor = e->cursor;
}
static void le_delete(LineEdit* e) {
    if (le_has_sel(e)) { le_insert(e, "", 0); return; }
    if (e->cursor >= e->len) return;
    for (int i = e->cursor + 1; i <= e->len; i++) e->buf[i - 1] = e->buf[i];
    e->len--;
}
static void le_goto(LineEdit* e, int pos, int extend) {
    if (pos < 0) pos = 0; if (pos > e->len) pos = e->len;
    e->cursor = pos; if (!extend) e->anchor = pos;
}
static void le_left(LineEdit* e, int extend)  { if (!extend && le_has_sel(e)) { int a, b; le_sel(e, &a, &b); le_goto(e, a, 0); } else le_goto(e, e->cursor - 1, extend); }
static void le_right(LineEdit* e, int extend) { if (!extend && le_has_sel(e)) { int a, b; le_sel(e, &a, &b); le_goto(e, b, 0); } else le_goto(e, e->cursor + 1, extend); }
static void le_home(LineEdit* e, int extend) { le_goto(e, 0, extend); }
static void le_end(LineEdit* e, int extend)  { le_goto(e, e->len, extend); }
static int le_wcls(char c) { return c == ' ' ? 0 : ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') ? 1 : 2; }
static void le_word_left(LineEdit* e, int extend) {
    int i = e->cursor; while (i > 0 && e->buf[i - 1] == ' ') i--;
    if (i > 0) { int c = le_wcls(e->buf[i - 1]); while (i > 0 && le_wcls(e->buf[i - 1]) == c) i--; }
    le_goto(e, i, extend);
}
static void le_word_right(LineEdit* e, int extend) {
    int i = e->cursor;
    if (i < e->len && e->buf[i] != ' ') { int c = le_wcls(e->buf[i]); while (i < e->len && le_wcls(e->buf[i]) == c) i++; }
    while (i < e->len && e->buf[i] == ' ') i++;
    le_goto(e, i, extend);
}
static void le_click(LineEdit* e, int col, int extend) { le_goto(e, col, extend); }   /* col = character index under the mouse */

/* ---- clipboard (the includer supplies LE_CLIP_SET / LE_CLIP_GET) ---- */
static int le_copy(LineEdit* e) {                    /* returns 1 if text was copied */
    if (e->password || !le_has_sel(e)) return 0;
    int a, b; le_sel(e, &a, &b);
    LE_CLIP_SET(e->buf + a, b - a);
    return 1;
}
static int le_cut(LineEdit* e) {
    if (!le_copy(e)) return 0;                       /* password fields: nothing copied, nothing deleted */
    le_insert(e, "", 0);
    return 1;
}
static int le_paste(LineEdit* e) {
    char tmp[256];
    int n = (int)LE_CLIP_GET(tmp, (int)sizeof tmp);
    if (n <= 0) return 0;
    if (n > (int)sizeof tmp) n = (int)sizeof tmp;
    return le_insert(e, tmp, n);
}
/* One key press, with modifiers. Returns 1 if the field used it. `ch` is the key code from
 * sys_keyev (ASCII, or 1001 up 1002 down 1003 left 1004 right 1005 home 1006 end 1007 delete). */
static int le_key(LineEdit* e, int ch, int shift, int ctrl) {
    if (ctrl) {
        int l = (ch >= 'A' && ch <= 'Z') ? ch + 32 : ch;
        if (l == 'a') { le_select_all(e); return 1; }
        if (l == 'c') { le_copy(e); return 1; }
        if (l == 'x') { le_cut(e); return 1; }
        if (l == 'v') { le_paste(e); return 1; }
        if (ch == 1003) { le_word_left(e, shift); return 1; }
        if (ch == 1004) { le_word_right(e, shift); return 1; }
        return 0;
    }
    if (ch == 1003) { le_left(e, shift); return 1; }
    if (ch == 1004) { le_right(e, shift); return 1; }
    if (ch == 1005) { le_home(e, shift); return 1; }
    if (ch == 1006) { le_end(e, shift); return 1; }
    if (ch == 1007) { le_delete(e); return 1; }
    if (ch == '\b' || ch == 127) { le_backspace(e); return 1; }
    if (ch >= 32 && ch < 127) { le_type(e, (char)ch); return 1; }
    return 0;
}
/* What to draw for character i: the character itself, or a bullet in a password field. */
static char le_display_char(const LineEdit* e, int i) { return e->password ? '*' : e->buf[i]; }

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC pop_options
#endif
#endif
