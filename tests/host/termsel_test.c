#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../../user/src/termsel.h"
static int failures = 0, checks = 0;
#define expect(c, msg) do { checks++; if (c) printf("  ok    %s\n", msg); else { printf("  FAIL  %s\n", msg); failures++; } } while (0)

static char L[8][TS_COLS];
static TermView V;
static void setup(void) {
    memset(L, 0, sizeof L);
    strcpy(L[0], "$ ls -la /ycfs/docs");
    strcpy(L[1], "total 3");
    strcpy(L[2], "");
    strcpy(L[3], "alpha beta  gamma.txt (x)");
    strcpy(L[4], "last line");
    V.lines = &L[0][0]; V.nlines = 5; V.cx = 8; V.cy = 36; V.first = 0; V.rows = 5; V.cols = 100;
}
static int X(int col) { return V.cx + col * TS_CW; }            /* left edge of a column */
static int Y(int row) { return V.cy + (row - V.first) * TS_CH + 4; }
static void down(TermSel* s, int col, int row, long long t) { ts_mouse_down(s, &V, X(col) + 1, Y(row), t); }
static int is_sel(TermSel* s, const char* want) { char b[600]; int n = ts_extract(s, &V, b, sizeof b); return n == (int)strlen(want) && !strcmp(b, want); }

/* paste sinks */
static char sunk[100000]; static int sunk_n, refuse_at = -1, stop_on_nl = 0, refused_calls = 0;
static int sink(void* ctx, char c) {
    (void)ctx;
    if (refuse_at >= 0 && sunk_n == refuse_at) { refused_calls++; return 0; }
    sunk[sunk_n++] = c; return (stop_on_nl && c == '\n') ? 2 : 1;
}

int main(void) {
    setbuf(stdout, 0);
    printf("== terminal selection / paste core (termsel.h)\n");
    TermSel s; TermPaste* P = malloc(sizeof *P);

    /* ---- pixel -> row / column ---- */
    setup(); ts_init(&s); int r, c;
    expect(ts_hit(&V, X(3) + 1, Y(1), 0, &r, &c) && r == 1 && c == 3, "a pointer just right of a column boundary: caret goes to that boundary");
    expect(ts_hit(&V, X(3) + 3, Y(0), 0, &r, &c) && r == 0 && c == 3, "caret mode: 3 px into a cell still rounds back");
    expect(ts_hit(&V, X(3) + 5, Y(0), 0, &r, &c) && r == 0 && c == 4, "caret mode: past the middle of a cell rounds forward");
    expect(ts_hit(&V, X(3) + 7, Y(0), 1, &r, &c) && c == 3, "cell mode: always the character under the pointer");
    expect(ts_hit(&V, X(60), Y(1), 0, &r, &c) && r == 1 && c == 7, "right of the text: the end of that row (\"total 3\" is 7 long)");
    expect(ts_hit(&V, 0, Y(3), 0, &r, &c) && r == 3 && c == 0, "left of the text area: column 0");
    expect(ts_hit(&V, X(5), V.cy - 20, 0, &r, &c) && r == 0 && c == 0, "above the first visible row: its very start");
    expect(ts_hit(&V, X(2), V.cy + 5 * TS_CH + 40, 0, &r, &c) && r == 4 && c == 9, "below the last row: the end of the last row");
    expect(ts_hit(&V, X(2), Y(2), 0, &r, &c) && r == 2 && c == 0, "an empty row: column 0");
    V.cols = 10; expect(ts_hit(&V, X(50), Y(0), 0, &r, &c) && c == 10, "a long row is clipped to the visible columns");
    V.cols = 100;
    V.first = 3; V.rows = 2; expect(ts_hit(&V, X(1), V.cy + 1, 0, &r, &c) && r == 3, "scrolled view: the first visible row is line 3");
    expect(ts_hit(&V, X(1), V.cy + TS_CH + 1, 0, &r, &c) && r == 4, "...and the second is line 4");
    expect(ts_hit(&V, X(1), V.cy + 5 * TS_CH, 0, &r, &c) && r == 4, "...and a pointer far below stays on line 4");
    V.first = 0; V.rows = 5;
    { TermView e = V; e.nlines = 0; expect(!ts_hit(&e, 10, 40, 0, &r, &c), "a terminal with no text has nothing to hit"); }

    /* ---- drag selection ---- */
    setup(); ts_init(&s);
    down(&s, 2, 0, 1000); expect(!s.active && s.drag, "pressing starts a drag but selects nothing yet");
    ts_mouse_up(&s); expect(!s.active && !s.drag, "a plain click selects nothing (and clears nothing wrongly)");
    down(&s, 2, 0, 5000); ts_mouse_drag(&s, &V, X(4) + 1, Y(0)); expect(s.active && is_sel(&s, "ls"), "dragging across two characters selects exactly those two");
    ts_mouse_drag(&s, &V, X(4) + 1, Y(0)); expect(is_sel(&s, "ls"), "repeating the same drag position changes nothing");
    ts_mouse_up(&s); ts_mouse_drag(&s, &V, X(9) + 1, Y(0)); expect(is_sel(&s, "ls"), "after the button is released the pointer no longer moves the selection");
    ts_clear(&s); down(&s, 4, 0, 9000); ts_mouse_drag(&s, &V, X(2) + 1, Y(0)); expect(is_sel(&s, "ls"), "dragging backwards selects the same text");
    ts_mouse_up(&s);
    ts_clear(&s); down(&s, 2, 0, 12000); ts_mouse_drag(&s, &V, X(3) + 1, Y(3)); ts_mouse_up(&s);
    expect(is_sel(&s, "ls -la /ycfs/docs\ntotal 3\n\nalp"), "a multi-line drag copies the rest of the first row, whole middle rows (an empty one stays empty) and the start of the last");
    { int a, b; expect(ts_row_range(&s, &V, 0, &a, &b) && a == 2 && b == 19, "row range: first row from the press to its end");
      expect(ts_row_range(&s, &V, 1, &a, &b) && a == 0 && b == 7, "row range: a middle row is fully selected");
      expect(!ts_row_range(&s, &V, 2, &a, &b), "row range: an empty row has nothing to highlight");
      expect(ts_row_range(&s, &V, 3, &a, &b) && a == 0 && b == 3, "row range: last row up to the pointer");
      expect(!ts_row_range(&s, &V, 4, &a, &b), "row range: rows after the selection are untouched"); }
    ts_clear(&s); down(&s, 3, 3, 15000); ts_mouse_drag(&s, &V, X(3) + 1, Y(0)); expect(s.active && is_sel(&s, "s -la /ycfs/docs\ntotal 3\n\nalp"), "an upward drag selects from the pointer to the press");
    ts_mouse_up(&s);
    ts_clear(&s); down(&s, 0, 0, 18000); ts_mouse_drag(&s, &V, X(50), V.cy + 400); ts_mouse_up(&s);
    expect(is_sel(&s, "$ ls -la /ycfs/docs\ntotal 3\n\nalpha beta  gamma.txt (x)\nlast line"), "dragging below the text selects everything to the very end");
    { char b[10]; int n = ts_extract(&s, &V, b, sizeof b); expect(n == 9 && !strcmp(b, "$ ls -la "), "a small output buffer is filled without overflowing"); }
    ts_clear(&s); expect(!s.active && is_sel(&s, ""), "clearing leaves nothing to copy");
    { char b[4] = "xyz"; int n = ts_extract(&s, &V, b, sizeof b); expect(n == 0 && b[0] == 0, "extracting with no selection gives an empty string"); }

    /* ---- word and line selection ---- */
    setup(); ts_init(&s);
    down(&s, 5, 0, 20000); down(&s, 5, 0, 20030); expect(is_sel(&s, "-la") && s.active && !s.drag, "double-click selects the word under the pointer (\"-la\")");
    down(&s, 5, 0, 20060); expect(is_sel(&s, "$ ls -la /ycfs/docs"), "triple-click selects the whole line");
    down(&s, 5, 0, 20090); expect(!s.active && s.drag, "a fourth quick click starts over as a single click");
    ts_mouse_up(&s);
    ts_clear(&s); down(&s, 9, 0, 30000); down(&s, 9, 0, 30010); expect(is_sel(&s, "/ycfs/docs"), "a path counts as one word");
    ts_clear(&s); down(&s, 8, 3, 31000); down(&s, 8, 3, 31010); expect(is_sel(&s, "beta"), "double-click on a plain word");
    ts_clear(&s); down(&s, 11, 3, 32000); down(&s, 11, 3, 32010); expect(is_sel(&s, "  "), "double-click on blanks selects the run of blanks");
    ts_clear(&s); down(&s, 22, 3, 33000); down(&s, 22, 3, 33010); expect(is_sel(&s, "("), "double-click on punctuation selects just that character");
    ts_clear(&s); down(&s, 14, 3, 34000); down(&s, 14, 3, 34010); expect(is_sel(&s, "gamma.txt"), "a file name with a dot is one word");
    ts_clear(&s); down(&s, 5, 2, 35000); down(&s, 5, 2, 35010); expect(!s.active, "double-click on an empty row selects nothing");
    ts_clear(&s); down(&s, 50, 4, 36000); down(&s, 50, 4, 36010); expect(is_sel(&s, "line"), "double-click right of the text selects the last word");
    ts_clear(&s); down(&s, 5, 0, 40000); down(&s, 5, 0, 40100); expect(is_sel(&s, "-la"), "two clicks 100 ticks apart are a double-click (the limit)");
    ts_clear(&s); ts_init(&s); down(&s, 5, 0, 41000); down(&s, 5, 0, 41101); expect(!s.active && s.click_n == 1, "...101 ticks apart are two single clicks");
    ts_init(&s); down(&s, 5, 0, 60000); down(&s, 5, 0, 60076); down(&s, 5, 0, 60156); expect(is_sel(&s, "$ ls -la /ycfs/docs"), "REGRESSION: a triple-click at the real measured pace (76 and 80 ticks between presses) selects the whole line");
    ts_init(&s); down(&s, 5, 0, 61000); down(&s, 5, 0, 61100); down(&s, 5, 0, 61201); expect(is_sel(&s, "-la") == 0 && s.click_n == 1, "a third press 101 ticks after the second starts a new series");
    ts_init(&s); down(&s, 5, 0, 42000); ts_mouse_down(&s, &V, X(5) + 1 + 9, Y(0), 42010); expect(!s.active && s.click_n == 1, "a second click more than 4 px away is not a double-click");
    ts_init(&s); down(&s, 5, 0, 43000); down(&s, 5, 1, 43010); expect(!s.active && s.click_n == 1, "a second click on another row is not a double-click");
    ts_init(&s); down(&s, 5, 0, 44000); down(&s, 5, 0, 44010); ts_mouse_up(&s); ts_mouse_drag(&s, &V, X(9), Y(3)); expect(is_sel(&s, "-la"), "dragging after a double-click does nothing (the word stays)");
    { int a, b; ts_word_bounds("", 0, 0, &a, &b); expect(a == 0 && b == 0, "word bounds of an empty line are empty");
      ts_word_bounds("abc", 3, 99, &a, &b); expect(a == 0 && b == 3, "word bounds with a column past the end use the last word");
      ts_word_bounds("ab cd", 5, 2, &a, &b); expect(a == 2 && b == 3, "word bounds on a single blank"); }

    /* ---- the terminal scrolls ---- */
    setup(); ts_init(&s); down(&s, 2, 1, 50000); ts_mouse_drag(&s, &V, X(4) + 1, Y(3)); ts_mouse_up(&s);
    { char before[300]; ts_extract(&s, &V, before, sizeof before);
      for (int i = 0; i < 7; i++) memcpy(L[i], L[i + 1], TS_COLS);
      memset(L[7], 0, TS_COLS); V.nlines = 4;   /* oldest line dropped */
      ts_scrolled(&s, 1); char after[300]; ts_extract(&s, &V, after, sizeof after);
      expect(!strcmp(before, after) && s.ar == 0 && s.cr == 2, "when the oldest line is dropped the selection follows the same text"); }
    setup(); ts_init(&s); down(&s, 2, 0, 51000); ts_mouse_drag(&s, &V, X(4) + 1, Y(2)); ts_mouse_up(&s);
    ts_scrolled(&s, 1); expect(s.active && s.ar == 0 && s.ac == 0 && s.cr == 1, "if the start scrolls off, the selection now starts at the top of the first remaining line");
    setup(); ts_init(&s); down(&s, 2, 0, 52000); ts_mouse_drag(&s, &V, X(4) + 1, Y(0)); ts_mouse_up(&s);
    ts_scrolled(&s, 1); expect(!s.active, "if the whole selection scrolls off, it is gone");
    setup(); ts_init(&s); down(&s, 0, 0, 53000); ts_mouse_drag(&s, &V, X(0), Y(1)); ts_mouse_up(&s);
    ts_scrolled(&s, 1); expect(!s.active, "a selection that collapses to nothing is not 'active'");
    setup(); ts_init(&s); down(&s, 0, 2, 54000); ts_mouse_drag(&s, &V, X(2), Y(3)); ts_scrolled(&s, 0); ts_scrolled(&s, -2); expect(s.ar == 2 && s.cr == 3, "scrolling by 0 or a negative number changes nothing");
    setup(); ts_init(&s); down(&s, 0, 2, 55000); ts_scrolled(&s, 1); expect(s.ar == 1 && s.cr == 1 && s.drag, "a drag in progress also follows the text");
    ts_mouse_up(&s);

    /* ---- paste: sanitising ---- */
    int n;
    n = tp_load(P, 3, 77, "a\r\nb\rc\nd", 8, 0); expect(n == 7 && !memcmp(P->buf, "a\nb\nc\nd", 7), "CRLF, lone CR and LF all become one newline each");
    n = tp_load(P, 3, 77, "x\ty", 3, 0); expect(n == 3 && P->buf[1] == '\t', "a process terminal keeps tabs");
    n = tp_load(P, 3, 77, "x\ty", 3, 1); expect(n == 3 && P->buf[1] == ' ', "the built-in terminal turns a tab into a space");
    n = tp_load(P, 3, 77, "a\x1b" "b\x01" "c\x7f" "d\x07" "e", 9, 0); expect(n == 5 && !memcmp(P->buf, "abcde", 5), "ESC and every other control character are dropped: a paste cannot carry terminal escape sequences");
    n = tp_load(P, 3, 77, "a\xD8\xA8\xD8\xA7z", 6, 0); expect(n == 4 && !memcmp(P->buf, "a??z", 4), "each UTF-8 character (here two Arabic letters) becomes exactly one '?'");
    n = tp_load(P, 3, 77, "\xE4\xB8\xAD" "x", 4, 0); expect(n == 2 && !memcmp(P->buf, "?x", 2), "a 3-byte UTF-8 character becomes one '?'");
    n = tp_load(P, 3, 77, "a\xA8" "b", 3, 0); expect(n == 2 && !memcmp(P->buf, "ab", 2), "a stray continuation byte is dropped");
    n = tp_load(P, 3, 77, "", 0, 0); expect(n == 0 && !P->active, "an empty paste does not start anything");
    n = tp_load(P, 3, 77, "\x1b\x01", 2, 0); expect(n == 0 && !P->active, "a paste of only control characters does not start anything");
    { char* big = malloc(TS_PASTE_MAX + 1000); memset(big, 'a', TS_PASTE_MAX + 1000);
      n = tp_load(P, 3, 77, big, TS_PASTE_MAX + 1000, 0); expect(n == TS_PASTE_MAX && P->truncated && P->active, "a paste longer than the limit is cut and flagged");
      n = tp_load(P, 3, 77, big, TS_PASTE_MAX, 0); expect(n == TS_PASTE_MAX && !P->truncated, "exactly the limit is not truncated");
      free(big); }
    n = tp_load(P, 4, 123, "hello", 5, 0); expect(P->active && P->target == 4 && P->pid == 123 && P->pos == 0, "a loaded paste remembers its window and process");

    /* ---- paste: feeding ---- */
    char txt[200]; for (int i = 0; i < 40; i++) txt[i] = (char)('A' + i % 26);
    tp_load(P, 1, 5, txt, 40, 0); sunk_n = 0; refuse_at = -1; stop_on_nl = 0;
    expect(tp_feed(P, 16, sink, 0) == 16 && sunk_n == 16 && P->active, "the first frame sends at most 16 characters");
    expect(tp_feed(P, 16, sink, 0) == 16 && sunk_n == 32 && P->active, "the second frame sends the next 16");
    expect(tp_feed(P, 16, sink, 0) == 8 && sunk_n == 40 && !P->active, "the last frame sends the rest and the paste ends");
    expect(!memcmp(sunk, txt, 40), "everything arrived in order, once");
    expect(tp_feed(P, 16, sink, 0) == 0 && sunk_n == 40, "feeding a finished paste does nothing");
    tp_load(P, 1, 5, txt, 40, 0); sunk_n = 0; refuse_at = 10; refused_calls = 0;
    expect(tp_feed(P, 16, sink, 0) == 10 && sunk_n == 10 && P->active && refused_calls == 1, "a full queue stops the frame at that character");
    expect(tp_feed(P, 16, sink, 0) == 0 && sunk_n == 10 && P->pos == 10, "...and while it stays full nothing is lost or skipped");
    refuse_at = -1; tp_feed(P, 100, sink, 0); expect(sunk_n == 40 && !memcmp(sunk, txt, 40) && !P->active, "when room appears the paste continues exactly where it stopped");
    tp_load(P, 1, 5, "ab\ncd\nef", 8, 0); sunk_n = 0; stop_on_nl = 1;
    expect(tp_feed(P, 100, sink, 0) == 3 && sunk_n == 3 && P->active, "a sink can stop the frame after a newline (a command was run)");
    expect(tp_feed(P, 100, sink, 0) == 3 && sunk_n == 6, "the next frame continues with the next line");
    expect(tp_feed(P, 100, sink, 0) == 2 && sunk_n == 8 && !P->active, "...until the paste is complete");
    tp_load(P, 1, 5, txt, 40, 0); tp_cancel(P); sunk_n = 0; expect(!P->active && tp_feed(P, 16, sink, 0) == 0 && sunk_n == 0 && P->target == -1, "a cancelled paste sends nothing more");
    tp_load(P, 1, 5, txt, 40, 0); tp_feed(P, 16, sink, 0); tp_load(P, 2, 6, "zz", 2, 0); sunk_n = 0; tp_feed(P, 16, sink, 0); expect(sunk_n == 2 && !memcmp(sunk, "zz", 2), "loading a new paste replaces the old one completely");

    /* ---- keys ---- */
    int code;
    expect(ts_classify_ctrl('c', 1, &code) == TS_KEY_COPY && ts_classify_ctrl('C', 1, &code) == TS_KEY_COPY, "Ctrl+Shift+C copies (either letter case)");
    expect(ts_classify_ctrl('v', 1, &code) == TS_KEY_PASTE && ts_classify_ctrl('V', 1, &code) == TS_KEY_PASTE, "Ctrl+Shift+V pastes");
    expect(ts_classify_ctrl('x', 1, &code) == TS_KEY_NONE && ts_classify_ctrl('a', 1, &code) == TS_KEY_NONE, "other Ctrl+Shift letters do nothing in a terminal");
    expect(ts_classify_ctrl('c', 0, &code) == TS_KEY_INTERRUPT && code == 3, "Ctrl+C is the interrupt: control code 3");
    expect(ts_classify_ctrl('a', 0, &code) == TS_KEY_CODE && code == 1, "Ctrl+A is control code 1");
    expect(ts_classify_ctrl('d', 0, &code) == TS_KEY_CODE && code == 4, "Ctrl+D is control code 4");
    expect(ts_classify_ctrl('u', 0, &code) == TS_KEY_CODE && code == 21, "Ctrl+U is control code 21");
    expect(ts_classify_ctrl('z', 0, &code) == TS_KEY_CODE && code == 26, "Ctrl+Z is control code 26");
    expect(ts_classify_ctrl('v', 0, &code) == TS_KEY_CODE && code == 22, "plain Ctrl+V is just control code 22 (paste is Ctrl+Shift+V)");
    expect(ts_classify_ctrl('1', 0, &code) == TS_KEY_NONE && code == 0, "Ctrl+digit is ignored");
    expect(ts_classify_ctrl('\n', 0, &code) == TS_KEY_NONE && ts_classify_ctrl(1001, 0, &code) == TS_KEY_NONE && ts_classify_ctrl(0, 0, &code) == TS_KEY_NONE, "Ctrl+Enter, Ctrl+arrow and nothing at all are ignored");

    free(P);
    printf("\n%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
