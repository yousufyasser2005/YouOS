/* textedit.h -- the text document engine behind Notepad (and any other text editing surface).
 *
 * Header-only, no drawing and no system calls, so it is compiled unchanged into the host test
 * (tests/host/textedit_test.c) where it is checked against a reference model. It provides:
 *   - the text buffer (up to TX_MAX_DOC bytes, memory from txarena.h)
 *   - edits with full undo/redo; runs of typing / backspace / delete become ONE undo step
 *   - a selection (anchor + cursor), word / line / all selection, selected-text extraction
 *   - word-wrapped layout for any column count, cursor <-> (row, column) mapping, and cursor
 *     movement over the wrapped rows (arrows, Home/End, words, pages, document ends)
 *   - line / column / character counters and the "modified" flag
 * Text is bytes, one column each, except Tab which advances to the next multiple of TX_TAB.
 *
 * Userspace has no libc, so nothing here may turn into a memcpy/memset call: GCC's loop-to-memcpy
 * conversion is switched off for this whole header and structures are only copied field by field. */
#ifndef TEXTEDIT_H
#define TEXTEDIT_H
#include "txarena.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC push_options
#pragma GCC optimize ("no-tree-loop-distribute-patterns")
#endif

#define TX_MAX_DOC    ((1 << 20) - 1)        /* 1 MiB less one: the buffer's size class is exactly 1 MiB with its NUL */
#define TX_TAB        4
#define TX_UNDO_TEXT  (512 * 1024)           /* bytes of text kept for undo/redo per document */
#define TX_UNDO_RECS  8192                   /* undo steps kept per document */
#define TX_RUN_MAX    256                    /* longest typing / deleting run merged into one undo step */

enum { TXK_OTHER = 0, TXK_TYPE = 1, TXK_BKSP = 2, TXK_DEL = 3 };
#define TXF_TRUNC 1                          /* an insertion was cut short at TX_MAX_DOC */
#define TXF_OOM   2                          /* an operation failed for lack of memory */

typedef struct { int pos, del_len, ins_len, del_off, ins_off, cur_before, anc_before, cur_after, kind; } TxUndo;

typedef struct {
    char*         buf;   int len;   unsigned long buf_bytes;         /* text, NUL terminated */
    int           cursor, anchor, want_col;                          /* selection = [min, max) of cursor/anchor */
    int           nlines;                                            /* number of '\n' + 1 */
    int*          rows;  int nrows; unsigned long rows_bytes;        /* start offset of every display row */
    int           layout_cols, layout_valid;
    TxUndo*       ur;    int un, uidx; unsigned long ur_bytes;       /* undo records; [uidx,un) is the redo tail */
    char*         ut;    int utlen;    unsigned long ut_bytes;       /* text of the undo records */
    int           save_point, merge_barrier, flags;
} TxDoc;

/* ---- plumbing ---- */
static void tx_copy(char* d, const char* s, int n) { for (int i = 0; i < n; i++) d[i] = s[i]; }
static void tx_mv(char* d, const char* s, int n) {
    if (d < s) { for (int i = 0; i < n; i++) d[i] = s[i]; }
    else if (d > s) { for (int i = n - 1; i >= 0; i--) d[i] = s[i]; }
}
static void tx_rec_copy(TxUndo* d, const TxUndo* s) {
    d->pos = s->pos; d->del_len = s->del_len; d->ins_len = s->ins_len; d->del_off = s->del_off; d->ins_off = s->ins_off;
    d->cur_before = s->cur_before; d->anc_before = s->anc_before; d->cur_after = s->cur_after; d->kind = s->kind;
}
static int tx_grow(void** p, unsigned long* bytes, unsigned long need, unsigned long keep) {
    if (*p && need <= *bytes) return 0;
    unsigned long nb = txa_round(need); if (!nb) return -1;
    char* np = (char*)txa_alloc(nb); if (!np) return -1;
    if (*p) { tx_copy(np, (const char*)*p, (int)keep); txa_free(*p, *bytes); }
    *p = np; *bytes = nb; return 0;
}
static int tx_reserve(TxDoc* d, int need_len) {
    return tx_grow((void**)&d->buf, &d->buf_bytes, (unsigned long)need_len + 1, (unsigned long)d->len + 1);
}

static int tx_init(TxDoc* d) {
    char* p = (char*)d; for (unsigned i = 0; i < sizeof *d; i++) p[i] = 0;
    d->nlines = 1; d->want_col = -1;
    if (tx_reserve(d, 0) != 0) return -1;
    d->buf[0] = 0;
    return 0;
}
static void tx_release(TxDoc* d) {
    if (d->buf) txa_free(d->buf, d->buf_bytes);
    if (d->rows) txa_free(d->rows, d->rows_bytes);
    if (d->ur) txa_free(d->ur, d->ur_bytes);
    if (d->ut) txa_free(d->ut, d->ut_bytes);
    char* p = (char*)d; for (unsigned i = 0; i < sizeof *d; i++) p[i] = 0;
}
static const char* tx_data(const TxDoc* d) { return d->buf ? d->buf : ""; }

/* the one place text changes: replace `del` bytes at `pos` by `n` bytes of `ins` (capacity already ensured) */
static void tx_raw_replace(TxDoc* d, int pos, int del, const char* ins, int n) {
    for (int i = 0; i < del; i++) if (d->buf[pos + i] == '\n') d->nlines--;
    for (int i = 0; i < n; i++)   if (ins[i] == '\n') d->nlines++;
    tx_mv(d->buf + pos + n, d->buf + pos + del, d->len - pos - del);
    tx_copy(d->buf + pos, ins, n);
    d->len += n - del; d->buf[d->len] = 0;
    d->layout_valid = 0;
}

/* ---- undo history ---- */
static void tx_undo_reset(TxDoc* d) { d->un = d->uidx = 0; d->utlen = 0; d->save_point = -2; }

static void tx_undo_drop_oldest(TxDoc* d, int k) {
    if (k <= 0 || d->un == 0) return;
    if (k > d->un) k = d->un;
    int base = (k < d->un) ? d->ur[k].del_off : d->utlen;
    if (k < d->un) tx_mv(d->ut, d->ut + base, d->utlen - base);
    d->utlen -= base;
    for (int i = k; i < d->un; i++) { tx_rec_copy(&d->ur[i - k], &d->ur[i]); d->ur[i - k].del_off -= base; d->ur[i - k].ins_off -= base; }
    d->un -= k; d->uidx -= k; if (d->uidx < 0) d->uidx = 0;
    if (d->save_point >= 0) { d->save_point -= k; if (d->save_point < 0) d->save_point = -2; }
}

/* Remember an edit that is ABOUT to be applied (the old text is still in the buffer). */
static void tx_undo_push(TxDoc* d, int pos, int del, const char* ins, int n, int kind) {
    if (d->uidx < d->un) {                         /* a new edit ends the redo tail */
        d->un = d->uidx;
        d->utlen = d->un ? d->ur[d->un - 1].ins_off + d->ur[d->un - 1].ins_len : 0;
        if (d->save_point > d->uidx) d->save_point = -2;
    }
    int barrier = d->merge_barrier; d->merge_barrier = 0;
    TxUndo* L = d->un ? &d->ur[d->un - 1] : 0;
    if (L && !barrier) {
        if (kind == TXK_TYPE && del == 0 && n == 1 && L->kind == TXK_TYPE && L->del_len == 0 && L->pos + L->ins_len == pos &&
            d->cursor == pos && L->ins_len < TX_RUN_MAX && d->ut[L->ins_off + L->ins_len - 1] != '\n' &&
            d->utlen + 1 <= TX_UNDO_TEXT && tx_grow((void**)&d->ut, &d->ut_bytes, (unsigned long)d->utlen + 1, (unsigned long)d->utlen) == 0) {
            d->ut[d->utlen++] = ins[0]; L->ins_len++; L->cur_after = pos + 1;
            if (d->save_point == d->uidx) d->save_point = -2;
            return;
        }
        if (kind == TXK_BKSP && del == 1 && n == 0 && L->kind == TXK_BKSP && L->ins_len == 0 && pos + 1 == L->pos &&
            d->cursor == pos + 1 && L->del_len < TX_RUN_MAX &&
            d->utlen + 1 <= TX_UNDO_TEXT && tx_grow((void**)&d->ut, &d->ut_bytes, (unsigned long)d->utlen + 1, (unsigned long)d->utlen) == 0) {
            tx_mv(d->ut + L->del_off + 1, d->ut + L->del_off, L->del_len);        /* the new character goes in FRONT of what was deleted */
            d->ut[L->del_off] = d->buf[pos]; d->utlen++; L->del_len++; L->ins_off = L->del_off + L->del_len; L->pos = pos; L->cur_after = pos;
            if (d->save_point == d->uidx) d->save_point = -2;
            return;
        }
        if (kind == TXK_DEL && del == 1 && n == 0 && L->kind == TXK_DEL && L->ins_len == 0 && pos == L->pos &&
            d->cursor == pos && L->del_len < TX_RUN_MAX &&
            d->utlen + 1 <= TX_UNDO_TEXT && tx_grow((void**)&d->ut, &d->ut_bytes, (unsigned long)d->utlen + 1, (unsigned long)d->utlen) == 0) {
            d->ut[d->utlen++] = d->buf[pos]; L->del_len++; L->ins_off = L->del_off + L->del_len; L->cur_after = pos;
            if (d->save_point == d->uidx) d->save_point = -2;
            return;
        }
    }
    int need = del + n;
    if (need > TX_UNDO_TEXT) { tx_undo_reset(d); return; }                        /* too big to keep: history starts over */
    while ((d->utlen + need > TX_UNDO_TEXT || d->un >= TX_UNDO_RECS) && d->un > 0) tx_undo_drop_oldest(d, 1 + d->un / 8);
    if (tx_grow((void**)&d->ur, &d->ur_bytes, (unsigned long)(d->un + 1) * sizeof(TxUndo), (unsigned long)d->un * sizeof(TxUndo)) != 0 ||
        tx_grow((void**)&d->ut, &d->ut_bytes, (unsigned long)(d->utlen + need > 0 ? d->utlen + need : 1), (unsigned long)d->utlen) != 0) {
        tx_undo_reset(d); d->flags |= TXF_OOM; return;
    }
    TxUndo* r = &d->ur[d->un];
    r->pos = pos; r->del_len = del; r->ins_len = n; r->kind = kind;
    r->del_off = d->utlen; tx_copy(d->ut + d->utlen, d->buf + pos, del); d->utlen += del;
    r->ins_off = d->utlen; tx_copy(d->ut + d->utlen, ins, n);            d->utlen += n;
    r->cur_before = d->cursor; r->anc_before = d->anchor; r->cur_after = pos + n;
    d->un++; d->uidx = d->un;
}

/* Replace `del` bytes at `pos` by `n` bytes of `ins`, with undo. Insertions that would pass
 * TX_MAX_DOC are cut short (TXF_TRUNC). Returns the number of bytes inserted. The cursor ends up
 * after the inserted text, with no selection. */
static int tx_edit(TxDoc* d, int pos, int del, const char* ins, int n, int kind) {
    if (pos < 0) pos = 0; if (pos > d->len) pos = d->len;
    if (del < 0) del = 0; if (del > d->len - pos) del = d->len - pos;
    if (n < 0) n = 0;
    if (d->len - del + n > TX_MAX_DOC) { n = TX_MAX_DOC - (d->len - del); if (n < 0) n = 0; d->flags |= TXF_TRUNC; }
    if (del == 0 && n == 0) return 0;
    if (tx_reserve(d, d->len - del + n) != 0) { d->flags |= TXF_OOM; return 0; }
    tx_undo_push(d, pos, del, ins, n, kind);
    tx_raw_replace(d, pos, del, ins, n);
    d->cursor = pos + n; d->anchor = d->cursor; d->want_col = -1;
    return n;
}

static int tx_clamp(const TxDoc* d, int off) { return off < 0 ? 0 : off > d->len ? d->len : off; }

static int tx_undo(TxDoc* d) {
    if (d->uidx <= 0) return 0;
    TxUndo* r = &d->ur[d->uidx - 1];
    if (tx_reserve(d, d->len - r->ins_len + r->del_len) != 0) { d->flags |= TXF_OOM; return 0; }
    tx_raw_replace(d, r->pos, r->ins_len, d->ut + r->del_off, r->del_len);
    d->uidx--; d->cursor = tx_clamp(d, r->cur_before); d->anchor = tx_clamp(d, r->anc_before);
    d->want_col = -1; d->merge_barrier = 1;
    return 1;
}
static int tx_redo(TxDoc* d) {
    if (d->uidx >= d->un) return 0;
    TxUndo* r = &d->ur[d->uidx];
    if (tx_reserve(d, d->len - r->del_len + r->ins_len) != 0) { d->flags |= TXF_OOM; return 0; }
    tx_raw_replace(d, r->pos, r->del_len, d->ut + r->ins_off, r->ins_len);
    d->uidx++; d->cursor = tx_clamp(d, r->cur_after); d->anchor = d->cursor;
    d->want_col = -1; d->merge_barrier = 1;
    return 1;
}
static int tx_can_undo(const TxDoc* d) { return d->uidx > 0; }
static int tx_can_redo(const TxDoc* d) { return d->uidx < d->un; }
static int tx_modified(const TxDoc* d) { return d->uidx != d->save_point; }
static void tx_mark_saved(TxDoc* d) { d->save_point = d->uidx; d->merge_barrier = 1; }
static void tx_break_undo(TxDoc* d) { d->merge_barrier = 1; }      /* the next edit starts a new undo step */

/* ---- selection ---- */
static void tx_sel(const TxDoc* d, int* a, int* b) {
    if (d->anchor < d->cursor) { *a = d->anchor; *b = d->cursor; } else { *a = d->cursor; *b = d->anchor; }
}
static int tx_has_sel(const TxDoc* d) { return d->anchor != d->cursor; }
static void tx_set_cursor(TxDoc* d, int off, int extend) {
    d->cursor = tx_clamp(d, off); if (!extend) d->anchor = d->cursor; d->merge_barrier = 1;
}
static void tx_select_all(TxDoc* d) { d->anchor = 0; d->cursor = d->len; d->merge_barrier = 1; d->want_col = -1; }
static int tx_get_sel(const TxDoc* d, char* out, int max) {      /* selected bytes copied to out (not NUL terminated); returns the full length */
    int a, b; tx_sel(d, &a, &b);
    int n = b - a;
    for (int i = 0; i < n && i < max; i++) out[i] = d->buf[a + i];
    return n;
}
static int tx_cls(char c) {
    if (c == ' ' || c == '\t' || c == '\n' || c == 0) return 0;
    if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || (unsigned char)c >= 128) return 1;
    return 2;
}
static void tx_select_word_at(TxDoc* d, int off) {
    off = tx_clamp(d, off);
    if (d->len == 0) { d->anchor = d->cursor = 0; return; }
    int i = off; if (i >= d->len) i = d->len - 1;
    if (d->buf[i] == '\n' && i > 0 && off > 0) i = off - 1;         /* clicked past the end of a line: the word before it */
    int c = tx_cls(d->buf[i]);
    int a = i, b = i + 1;
    while (a > 0 && d->buf[a - 1] != '\n' && tx_cls(d->buf[a - 1]) == c) a--;
    while (b < d->len && d->buf[b] != '\n' && tx_cls(d->buf[b]) == c) b++;
    d->anchor = a; d->cursor = b; d->merge_barrier = 1; d->want_col = -1;
}
static void tx_select_line_at(TxDoc* d, int off) {                /* the whole logical line, including its newline */
    off = tx_clamp(d, off);
    int a = off; while (a > 0 && d->buf[a - 1] != '\n') a--;
    int b = off; while (b < d->len && d->buf[b] != '\n') b++;
    if (b < d->len) b++;
    d->anchor = a; d->cursor = b; d->merge_barrier = 1; d->want_col = -1;
}

/* ---- editing commands ---- */
static int tx_insert(TxDoc* d, const char* s, int n, int kind) {      /* typed or pasted text; replaces the selection */
    int a = d->cursor, b = d->cursor; if (tx_has_sel(d)) tx_sel(d, &a, &b);
    return tx_edit(d, a, b - a, s, n, a == b ? kind : TXK_OTHER);
}
static int tx_type_char(TxDoc* d, char c) { return tx_insert(d, &c, 1, TXK_TYPE); }
static int tx_delete_sel(TxDoc* d) { int a, b; tx_sel(d, &a, &b); return b > a ? (tx_edit(d, a, b - a, 0, 0, TXK_OTHER), 1) : 0; }
static int tx_backspace(TxDoc* d) {
    if (tx_has_sel(d)) return tx_delete_sel(d);
    if (d->cursor <= 0) return 0;
    tx_edit(d, d->cursor - 1, 1, 0, 0, TXK_BKSP); return 1;
}
static int tx_delete_fwd(TxDoc* d) {
    if (tx_has_sel(d)) return tx_delete_sel(d);
    if (d->cursor >= d->len) return 0;
    tx_edit(d, d->cursor, 1, 0, 0, TXK_DEL); return 1;
}
/* Clean text from outside (a file, the clipboard) for the editor: CRLF and lone CR become LF, control
 * characters other than Tab and LF are dropped. Writes at most n bytes to dst (dst may equal src). */
static int tx_normalize(char* dst, const char* src, int n) {
    int o = 0;
    for (int i = 0; i < n; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == '\r') { if (i + 1 < n && src[i + 1] == '\n') continue; c = '\n'; }
        else if (c < 32 && c != '\n' && c != '\t') continue;
        dst[o++] = (char)c;
    }
    return o;
}
/* Replace the whole document with `data` (a file just read): the text is normalized, undo history
 * cleared, the document marked unmodified. -1 if it does not fit. */
static int tx_load(TxDoc* d, const char* data, int n) {
    if (n < 0 || n > TX_MAX_DOC) return -1;
    if (tx_reserve(d, n) != 0) return -1;
    int o = tx_normalize(d->buf, data, n);
    d->buf[o] = 0; d->len = o;
    d->nlines = 1; for (int i = 0; i < o; i++) if (d->buf[i] == '\n') d->nlines++;
    d->cursor = d->anchor = 0; d->want_col = -1; d->layout_valid = 0;
    d->un = d->uidx = 0; d->utlen = 0; d->save_point = 0; d->merge_barrier = 1; d->flags = 0;
    return 0;
}

/* ---- layout: word wrap ---- */
static int tx_row_push(TxDoc* d, int off) {
    if (tx_grow((void**)&d->rows, &d->rows_bytes, (unsigned long)(d->nrows + 1) * sizeof(int), (unsigned long)d->nrows * sizeof(int)) != 0) {
        d->flags |= TXF_OOM; return -1;
    }
    d->rows[d->nrows++] = off; return 0;
}
static int tx_tabw(int col) { return TX_TAB - (col % TX_TAB); }

/* Lay the text out for `cols` columns (recomputed only when the text or the width changed).
 * A row breaks after the last space or tab that fits; a word longer than a row is broken at the edge. */
static void tx_layout(TxDoc* d, int cols) {
    if (cols < 1) cols = 1;
    if (d->layout_valid && d->layout_cols == cols && d->nrows > 0) return;
    d->nrows = 0; d->layout_cols = cols; d->layout_valid = 1;
    if (tx_row_push(d, 0) != 0) { d->nrows = 0; d->layout_valid = 0; return; }
    int i = 0, col = 0, rowstart = 0, last_break = -1;
    while (i < d->len) {
        char c = d->buf[i];
        if (c == '\n') { i++; rowstart = i; col = 0; last_break = -1; if (tx_row_push(d, i) != 0) goto trunc; continue; }
        int w = (c == '\t') ? tx_tabw(col) : 1;
        if (col + w > cols && col > 0) {
            if (last_break > rowstart) {
                rowstart = last_break; if (tx_row_push(d, rowstart) != 0) goto trunc;
                col = 0; for (int k = rowstart; k < i; k++) col += (d->buf[k] == '\t') ? tx_tabw(col) : 1;
            } else { rowstart = i; col = 0; if (tx_row_push(d, i) != 0) goto trunc; }
            last_break = -1;
            continue;
        }
        col += w; i++;
        if (c == ' ' || c == '\t') last_break = i;
    }
    return;
trunc:
    d->layout_valid = 0;
}
static int tx_rows(const TxDoc* d) { return d->nrows; }
static int tx_row_start(const TxDoc* d, int r) { return d->rows[r]; }
static int tx_row_end(const TxDoc* d, int r) {                     /* end of the row's text, not counting the newline that ends its line */
    int e = (r + 1 < d->nrows) ? d->rows[r + 1] : d->len;
    if (e > d->rows[r] && d->buf[e - 1] == '\n') e--;
    return e;
}
static int tx_row_of(const TxDoc* d, int off) {                    /* the display row a text offset is shown on */
    int lo = 0, hi = d->nrows - 1;
    while (lo < hi) { int mid = (lo + hi + 1) / 2; if (d->rows[mid] <= off) lo = mid; else hi = mid - 1; }
    return lo;
}
static int tx_col_of(const TxDoc* d, int row, int off) {           /* display column of an offset within its row */
    int col = 0;
    for (int k = d->rows[row]; k < off && k < d->len; k++) col += (d->buf[k] == '\t') ? tx_tabw(col) : 1;
    return col;
}
/* Offset for a display column on a row. round = 1: nearest character boundary (mouse clicks);
 * 0: the character that covers the column (keeping a column while moving up and down). */
static int tx_off_at(const TxDoc* d, int row, int col, int round) {
    int e = tx_row_end(d, row), c = 0, k = d->rows[row];
    for (; k < e; k++) {
        int w = (d->buf[k] == '\t') ? tx_tabw(c) : 1;
        if (round ? (col < c + (w + 1) / 2) : (col < c + w)) return k;
        c += w;
    }
    return e;
}
/* 1-based line and column of an offset, counted in characters over the real lines (not wrapped rows) */
static void tx_logical_pos(const TxDoc* d, int off, int* line, int* col) {
    int ln = 0, ls = 0;
    for (int k = 0; k < off && k < d->len; k++) if (d->buf[k] == '\n') { ln++; ls = k + 1; }
    *line = ln + 1; *col = off - ls + 1;
}

/* ---- movement (call tx_layout first) ---- */
static void tx_move_left(TxDoc* d, int extend) {
    if (!extend && tx_has_sel(d)) { int a, b; tx_sel(d, &a, &b); tx_set_cursor(d, a, 0); }
    else if (d->cursor > 0) tx_set_cursor(d, d->cursor - 1, extend);
    else tx_set_cursor(d, 0, extend);
    d->want_col = -1;
}
static void tx_move_right(TxDoc* d, int extend) {
    if (!extend && tx_has_sel(d)) { int a, b; tx_sel(d, &a, &b); tx_set_cursor(d, b, 0); }
    else tx_set_cursor(d, d->cursor + 1, extend);
    d->want_col = -1;
}
static void tx_move_rows(TxDoc* d, int delta, int extend) {         /* up (<0) or down (>0) by display rows, keeping the column */
    int r = tx_row_of(d, d->cursor);
    if (d->want_col < 0) d->want_col = tx_col_of(d, r, d->cursor);
    int nr = r + delta;
    int keep = d->want_col;
    if (nr < 0) { tx_set_cursor(d, 0, extend); d->want_col = keep; return; }
    if (nr >= d->nrows) { tx_set_cursor(d, d->len, extend); d->want_col = keep; return; }
    tx_set_cursor(d, tx_off_at(d, nr, keep, 0), extend);
    d->want_col = keep;
}
static void tx_move_home(TxDoc* d, int extend) { tx_set_cursor(d, d->rows[tx_row_of(d, d->cursor)], extend); d->want_col = -1; }
static void tx_move_end(TxDoc* d, int extend) {
    int r = tx_row_of(d, d->cursor), e = tx_row_end(d, r);
    if (r + 1 < d->nrows && d->rows[r + 1] == e && e > d->rows[r]) e--;     /* a wrapped row: stay on it, before its last character */
    tx_set_cursor(d, e, extend); d->want_col = -1;
}
static void tx_move_doc_home(TxDoc* d, int extend) { tx_set_cursor(d, 0, extend); d->want_col = -1; }
static void tx_move_doc_end(TxDoc* d, int extend)  { tx_set_cursor(d, d->len, extend); d->want_col = -1; }
static void tx_move_word_left(TxDoc* d, int extend) {
    int i = d->cursor;
    while (i > 0 && tx_cls(d->buf[i - 1]) == 0) i--;
    if (i > 0) { int c = tx_cls(d->buf[i - 1]); while (i > 0 && tx_cls(d->buf[i - 1]) == c) i--; }
    tx_set_cursor(d, i, extend); d->want_col = -1;
}
static void tx_move_word_right(TxDoc* d, int extend) {
    int i = d->cursor;
    if (i < d->len && tx_cls(d->buf[i]) != 0) { int c = tx_cls(d->buf[i]); while (i < d->len && tx_cls(d->buf[i]) == c) i++; }
    while (i < d->len && tx_cls(d->buf[i]) == 0) i++;
    tx_set_cursor(d, i, extend); d->want_col = -1;
}
static void tx_click(TxDoc* d, int row, int col, int extend) {      /* a mouse press or drag at a display row and column */
    if (row < 0) row = 0; if (row >= d->nrows) row = d->nrows - 1;
    tx_set_cursor(d, tx_off_at(d, row, col, 1), extend); d->want_col = -1;
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC pop_options
#endif
#endif /* TEXTEDIT_H */
