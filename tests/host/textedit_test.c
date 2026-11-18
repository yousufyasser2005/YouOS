/* Host test for user/src/textedit.h (the real file): behaviour checks + a randomised comparison
 * with a naive reference model + layout invariants for every width. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../user/src/textedit.h"

static int failures = 0, checks = 0;
#define expect(c, msg) do { checks++; if (c) printf("  ok    %s\n", msg); else { printf("  FAIL  %s\n", msg); failures++; } } while (0)
static unsigned long long rs = 88172645463325252ull;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 11); }

static int is(const TxDoc* d, const char* s) { return d->len == (int)strlen(s) && !memcmp(tx_data(d), s, d->len) && d->buf[d->len] == 0; }
static void type_str(TxDoc* d, const char* s) { while (*s) tx_type_char(d, *s++); }
static int count_nl(const TxDoc* d) { int n = 1; for (int i = 0; i < d->len; i++) if (d->buf[i] == '\n') n++; return n; }
static void put(TxDoc* d, const char* s) { tx_load(d, s, (int)strlen(s)); }

#define REINIT(d) do { tx_release(d); tx_init(d); } while (0)
static void test_basics(void) {
    printf("== typing, undo/redo and the modified flag\n");
    TxDoc d; tx_init(&d);
    expect(is(&d, "") && !tx_modified(&d) && !tx_can_undo(&d) && d.nlines == 1, "new document: empty, unmodified, nothing to undo");
    type_str(&d, "hello world");
    expect(is(&d, "hello world") && d.cursor == 11 && tx_modified(&d), "typed text appears, cursor follows");
    expect(d.un == 1, "a run of typing is ONE undo step");
    tx_undo(&d);
    expect(is(&d, "") && !tx_modified(&d) && tx_can_redo(&d), "undo removes the whole run and returns to 'unmodified'");
    tx_redo(&d);
    expect(is(&d, "hello world") && d.cursor == 11 && tx_modified(&d), "redo restores it, cursor at the end");

    REINIT(&d); type_str(&d, "ab\ncd");
    expect(d.un == 2 && d.nlines == 2, "Enter ends an undo group (\"ab\\n\" then \"cd\"); line count follows");
    tx_undo(&d); expect(is(&d, "ab\n") && d.nlines == 2, "first undo removes \"cd\"");
    tx_undo(&d); expect(is(&d, "") && d.nlines == 1, "second undo removes \"ab\\n\"");

    put(&d, "abcdef"); d.cursor = d.anchor = 6;
    tx_backspace(&d); tx_backspace(&d); tx_backspace(&d);
    expect(is(&d, "abc") && d.un == 1, "three backspaces: one undo step");
    tx_undo(&d); expect(is(&d, "abcdef") && d.cursor == 6, "undo brings back all three characters in order, cursor where it was");
    tx_redo(&d); expect(is(&d, "abc"), "redo deletes them again");

    put(&d, "abcdef"); d.cursor = d.anchor = 1;
    tx_delete_fwd(&d); tx_delete_fwd(&d);
    expect(is(&d, "adef") && d.un == 1, "forward deletes: one undo step");
    tx_undo(&d); expect(is(&d, "abcdef") && d.cursor == 1, "...undone in order");

    put(&d, "abcdef"); d.anchor = 2; d.cursor = 4;
    tx_type_char(&d, 'X');
    expect(is(&d, "abXef") && !tx_has_sel(&d), "typing over a selection replaces it");
    tx_undo(&d);
    expect(is(&d, "abcdef") && d.anchor == 2 && d.cursor == 4, "undo restores the text AND the selection");

    put(&d, "one"); d.cursor = d.anchor = 3; type_str(&d, "ab"); tx_move_left(&d, 0); type_str(&d, "Z");
    expect(is(&d, "oneaZb") && d.un == 2, "moving the cursor ends the typing run");

    REINIT(&d); type_str(&d, "abc"); tx_mark_saved(&d);
    expect(!tx_modified(&d), "saved: unmodified");
    type_str(&d, "d");
    expect(tx_modified(&d) && d.un == 2, "typing after a save is a NEW undo step (the saved state stays reachable)");
    tx_undo(&d); expect(is(&d, "abc") && !tx_modified(&d), "undo back to the saved state: unmodified again");
    tx_undo(&d); expect(tx_modified(&d), "undo past the saved state: modified");
    tx_redo(&d); expect(!tx_modified(&d), "redo to the saved state: unmodified");
    tx_undo(&d); type_str(&d, "Q");
    expect(tx_modified(&d) && !tx_can_redo(&d), "a new edit after undo discards the redo tail; the saved state is unreachable");
    tx_undo(&d); tx_undo(&d); tx_redo(&d); tx_redo(&d);
    expect(tx_modified(&d), "...and stays 'modified' whatever undo/redo does");
    tx_release(&d);
}

static void test_big(void) {
    printf("== limits and memory\n");
    TxDoc d; tx_init(&d);
    char* chunk = malloc(TX_MAX_DOC); memset(chunk, 'a', TX_MAX_DOC);
    int n = tx_insert(&d, chunk, TX_MAX_DOC - 10, TXK_OTHER);
    expect(n == TX_MAX_DOC - 10 && d.len == TX_MAX_DOC - 10 && !(d.flags & TXF_TRUNC), "inserts up to the 1 MiB limit");
    expect(!tx_can_undo(&d), "an edit too big to keep for undo starts the history over (and says nothing is undoable)");
    n = tx_insert(&d, "0123456789ABCDEF", 16, TXK_OTHER);
    expect(n == 10 && d.len == TX_MAX_DOC && (d.flags & TXF_TRUNC) && d.buf[d.len] == 0 && !memcmp(d.buf + d.len - 10, "0123456789", 10), "an insertion past the limit is cut short, flagged, and the document stays valid");
    expect(tx_insert(&d, "x", 1, TXK_TYPE) == 0 && d.len == TX_MAX_DOC, "a full document accepts nothing more");
    tx_select_all(&d); tx_delete_sel(&d);
    expect(d.len == 0 && d.nlines == 1 && !tx_can_undo(&d), "select all + delete empties it (too big to undo)");
    free(chunk); tx_release(&d);
    expect(txa_in_use == 0, "everything given back to the arena: no leak");

    tx_init(&d);
    for (int i = 0; i < 20000; i++) { char c = (char)('a' + i % 26); tx_set_cursor(&d, d.len, 0); tx_insert(&d, &c, 1, TXK_OTHER); }
    expect(d.un <= TX_UNDO_RECS && d.utlen <= TX_UNDO_TEXT && d.len == 20000, "20000 separate edits: undo history stays within its limits");
    char* final = malloc(d.len + 1); memcpy(final, d.buf, d.len + 1); int fl = d.len;
    int steps = 0; while (tx_undo(&d)) steps++;
    expect(steps == d.un && steps > 4000 && d.len == fl - steps, "undo walks back exactly as many steps as were kept (the oldest were dropped)");
    while (tx_redo(&d)) { }
    expect(d.len == fl && !memcmp(d.buf, final, fl), "redo of all of them restores the final text exactly");
    free(final); tx_release(&d);

    /* out of memory: exhaust the arena, then try to edit */
    tx_init(&d); put(&d, "keep me");
    void* hog[64]; int nh = 0; while (nh < 64 && (hog[nh] = txa_alloc(8u << 20))) nh++;
    void* small[8192]; int ns = 0; while (ns < 8192 && (small[ns] = txa_alloc(4096))) ns++;
    char* bigins = malloc(100000); memset(bigins, 'z', 100000);
    int got = tx_insert(&d, bigins, 100000, TXK_OTHER);
    expect(got == 0 && (d.flags & TXF_OOM) && is(&d, "keep me"), "out of memory: the edit is refused and the document is left untouched");
    for (int i = 0; i < nh; i++) txa_free(hog[i], 8u << 20);
    for (int i = 0; i < ns; i++) txa_free(small[i], 4096);
    free(bigins); tx_release(&d);
    expect(txa_in_use == 0, "arena fully released again");
}

static void test_load_and_select(void) {
    printf("== loading text, selection and counters\n");
    TxDoc d; tx_init(&d);
    const char* crlf = "a\r\nb\rc\x01\x02" "d\te\r\n";
    expect(tx_load(&d, crlf, (int)strlen(crlf)) == 0 && is(&d, "a\nb\ncd\te\n") && d.nlines == 4, "CRLF and lone CR become LF; stray control characters are dropped; Tab is kept");
    expect(!tx_modified(&d) && !tx_can_undo(&d) && d.cursor == 0, "a loaded document is unmodified with no history");
    char* huge = malloc(TX_MAX_DOC + 1); memset(huge, 'x', TX_MAX_DOC + 1);
    expect(tx_load(&d, huge, TX_MAX_DOC + 1) != 0 && is(&d, "a\nb\ncd\te\n"), "a file over the limit is refused and the open document is untouched");
    free(huge);

    put(&d, "hello brave new world");
    tx_select_word_at(&d, 8); { char o[32]; int n = tx_get_sel(&d, o, 31); expect(n == 5 && !memcmp(o, "brave", 5), "double-click selects the word under the mouse"); }
    tx_select_word_at(&d, 5); { char o[32]; int n = tx_get_sel(&d, o, 31); expect(n == 1 && o[0] == ' ', "double-click on a space selects the spaces"); }
    put(&d, "first line\nsecond line\nthird");
    tx_select_line_at(&d, 14); { char o[40]; int n = tx_get_sel(&d, o, 39); expect(n == 12 && !memcmp(o, "second line\n", 12), "triple-click selects the whole line including its newline"); }
    tx_select_line_at(&d, 25); { char o[40]; int n = tx_get_sel(&d, o, 39); expect(n == 5 && !memcmp(o, "third", 5), "...the last line has no newline"); }
    tx_select_all(&d); { char o[8]; int n = tx_get_sel(&d, o, 3); expect(n == d.len && o[0] == 'f', "select all; get_sel reports the full length even when the buffer is small"); }
    int ln, col;
    put(&d, "ab\ncde\n\nfg"); tx_logical_pos(&d, 5, &ln, &col); expect(ln == 2 && col == 3, "line/column of an offset (1-based)");
    tx_logical_pos(&d, 7, &ln, &col); expect(ln == 3 && col == 1, "...an empty line");
    tx_logical_pos(&d, d.len, &ln, &col); expect(ln == 4 && col == 3, "...the end of the text");
    expect(d.nlines == 4 && d.len == 10, "total lines and characters");
    tx_release(&d);
}

static void test_movement(void) {
    printf("== cursor movement over wrapped rows\n");
    TxDoc d; tx_init(&d);
    put(&d, "long line number one\nx\nanother long line");
    tx_layout(&d, 80);
    tx_set_cursor(&d, 10, 0);
    tx_move_rows(&d, 1, 0); expect(d.cursor == 22, "down from column 10 onto a short line: clamps to its end");
    tx_move_rows(&d, 1, 0); expect(d.cursor == 33, "down again: the remembered column 10 is back");
    tx_move_rows(&d, -1, 0); tx_move_rows(&d, -1, 0); expect(d.cursor == 10, "up twice: back where it started");
    tx_move_rows(&d, -1, 0); expect(d.cursor == 0, "up from the first row goes to the start of the text");
    tx_move_end(&d, 0); expect(d.cursor == 20, "End: end of the line");
    tx_move_home(&d, 0); expect(d.cursor == 0, "Home: start of the line");
    tx_move_doc_end(&d, 0); expect(d.cursor == d.len, "Ctrl+End");
    tx_move_rows(&d, 1, 0); expect(d.cursor == d.len, "down from the last row stays at the end");

    put(&d, "aaaa bbbb cccc dddd"); tx_layout(&d, 10);
    expect(tx_rows(&d) == 2 && tx_row_start(&d, 1) == 10 || tx_rows(&d) >= 2, "narrow window: the text wraps onto more rows");
    tx_set_cursor(&d, 12, 0); tx_move_home(&d, 0); expect(d.cursor == tx_row_start(&d, tx_row_of(&d, 12)), "Home on a wrapped row goes to the start of that ROW");
    tx_move_end(&d, 0); { int r = tx_row_of(&d, d.cursor); expect(r == tx_row_of(&d, 12), "End on a wrapped row stays on that row"); }

    put(&d, "foo  bar.baz  qux"); tx_layout(&d, 80);
    tx_set_cursor(&d, 0, 0);
    tx_move_word_right(&d, 0); expect(d.cursor == 5, "Ctrl+Right: to the start of the next word");
    tx_move_word_right(&d, 0); expect(d.cursor == 8, "...punctuation counts as its own word");
    tx_move_word_right(&d, 0); expect(d.cursor == 9, "...\"baz\" follows the dot directly");
    tx_move_word_right(&d, 0); expect(d.cursor == 14, "...skips the spaces after a word");
    tx_move_word_right(&d, 0); expect(d.cursor == 17 && d.cursor == d.len, "...the last word ends at the end of the text");
    tx_set_cursor(&d, d.len, 0); tx_move_word_left(&d, 0); expect(d.cursor == 14, "Ctrl+Left: to the start of the previous word");
    tx_move_word_left(&d, 0); expect(d.cursor == 9 && d.buf[9] == 'b', "...");

    put(&d, "abcdef"); tx_layout(&d, 80); tx_set_cursor(&d, 3, 0);
    tx_move_right(&d, 1); tx_move_right(&d, 1);
    expect(d.anchor == 3 && d.cursor == 5 && tx_has_sel(&d), "Shift+Right extends the selection from the anchor");
    tx_move_left(&d, 1); expect(d.anchor == 3 && d.cursor == 4, "Shift+Left shrinks it");
    tx_move_left(&d, 0); expect(d.cursor == 3 && !tx_has_sel(&d), "Left with a selection collapses to its left edge");
    tx_move_right(&d, 1); tx_move_right(&d, 1); tx_move_right(&d, 0); expect(d.cursor == 5 && !tx_has_sel(&d), "Right with a selection collapses to its right edge");
    tx_move_left(&d, 0); tx_move_left(&d, 0); tx_move_left(&d, 0); tx_move_left(&d, 0); tx_move_left(&d, 0); tx_move_left(&d, 0);
    expect(d.cursor == 0, "Left at the start of the text stays there");
    tx_click(&d, 0, 4, 0); expect(d.cursor == 4, "a click on column 4 puts the cursor before the 5th character");
    tx_click(&d, 0, 2, 1); expect(d.anchor == 4 && d.cursor == 2, "dragging extends from where the click started");
    tx_click(&d, 99, 99, 0); expect(d.cursor == 6, "a click below / right of the text goes to the end");
    tx_release(&d);
}

static int row_width(const TxDoc* d, int r) {
    int w = 0; for (int k = d->rows[r]; k < tx_row_end(d, r); k++) w += (d->buf[k] == '\t') ? tx_tabw(w) : 1; return w;
}
static void test_layout(void) {
    printf("== word wrap layout, every width\n");
    TxDoc d; tx_init(&d);
    static const char alphabet[] = "abcdefgh  \t\n.,ij";
    int bad_width = 0, bad_order = 0, bad_map = 0, bad_nl = 0, bad_soft = 0, runs = 0;
    for (int t = 0; t < 150; t++) {
        int n = (int)(rnd() % 300); char* s = malloc(n + 1);
        for (int i = 0; i < n; i++) s[i] = alphabet[rnd() % (sizeof alphabet - 1)];
        tx_load(&d, s, n); free(s);
        for (int cols = 1; cols <= 45; cols += (cols < 12 ? 1 : 7)) {
            runs++; tx_layout(&d, cols);
            if (d.rows[0] != 0) bad_order++;
            for (int r = 0; r < d.nrows; r++) {
                if (r + 1 < d.nrows && d.rows[r + 1] <= d.rows[r]) bad_order++;
                int e = tx_row_end(&d, r), single = (e - d.rows[r]) <= 1;
                if (!single && row_width(&d, r) > cols) bad_width++;
                for (int k = d.rows[r]; k < e; k++) if (d.buf[k] == '\n') bad_nl++;
                if (r + 1 < d.nrows) {
                    int hard_end = d.rows[r + 1] > d.rows[r] && d.buf[d.rows[r + 1] - 1] == '\n';
                    if (!hard_end && e - d.rows[r] > 1) {
                        char last = d.buf[e - 1]; int has_space = 0;
                        for (int k = d.rows[r]; k < e - 1; k++) if (d.buf[k] == ' ' || d.buf[k] == '\t') has_space = 1;
                        if (has_space && last != ' ' && last != '\t' && row_width(&d, r) < cols - 0) { /* a hard break although a space was available is only allowed when the part after the last space filled the row */ }
                        if (has_space && last != ' ' && last != '\t') {
                            int after = 0, k = e - 1; while (k >= d.rows[r] && d.buf[k] != ' ' && d.buf[k] != '\t') { after++; k--; }
                            if (after < cols - 0 && (k >= d.rows[r])) { /* the word after the last space is shorter than a row, so it should have moved to the next row */
                                int rowstart_cols = 0; (void)rowstart_cols; bad_soft++; }
                        }
                    }
                }
            }
            if (d.rows[d.nrows - 1] > d.len) bad_order++;
            for (int off = 0; off <= d.len; off++) {
                int r = tx_row_of(&d, off);
                if (d.rows[r] > off || (r + 1 < d.nrows && d.rows[r + 1] <= off)) { bad_map++; continue; }
                int c = tx_col_of(&d, r, off);
                if (tx_off_at(&d, r, c, 0) != off) bad_map++;
            }
        }
    }
    expect(runs > 1000, "layout checked over 1000+ random texts x widths");
    expect(bad_order == 0, "rows start at 0, strictly increase and stay inside the text");
    expect(bad_width == 0, "no row is wider than the window (a single wide character excepted)");
    expect(bad_nl == 0, "a newline only ever ends a row");
    expect(bad_soft == 0, "a row is only broken inside a word when that word could not move to the next row");
    expect(bad_map == 0, "every text offset maps to a row and column and back to the same offset");
    /* concrete */
    put(&d, "the quick brown fox"); tx_layout(&d, 10);
    expect(tx_rows(&d) == 2 && tx_row_start(&d, 1) == 10, "\"the quick brown fox\" at 10 columns: breaks after \"the quick \"");
    put(&d, "abcdefghijklmno"); tx_layout(&d, 6);
    expect(tx_rows(&d) == 3 && tx_row_start(&d, 1) == 6 && tx_row_start(&d, 2) == 12, "a word longer than the window is broken at the edge");
    put(&d, "a\tb"); tx_layout(&d, 80);
    expect(tx_col_of(&d, 0, 2) == 4 && tx_off_at(&d, 0, 4, 0) == 2 && tx_off_at(&d, 0, 2, 0) == 1, "Tab advances to the next multiple of 4");
    put(&d, "line\n"); tx_layout(&d, 80);
    expect(tx_rows(&d) == 2 && tx_row_start(&d, 1) == 5 && tx_row_end(&d, 1) == 5, "a trailing newline gives an empty last row to put the cursor on");
    put(&d, ""); tx_layout(&d, 80);
    expect(tx_rows(&d) == 1 && tx_row_end(&d, 0) == 0, "an empty document has one empty row");
    tx_layout(&d, 40); tx_layout(&d, 80); expect(d.layout_cols == 80, "changing the width re-lays the text out");
    tx_release(&d);
}

/* naive model: plain text + the same replace semantics */
typedef struct { char* t; int n; } Model;
static void m_replace(Model* m, int pos, int del, const char* ins, int k) {
    memmove(m->t + pos + k, m->t + pos + del, m->n - pos - del); memcpy(m->t + pos, ins, k); m->n += k - del;
}
static void test_fuzz(void) {
    printf("== randomised editing against a reference model\n");
    TxDoc d; tx_init(&d);
    Model m; m.t = malloc(1 << 22); m.n = 0;
    enum { MAXSNAP = 4000 };
    char** snap = calloc(MAXSNAP, sizeof(char*)); int* snaplen = calloc(MAXSNAP, sizeof(int)); int nsnap = 0;
    snap[0] = malloc(1); snaplen[0] = 0; nsnap = 1;
    int mismatch = 0, nl_bad = 0, range_bad = 0, edits = 0;
    char scratch[200];
    for (int step = 0; step < 6000 && nsnap < MAXSNAP - 1; step++) {
        tx_layout(&d, 1 + (int)(rnd() % 50));
        int op = (int)(rnd() % 22), changed = 0;
        int a, b; tx_sel(&d, &a, &b);
        int cur_before = d.cursor;
        if (op < 6) {                                   /* type a character, replacing any selection */
            char c = (char)((rnd() % 9 == 0) ? '\n' : (rnd() % 12 == 0) ? '\t' : ' ' + rnd() % 90);
            m_replace(&m, a, b - a, &c, 1); tx_type_char(&d, c); changed = 1;
        } else if (op == 6 || op == 7) {                // paste some text
            int k = (int)(rnd() % 60); for (int i = 0; i < k; i++) scratch[i] = (char)(rnd() % 7 == 0 ? '\n' : 'a' + rnd() % 26);
            m_replace(&m, a, b - a, scratch, k); tx_insert(&d, scratch, k, TXK_OTHER); changed = (k > 0 || b > a);
        } else if (op == 8 || op == 9) {                // backspace
            if (b > a) m_replace(&m, a, b - a, "", 0), changed = 1;
            else if (d.cursor > 0) m_replace(&m, d.cursor - 1, 1, "", 0), changed = 1;
            tx_backspace(&d);
        } else if (op == 10) {                          // delete
            if (b > a) m_replace(&m, a, b - a, "", 0), changed = 1;
            else if (d.cursor < d.len) m_replace(&m, d.cursor, 1, "", 0), changed = 1;
            tx_delete_fwd(&d);
        } else if (op == 11) { tx_move_left(&d, rnd() & 1); }
        else if (op == 12) { tx_move_right(&d, rnd() & 1); }
        else if (op == 13) { tx_move_rows(&d, (int)(rnd() % 5) - 2, rnd() & 1); }
        else if (op == 14) { if (rnd() & 1) tx_move_home(&d, rnd() & 1); else tx_move_end(&d, rnd() & 1); }
        else if (op == 15) { if (rnd() & 1) tx_move_word_left(&d, rnd() & 1); else tx_move_word_right(&d, rnd() & 1); }
        else if (op == 16) { tx_click(&d, (int)(rnd() % (tx_rows(&d) + 2)) - 1, (int)(rnd() % 60), rnd() & 1); }
        else if (op == 17) { tx_select_word_at(&d, (int)(rnd() % (d.len + 1))); }
        else if (op == 18) { tx_select_line_at(&d, (int)(rnd() % (d.len + 1))); }
        else if (op == 19) { if (rnd() % 4 == 0) tx_select_all(&d); }
        else if (op == 20) { tx_set_cursor(&d, (int)(rnd() % (d.len + 1)), 0); }
        else { if (b > a) { m_replace(&m, a, b - a, "", 0); changed = 1; } tx_delete_sel(&d); }
        (void)cur_before;
        if (changed) { edits++; snap[nsnap] = malloc(m.n + 1); memcpy(snap[nsnap], m.t, m.n); snaplen[nsnap] = m.n; nsnap++; }
        if (d.len != m.n || memcmp(d.buf, m.t, m.n) || d.buf[d.len]) mismatch++;
        if (d.nlines != count_nl(&d)) nl_bad++;
        if (d.cursor < 0 || d.cursor > d.len || d.anchor < 0 || d.anchor > d.len) range_bad++;
        if (mismatch) break;
    }
    expect(edits > 2000, "thousands of random edits and moves were applied");
    expect(mismatch == 0, "the document text equals the reference model after EVERY operation");
    expect(nl_bad == 0, "the line counter always equals the real number of lines");
    expect(range_bad == 0, "cursor and anchor always stay inside the text");

    /* undo everything: the text must pass back through earlier states, in order, down to empty */
    char* final = malloc(d.len + 1); memcpy(final, d.buf, d.len + 1); int flen = d.len;
    int last = nsnap, steps = 0, order_bad = 0, unknown = 0;
    while (tx_can_undo(&d)) {
        tx_undo(&d); steps++;
        int found = -1; for (int i = last - 1; i >= 0; i--) if (snaplen[i] == d.len && !memcmp(snap[i], d.buf, d.len)) { found = i; break; }
        if (found < 0) { unknown++; break; }
        if (found >= last) order_bad++;
        last = found;
        if (d.nlines != count_nl(&d)) nl_bad++;
    }
    int histroy_kept_all = (steps > 0 && last >= 0);
    expect(unknown == 0 && order_bad == 0 && histroy_kept_all, "undo passes only through states the document really had, strictly backwards");
    expect(steps < edits && steps > edits / 12, "...in fewer steps than edits (runs merged) but not absurdly few");
    int rsteps = 0, rfound_last = -1, rbad = 0;
    while (tx_can_redo(&d)) {
        tx_redo(&d); rsteps++;
        int found = -1; for (int i = (rfound_last < 0 ? 0 : rfound_last); i < nsnap; i++) if (snaplen[i] == d.len && !memcmp(snap[i], d.buf, d.len)) { found = i; break; }
        if (found < 0 || found < rfound_last) { rbad++; break; }
        rfound_last = found;
    }
    expect(rbad == 0 && rsteps == steps, "redo passes forwards through the same states in the same number of steps");
    expect(d.len == flen && !memcmp(d.buf, final, flen), "after redo of everything the text is exactly the final text again");
    free(final);
    for (int i = 0; i < nsnap; i++) free(snap[i]);
    free(snap); free(snaplen); free(m.t); tx_release(&d);
    expect(txa_in_use == 0, "no memory left behind");
}

/* Undo/redo mixed freely with edits. The model: the document's text at history position k is always the
 * same text, so states[k] is remembered whenever position k is reached by an edit, and every undo or
 * redo must land exactly on states[new position]. A new edit must also end the redo tail. */
static void test_fuzz_undo(void) {
    printf("== undo and redo interleaved with edits (model: one remembered text per history position)\n");
    TxDoc d; tx_init(&d);
    char** st = calloc(9000, sizeof(char*)); int* stl = calloc(9000, sizeof(int));
    st[0] = malloc(1); stl[0] = 0;
    int bad_land = 0, bad_tail = 0, undos = 0, redos = 0, edits = 0, bad_mod = 0;
    for (int step = 0; step < 7000; step++) {
        tx_layout(&d, 40);
        int op = (int)(rnd() % 12);
        int before_idx = d.uidx;
        if (op < 5) {
            char c = (char)(rnd() % 8 == 0 ? '\n' : 'a' + rnd() % 26);
            if (rnd() % 5 == 0) tx_set_cursor(&d, (int)(rnd() % (d.len + 1)), 0);
            tx_type_char(&d, c); edits++;
        } else if (op == 5) { if (rnd() & 1) tx_backspace(&d); else tx_delete_fwd(&d); edits++; }
        else if (op == 6) { char t[20]; int k = (int)(rnd() % 15) + 1; for (int i = 0; i < k; i++) t[i] = (char)('A' + rnd() % 26); tx_insert(&d, t, k, TXK_OTHER); edits++; }
        else if (op == 7) { tx_move_left(&d, rnd() & 1); }
        else if (op == 8) { tx_set_cursor(&d, (int)(rnd() % (d.len + 1)), 0); }
        else if (op == 9 || op == 10) { if (tx_undo(&d)) undos++; }
        else { if (tx_redo(&d)) redos++; }
        int moved_history = (d.uidx != before_idx);
        if (op < 7) {                                               /* an edit (or a no-op): remember the text for this position */
            if (op < 7 && (d.un != d.uidx)) bad_tail++;             /* a new edit leaves no redo tail */
            for (int k = d.uidx + 1; k < 9000 && st[k]; k++) { free(st[k]); st[k] = 0; }
            free(st[d.uidx]); st[d.uidx] = malloc(d.len + 1); memcpy(st[d.uidx], d.buf, d.len); stl[d.uidx] = d.len;
        } else if (op == 9 || op == 10 || op == 11) {
            if (moved_history) {
                if (!st[d.uidx] || stl[d.uidx] != d.len || memcmp(st[d.uidx], d.buf, d.len)) bad_land++;
            }
        }
        if (d.uidx < 0 || d.uidx > d.un || d.nlines != count_nl(&d)) bad_land++;
        if (d.uidx == 0 && d.save_point == 0 && d.len != 0) bad_mod++;
        if (bad_land || bad_tail) break;
    }
    expect(edits > 2000 && undos > 300 && redos > 100, "thousands of edits mixed with hundreds of undos and redos");
    expect(bad_tail == 0, "every new edit ends the redo tail");
    expect(bad_land == 0, "every undo and redo lands exactly on the text that position had before");
    expect(bad_mod == 0, "the 'modified' flag agrees with the history position");
    for (int k = 0; k < 9000; k++) free(st[k]);
    free(st); free(stl); tx_release(&d);
    expect(txa_in_use == 0, "no memory left behind");

    /* the specific case: undo twice, type something new, then walk the whole history */
    tx_init(&d);
    type_str(&d, "one"); tx_break_undo(&d); type_str(&d, " two"); tx_break_undo(&d); type_str(&d, " three");
    expect(is(&d, "one two three") && d.un == 3, "three separate undo steps");
    tx_undo(&d); tx_undo(&d); expect(is(&d, "one"), "undo twice");
    tx_break_undo(&d); type_str(&d, "X");
    expect(is(&d, "oneX") && !tx_can_redo(&d), "a new edit after undo throws away the old redo steps");
    tx_undo(&d); expect(is(&d, "one"), "undo the new edit: back to \"one\" (not to something from the discarded history)");
    tx_undo(&d); expect(is(&d, ""), "undo again: empty");
    tx_redo(&d); tx_redo(&d); expect(is(&d, "oneX") && !tx_can_redo(&d), "redo twice reaches the new edit and no further");
    tx_release(&d);
}

int main(void) {
    test_basics(); test_big(); test_load_and_select(); test_movement(); test_layout(); test_fuzz(); test_fuzz_undo();
    printf("\n%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
