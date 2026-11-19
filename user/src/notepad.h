/* notepad.h -- the Notepad window. Included by desktop.c in place of the old inline Notepad.
 *
 * Layers (each tested on the host where it can be):
 *   textedit.h   the text engine: buffer, undo/redo, selection, word wrap, movement
 *   npcore.h     paths, file-name rules, sorting, tab labels, scrollbar arithmetic
 *   lineedit.h   the file name box (and, elsewhere, password boxes)
 *   this file    window state, drawing, mouse and keyboard handling, the Open / Save As dialog
 *
 * One Notepad window holds up to NPX_MAX_TABS documents (tabs). Everything below is addressed by the
 * window slot index `wi` (the same index as wins[]). It relies on the desktop's drawing primitives
 * (rect, outline, hline, vline, text, glyph, in_box), the colours, wins[], g_now_ticks and
 * cursor_blink, so it must be included after those. The includer defines LE_CLIP_SET / LE_CLIP_GET
 * (for lineedit.h) before including this file. */
#ifndef NOTEPAD_H
#define NOTEPAD_H

#include "textedit.h"
#include "npcore.h"
#include "lineedit.h"
#include "ctxmenu.h"

#define NPX_MAX_TABS 10
#define NPX_ENTS     256
#define NPX_TB_H     30
#define NPX_TAB_H    24
#define NPX_SB_H     20
#define NPX_SCRW     12
#define NPX_PAD      8
#define NPX_CW       8
#define NPX_CH       16
#define NPX_STATUS_TICKS 250

enum { NPM_EDIT = 0, NPM_FILE = 1, NPM_CONFIRM = 2, NPM_MSG = 3 };
enum { NPC_CLOSE_TAB = 1, NPC_CLOSE_WIN = 2, NPC_REPLACE = 3 };
enum { NB_NEW = 1, NB_OPEN, NB_SAVE, NB_SAVEAS, NB_UNDO, NB_REDO };

typedef struct {
    TxDoc doc;
    char  path[NP_PATH_MAX];           /* full path, "" while untitled */
    char  name[40];
    int   scroll;                      /* first visible display row */
} NpTab;

typedef struct {
    int   used, ntabs, cur, mode, untitled_seq;
    NpTab tabs[NPX_MAX_TABS];
    int   drag, sb_grab;               /* mouse drag in progress: 1 = text selection, 2 = scrollbar thumb */
    int   click_n, click_x, click_y; long long click_tick;
    char  status[64]; long long status_until;
    /* Open / Save As dialog */
    int   dk;                          /* 0 = open, 1 = save as */
    char  dir[NP_PATH_MAX];
    Dirent ents[NPX_ENTS]; int nents, dsel, dscroll, dclick_n; long long dclick_tick; int dclick_idx;
    char  fnbuf[40]; LineEdit fn;
    char  derr[96];
    /* confirm / message dialogs */
    int   confirm_kind, pending_tab, close_after_save;
    char  msg[112];
    char  replace_path[NP_PATH_MAX];
} NpWin;
static NpWin npx[MAX_WINDOWS];

typedef struct { int x, y, w, h; int tb_y, tab_y, ta_x, ta_y, ta_w, ta_h, sb_y, sbx, cols, rows; } NpGeom;

/* ------------------------------------------------------------------ small helpers */
#define NPX_DBLCLICK_TICKS 100      /* max gap (100 Hz ticks = 600 ms) between presses of a double/triple click; 40 was too tight: a real 41-tick double-click was missed */
static int npx_num(char* d, long long v) {                      /* decimal; returns the length */
    char t[24]; int n = 0; if (v < 0) v = 0; if (!v) t[n++] = '0'; while (v) { t[n++] = (char)('0' + v % 10); v /= 10; }
    int k = 0; while (n) d[k++] = t[--n]; d[k] = 0; return k;
}
static int npx_cat(char* d, int k, const char* s, int cap) { while (*s && k < cap - 1) d[k++] = *s++; d[k] = 0; return k; }
#ifdef NPX_TRACE        /* serial-only diagnostics for the Open/Save dialog (SYS_DBGLOG; never drawn) */
static void npx_tr(const char* tag, const char* a, long long x1, long long x2, long long x3, long long x4, long long x5) {
    char b[200]; int k = 0; k = npx_cat(b, k, "[npx] ", sizeof b); k = npx_cat(b, k, tag, sizeof b); k = npx_cat(b, k, " ", sizeof b);
    k = npx_cat(b, k, a, sizeof b); long long v[5] = { x1, x2, x3, x4, x5 };
    for (int i = 0; i < 5; i++) { k = npx_cat(b, k, " ", sizeof b); k += npx_num(b + k, v[i]); b[k] = 0; }
    b[k++] = '\n'; sys_dbglog(b, (uint64_t)k);
}
#define NPX_TR(...) npx_tr(__VA_ARGS__)
#else
#define NPX_TR(...) ((void)0)
#endif
static void npx_status(int wi, const char* s) { np_cpy(npx[wi].status, s, sizeof npx[wi].status); npx[wi].status_until = (long long)g_now_ticks + NPX_STATUS_TICKS; }
static void npx_geom(const Win* w, NpGeom* g) {
    g->x = w->x; g->y = w->y + TITLEBAR_H; g->w = w->w; g->h = w->h - TITLEBAR_H;
    g->tb_y = g->y; g->tab_y = g->y + NPX_TB_H; g->ta_y = g->tab_y + NPX_TAB_H + 2;
    g->sb_y = g->y + g->h - NPX_SB_H;
    g->ta_x = g->x + NPX_PAD;
    g->sbx  = g->x + g->w - NPX_SCRW - 2;
    g->ta_w = g->sbx - g->ta_x - 4;
    g->ta_h = g->sb_y - g->ta_y - 2;
    g->cols = g->ta_w / NPX_CW; if (g->cols < 1) g->cols = 1;
    g->rows = g->ta_h / NPX_CH; if (g->rows < 1) g->rows = 1;
}
static NpTab* npx_tab(int wi) { return &npx[wi].tabs[npx[wi].cur]; }

/* make the cursor row visible */
static void npx_reveal(int wi) {
    NpGeom g; npx_geom(&wins[wi], &g);
    NpTab* t = npx_tab(wi); TxDoc* d = &t->doc;
    tx_layout(d, g.cols);
    if (d->nrows < 1) return;
    t->scroll = np_scroll_to_show(t->scroll, g.rows, tx_row_of(d, d->cursor), tx_rows(d));
}
static void npx_title(int wi) {
    NpTab* t = npx_tab(wi); int k = 0;
    while (t->name[k] && k < 38) { wins[wi].title[k] = t->name[k]; k++; }
    if (tx_modified(&t->doc) && k < 38) wins[wi].title[k++] = '*';
    wins[wi].title[k] = 0;
}

/* ------------------------------------------------------------------ tabs */
static int npx_tab_new(int wi, const char* path, const char* name) {
    NpWin* n = &npx[wi];
    if (n->ntabs >= NPX_MAX_TABS) return -1;
    NpTab* t = &n->tabs[n->ntabs];
    zero_bytes(t, sizeof *t);
    if (tx_init(&t->doc) != 0) return -1;
    if (path && path[0]) { np_cpy(t->path, path, sizeof t->path); np_cpy(t->name, name && name[0] ? name : np_basename(path), sizeof t->name); }
    else { n->untitled_seq++; np_untitled(t->name, sizeof t->name, n->untitled_seq); }
    n->cur = n->ntabs; n->ntabs++;
    return n->cur;
}
static void npx_tab_remove(int wi, int idx) {
    NpWin* n = &npx[wi];
    if (idx < 0 || idx >= n->ntabs) return;
    tx_release(&n->tabs[idx].doc);
    for (int i = idx; i < n->ntabs - 1; i++) {
        /* NpTab holds only plain data and pointers into the arena: a field-wise copy is a safe move */
        char* a = (char*)&n->tabs[i]; const char* b = (const char*)&n->tabs[i + 1];
        for (unsigned k = 0; k < sizeof(NpTab); k++) a[k] = b[k];
    }
    n->ntabs--;
    zero_bytes(&n->tabs[n->ntabs], sizeof(NpTab));
    if (n->ntabs == 0) npx_tab_new(wi, 0, 0);                      /* the window always keeps one document */
    if (n->cur >= n->ntabs) n->cur = n->ntabs - 1;
    if (n->cur > idx) n->cur--;
    if (n->cur < 0) n->cur = 0;
    npx_title(wi);
}
static void npx_switch(int wi, int idx) {
    NpWin* n = &npx[wi]; if (idx < 0 || idx >= n->ntabs) return;
    n->cur = idx; n->drag = 0; npx_title(wi); npx_reveal(wi);
}
static int npx_any_modified(int wi) { for (int i = 0; i < npx[wi].ntabs; i++) if (tx_modified(&npx[wi].tabs[i].doc)) return 1; return 0; }
static int npx_tab_pristine(const NpTab* t) { return t->path[0] == 0 && t->doc.len == 0 && !tx_modified(&t->doc) && !tx_can_undo(&t->doc); }

/* ------------------------------------------------------------------ files */
static const char* npx_read_file(TxDoc* d, const char* path) {
    unsigned int sz = 0; unsigned char isd = 0;
    if (sys_stat(path, &sz, &isd) != 0) return "File not found.";
    if (isd) return "That is a folder, not a file.";
    if (sz > (unsigned)TX_MAX_DOC) return "File too large (1 MB at most).";
    unsigned long cap = sz ? sz : 1;
    char* tmp = (char*)txa_alloc(cap);
    if (!tmp) return "Not enough memory to open this file.";
    int fd = sys_open(path, 0);
    if (fd < 0) { txa_free(tmp, cap); return "Cannot open the file."; }
    unsigned int got = 0;
    while (got < sz) { long long r = sys_fread(fd, tmp + got, sz - got); if (r <= 0) break; got += (unsigned)r; }
    sys_close(fd);
    for (unsigned i = 0; i < got && i < 4096; i++) if (tmp[i] == 0) { txa_free(tmp, cap); return "This looks like a binary file. Notepad opens text files."; }
    int rc = tx_load(d, tmp, (int)got);
    txa_free(tmp, cap);
    return rc == 0 ? 0 : "Not enough memory to open this file.";
}
static const char* npx_write_file(TxDoc* d, const char* path) {
    if (!(path[0] == '/' && path[1] == 'y' && path[2] == 'c' && path[3] == 'f' && path[4] == 's' && path[5] == '/')) return "Files can only be saved inside /ycfs.";
    long long r = sys_save_chunk(path, tx_data(d), (unsigned long)d->len, 0);
    if (r != (long long)d->len) return "Could not save the file (disk full or no access?).";
    return 0;
}
static void npx_message(int wi, const char* m) { npx[wi].mode = NPM_MSG; np_cpy(npx[wi].msg, m, sizeof npx[wi].msg); }

/* Save the tab to its own path. Returns 1 on success; on failure shows why. */
static int npx_save_tab_to(int wi, int idx, const char* path) {
    NpTab* t = &npx[wi].tabs[idx];
    const char* err = npx_write_file(&t->doc, path);
    if (err) { npx_message(wi, err); return 0; }
    np_cpy(t->path, path, sizeof t->path); np_cpy(t->name, np_basename(path), sizeof t->name);
    tx_mark_saved(&t->doc);
    if (idx == npx[wi].cur) { npx_title(wi); npx_status(wi, "Saved"); }
    return 1;
}

/* ------------------------------------------------------------------ the Open / Save As dialog */
static void npx_dlg_reload(int wi) {
    NpWin* n = &npx[wi];
    long c = sys_readdir2(n->dir, n->ents, NPX_ENTS);
    NPX_TR("reload", n->dir, c, 0, 0, 0, 0);
    if (c < 0) { c = 0; np_cpy(n->derr, "Cannot read this folder.", sizeof n->derr); } else n->derr[0] = 0;
    if (c > NPX_ENTS) c = NPX_ENTS;
    n->nents = (int)c;
    np_sort_entries(n->ents, n->nents);
    n->dsel = -1; n->dscroll = 0; n->dclick_n = 0; n->dclick_idx = -1;
}
static void npx_dlg_open(int wi, int kind) {
    NpWin* n = &npx[wi]; NpTab* t = npx_tab(wi);
    n->mode = NPM_FILE; n->dk = kind;
    if (t->path[0]) np_parent(n->dir, sizeof n->dir, t->path);
    else if (!n->dir[0]) np_cpy(n->dir, NP_ROOT, sizeof n->dir);
    le_init(&n->fn, n->fnbuf, sizeof n->fnbuf, 0);
    if (kind == 1) le_set(&n->fn, t->path[0] ? t->name : "");
    n->derr[0] = 0;
    npx_dlg_reload(wi);
    if (kind == 1 && t->path[0]) n->dsel = np_find_entry(n->ents, n->nents, t->name);
}
static void npx_dlg_enter(int wi, const char* name) {          /* go into a sub folder */
    NpWin* n = &npx[wi]; char p[NP_PATH_MAX];
    if (np_join(p, sizeof p, n->dir, name) != 0) { np_cpy(n->derr, "That path is too long.", sizeof n->derr); return; }
    np_cpy(n->dir, p, sizeof n->dir);
    npx_dlg_reload(wi);
}
static void npx_do_close_tab(int wi, int idx) { npx_tab_remove(wi, idx); npx[wi].close_after_save = 0; }

static void npx_open_in_tab(int wi, const char* path) {          /* open a file in a (new or reused) tab */
    NpWin* n = &npx[wi];
    for (int i = 0; i < n->ntabs; i++) if (np_eq(n->tabs[i].path, path)) { npx_switch(wi, i); return; }
    int reuse = (n->ntabs == 1 && npx_tab_pristine(&n->tabs[0]));
    int idx = reuse ? 0 : npx_tab_new(wi, path, np_basename(path));
    if (idx < 0) { npx_message(wi, "Too many tabs are open. Close one first."); return; }
    NpTab* t = &n->tabs[idx];
    const char* err = npx_read_file(&t->doc, path);
    if (err) {
        if (!reuse) npx_tab_remove(wi, idx);
        npx_message(wi, err); return;
    }
    np_cpy(t->path, path, sizeof t->path); np_cpy(t->name, np_basename(path), sizeof t->name);
    t->scroll = 0; n->cur = idx; npx_title(wi);
}
static void npx_dlg_accept(int wi) {
    NpWin* n = &npx[wi]; char name[40], path[NP_PATH_MAX];
    np_cpy(name, n->fn.buf, sizeof name);
    int e = np_len(name);
    while (e > 0 && name[e - 1] == ' ') name[--e] = 0;
    if (!name[0] && n->dsel >= 0 && n->dsel < n->nents) np_cpy(name, n->ents[n->dsel].name, sizeof name);
    int ci = np_find_entry(n->ents, n->nents, name);
    if (ci >= 0 && n->ents[ci].is_dir) { npx_dlg_enter(wi, name); le_set(&n->fn, ""); return; }
    int bad = np_check_name(name);
    if (bad) { np_cpy(n->derr, np_name_error(bad), sizeof n->derr); return; }
    if (n->dk == 0) {                                          /* open */
        if (ci < 0) { np_cpy(n->derr, "File not found.", sizeof n->derr); return; }
        if (np_join(path, sizeof path, n->dir, name) != 0) { np_cpy(n->derr, "That path is too long.", sizeof n->derr); return; }
        n->mode = NPM_EDIT; npx_open_in_tab(wi, path); return;
    }
    np_default_ext(name, sizeof name);                         /* save */
    ci = np_find_entry(n->ents, n->nents, name);
    if (ci >= 0 && n->ents[ci].is_dir) { npx_dlg_enter(wi, name); le_set(&n->fn, ""); return; }
    if (np_join(path, sizeof path, n->dir, name) != 0) { np_cpy(n->derr, "That path is too long.", sizeof n->derr); return; }
    NpTab* t = npx_tab(wi);
    if (ci >= 0 && !np_eq(t->path, path)) {                    /* exists and it is not this document's own file: ask */
        n->mode = NPM_CONFIRM; n->confirm_kind = NPC_REPLACE; n->pending_tab = n->cur;
        np_cpy(n->replace_path, path, sizeof n->replace_path);
        int k = npx_cat(n->msg, 0, name, sizeof n->msg); npx_cat(n->msg, k, " already exists. Replace it?", sizeof n->msg);
        return;
    }
    n->mode = NPM_EDIT;
    if (npx_save_tab_to(wi, n->cur, path) && n->close_after_save) { int idx = n->cur; npx_do_close_tab(wi, idx); }
    n->close_after_save = 0;
}

/* ------------------------------------------------------------------ commands */
static void npx_cmd_save(int wi, int as) {
    NpWin* n = &npx[wi]; NpTab* t = npx_tab(wi);
    if (as || !t->path[0]) { npx_dlg_open(wi, 1); return; }
    npx_save_tab_to(wi, n->cur, t->path);
}
static void npx_cmd_new_tab(int wi) {
    if (npx_tab_new(wi, 0, 0) < 0) { npx_message(wi, "Too many tabs are open. Close one first."); return; }
    npx_title(wi);
}
static void npx_cmd_close_tab(int wi, int idx) {
    NpWin* n = &npx[wi]; if (idx < 0 || idx >= n->ntabs) return;
    if (tx_modified(&n->tabs[idx].doc)) {
        n->mode = NPM_CONFIRM; n->confirm_kind = NPC_CLOSE_TAB; n->pending_tab = idx;
        int k = npx_cat(n->msg, 0, "Save changes to ", sizeof n->msg);
        k = npx_cat(n->msg, k, n->tabs[idx].name, sizeof n->msg); npx_cat(n->msg, k, "?", sizeof n->msg);
        return;
    }
    npx_do_close_tab(wi, idx);
}
static void npx_after_edit(int wi) { npx_reveal(wi); npx_title(wi); }

static void npx_cmd_copy(int wi, int cut) {
    NpTab* t = npx_tab(wi); TxDoc* d = &t->doc;
    if (!tx_has_sel(d)) return;
    int a, b; tx_sel(d, &a, &b); int n = b - a;
    char* tmp = (char*)txa_alloc((unsigned long)n);
    if (!tmp) { npx_status(wi, "Not enough memory to copy."); return; }
    tx_get_sel(d, tmp, n);
    long long r = sys_clip_set(tmp, (unsigned long)n, 1);
    txa_free(tmp, (unsigned long)n);
    if (r < 0) { npx_status(wi, "Could not copy that much text."); return; }
    if (cut) { tx_delete_sel(d); npx_after_edit(wi); }
}
static void npx_cmd_paste(int wi) {
    TxDoc* d = &npx_tab(wi)->doc;
    unsigned len = 0, type = 0, seq = 0;
    sys_clip_info(&len, &type, &seq);
    if (len == 0 || type != 1u) return;
    unsigned want = len;
    if (want > (unsigned)(2 * TX_MAX_DOC)) want = (unsigned)(2 * TX_MAX_DOC);
    char* tmp = (char*)txa_alloc(want);
    if (!tmp) { npx_status(wi, "Not enough memory to paste."); return; }
    long long got = np_clip_text(tmp, (int)want);
    int n = tx_normalize(tmp, tmp, (int)got);
    d->flags = 0;
    tx_insert(d, tmp, n, TXK_OTHER);
    txa_free(tmp, want);
    if (d->flags & TXF_TRUNC) npx_status(wi, "Pasted text was cut: the document limit (1 MB) was reached.");
    if (d->flags & TXF_OOM) npx_status(wi, "Not enough memory.");
    npx_after_edit(wi);
}
static void npx_cmd_undo(int wi) { if (tx_undo(&npx_tab(wi)->doc)) npx_after_edit(wi); }
static void npx_cmd_redo(int wi) { if (tx_redo(&npx_tab(wi)->doc)) npx_after_edit(wi); }

/* ------------------------------------------------------------------ window lifecycle */
static int npx_new_window(void) {
    int cnt = 0; for (int k = 0; k < win_count; k++) if (wins[k].id == WIN_NOTEPAD) cnt++;
    int i = wm_new(WIN_NOTEPAD, 150 + (cnt % 6) * 24, 70 + (cnt % 6) * 24, 660, 480, "Notepad", YELLOW);
    if (i < 0) return -1;
    wins[i].min_w = 400; wins[i].min_h = 260;
    zero_bytes(&npx[i], sizeof npx[i]);
    npx[i].used = 1;
    if (npx_tab_new(i, 0, 0) < 0) { npx[i].used = 0; return -1; }
    npx[i].dir[0] = 0;
    npx_title(i);
    return i;
}
static void npx_destroy(int wi) {
    NpWin* n = &npx[wi];
    for (int i = 0; i < n->ntabs; i++) tx_release(&n->tabs[i].doc);
    zero_bytes(n, sizeof *n);
}
/* The window's close button: if anything is unsaved, ask first. 1 = go ahead and close. */
static int npx_can_close(int wi) {
    NpWin* n = &npx[wi];
    if (!n->used || !npx_any_modified(wi)) return 1;
    int cnt = 0; for (int i = 0; i < n->ntabs; i++) if (tx_modified(&n->tabs[i].doc)) cnt++;
    n->mode = NPM_CONFIRM; n->confirm_kind = NPC_CLOSE_WIN;
    int k = npx_num(n->msg, cnt); k = npx_cat(n->msg, k, cnt == 1 ? " document has unsaved changes." : " documents have unsaved changes.", sizeof n->msg);
    return 0;
}
static void npx_init_all(void) { for (int k = 0; k < MAX_WINDOWS; k++) zero_bytes(&npx[k], sizeof npx[k]); }

/* Open a file from elsewhere (the file manager): in the focused Notepad window if there is one, else the
 * most recent one, else a new window. */
static void npx_open_file(const char* full) {
    int wi = -1;
    if (focused >= 0 && wins[focused].id == WIN_NOTEPAD && wins[focused].visible) wi = focused;
    else { int bz = -1; for (int k = 0; k < win_count; k++) if (wins[k].id == WIN_NOTEPAD && wins[k].visible && wins[k].z > bz) { bz = wins[k].z; wi = k; } }
    int fresh = 0;
    if (wi < 0) { wi = npx_new_window(); fresh = 1; }
    if (wi < 0) return;
    if (!fresh) { wins[wi].minimized = 0; wins[wi].visible = 1; wm_focus(wi); }
    npx[wi].mode = NPM_EDIT;
    npx_open_in_tab(wi, full);
}
static void open_notepad_p(const char* fn, const char* full) {
    char path[NP_PATH_MAX];
    if (full && full[0]) np_cpy(path, full, sizeof path);
    else if (fn && fn[0]) np_join(path, sizeof path, NP_ROOT, fn);
    else { npx_new_window(); return; }
    npx_open_file(path);
}
static void open_notepad(const char* fn) { open_notepad_p(fn, 0); }

/* ------------------------------------------------------------------ geometry of the toolbar, tabs and dialogs */
typedef struct { int id, x, w; const char* label; int enabled; } NpBtn;
static int npx_buttons(int wi, const NpGeom* g, NpBtn* b) {
    TxDoc* d = &npx_tab(wi)->doc;
    static const struct { int id; const char* l; int gap; } def[6] = { {NB_NEW,"New",0}, {NB_OPEN,"Open",0}, {NB_SAVE,"Save",0}, {NB_SAVEAS,"Save As",0}, {NB_UNDO,"Undo",14}, {NB_REDO,"Redo",0} };
    int x = g->x + 6;
    for (int i = 0; i < 6; i++) {
        x += def[i].gap;
        int w = np_len(def[i].l) * NPX_CW + 16;
        b[i].id = def[i].id; b[i].x = x; b[i].w = w; b[i].label = def[i].l;
        b[i].enabled = def[i].id == NB_UNDO ? tx_can_undo(d) : def[i].id == NB_REDO ? tx_can_redo(d) : 1;
        x += w + 4;
    }
    return 6;
}
static void npx_tab_rect(const NpWin* n, const NpGeom* g, int i, int* x, int* w) {
    int avail = g->w - 8 - 26; int tw = n->ntabs ? avail / n->ntabs : avail;
    if (tw > 170) tw = 170; if (tw < 64) tw = 64;
    *x = g->x + 4 + i * tw; *w = tw - 2;
}
typedef struct { int x, y, w, h, list_x, list_y, list_w, list_rows, name_y, btn_y, ok_x, cancel_x, up_x, up_y; } NpDlg;
static void npx_dlg_geom(const NpGeom* g, NpDlg* d) {
    d->w = g->w - 24; if (d->w > 500) d->w = 500; if (d->w < 300) d->w = 300;
    d->h = g->h - 24; if (d->h > 400) d->h = 400; if (d->h < 250) d->h = 250;
    d->x = g->x + (g->w - d->w) / 2; d->y = g->y + (g->h - d->h) / 2;
    d->list_x = d->x + 10; d->list_y = d->y + 62; d->list_w = d->w - 20 - 10;
    d->name_y = d->y + d->h - 78; d->btn_y = d->y + d->h - 34;
    d->list_rows = (d->name_y - 8 - d->list_y) / 18; if (d->list_rows < 1) d->list_rows = 1;
    d->cancel_x = d->x + d->w - 10 - 84; d->ok_x = d->cancel_x - 8 - 84;
    d->up_x = d->x + d->w - 10 - 44; d->up_y = d->y + 32;
}
typedef struct { int x, y, w, h; int nb, bx[3], bw[3]; const char* bl[3]; int by; } NpBox;
static void npx_box_geom(const NpGeom* g, const NpWin* n, NpBox* b) {
    b->w = 400; if (b->w > g->w - 20) b->w = g->w - 20; b->h = 120;
    b->x = g->x + (g->w - b->w) / 2; b->y = g->y + (g->h - b->h) / 2; b->by = b->y + b->h - 34;
    if (n->mode == NPM_MSG) { b->nb = 1; b->bl[0] = "OK"; }
    else if (n->confirm_kind == NPC_CLOSE_TAB) { b->nb = 3; b->bl[0] = "Save"; b->bl[1] = "Don't save"; b->bl[2] = "Cancel"; }
    else if (n->confirm_kind == NPC_CLOSE_WIN) { b->nb = 2; b->bl[0] = "Close anyway"; b->bl[1] = "Cancel"; }
    else { b->nb = 2; b->bl[0] = "Replace"; b->bl[1] = "Cancel"; }
    int x = b->x + b->w - 12;
    for (int i = b->nb - 1; i >= 0; i--) { int w = np_len(b->bl[i]) * NPX_CW + 20; x -= w; b->bx[i] = x; b->bw[i] = w; x -= 8; }
}
static void npx_box_choose(int wi, int which) {                  /* a button of the confirm / message box was chosen */
    NpWin* n = &npx[wi];
    if (n->mode == NPM_MSG) { n->mode = NPM_EDIT; return; }
    int kind = n->confirm_kind;
    n->mode = NPM_EDIT;
    if (kind == NPC_CLOSE_TAB) {
        int idx = n->pending_tab;
        if (which == 0) {                                          /* Save */
            if (idx != n->cur) npx_switch(wi, idx);
            if (n->tabs[idx].path[0]) { if (npx_save_tab_to(wi, idx, n->tabs[idx].path)) npx_do_close_tab(wi, idx); }
            else { n->close_after_save = 1; npx_dlg_open(wi, 1); }
        } else if (which == 1) npx_do_close_tab(wi, idx);          /* Don't save */
    } else if (kind == NPC_CLOSE_WIN) {
        if (which == 0) { for (int i = 0; i < n->ntabs; i++) n->tabs[i].doc.save_point = n->tabs[i].doc.uidx; wm_close(wi); }
    } else if (kind == NPC_REPLACE) {
        if (which == 0) { int idx = n->pending_tab; char p[NP_PATH_MAX]; np_cpy(p, n->replace_path, sizeof p);
            if (npx_save_tab_to(wi, idx, p) && n->close_after_save) npx_do_close_tab(wi, idx); n->close_after_save = 0; }
        else { n->mode = NPM_FILE; }
    }
}

/* ------------------------------------------------------------------ drawing */
#define NPX_BG     0x0D1117
#define NPX_BAR    0x161B22
#define NPX_SEL    0x264F78
static int npx_btn_hover(int x, int y, int w, int h) { return in_box(mouse_x, mouse_y, x, y, w, h); }
static void npx_draw_button(int x, int y, int w, int h, const char* label, int enabled, int primary) {
    int hov = enabled && npx_btn_hover(x, y, w, h);
    u32 bg = !enabled ? 0x161B22 : primary ? (hov ? 0x2F81F7 : cfg_accent) : (hov ? 0x30363D : 0x21262D);
    rect(x, y, w, h, bg); outline(x, y, w, h, enabled ? BORDER : 0x21262D);
    int tx = x + (w - np_len(label) * NPX_CW) / 2;
    text(tx, y + (h - NPX_CH) / 2, label, enabled ? (primary ? 0xFFFFFF : TEXT) : 0x484F58, bg);
}
static void npx_draw_field(int x, int y, int w, const LineEdit* e, int focus) {
    rect(x, y, w, 22, 0x010409); outline(x, y, w, 22, focus ? cfg_accent : BORDER);
    int maxc = (w - 12) / NPX_CW, first = 0;
    if (e->cursor > maxc - 1) first = e->cursor - (maxc - 1);
    int a, b; le_sel(e, &a, &b);
    for (int i = first; i < e->len && i - first < maxc; i++) {
        int gx = x + 6 + (i - first) * NPX_CW;
        int sel = (i >= a && i < b);
        glyph(gx, y + 3, le_display_char(e, i), TEXT, sel ? NPX_SEL : 0x010409);
    }
    if (focus && cursor_blink < 50) rect(x + 6 + (e->cursor - first) * NPX_CW, y + 3, 2, 16, cfg_accent);
}

static void npx_draw_dialog(int wi, const NpGeom* g) {
    NpWin* n = &npx[wi]; NpDlg d; npx_dlg_geom(g, &d);
    rect(d.x + 3, d.y + 3, d.w, d.h, 0x010409);                       /* shadow */
    rect(d.x, d.y, d.w, d.h, 0x161B22); outline(d.x, d.y, d.w, d.h, cfg_accent);
    rect(d.x, d.y, d.w, 26, 0x1C2128); hline(d.x, d.y + 26, d.w, BORDER);
    text(d.x + 10, d.y + 5, n->dk ? "Save As" : "Open", TEXT, 0x1C2128);
    /* folder + Up */
    char shown[NP_PATH_MAX]; int maxc = (d.w - 20 - 52) / NPX_CW, dl = np_len(n->dir);
    if (dl > maxc) { shown[0] = '.'; shown[1] = '.'; np_cpy(shown + 2, n->dir + dl - (maxc - 2), (int)sizeof shown - 2); } else np_cpy(shown, n->dir, sizeof shown);
    text(d.x + 10, d.y + 36, shown, DIM, 0x161B22);
    npx_draw_button(d.up_x, d.up_y, 44, 22, "Up", !np_at_root(n->dir), 0);
    /* list */
    rect(d.list_x, d.list_y, d.list_w, d.list_rows * 18, 0x010409); outline(d.list_x - 1, d.list_y - 1, d.list_w + 2, d.list_rows * 18 + 2, BORDER);
    for (int r = 0; r < d.list_rows; r++) {
        int i = n->dscroll + r; if (i >= n->nents) break;
        int ry = d.list_y + r * 18;
        int hov = in_box(mouse_x, mouse_y, d.list_x, ry, d.list_w, 18);
        if (i == n->dsel) rect(d.list_x, ry, d.list_w, 18, 0x1F3A5F); else if (hov) rect(d.list_x, ry, d.list_w, 18, 0x21262D);
        u32 bgc = i == n->dsel ? 0x1F3A5F : hov ? 0x21262D : 0x010409;
        if (n->ents[i].is_dir) { rect(d.list_x + 6, ry + 5, 12, 9, YELLOW); rect(d.list_x + 6, ry + 3, 5, 3, YELLOW); }
        else { rect(d.list_x + 8, ry + 3, 9, 12, DIM); rect(d.list_x + 9, ry + 4, 7, 10, 0x010409); }
        text(d.list_x + 24, ry + 1, n->ents[i].name, n->ents[i].is_dir ? TEXT : 0xC9D1D9, bgc);
        if (!n->ents[i].is_dir) {
            char sz[24]; unsigned v = n->ents[i].size; int k;
            if (v < 1024) { k = npx_num(sz, v); sz[k++] = ' '; sz[k++] = 'B'; sz[k] = 0; }
            else if (v < 1048576) { k = npx_num(sz, v / 1024); sz[k++] = ' '; sz[k++] = 'K'; sz[k++] = 'B'; sz[k] = 0; }
            else { k = npx_num(sz, v / 1048576); sz[k++] = ' '; sz[k++] = 'M'; sz[k++] = 'B'; sz[k] = 0; }
            text(d.list_x + d.list_w - np_len(sz) * NPX_CW - 8, ry + 1, sz, DIM, bgc);
        }
    }
    if (n->nents == 0) text(d.list_x + 10, d.list_y + 6, "(empty folder)", DIM, 0x010409);
    if (n->nents > d.list_rows) {                                       /* list scrollbar */
        int ty, th; np_thumb(n->nents, d.list_rows, n->dscroll, d.list_rows * 18, 14, &ty, &th);
        rect(d.list_x + d.list_w + 1, d.list_y, 8, d.list_rows * 18, 0x0D1117);
        rect(d.list_x + d.list_w + 1, d.list_y + ty, 8, th, 0x484F58);
    }
    text(d.x + 10, d.name_y + 3, "File name:", DIM, 0x161B22);
    npx_draw_field(d.x + 10 + 11 * NPX_CW, d.name_y, d.w - 20 - 11 * NPX_CW, &n->fn, 1);
    if (n->derr[0]) text(d.x + 10, d.name_y + 26, n->derr, RED, 0x161B22);
    npx_draw_button(d.ok_x, d.btn_y, 84, 26, n->dk ? "Save" : "Open", 1, 1);
    npx_draw_button(d.cancel_x, d.btn_y, 84, 26, "Cancel", 1, 0);
}
static void npx_draw_box(int wi, const NpGeom* g) {
    NpWin* n = &npx[wi]; NpBox b; npx_box_geom(g, n, &b);
    rect(b.x + 3, b.y + 3, b.w, b.h, 0x010409);
    rect(b.x, b.y, b.w, b.h, 0x161B22); outline(b.x, b.y, b.w, b.h, n->mode == NPM_MSG ? RED : cfg_accent);
    rect(b.x, b.y, b.w, 24, 0x1C2128);
    text(b.x + 10, b.y + 4, n->mode == NPM_MSG ? "Notepad" : "Unsaved changes", TEXT, 0x1C2128);
    int maxc = (b.w - 24) / NPX_CW; char ln[120]; int ml = np_len(n->msg);
    if (ml <= maxc) text(b.x + 12, b.y + 40, n->msg, TEXT, 0x161B22);
    else { int cut = maxc; while (cut > 0 && n->msg[cut] != ' ') cut--; if (cut <= 0) cut = maxc;
        for (int k = 0; k < cut && k < 119; k++) ln[k] = n->msg[k]; ln[cut < 119 ? cut : 119] = 0;
        text(b.x + 12, b.y + 34, ln, TEXT, 0x161B22); text(b.x + 12, b.y + 52, n->msg + cut + (n->msg[cut] == ' ' ? 1 : 0), TEXT, 0x161B22); }
    for (int i = 0; i < b.nb; i++) npx_draw_button(b.bx[i], b.by, b.bw[i], 26, b.bl[i], 1, i == 0);
}

static void npx_draw(int wi) {
    Win* w = &wins[wi]; NpWin* n = &npx[wi]; if (!n->used) return;
    NpGeom g; npx_geom(w, &g);
    NpTab* t = npx_tab(wi); TxDoc* d = &t->doc;
    npx_title(wi);
    rect(g.x, g.y, g.w, g.h, NPX_BG);

    /* toolbar */
    rect(g.x, g.tb_y, g.w, NPX_TB_H, NPX_BAR); hline(g.x, g.tb_y + NPX_TB_H - 1, g.w, BORDER);
    NpBtn bt[6]; int nb = npx_buttons(wi, &g, bt);
    for (int i = 0; i < nb; i++) npx_draw_button(bt[i].x, g.tb_y + 4, bt[i].w, 22, bt[i].label, bt[i].enabled, 0);
    /* tabs */
    rect(g.x, g.tab_y, g.w, NPX_TAB_H, NPX_BAR); hline(g.x, g.tab_y + NPX_TAB_H, g.w, BORDER);
    for (int i = 0; i < n->ntabs; i++) {
        int tx, tw; npx_tab_rect(n, &g, i, &tx, &tw);
        int act = (i == n->cur), hov = npx_btn_hover(tx, g.tab_y + 2, tw, NPX_TAB_H - 2);
        u32 bg = act ? NPX_BG : hov ? 0x21262D : 0x1C2128;
        rect(tx, g.tab_y + 2, tw, NPX_TAB_H - 2, bg);
        if (act) rect(tx, g.tab_y + 2, tw, 2, cfg_accent);
        char lb[48]; np_tab_label(lb, sizeof lb, n->tabs[i].name, tx_modified(&n->tabs[i].doc), (tw - 8 - 16) / NPX_CW);
        text(tx + 6, g.tab_y + 6, lb, act ? TEXT : DIM, bg);
        int cx = tx + tw - 16, chov = npx_btn_hover(cx - 2, g.tab_y + 4, 14, 16);
        glyph(cx, g.tab_y + 6, 'x', chov ? RED : DIM, chov ? 0x30363D : bg);
    }
    { int px = g.x + 4 + (n->ntabs ? ((g.w - 8 - 26) / n->ntabs > 170 ? 170 : ((g.w - 8 - 26) / n->ntabs < 64 ? 64 : (g.w - 8 - 26) / n->ntabs)) : 0) * n->ntabs;
      int hov = npx_btn_hover(px, g.tab_y + 2, 22, NPX_TAB_H - 2);
      if (n->ntabs < NPX_MAX_TABS) { rect(px, g.tab_y + 2, 22, NPX_TAB_H - 2, hov ? 0x30363D : 0x1C2128); glyph(px + 7, g.tab_y + 6, '+', TEXT, hov ? 0x30363D : 0x1C2128); } }

    /* text */
    tx_layout(d, g.cols);
    int total = tx_rows(d);
    int maxs = total - g.rows; if (maxs < 0) maxs = 0;
    if (t->scroll > maxs) t->scroll = maxs; if (t->scroll < 0) t->scroll = 0;
    int a = 0, b = 0; if (tx_has_sel(d)) tx_sel(d, &a, &b);
    for (int r = 0; r < g.rows; r++) {
        int row = t->scroll + r; if (row >= total) break;
        int ry = g.ta_y + r * NPX_CH, s = tx_row_start(d, row), e = tx_row_end(d, row), col = 0;
        for (int k = s; k < e; k++) {
            char c = d->buf[k]; int cw = (c == '\t') ? tx_tabw(col) : 1;
            int sel = (k >= a && k < b);
            if (col >= g.cols) break;
            if (c == '\t') { if (sel) rect(g.ta_x + col * NPX_CW, ry, cw * NPX_CW, NPX_CH, NPX_SEL); }
            else glyph(g.ta_x + col * NPX_CW, ry, (c >= 32 && c < 127) ? c : '?', 0xE6EDF3, sel ? NPX_SEL : NPX_BG);
            col += cw;
        }
        if (e < d->len && d->buf[e] == '\n' && e >= a && e < b && col < g.cols) rect(g.ta_x + col * NPX_CW, ry, NPX_CW, NPX_CH, NPX_SEL);   /* a selected line break */
    }
    if (wi == focused && n->mode == NPM_EDIT && cursor_blink < 50) {
        int row = tx_row_of(d, d->cursor);
        if (row >= t->scroll && row < t->scroll + g.rows) {
            int col = tx_col_of(d, row, d->cursor); if (col > g.cols) col = g.cols;
            rect(g.ta_x + col * NPX_CW, g.ta_y + (row - t->scroll) * NPX_CH, 2, NPX_CH, cfg_accent);
        }
    }
    /* scrollbar */
    rect(g.sbx, g.ta_y, NPX_SCRW, g.ta_h, NPX_BAR);
    { int ty, th; np_thumb(total, g.rows, t->scroll, g.ta_h, 20, &ty, &th);
      if (total > g.rows) rect(g.sbx + 1, g.ta_y + ty, NPX_SCRW - 2, th, n->drag == 2 ? cfg_accent : 0x484F58); }
    /* status bars */
    rect(g.x, g.sb_y, g.w, NPX_SB_H, NPX_BAR); hline(g.x, g.sb_y, g.w, BORDER);
    { int ln, col; tx_logical_pos(d, d->cursor, &ln, &col);
      char s[96]; int k = 0;
      k = npx_cat(s, k, "Ln ", sizeof s); k += npx_num(s + k, ln); k = npx_cat(s, k, ", Col ", sizeof s); k += npx_num(s + k, col);
      if (tx_has_sel(d)) { int sa, sb2; tx_sel(d, &sa, &sb2); k = npx_cat(s, k, "  (", sizeof s); k += npx_num(s + k, sb2 - sa); k = npx_cat(s, k, " selected)", sizeof s); }
      text(g.x + 8, g.sb_y + 2, s, DIM, NPX_BAR);
      if (n->status[0] && (long long)g_now_ticks < n->status_until) text(g.x + 8 + (k + 3) * NPX_CW, g.sb_y + 2, n->status, GREEN, NPX_BAR);
      char r2[64]; int q = 0;
      q = npx_cat(r2, q, "Chars: ", sizeof r2); q += npx_num(r2 + q, d->len); q = npx_cat(r2, q, "   Lines: ", sizeof r2); q += npx_num(r2 + q, d->nlines);
      text(g.x + g.w - 8 - q * NPX_CW, g.sb_y + 2, r2, DIM, NPX_BAR); }
    if (n->mode == NPM_FILE) npx_draw_dialog(wi, &g);
    else if (n->mode == NPM_CONFIRM || n->mode == NPM_MSG) npx_draw_box(wi, &g);
}
/* the desktop skips redrawing an idle frame; Notepad needs frames while a message is fading or a drag is active */
static int npx_needs_frames(int wi) { NpWin* n = &npx[wi]; return n->used && (n->drag || (n->status[0] && (long long)g_now_ticks < n->status_until)); }

/* ------------------------------------------------------------------ mouse */
static void npx_text_pos(int wi, int mx, int my, int* row, int* col) {
    NpGeom g; npx_geom(&wins[wi], &g); NpTab* t = npx_tab(wi);
    int r = (my - g.ta_y); r = (r < 0) ? -1 : r / NPX_CH;
    *row = t->scroll + r;
    *col = (mx - g.ta_x + NPX_CW / 2) / NPX_CW; if (mx < g.ta_x) *col = 0;
}
/* A left (button 1) or right (2) press inside the Notepad window. Returns 1 if used. */
static int npx_mouse_down(int wi, int mx, int my, int button) {
    NpWin* n = &npx[wi]; Win* w = &wins[wi]; if (!n->used) return 0;
    NpGeom g; npx_geom(w, &g); NpTab* t = npx_tab(wi); TxDoc* d = &t->doc;
    if (button != 1) return 1;
    if (n->mode == NPM_FILE) {
        NpDlg dl; npx_dlg_geom(&g, &dl);
        NPX_TR("dlg-down", n->dir, mx, my, (long long)g_now_ticks, n->dclick_idx, n->nents);
        if (!in_box(mx, my, dl.x, dl.y, dl.w, dl.h)) return 1;
        if (in_box(mx, my, dl.cancel_x, dl.btn_y, 84, 26)) { n->mode = NPM_EDIT; n->close_after_save = 0; return 1; }
        if (in_box(mx, my, dl.ok_x, dl.btn_y, 84, 26)) { npx_dlg_accept(wi); return 1; }
        if (!np_at_root(n->dir) && in_box(mx, my, dl.up_x, dl.up_y, 44, 22)) { char p[NP_PATH_MAX]; np_parent(p, sizeof p, n->dir); np_cpy(n->dir, p, sizeof n->dir); npx_dlg_reload(wi); return 1; }
        if (in_box(mx, my, dl.list_x, dl.list_y, dl.list_w, dl.list_rows * 18)) {
            int i = n->dscroll + (my - dl.list_y) / 18;
            if (i >= 0 && i < n->nents) {
                int cnt = (n->dclick_idx == i && (long long)g_now_ticks - n->dclick_tick <= NPX_DBLCLICK_TICKS) ? 2 : 1;
                NPX_TR("row", n->ents[i].name, i, n->ents[i].is_dir, cnt, (long long)g_now_ticks - n->dclick_tick, n->dsel);
                n->dsel = i; n->dclick_idx = i; n->dclick_tick = (long long)g_now_ticks; n->derr[0] = 0;
                if (!n->ents[i].is_dir) le_set(&n->fn, n->ents[i].name);
                if (cnt == 2) { if (n->ents[i].is_dir) { char nm[32]; np_cpy(nm, n->ents[i].name, sizeof nm); npx_dlg_enter(wi, nm); le_set(&n->fn, ""); } else npx_dlg_accept(wi); n->dclick_idx = -1; }
            }
            return 1;
        }
        int fx = dl.x + 10 + 11 * NPX_CW, fw = dl.w - 20 - 11 * NPX_CW;
        if (in_box(mx, my, fx, dl.name_y, fw, 22)) {
            int first = n->fn.cursor > (fw - 12) / NPX_CW - 1 ? n->fn.cursor - ((fw - 12) / NPX_CW - 1) : 0;
            le_click(&n->fn, first + (mx - fx - 6 + NPX_CW / 2) / NPX_CW, 0);
        }
        return 1;
    }
    if (n->mode == NPM_CONFIRM || n->mode == NPM_MSG) {
        NpBox b; npx_box_geom(&g, n, &b);
        for (int i = 0; i < b.nb; i++) if (in_box(mx, my, b.bx[i], b.by, b.bw[i], 26)) { npx_box_choose(wi, i); return 1; }
        return 1;
    }
    /* toolbar */
    if (my >= g.tb_y && my < g.tb_y + NPX_TB_H) {
        NpBtn bt[6]; int nb = npx_buttons(wi, &g, bt);
        for (int i = 0; i < nb; i++) if (bt[i].enabled && in_box(mx, my, bt[i].x, g.tb_y + 4, bt[i].w, 22)) {
            switch (bt[i].id) {
                case NB_NEW: npx_cmd_new_tab(wi); break;
                case NB_OPEN: npx_dlg_open(wi, 0); break;
                case NB_SAVE: npx_cmd_save(wi, 0); break;
                case NB_SAVEAS: npx_cmd_save(wi, 1); break;
                case NB_UNDO: npx_cmd_undo(wi); break;
                case NB_REDO: npx_cmd_redo(wi); break;
            }
            return 1;
        }
        return 1;
    }
    /* tab bar */
    if (my >= g.tab_y && my < g.tab_y + NPX_TAB_H + 1) {
        for (int i = 0; i < n->ntabs; i++) {
            int tx, tw; npx_tab_rect(n, &g, i, &tx, &tw);
            if (!in_box(mx, my, tx, g.tab_y + 2, tw, NPX_TAB_H - 2)) continue;
            if (mx >= tx + tw - 20) npx_cmd_close_tab(wi, i); else npx_switch(wi, i);
            return 1;
        }
        int tw0 = n->ntabs ? (g.w - 8 - 26) / n->ntabs : 0; if (tw0 > 170) tw0 = 170; if (tw0 < 64) tw0 = 64;
        if (in_box(mx, my, g.x + 4 + tw0 * n->ntabs, g.tab_y + 2, 22, NPX_TAB_H - 2)) npx_cmd_new_tab(wi);
        return 1;
    }
    /* scrollbar */
    if (mx >= g.sbx && mx < g.sbx + NPX_SCRW && my >= g.ta_y && my < g.ta_y + g.ta_h) {
        tx_layout(d, g.cols); int total = tx_rows(d), ty, th;
        np_thumb(total, g.rows, t->scroll, g.ta_h, 20, &ty, &th);
        int rel = my - g.ta_y;
        if (rel >= ty && rel < ty + th) { n->drag = 2; n->sb_grab = rel - ty; }
        else { t->scroll += rel < ty ? -(g.rows - 1) : (g.rows - 1); int mx2 = total - g.rows; if (mx2 < 0) mx2 = 0; if (t->scroll > mx2) t->scroll = mx2; if (t->scroll < 0) t->scroll = 0; }
        return 1;
    }
    /* text area */
    if (my >= g.ta_y && my < g.ta_y + g.ta_h && mx >= g.ta_x - 4 && mx < g.sbx) {
        tx_layout(d, g.cols);
        int row, col; npx_text_pos(wi, mx, my, &row, &col);
        n->click_n = np_click_count(n->click_n, n->click_tick, n->click_x, n->click_y, (long long)g_now_ticks, mx, my, NPX_DBLCLICK_TICKS);
        n->click_tick = (long long)g_now_ticks; n->click_x = mx; n->click_y = my;
        tx_click(d, row, col, 0);
        if (n->click_n == 2) tx_select_word_at(d, d->cursor);
        else if (n->click_n == 3) tx_select_line_at(d, d->cursor);
        n->drag = 1; cursor_blink = 0;
        return 1;
    }
    return 1;
}
static void npx_mouse_drag(int wi, int mx, int my) {
    NpWin* n = &npx[wi]; if (!n->used || !n->drag || n->mode != NPM_EDIT) return;
    NpGeom g; npx_geom(&wins[wi], &g); NpTab* t = npx_tab(wi); TxDoc* d = &t->doc;
    tx_layout(d, g.cols); int total = tx_rows(d);
    int maxs = total - g.rows; if (maxs < 0) maxs = 0;
    if (n->drag == 2) {
        t->scroll = np_scroll_from_thumb(total, g.rows, g.ta_h, 20, my - g.ta_y - n->sb_grab);
        return;
    }
    if (my < g.ta_y) { t->scroll -= 1 + (g.ta_y - my) / 24; if (t->scroll < 0) t->scroll = 0; }
    else if (my >= g.ta_y + g.ta_h) { t->scroll += 1 + (my - g.ta_y - g.ta_h) / 24; if (t->scroll > maxs) t->scroll = maxs; }
    int row, col; npx_text_pos(wi, mx, my < g.ta_y ? g.ta_y : my, &row, &col);
    if (my < g.ta_y) row = t->scroll; else if (my >= g.ta_y + g.ta_h) row = t->scroll + g.rows - 1;
    tx_click(d, row, col, 1);
    if (n->click_n >= 2) { /* a drag that began with a double click keeps the whole-word selection start; plain extension is fine */ }
}
static void npx_mouse_up(int wi) { npx[wi].drag = 0; }
/* ------------------------------------------------------------------ right-click menu support
 * The desktop draws the menu (ctxmenu.h); these three tell it about the editor. The commands themselves are the
 * ordinary keyboard shortcuts (npx_key), so the menu and the keys can never behave differently. */
static int npx_ctx_hit(int wi, int mx, int my) {                /* is the point on the editable text of an editing window? */
    NpWin* n = &npx[wi]; if (!n->used || n->mode != NPM_EDIT) return 0;
    NpGeom g; npx_geom(&wins[wi], &g);
    return my >= g.ta_y && my < g.ta_y + g.ta_h && mx >= g.ta_x - 4 && mx < g.sbx;
}
/* A right-click first places the caret where you clicked -- unless you clicked inside the current selection,
 * which stays (so you can right-click selected text and choose Copy). */
static void npx_ctx_prepare(int wi, int mx, int my) {
    NpGeom g; npx_geom(&wins[wi], &g); TxDoc* d = &npx_tab(wi)->doc;
    tx_layout(d, g.cols);
    int a0 = d->anchor, c0 = d->cursor;
    int row, col; npx_text_pos(wi, mx, my, &row, &col);
    tx_click(d, row, col, 0);
    if (a0 != c0) {
        int lo = a0 < c0 ? a0 : c0, hi = a0 < c0 ? c0 : a0;
        if (d->cursor >= lo && d->cursor <= hi) { d->anchor = a0; d->cursor = c0; }
    }
    cursor_blink = 0;
}
static void npx_ctx_state(int wi, int* has_sel, int* can_undo, int* can_redo, int* has_text) {
    TxDoc* d = &npx_tab(wi)->doc;
    *has_sel = tx_has_sel(d); *can_undo = tx_can_undo(d); *can_redo = tx_can_redo(d); *has_text = d->len > 0;
}
static void npx_wheel(int wi, int notches) {
    NpWin* n = &npx[wi]; if (!n->used) return;
    if (n->mode == NPM_FILE) {
        NpGeom g; npx_geom(&wins[wi], &g); NpDlg dl; npx_dlg_geom(&g, &dl);
        n->dscroll -= notches * 3; int mxs = n->nents - dl.list_rows; if (mxs < 0) mxs = 0;
        if (n->dscroll > mxs) n->dscroll = mxs; if (n->dscroll < 0) n->dscroll = 0; return;
    }
    if (n->mode != NPM_EDIT) return;
    NpGeom g; npx_geom(&wins[wi], &g); NpTab* t = npx_tab(wi); TxDoc* d = &t->doc;
    tx_layout(d, g.cols);
    int maxs = tx_rows(d) - g.rows; if (maxs < 0) maxs = 0;
    t->scroll -= notches * 3; if (t->scroll > maxs) t->scroll = maxs; if (t->scroll < 0) t->scroll = 0;
}

/* ------------------------------------------------------------------ keyboard */
/* One key press for the focused Notepad window. ch = key code from sys_keyev (ASCII, or 1001 up 1002 down
 * 1003 left 1004 right 1005 home 1006 end 1007 delete 1008 page up 1009 page down). Returns 1 if used. */
static int npx_key(int wi, int ch, int shift, int ctrl) {
    NpWin* n = &npx[wi]; if (!n->used) return 0;
    NpTab* t = npx_tab(wi); TxDoc* d = &t->doc; NpGeom g; npx_geom(&wins[wi], &g);
    cursor_blink = 0;
    if (n->mode == NPM_MSG) { if (ch == '\n' || ch == '\r' || ch == 27 || ch == ' ') npx_box_choose(wi, 0); return 1; }
    if (n->mode == NPM_CONFIRM) {
        NpBox b; npx_box_geom(&g, n, &b);
        if (ch == '\n' || ch == '\r') npx_box_choose(wi, 0);
        else if (ch == 27) npx_box_choose(wi, b.nb - 1);
        return 1;
    }
    if (n->mode == NPM_FILE) {
        NpDlg dl; npx_dlg_geom(&g, &dl);
        if (ch == 27) { n->mode = NPM_EDIT; n->close_after_save = 0; return 1; }
        if (ch == '\n' || ch == '\r') { npx_dlg_accept(wi); return 1; }
        if (ch == 1001 && n->nents) { n->dsel = n->dsel <= 0 ? 0 : n->dsel - 1; if (n->dsel < n->dscroll) n->dscroll = n->dsel; if (!n->ents[n->dsel].is_dir) le_set(&n->fn, n->ents[n->dsel].name); return 1; }
        if (ch == 1002 && n->nents) { n->dsel = n->dsel + 1 >= n->nents ? n->nents - 1 : n->dsel + 1; if (n->dsel >= n->dscroll + dl.list_rows) n->dscroll = n->dsel - dl.list_rows + 1; if (!n->ents[n->dsel].is_dir) le_set(&n->fn, n->ents[n->dsel].name); return 1; }
        n->derr[0] = 0;
        le_key(&n->fn, ch, shift, ctrl);
        return 1;
    }
    /* ---- editing ---- */
    tx_layout(d, g.cols);
    if (ctrl) {
        int l = (ch >= 'A' && ch <= 'Z') ? ch + 32 : ch;
        switch (l) {
            case 'a': tx_select_all(d); return 1;
            case 'c': npx_cmd_copy(wi, 0); return 1;
            case 'x': npx_cmd_copy(wi, 1); return 1;
            case 'v': npx_cmd_paste(wi); return 1;
            case 'z': if (shift) npx_cmd_redo(wi); else npx_cmd_undo(wi); return 1;
            case 'y': npx_cmd_redo(wi); return 1;
            case 's': npx_cmd_save(wi, shift); return 1;
            case 'n': npx_cmd_new_tab(wi); return 1;
            case 'o': npx_dlg_open(wi, 0); return 1;
            case 'w': npx_cmd_close_tab(wi, n->cur); return 1;
            case '\t': npx_switch(wi, shift ? (n->cur + n->ntabs - 1) % n->ntabs : (n->cur + 1) % n->ntabs); return 1;
            case 1003: tx_move_word_left(d, shift); npx_reveal(wi); return 1;
            case 1004: tx_move_word_right(d, shift); npx_reveal(wi); return 1;
            case 1005: tx_move_doc_home(d, shift); npx_reveal(wi); return 1;
            case 1006: tx_move_doc_end(d, shift); npx_reveal(wi); return 1;
        }
        return 1;                                                  /* other Ctrl combinations are swallowed, never typed */
    }
    switch (ch) {
        case 1001: tx_move_rows(d, -1, shift); npx_reveal(wi); return 1;
        case 1002: tx_move_rows(d, 1, shift); npx_reveal(wi); return 1;
        case 1003: tx_move_left(d, shift); npx_reveal(wi); return 1;
        case 1004: tx_move_right(d, shift); npx_reveal(wi); return 1;
        case 1005: tx_move_home(d, shift); npx_reveal(wi); return 1;
        case 1006: tx_move_end(d, shift); npx_reveal(wi); return 1;
        case 1008: tx_move_rows(d, -(g.rows - 1), shift); npx_reveal(wi); return 1;
        case 1009: tx_move_rows(d, g.rows - 1, shift); npx_reveal(wi); return 1;
        case 1007: d->flags = 0; tx_delete_fwd(d); npx_after_edit(wi); return 1;
        case 8: case 127: d->flags = 0; tx_backspace(d); npx_after_edit(wi); return 1;
        case '\n': case '\r': d->flags = 0; tx_type_char(d, '\n'); npx_after_edit(wi); return 1;
        case '\t': d->flags = 0; tx_type_char(d, '\t'); npx_after_edit(wi); return 1;
        case 27: if (tx_has_sel(d)) { tx_set_cursor(d, d->cursor, 0); } return 1;
    }
    if (ch >= 32 && ch < 127) {
        d->flags = 0; tx_type_char(d, (char)ch);
        if (d->flags & TXF_TRUNC) npx_status(wi, "Document limit reached (1 MB).");
        npx_after_edit(wi); return 1;
    }
    return 0;
}
/* carries out a chosen menu entry (CM_*) exactly like the matching keyboard shortcut; only while editing */
static void npx_ctx_do(int wi, int cm_id) {
    if (!npx[wi].used || npx[wi].mode != NPM_EDIT) return;
    switch (cm_id) {
    case CM_UNDO:   npx_key(wi, 'z', 0, 1); break;
    case CM_REDO:   npx_key(wi, 'y', 0, 1); break;
    case CM_CUT:    npx_key(wi, 'x', 0, 1); break;
    case CM_COPY:   npx_key(wi, 'c', 0, 1); break;
    case CM_PASTE:  npx_key(wi, 'v', 0, 1); break;
    case CM_SELALL: npx_key(wi, 'a', 0, 1); break;
    default: break;
    }
}

#endif /* NOTEPAD_H */
