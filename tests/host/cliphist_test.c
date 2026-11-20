#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../../user/src/cliphist.h"
static int failures = 0, checks = 0;
#define expect(c, msg) do { checks++; if (c) printf("  ok    %s\n", msg); else { printf("  FAIL  %s\n", msg); failures++; } } while (0)

static ClipHist* H;
static int add(const char* s) { return cbh_add(H, s, (int)strlen(s)); }
static int top_is(int i, const char* s) { return cbh_len(H, i) == (int)strlen(s) && cbh_text(H, i) && !memcmp(cbh_text(H, i), s, strlen(s)); }
static char PV[600];
static const char* pv(const char* s, int max) { cbh_preview(s, (int)strlen(s), PV, sizeof PV, max); return PV; }
static char LB[32];
static const char* lb(const char* s, int len) { cbh_label(s, len, LB, sizeof LB); return LB; }

/* ---- a plain reference model of the store, to compare against after random operations ---- */
#define MODEL_MAX 20
static char* M[MODEL_MAX]; static int ML[MODEL_MAX]; static int MN;
static void m_clear(void) { for (int i = 0; i < MN; i++) free(M[i]); MN = 0; }
static int m_blank(const char* s, int n) { for (int i = 0; i < n; i++) if (s[i] != ' ' && s[i] != '\t' && s[i] != '\n' && s[i] != '\r') return 0; return 1; }
static int m_add(const char* s, int n) {
    if (n <= 0 || n > CBH_ENTRY_MAX || m_blank(s, n)) return CBH_SKIPPED;
    for (int i = 0; i < MN; i++) if (ML[i] == n && !memcmp(M[i], s, n)) {
        char* t = M[i]; int tl = ML[i]; for (int k = i; k > 0; k--) { M[k] = M[k - 1]; ML[k] = ML[k - 1]; } M[0] = t; ML[0] = tl; return CBH_MOVED; }
    if (MN == MODEL_MAX) { free(M[MN - 1]); MN--; }
    for (int k = MN; k > 0; k--) { M[k] = M[k - 1]; ML[k] = ML[k - 1]; }
    M[0] = malloc(n); memcpy(M[0], s, n); ML[0] = n; MN++; return CBH_ADDED;
}
static void m_remove(int i) { if (i < 0 || i >= MN) return; free(M[i]); for (int k = i; k < MN - 1; k++) { M[k] = M[k + 1]; ML[k] = ML[k + 1]; } MN--; }
static int same_as_model(void) {
    if (cbh_count(H) != MN) return 0;
    for (int i = 0; i < MN; i++) if (cbh_len(H, i) != ML[i] || memcmp(cbh_text(H, i), M[i], ML[i])) return 0;
    return 1;
}

int main(void) {
    setbuf(stdout, 0);
    printf("== clipboard history (cliphist.h)\n");
    H = malloc(sizeof *H); cbh_init(H);

    /* ---- the store ---- */
    expect(cbh_count(H) == 0 && cbh_text(H, 0) == 0 && cbh_len(H, 0) == 0, "a new history is empty");
    expect(add("hello") == CBH_ADDED && cbh_count(H) == 1 && top_is(0, "hello"), "the first copy is recorded");
    expect(add("world") == CBH_ADDED && cbh_count(H) == 2 && top_is(0, "world") && top_is(1, "hello"), "newest first: \"world\" is on top of \"hello\"");
    expect(add("hello") == CBH_MOVED && cbh_count(H) == 2 && top_is(0, "hello") && top_is(1, "world"), "copying \"hello\" again moves it to the top; no duplicate");
    expect(add("hello") == CBH_MOVED && cbh_count(H) == 2 && top_is(0, "hello") && top_is(1, "world"), "copying the newest entry again changes nothing");
    expect(add("abcde") == CBH_ADDED && add("abcdf") == CBH_ADDED && cbh_count(H) == 4 && top_is(0, "abcdf") && top_is(1, "abcde"), "same length but different text: two entries");
    expect(add("hell") == CBH_ADDED && add("hello") == CBH_MOVED && top_is(0, "hello") && top_is(1, "hell"), "a prefix is not the same text");
    cbh_clear(H); expect(cbh_count(H) == 0 && cbh_text(H, 0) == 0, "clear empties the history");
    { char z1[] = { 'a', 0, 'b' }, z2[] = { 'a', 0, 'c' };
      cbh_add(H, z1, 3); expect(cbh_add(H, z2, 3) == CBH_ADDED && cbh_count(H) == 2, "text containing a NUL byte is compared by length, not as a C string");
      expect(cbh_add(H, z1, 3) == CBH_MOVED && cbh_count(H) == 2, "...and found again"); }
    cbh_clear(H);
    expect(cbh_add(H, "x", 0) == CBH_SKIPPED && cbh_add(H, "x", -5) == CBH_SKIPPED && cbh_count(H) == 0, "an empty copy is not recorded");
    expect(add("   ") == CBH_SKIPPED && add("\n\n") == CBH_SKIPPED && add(" \t\r\n ") == CBH_SKIPPED && cbh_count(H) == 0, "a copy of only blanks and line breaks is not recorded");
    expect(add(" x ") == CBH_ADDED && top_is(0, " x "), "blanks around real text are kept exactly");
    cbh_clear(H);
    { char* big = malloc(CBH_ENTRY_MAX + 10); memset(big, 'q', CBH_ENTRY_MAX + 10);
      expect(cbh_add(H, big, CBH_ENTRY_MAX + 1) == CBH_SKIPPED && cbh_count(H) == 0, "a copy over the limit is not recorded (a cut copy would silently lose text)");
      expect(cbh_add(H, big, CBH_ENTRY_MAX) == CBH_ADDED && cbh_len(H, 0) == CBH_ENTRY_MAX && cbh_text(H, 0)[CBH_ENTRY_MAX - 1] == 'q', "exactly the limit is recorded in full");
      big[CBH_ENTRY_MAX - 1] = 'r'; expect(cbh_add(H, big, CBH_ENTRY_MAX) == CBH_ADDED && cbh_count(H) == 2, "two limit-sized texts that differ only in the last byte are two entries");
      free(big); }
    cbh_clear(H);
    { char b[32]; for (int i = 1; i <= 25; i++) { snprintf(b, sizeof b, "entry %d", i); add(b); }
      int ok = cbh_count(H) == CBH_MAX; for (int i = 0; i < CBH_MAX; i++) { snprintf(b, sizeof b, "entry %d", 25 - i); ok &= top_is(i, b); }
      expect(ok, "after 25 copies the 20 newest remain, in order; the 5 oldest are gone");
      expect(add("entry 12") == CBH_MOVED && cbh_count(H) == CBH_MAX && top_is(0, "entry 12") && top_is(1, "entry 25") && top_is(13, "entry 13"), "a full history: re-copying an old entry moves it up without losing anyone");
      cbh_remove(H, 5); expect(cbh_count(H) == CBH_MAX - 1 && top_is(4, "entry 22") && top_is(5, "entry 20"), "removing the 6th entry (entry 21) closes the gap");
      cbh_remove(H, 0); expect(cbh_count(H) == CBH_MAX - 2 && top_is(0, "entry 25"), "removing the newest");
      cbh_remove(H, cbh_count(H) - 1); expect(cbh_count(H) == CBH_MAX - 3, "removing the oldest");
      cbh_remove(H, 99); cbh_remove(H, -1); expect(cbh_count(H) == CBH_MAX - 3, "removing a nonexistent entry does nothing");
      add("fresh one"); expect(top_is(0, "fresh one") && cbh_count(H) == CBH_MAX - 2, "a new copy after removals goes on top");
      expect(cbh_text(H, 1) && !strncmp(cbh_text(H, 1), "entry 25", 8), "...and the others are undisturbed"); }

    /* ---- many random operations against the reference model ---- */
    cbh_clear(H); m_clear(); srand(12345);
    { char pool[40][80]; int pl[40];
      for (int i = 0; i < 40; i++) { snprintf(pool[i], sizeof pool[i], "text number %d %s", i % 33, (i % 7 == 0) ? "\nsecond line" : ""); pl[i] = (int)strlen(pool[i]); }
      strcpy(pool[38], "   \n  "); pl[38] = 6; pl[39] = 0;
      int ok = 1, ops = 0;
      for (int it = 0; it < 6000 && ok; it++) {
          int r = rand() % 100;
          if (r < 70) { int k = rand() % 40; int a = cbh_add(H, pool[k], pl[k]); int b = m_add(pool[k], pl[k]); if (a != b) ok = 0; }
          else if (r < 95) { int k = rand() % (MN + 2) - 1; cbh_remove(H, k); m_remove(k); }
          else { cbh_clear(H); m_clear(); }
          ops++; if (!same_as_model()) ok = 0;
      }
      expect(ok && ops == 6000, "6000 random adds / removes / clears behave exactly like the plain reference model"); }
    m_clear();

    /* ---- watching the system clipboard ---- */
    cbh_init(H);
    expect(cbh_should_read(H, 0, 5, 1) == 1, "the first look at a clipboard with text: read it");
    expect(cbh_should_read(H, 0, 5, 1) == 0, "the same sequence number again: nothing new");
    expect(cbh_should_read(H, 1, 5, 1) == 1 && cbh_should_read(H, 1, 5, 1) == 0, "a new sequence number: read once");
    expect(cbh_should_read(H, 2, 9, 2) == 0 && cbh_should_read(H, 2, 9, 1) == 0, "a non-text clipboard is not read, and not looked at again");
    expect(cbh_should_read(H, 3, 0, 1) == 0, "a cleared clipboard (length 0) is not read");
    expect(cbh_should_read(H, 4, CBH_ENTRY_MAX, 1) == 1, "a clipboard of exactly the limit is read");
    expect(cbh_should_read(H, 5, CBH_ENTRY_MAX + 1, 1) == 0 && cbh_should_read(H, 5, CBH_ENTRY_MAX + 1, 1) == 0, "an oversized clipboard is skipped, and not re-examined every frame");
    expect(cbh_should_read(H, 6, 3, 1) == 1, "the next normal copy after that is read");
    expect(cbh_should_read(H, 0xFFFFFFFFu, 3, 1) == 1 && cbh_should_read(H, 0, 3, 1) == 1, "the sequence number wrapping around is just another change");
    cbh_init(H); expect(cbh_should_read(H, 0, 0, 0) == 0 && cbh_should_read(H, 0, 4, 1) == 0, "an empty clipboard at the start is remembered, nothing is read");

    /* ---- one-line previews ---- */
    expect(!strcmp(pv("hello world", 40), "hello world"), "short text is shown as it is");
    expect(!strcmp(pv("   \n\t  indented text", 40), "indented text"), "leading blanks and line breaks are dropped");
    expect(!strcmp(pv("a    b\t\tc", 40), "a b c"), "runs of blanks become one space");
    expect(!strcmp(pv("line one\nline two", 40), "line one | line two"), "a line break is shown as \" | \"");
    expect(!strcmp(pv("one\r\ntwo\r\n\r\nthree", 40), "one | two | three"), "CRLF and blank lines make one separator each");
    expect(!strcmp(pv("one  \n  two", 40), "one | two"), "blanks around a line break are absorbed");
    expect(!strcmp(pv("text\n\n\n", 40), "text") && !strcmp(pv("text   ", 40), "text"), "trailing line breaks and blanks are not shown");
    expect(!strcmp(pv("bell\x07here", 40), "bell?here") && !strcmp(pv("esc\x1b[0m", 40), "esc?[0m"), "control characters are shown as '?'");
    expect(!strcmp(pv("a\xD8\xA8\xD8\xA7z", 40), "a??z"), "each UTF-8 character (here two Arabic letters) is one '?'");
    expect(!strcmp(pv("0123456789", 10), "0123456789"), "text of exactly the maximum length is not cut");
    expect(!strcmp(pv("0123456789A", 10), "0123456..."), "one more character: cut to the maximum, ending in dots");
    expect(strlen(pv("a very long line of text that goes on and on", 20)) == 20 && !strcmp(pv("a very long line of text that goes on and on", 20), "a very long line ..."), "a long line is cut to exactly the maximum");
    expect(!strcmp(pv("abcdefgh", 3), "abc") && !strcmp(pv("abcdefgh", 1), "a") && !strcmp(pv("abcdefgh", 0), ""), "tiny maximums cut without dots");
    expect(!strcmp(pv("abcdefgh", 4), "a...") , "a maximum of 4 leaves one character and the dots");
    expect(!strcmp(pv("", 40), "") && !strcmp(pv(" \n ", 40), ""), "empty or blank text gives an empty preview");
    { char big[1500]; memset(big, 'x', sizeof big - 1); big[sizeof big - 1] = 0; cbh_preview(big, (int)strlen(big), PV, sizeof PV, 56);
      expect(strlen(PV) == 56 && !strcmp(PV + 53, "..."), "a very long text is cut safely"); }
    { char small[8]; int n = cbh_preview("abcdefghijklmnop", 16, small, sizeof small, 40); expect(n == 7 && small[7] == 0 && !strcmp(small, "abcd..."), "a small output buffer limits the preview and is terminated"); }
    expect(cbh_preview("abc", 3, PV, 0, 10) == 0, "a zero-size buffer is refused");
    { char nl[8] = "a\nb\nc\nd"; cbh_preview(nl, 7, PV, sizeof PV, 40); expect(!strcmp(PV, "a | b | c | d"), "several short lines"); }

    /* ---- the size label ---- */
    expect(!strcmp(lb("hello", 5), "5 chars") && !strcmp(lb("x", 1), "1 char") && !strcmp(lb("", 0), "0 chars"), "small texts: N chars");
    expect(!strcmp(lb("a\nb", 3), "2 lines") && !strcmp(lb("a\nb\nc\nd", 7), "4 lines"), "several lines: N lines");
    expect(!strcmp(lb("one line\n", 9), "9 chars"), "a single line with a trailing newline is one line");
    { char t[2000]; memset(t, 'a', sizeof t);
      expect(!strcmp(lb(t, 999), "999 chars") && !strcmp(lb(t, 1000), "1.0 KB") && !strcmp(lb(t, 1536), "1.5 KB") && !strcmp(lb(t, 1999), "2.0 KB"), "1000 bytes and up: N.N KB (rounded)"); }
    { char* t = malloc(65536); memset(t, 'a', 65536); expect(!strcmp(lb(t, 65536), "64.0 KB") && !strcmp(lb(t, 10240), "10.0 KB"), "the largest entry is 64.0 KB"); free(t); }
    { char tiny[8]; expect(cbh_label("abc", 3, tiny, sizeof tiny) == 0 && tiny[0] == 0, "a label buffer that is too small is refused"); }

    /* ---- panel geometry ---- */
    expect(cbp_rows(0) == 1 && cbp_rows(1) == 1 && cbp_rows(5) == 5 && cbp_rows(8) == 8 && cbp_rows(9) == 8 && cbp_rows(20) == 8, "the panel shows between 1 and 8 rows");
    expect(cbp_height(0) == CBP_HEAD_H + CBP_ROW_H + CBP_FOOT_H && cbp_height(20) == CBP_HEAD_H + 8 * CBP_ROW_H + CBP_FOOT_H, "its height follows the rows (an empty history still has room for its message)");
    { int x, y; cbp_place(1920, 1000, 8, &x, &y); expect(x == (1920 - CBP_W) / 2 && y == (1000 - cbp_height(8)) / 2, "the panel is centred");
      cbp_place(300, 100, 8, &x, &y); expect(x == 0 && y == 8, "on a tiny screen it stays at the corner, never off screen"); }
    expect(cbp_clamp_scroll(12, 4) == 4 && cbp_clamp_scroll(12, 99) == 4 && cbp_clamp_scroll(12, -3) == 0 && cbp_clamp_scroll(5, 2) == 0, "scroll is held between 0 and count - 8");
    expect(cbp_follow(0, 3, 12) == 0 && cbp_follow(5, 0, 12) == 0 && cbp_follow(8, 0, 12) == 1 && cbp_follow(11, 0, 12) == 4 && cbp_follow(6, 2, 12) == 2, "the scroll follows the selection just enough");

    /* ---- panel hit testing: panel at (400, 200) ---- */
    { int X = 400, Y = 200;
      int rt = cbp_row_top(Y, 0);
      expect(rt == Y + CBP_HEAD_H && cbp_row_top(Y, 3) == Y + CBP_HEAD_H + 3 * CBP_ROW_H, "rows start below the header");
      expect(cbp_row_at(X, Y, 12, 0, X + 100, rt + 5) == 0 && cbp_row_at(X, Y, 12, 0, X + 100, rt + CBP_ROW_H + 5) == 1, "the rows are found top to bottom");
      expect(cbp_row_at(X, Y, 12, 4, X + 100, rt + 5) == 4 && cbp_row_at(X, Y, 12, 4, X + 100, rt + 7 * CBP_ROW_H + 5) == 11, "scrolled by 4: the top row is entry 4, the last visible is 11");
      expect(cbp_row_at(X, Y, 12, 99, X + 100, rt + 5) == 4, "a scroll beyond the end acts as the clamped scroll");
      expect(cbp_row_at(X, Y, 12, 0, X + 100, rt + CBP_ROW_H - 4) == -1 && cbp_row_at(X, Y, 12, 0, X + 100, rt + CBP_ROW_H - 5) == 0, "the 4 pixels between two rows belong to neither");
      expect(cbp_row_at(X, Y, 12, 0, X + 100, rt - 1) == -1, "the header is not a row");
      expect(cbp_row_at(X, Y, 12, 0, X + 100, rt + 8 * CBP_ROW_H + 5) == -1, "below the last visible row: nothing");
      expect(cbp_row_at(X, Y, 3, 0, X + 100, rt + 3 * CBP_ROW_H + 5) == -1 && cbp_row_at(X, Y, 3, 0, X + 100, rt + 2 * CBP_ROW_H + 5) == 2, "with 3 entries only 3 rows exist");
      expect(cbp_row_at(X, Y, 0, 0, X + 100, rt + 5) == -1, "an empty history has no rows");
      expect(cbp_row_at(X, Y, 12, 0, X + CBP_INSET, rt + 5) == 0 && cbp_row_at(X, Y, 12, 0, X + CBP_INSET - 1, rt + 5) == -1, "a row starts CBP_INSET inside the panel");
      expect(cbp_row_at(X, Y, 12, 0, X + CBP_W - CBP_INSET - 1, rt + 5) == 0 && cbp_row_at(X, Y, 12, 0, X + CBP_W - CBP_INSET, rt + 5) == -1, "...and ends CBP_INSET before the right edge");
      int dx = cbp_del_x(X);
      expect(cbp_del_at(X, Y, 12, 0, dx, rt + 5) == 0 && cbp_del_at(X, Y, 12, 0, dx + CBP_DEL_W - 1, rt + 5) == 0, "the delete button is found over its width");
      expect(cbp_del_at(X, Y, 12, 0, dx - 1, rt + 5) == -1 && cbp_del_at(X, Y, 12, 0, dx + CBP_DEL_W, rt + 5) == -1, "...and not beside it");
      expect(cbp_del_at(X, Y, 12, 3, dx + 3, rt + 2 * CBP_ROW_H + 5) == 5, "the delete button of a scrolled row names the right entry");
      expect(cbp_del_x(X) + CBP_DEL_W + 8 + CBP_INSET == X + CBP_W, "the delete button sits 8 pixels left of the row's right end");
      int bx = X + CBP_W - CBP_INSET - CBP_CLR_W, by = Y + 14;
      expect(cbp_clear_at(X, Y, bx, by) && cbp_clear_at(X, Y, bx + CBP_CLR_W - 1, by + CBP_CLR_H - 1), "the Clear button: its first and last pixel");
      expect(!cbp_clear_at(X, Y, bx - 1, by) && !cbp_clear_at(X, Y, bx + CBP_CLR_W, by) && !cbp_clear_at(X, Y, bx, by - 1) && !cbp_clear_at(X, Y, bx, by + CBP_CLR_H), "...and not one pixel outside it");
      expect(cbp_inside(X, Y, 8, X, Y) && cbp_inside(X, Y, 8, X + CBP_W - 1, Y + cbp_height(8) - 1), "inside: both corners");
      expect(!cbp_inside(X, Y, 8, X - 1, Y) && !cbp_inside(X, Y, 8, X + CBP_W, Y) && !cbp_inside(X, Y, 8, X, Y - 1) && !cbp_inside(X, Y, 8, X, Y + cbp_height(8)), "outside: one pixel beyond each edge");
      expect(cbp_inside(X, Y, 1, X + 5, Y + cbp_height(1) - 1) && !cbp_inside(X, Y, 1, X + 5, Y + cbp_height(1)), "a short panel (1 row) is shorter"); }

    /* ---- panel keys ---- */
    expect(cbp_quick('1', 5) == 0 && cbp_quick('5', 5) == 4 && cbp_quick('6', 5) == -1 && cbp_quick('9', 20) == 8 && cbp_quick('0', 20) == -1 && cbp_quick('a', 20) == -1, "digit keys pick the Nth newest entry, only if it exists");
    { CbpState s = { 0, 0 };
      expect(cbp_key(&s, 12, 1002) == CBA_NONE && s.sel == 1 && cbp_key(&s, 12, 1002) == CBA_NONE && cbp_key(&s, 12, 1002) == CBA_NONE && s.sel == 3 && s.scroll == 0, "Down x3 moves the selection to entry 3");
      cbp_key(&s, 12, 1001); expect(s.sel == 2, "Up moves it back");
      s.sel = 0; cbp_key(&s, 12, 1001); expect(s.sel == 0, "Up at the top stays at the top");
      cbp_key(&s, 12, 1006); expect(s.sel == 11 && s.scroll == 4, "End: last entry, scrolled so it shows");
      cbp_key(&s, 12, 1002); expect(s.sel == 11 && s.scroll == 4, "Down at the end stays");
      cbp_key(&s, 12, 1005); expect(s.sel == 0 && s.scroll == 0, "Home: first entry, scrolled back");
      cbp_key(&s, 12, 1009); expect(s.sel == 8 && s.scroll == 1, "Page Down moves by one page (8) and scrolls just enough");
      cbp_key(&s, 12, 1009); expect(s.sel == 11 && s.scroll == 4, "...and stops at the last entry");
      cbp_key(&s, 12, 1008); expect(s.sel == 3 && s.scroll == 3, "Page Up goes back one page");
      cbp_key(&s, 12, 1008); expect(s.sel == 0 && s.scroll == 0, "...and stops at the first");
      s.sel = 6; expect(cbp_key(&s, 12, '\n') == CBA_PICK && s.sel == 6, "Enter picks the selected entry");
      expect(cbp_key(&s, 12, '\r') == CBA_PICK && s.sel == 6, "(the other Enter code too)");
      expect(cbp_key(&s, 12, 1007) == CBA_DELETE && s.sel == 6, "Delete asks to remove the selected entry");
      expect(cbp_key(&s, 12, '3') == CBA_PICK && s.sel == 2, "a digit picks that entry directly and selects it");
      s.sel = 4; expect(cbp_key(&s, 5, '9') == CBA_NONE && s.sel == 4, "a digit with no such entry does nothing");
      expect(cbp_key(&s, 12, 27) == CBA_CLOSE, "Esc closes the panel");
      expect(cbp_key(&s, 12, 'x') == CBA_NONE && s.sel == 4 && cbp_key(&s, 12, 0) == CBA_NONE && cbp_key(&s, 12, 1003) == CBA_NONE, "other keys do nothing (the panel still uses them up)"); }
    { CbpState s = { 0, 0 };
      expect(cbp_key(&s, 0, 27) == CBA_CLOSE, "an empty panel: Esc closes");
      expect(cbp_key(&s, 0, '\n') == CBA_NONE && cbp_key(&s, 0, 1007) == CBA_NONE && cbp_key(&s, 0, 1002) == CBA_NONE && cbp_key(&s, 0, '1') == CBA_NONE && s.sel == 0, "...and nothing else does anything"); }
    { CbpState s = { 11, 4 }; cbp_fix(&s, 11); expect(s.sel == 10 && s.scroll == 3, "after removing the last of 12 entries the selection moves up and the scroll follows");
      cbp_fix(&s, 3); expect(s.sel == 2 && s.scroll == 0, "after the list shrank to 3: selection 2, no scroll");
      cbp_fix(&s, 0); expect(s.sel == 0 && s.scroll == 0, "an empty list resets both");
      s.sel = -4; s.scroll = -2; cbp_fix(&s, 5); expect(s.sel == 0 && s.scroll == 0, "negative values are repaired"); }
    { CbpState s = { 0, 0 }; cbp_wheel(&s, 12, 3); expect(s.scroll == 3, "the wheel scrolls");
      cbp_wheel(&s, 12, 9); expect(s.scroll == 4, "...down to the end only");
      cbp_wheel(&s, 12, -99); expect(s.scroll == 0, "...and back up to the start only");
      cbp_wheel(&s, 5, 3); expect(s.scroll == 0, "a list that fits never scrolls"); }
    { /* a whole interaction: three entries; delete the middle one with the keyboard, then pick */
      cbh_clear(H); add("first"); add("second"); add("third");
      CbpState s = { 0, 0 }; cbp_key(&s, cbh_count(H), 1002);
      int act = cbp_key(&s, cbh_count(H), 1007); expect(act == CBA_DELETE && top_is(s.sel, "second"), "select the middle entry and ask to delete it");
      cbh_remove(H, s.sel); cbp_fix(&s, cbh_count(H));
      expect(cbh_count(H) == 2 && top_is(0, "third") && top_is(1, "first") && s.sel == 1, "the entry is gone and the selection stays on a valid entry");
      act = cbp_key(&s, cbh_count(H), '\n'); expect(act == CBA_PICK && top_is(s.sel, "first"), "Enter now picks what is selected"); }

    free(H);
    printf("\n%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
