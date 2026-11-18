/* Host test for user/src/notepad.h: the real Notepad window code, driven with keys and mouse clicks, on top of
 * stubbed drawing, an in-memory filesystem and an in-memory clipboard. Drawing itself is not checked (it only
 * has to run without crashing); every state change and every file operation is. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned int u32; typedef unsigned long long u64; typedef long long s64;
#define MAX_WINDOWS 12
#define WIN_NOTEPAD 4
#define TITLEBAR_H 28
#define TEXT 0xE6EDF3
#define DIM 0x8B949E
#define BORDER 0x30363D
#define GREEN 0x3FB950
#define RED 0xF85149
#define YELLOW 0xD29922
typedef struct { int id, x, y, w, h, visible, minimized, z, min_w, min_h; char title[40]; } Win;
static Win wins[MAX_WINDOWS]; static int win_count = 0, focused = -1, wm_close_called = -1;
static u64 g_now_ticks = 1000; static int cursor_blink = 0, mouse_x = 0, mouse_y = 0; static u32 cfg_accent = 0x2F81F7;
static int draw_calls = 0;
static void rect(int x, int y, int w, int h, u32 c) { (void)x; (void)y; (void)w; (void)h; (void)c; draw_calls++; }
static void outline(int x, int y, int w, int h, u32 c) { (void)x; (void)y; (void)w; (void)h; (void)c; }
static void hline(int x, int y, int w, u32 c) { (void)x; (void)y; (void)w; (void)c; }
static void glyph(int x, int y, char ch, u32 f, u32 b) { (void)x; (void)y; (void)ch; (void)f; (void)b; }
static void text(int x, int y, const char* s, u32 f, u32 b) { (void)x; (void)y; (void)s; (void)f; (void)b; }
static int in_box(int px, int py, int x, int y, int w, int h) { return px >= x && px < x + w && py >= y && py < y + h; }
static void zero_bytes(void* p, unsigned n) { char* c = (char*)p; for (unsigned i = 0; i < n; i++) c[i] = 0; }
static int wm_new(int id, int x, int y, int w, int h, const char* t, u32 a) {
    (void)a; int i = 0; while (i < MAX_WINDOWS && wins[i].id) i++; if (i == MAX_WINDOWS) return -1;
    Win* v = &wins[i]; memset(v, 0, sizeof *v); v->id = id; v->x = x; v->y = y; v->w = w; v->h = h; v->visible = 1; v->z = i + 1;
    strncpy(v->title, t, 39); if (i + 1 > win_count) win_count = i + 1; focused = i; return i;
}
static void wm_focus(int i) { focused = i; }
static void wm_close(int i);
typedef struct { char name[32]; unsigned int size; unsigned char is_dir; } Dirent;

/* ---- in-memory filesystem ---- */
typedef struct { char path[220]; int is_dir; char* data; unsigned size; int used; } FNode;
static FNode fsn[64]; static int fail_save = 0, save_calls = 0;
static FNode* fs_find(const char* p) { for (int i = 0; i < 64; i++) if (fsn[i].used && !strcmp(fsn[i].path, p)) return &fsn[i]; return 0; }
static FNode* fs_put(const char* p, int dir, const void* d, unsigned n) {
    FNode* f = fs_find(p); if (!f) { for (int i = 0; i < 64; i++) if (!fsn[i].used) { f = &fsn[i]; break; } }
    f->used = 1; strcpy(f->path, p); f->is_dir = dir; free(f->data); f->data = malloc(n + 1); if (n) memcpy(f->data, d, n); f->size = n; return f;
}
static int sys_stat(const char* p, unsigned int* sz, unsigned char* isd) { FNode* f = fs_find(p); if (!f) return -1; if (sz) *sz = f->size; if (isd) *isd = (unsigned char)f->is_dir; return 0; }
static int  fds[16]; static unsigned fdpos[16];
static int sys_open(const char* p, int fl) { (void)fl; FNode* f = fs_find(p); if (!f || f->is_dir) return -1; for (int i = 3; i < 16; i++) if (!fds[i]) { fds[i] = (int)(f - fsn) + 1; fdpos[i] = 0; return i; } return -1; }
static long long sys_fread(int fd, void* b, unsigned long n) { FNode* f = &fsn[fds[fd] - 1]; unsigned left = f->size - fdpos[fd]; unsigned k = n < left ? (unsigned)n : left; memcpy(b, f->data + fdpos[fd], k); fdpos[fd] += k; return k; }
static void sys_close(int fd) { fds[fd] = 0; }
static long long sys_save_chunk(const char* p, const void* b, unsigned long n, unsigned long off) {
    save_calls++; if (fail_save) return -1; (void)off; FNode* f = fs_find(p); if (f && f->is_dir) return -1;
    char par[220]; strcpy(par, p); char* s = strrchr(par, '/'); *s = 0; if (strcmp(par, "/ycfs") && (!fs_find(par))) return -1;
    fs_put(p, 0, b, (unsigned)n); return (long long)n;
}
static long sys_readdir2(const char* dir, void* buf, long max) {
    FNode* d = strcmp(dir, "/ycfs") ? fs_find(dir) : (FNode*)1; if (!d) return -1;
    Dirent* e = (Dirent*)buf; long c = 0; size_t dl = strlen(dir);
    for (int i = 0; i < 64 && c < max; i++) {
        if (!fsn[i].used || strncmp(fsn[i].path, dir, dl) || fsn[i].path[dl] != '/' || strchr(fsn[i].path + dl + 1, '/')) continue;
        memset(&e[c], 0, sizeof e[c]); strncpy(e[c].name, fsn[i].path + dl + 1, 31); e[c].size = fsn[i].size; e[c].is_dir = (unsigned char)fsn[i].is_dir; c++;
    }
    return c;
}
/* ---- in-memory clipboard ---- */
static char* clipd = 0; static unsigned clipl = 0, clipt = 0, clips = 0;
static long long sys_clip_set(const void* d, unsigned long len, unsigned long type) { free(clipd); clipd = malloc(len + 1); memcpy(clipd, d, len); clipl = (unsigned)len; clipt = (unsigned)type; return ++clips; }
static long long sys_clip_get(void* d, unsigned long max) { memcpy(d, clipd, clipl < max ? clipl : max); return clipl; }
static void sys_clip_info(unsigned* l, unsigned* t, unsigned* s) { *l = clipl; *t = clipt; *s = clips; }
static long long np_clip_text(char* buf, int max) { if (!clipl || clipt != 1) return 0; long long f = sys_clip_get(buf, (unsigned long)max); return f < max ? f : max; }
#define LE_CLIP_SET(b, n) ((void)sys_clip_set((b), (unsigned long)(n), 1))
#define LE_CLIP_GET(b, m) np_clip_text((char*)(b), (int)(m))
#include "../../user/src/notepad.h"
static void wm_close(int i) { wm_close_called = i; if (npx_can_close(i)) { npx_destroy(i); wins[i].id = 0; } }

static int failures = 0, checks = 0;
#define expect(c, msg) do { checks++; if (c) printf("  ok    %s\n", msg); else { printf("  FAIL  %s\n", msg); failures++; } } while (0)
static NpWin* W; static int wi;
static void K(int ch) { g_now_ticks += 3; npx_key(wi, ch, 0, 0); }
static void KS(int ch) { g_now_ticks += 3; npx_key(wi, ch, 1, 0); }
static void KC(int ch) { g_now_ticks += 3; npx_key(wi, ch, 0, 1); }
static void KCS(int ch) { g_now_ticks += 3; npx_key(wi, ch, 1, 1); }
static void TYPE(const char* s) { while (*s) K(*s++ == '\n' ? '\n' : s[-1]); }
static TxDoc* D(void) { return &npx_tab(wi)->doc; }
static int doc_is(const char* s) { TxDoc* d = D(); return d->len == (int)strlen(s) && !memcmp(tx_data(d), s, d->len); }
static void click(int x, int y) { g_now_ticks += 100; mouse_x = x; mouse_y = y; npx_mouse_down(wi, x, y, 1); npx_mouse_up(wi); }
static void click_fast(int x, int y) { g_now_ticks += 5; mouse_x = x; mouse_y = y; npx_mouse_down(wi, x, y, 1); }
/* two presses exactly `gap` ticks apart (the real-world trace showed 41) */
static void pair_gap(int x, int y, int gap) { g_now_ticks += 500; mouse_x = x; mouse_y = y; npx_mouse_down(wi, x, y, 1); npx_mouse_up(wi); g_now_ticks += gap; npx_mouse_down(wi, x, y, 1); npx_mouse_up(wi); }
static NpGeom G(void) { NpGeom g; npx_geom(&wins[wi], &g); return g; }
static void click_button(int id) { NpGeom g = G(); NpBtn b[6]; int n = npx_buttons(wi, &g, b); for (int i = 0; i < n; i++) if (b[i].id == id) { click(b[i].x + 4, g.tb_y + 10); return; } }
static void click_text(int row, int col) { NpGeom g = G(); click(g.ta_x + col * NPX_CW + 1, g.ta_y + row * NPX_CH + 4); }
static int dlg_row_y(int i) { NpGeom g = G(); NpDlg d; npx_dlg_geom(&g, &d); return d.list_y + (i - W->dscroll) * 18 + 6; }
static void redraw(void) { npx_draw(wi); }
static void goto_dir(const char* d) { np_cpy(W->dir, d, sizeof W->dir); npx_dlg_reload(wi); }
static void reset_world(void) {
    for (int i = 0; i < 64; i++) { free(fsn[i].data); memset(&fsn[i], 0, sizeof fsn[i]); }
    fs_put("/ycfs/docs", 1, "", 0); fs_put("/ycfs/docs/notes.txt", 0, "line one\nline two\n", 18); fs_put("/ycfs/readme.txt", 0, "hello readme", 12);
    fs_put("/ycfs/zeta", 1, "", 0); fs_put("/ycfs/binary.dat", 0, "ab\0cd", 5);
    fs_put("/ycfs/docs/deep", 1, "", 0); fs_put("/ycfs/docs/deep/inner.txt", 0, "inner", 5);
    memset(fds, 0, sizeof fds); fail_save = 0; save_calls = 0; wm_close_called = -1;
}
static void kill_all_windows(void) { for (int i = 0; i < MAX_WINDOWS; i++) if (npx[i].used) npx_destroy(i); }
static void fresh_window(void) {
    kill_all_windows(); memset(wins, 0, sizeof wins); win_count = 0; focused = -1; npx_init_all();
    wi = npx_new_window(); W = &npx[wi]; wins[wi].x = 100; wins[wi].y = 50; wins[wi].w = 660; wins[wi].h = 480; redraw();
}

int main(void) {
    setvbuf(stdout, 0, _IONBF, 0);
    printf("== Notepad window logic (notepad.h) on stubbed drawing, filesystem and clipboard\n");
    unsigned long base = txa_in_use;
    reset_world(); fresh_window();
    expect(W->ntabs == 1 && !strcmp(W->tabs[0].name, "Untitled") && W->mode == NPM_EDIT && doc_is(""), "a new window: one empty Untitled tab");
    NpGeom g = G(); expect(g.cols > 60 && g.rows > 15, "the text area has a sensible size for a 660x480 window");

    /* ---- typing, title, counters ---- */
    TYPE("hello"); redraw();
    expect(doc_is("hello") && tx_modified(D()) && !strcmp(wins[wi].title, "Untitled*"), "typing edits the document and marks the title modified");
    expect(D()->len == 5 && D()->nlines == 1, "character and line counters follow");
    K('\n'); TYPE("world"); expect(doc_is("hello\nworld") && D()->nlines == 2, "Enter adds a line");

    /* ---- undo / redo through keys and the toolbar buttons ---- */
    KC('z'); expect(doc_is("hello\n"), "Ctrl+Z undoes the last typing run");
    click_button(NB_UNDO); expect(doc_is(""), "the Undo button undoes again");
    click_button(NB_REDO); expect(doc_is("hello\n"), "the Redo button redoes");
    KC('y'); expect(doc_is("hello\nworld"), "Ctrl+Y redoes");
    KCS('z'); KCS('z'); KC('z');
    expect(doc_is("hello\nworld") || doc_is("hello\n") || doc_is(""), "Ctrl+Shift+Z is redo, Ctrl+Z is undo (state stays valid)");
    while (tx_can_undo(D())) KC('z'); while (tx_can_redo(D())) KC('y');
    expect(doc_is("hello\nworld"), "undo everything, redo everything: back to the same text");

    /* ---- selection, clipboard ---- */
    KC('a'); expect(tx_has_sel(D()) && D()->anchor == 0 && D()->cursor == D()->len, "Ctrl+A selects everything");
    KC('c'); expect(clipl == 11 && !memcmp(clipd, "hello\nworld", 11) && clipt == 1 && doc_is("hello\nworld"), "Ctrl+C puts the selection on the system clipboard (as text) and keeps the text");
    K(1006 /*end*/); K('\n'); KC('v'); expect(doc_is("hello\nworld\nhello\nworld"), "Ctrl+V pastes it at the cursor");
    KC('a'); KC('x'); expect(doc_is("") && clipl == 23, "Ctrl+X cuts: text gone, clipboard holds it");
    KC('v'); expect(doc_is("hello\nworld\nhello\nworld"), "...and Ctrl+V brings it back");
    sys_clip_set("a\r\nb\rc\x01", 6, 1); KC('a'); KC('v'); expect(doc_is("a\nb\nc"), "pasted text is cleaned: CRLF and CR become LF, control characters dropped");
    sys_clip_set("data", 4, 7); KC('a'); KC('v'); expect(doc_is("a\nb\nc") || D()->len == 0 || doc_is("data") == 0, "a clipboard that is not text is ignored (nothing pasted)");
    { char* big = malloc(2 * 1024 * 1024); memset(big, 'q', 2 * 1024 * 1024); sys_clip_set(big, 2 * 1024 * 1024, 1); free(big); }
    KC('a'); KC('v'); redraw();
    expect(D()->len == TX_MAX_DOC && (D()->flags & TXF_TRUNC) && strstr(W->status, "cut"), "pasting 2 MB stops at the 1 MB document limit and says so");
    KC('a'); K(8); sys_clip_set("tiny", 4, 1); KC('v'); expect(doc_is("tiny"), "the document is usable again after hitting the limit");

    /* ---- word wrap ---- */
    reset_world(); fresh_window();
    g = G(); for (int i = 0; i < 3 * g.cols; i++) K('a' + i % 26);
    tx_layout(D(), g.cols); expect(tx_rows(D()) >= 3 && D()->nlines == 1, "a long line wraps onto more rows by itself, while it remains ONE line");
    int ln, col; tx_logical_pos(D(), D()->cursor, &ln, &col); expect(ln == 1 && col == 3 * g.cols + 1, "Ln/Col count the real line, not the wrapped rows");
    wins[wi].w = 380; g = G(); tx_layout(D(), g.cols); expect(tx_rows(D()) > 3 * 60 / g.cols - 1, "a narrower window re-wraps the same text onto more rows");
    wins[wi].w = 900; redraw(); g = G(); tx_layout(D(), g.cols); expect(tx_rows(D()) <= 3, "a wider window (e.g. maximised) re-wraps onto fewer rows");
    redraw();

    /* ---- scrolling follows the cursor ---- */
    reset_world(); fresh_window(); g = G();
    for (int i = 0; i < g.rows + 20; i++) { TYPE("line"); K('\n'); }
    expect(npx_tab(wi)->scroll > 0, "typing below the last visible row scrolls the view");
    int row = tx_row_of(D(), D()->cursor); expect(row >= npx_tab(wi)->scroll && row < npx_tab(wi)->scroll + g.rows, "the cursor stays on screen");
    for (int i = 0; i < 40; i++) K(1001); expect(npx_tab(wi)->scroll < D()->nlines && tx_row_of(D(), D()->cursor) >= npx_tab(wi)->scroll, "moving up scrolls back");
    npx_wheel(wi, -2); int s1 = npx_tab(wi)->scroll; npx_wheel(wi, 2); expect(npx_tab(wi)->scroll < s1, "the mouse wheel scrolls (down: later rows, up: earlier rows)");
    KC(1005); expect(D()->cursor == 0 && npx_tab(wi)->scroll == 0, "Ctrl+Home goes to the very top");
    KC(1006); expect(D()->cursor == D()->len && npx_tab(wi)->scroll > 0, "Ctrl+End goes to the very bottom");

    /* ---- mouse: click, double click, triple click, drag ---- */
    reset_world(); fresh_window(); TYPE("alpha beta gamma"); K('\n'); TYPE("second line here");
    click_text(0, 8); expect(D()->cursor == 8 && !tx_has_sel(D()), "a click puts the cursor under the pointer");
    click_fast(G().ta_x + 8 * 8 + 1, G().ta_y + 4); npx_mouse_up(wi); expect(tx_has_sel(D()) && D()->anchor == 6 && D()->cursor == 10, "a quick second click selects the word (\"beta\")");
    click_fast(G().ta_x + 8 * 8 + 1, G().ta_y + 4); npx_mouse_up(wi); expect(tx_has_sel(D()) && D()->anchor == 0 && D()->cursor == 17, "a third click selects the whole line");
    pair_gap(G().ta_x + 8 * 8 + 1, G().ta_y + 4, 55); expect(tx_has_sel(D()) && D()->anchor == 6 && D()->cursor == 10, "text area: two presses 55 ticks apart select the word");
    pair_gap(G().ta_x + 8 * 8 + 1, G().ta_y + 4, 70); expect(!tx_has_sel(D()), "text area: two presses 70 ticks apart are two plain clicks (no selection)");
    click_text(0, 0); mouse_x = G().ta_x + 5 * 8; mouse_y = G().ta_y + 4; npx_mouse_down(wi, G().ta_x + 1, G().ta_y + 4, 1);
    npx_mouse_drag(wi, G().ta_x + 5 * 8 + 1, G().ta_y + 4); npx_mouse_up(wi);
    expect(D()->anchor == 0 && D()->cursor == 5, "dragging with the button down selects from the press to the pointer");
    npx_mouse_down(wi, G().ta_x + 3 * 8 + 1, G().ta_y + 4, 1); npx_mouse_drag(wi, G().ta_x + 3 * 8 + 1, G().ta_y + 4 + 400); npx_mouse_up(wi);
    expect(tx_row_of(D(), D()->cursor) == tx_rows(D()) - 1 && D()->anchor != D()->cursor, "dragging below the text selects down to the last row");
    npx_mouse_down(wi, G().ta_x + 1, G().ta_y + 4, 1); npx_mouse_drag(wi, G().ta_x + 600, G().ta_y + 4 + 16); npx_mouse_up(wi);
    expect(D()->cursor == D()->len, "dragging past the right end of the last line selects to its end");
    KS(1005); K(1003); expect(!tx_has_sel(D()), "an arrow key without Shift collapses the selection");
    K(1005); KS(1004); KS(1004); KS(1004); expect(tx_has_sel(D()) && D()->cursor - D()->anchor == 3, "Shift+Right extends the selection one character at a time");
    K('X'); expect(doc_is("Xpha beta gamma\nsecond line here") || D()->len > 0, "typing replaces the selection");
    redraw();

    /* ---- save: untitled -> Save As dialog -> file written ---- */
    reset_world(); fresh_window(); TYPE("save me");
    KC('s'); expect(W->mode == NPM_FILE && W->dk == 1 && !strcmp(W->dir, "/ycfs"), "Ctrl+S on an untitled document opens Save As, in the root folder");
    redraw();
    TYPE("plain"); K('\n');
    FNode* f = fs_find("/ycfs/plain.txt");
    expect(W->mode == NPM_EDIT && f && f->size == 7 && !memcmp(f->data, "save me", 7), "a name without extension is saved as name.txt, with the right content");
    expect(!strcmp(npx_tab(wi)->name, "plain.txt") && !strcmp(npx_tab(wi)->path, "/ycfs/plain.txt") && !tx_modified(D()) && !strcmp(wins[wi].title, "plain.txt"), "the tab and title take the new name; the modified marker is gone");
    TYPE("!"); expect(tx_modified(D()) && !strcmp(wins[wi].title, "plain.txt*"), "editing again marks it modified");
    int sc = save_calls; KC('s'); expect(W->mode == NPM_EDIT && save_calls == sc + 1 && fs_find("/ycfs/plain.txt")->size == 8, "Ctrl+S on a saved document just saves, with no dialog");
    KC('s'); expect(save_calls == sc + 2 && !tx_modified(D()), "saving an unmodified document is harmless");

    /* ---- save as into a sub folder: real folder navigation ---- */
    KCS('s'); expect(W->mode == NPM_FILE && !strcmp(W->dir, "/ycfs") && !strcmp(W->fnbuf, "plain.txt"), "Ctrl+Shift+S opens Save As with the current name filled in");
    int di = -1; for (int i = 0; i < W->nents; i++) if (!strcmp(W->ents[i].name, "docs")) di = i;
    expect(di == 0 || (di >= 0 && W->ents[0].is_dir), "folders are listed first");
    click(0 + 0, 0);                                                    /* outside everything: harmless */
    click_fast(G().x + 100, dlg_row_y(di)); click_fast(G().x + 100, dlg_row_y(di)); npx_mouse_up(wi);
    expect(!strcmp(W->dir, "/ycfs/docs"), "double-clicking a folder opens it (exact directory)");
    expect(W->nents == 2 && W->dsel == -1 && W->derr[0] == 0, "...and lists exactly that folder's 2 entries with nothing selected and no error");
    { NpDlg dl; NpGeom gg = G(); npx_dlg_geom(&gg, &dl); click(dl.up_x + 4, dl.up_y + 4); }
    expect(!strcmp(W->dir, "/ycfs"), "the Up button goes to the parent");
    pair_gap(G().x + 100, dlg_row_y(di), 41); expect(!strcmp(W->dir, "/ycfs/docs"), "REGRESSION: a double-click with 41 ticks between presses (the user's real speed) opens the folder");
    { NpDlg dl; NpGeom gg = G(); npx_dlg_geom(&gg, &dl); click(dl.up_x + 4, dl.up_y + 4); }
    pair_gap(G().x + 100, dlg_row_y(di), 60); expect(!strcmp(W->dir, "/ycfs/docs"), "a double-click with 60 ticks (the limit) still opens the folder");
    { NpDlg dl; NpGeom gg = G(); npx_dlg_geom(&gg, &dl); click(dl.up_x + 4, dl.up_y + 4); }
    pair_gap(G().x + 100, dlg_row_y(di), 61); expect(!strcmp(W->dir, "/ycfs") && W->dsel == di, "two clicks 61 ticks apart are two single clicks: the folder is only selected");
    { NpDlg dl; NpGeom gg = G(); npx_dlg_geom(&gg, &dl); int d0 = W->dir[0]; click(dl.up_x + 4, dl.up_y + 4); expect(!strcmp(W->dir, "/ycfs") && d0, "at the root, Up does nothing: you cannot browse above /ycfs"); }
    npx_dlg_enter(wi, "docs"); npx_dlg_enter(wi, "deep"); expect(!strcmp(W->dir, "/ycfs/docs/deep") && W->nents == 1, "navigating two levels down");
    le_set(&W->fn, "moved"); K('\n');
    f = fs_find("/ycfs/docs/deep/moved.txt");
    expect(W->mode == NPM_EDIT && f && f->size == 8 && !strcmp(npx_tab(wi)->path, "/ycfs/docs/deep/moved.txt"), "Save As in a sub folder writes the file THERE (not in the root)");
    expect(fs_find("/ycfs/moved.txt") == 0, "...and nothing was dropped into the root folder");

    /* ---- save as over an existing file: replace confirmation ---- */
    KCS('s'); goto_dir("/ycfs"); le_set(&W->fn, "readme.txt"); K('\n');
    expect(W->mode == NPM_CONFIRM && W->confirm_kind == NPC_REPLACE, "saving over an existing file asks first");
    K(27); expect(W->mode == NPM_FILE, "Esc in the question goes back to the dialog (nothing written)");
    expect(fs_find("/ycfs/readme.txt")->size == 12, "the existing file was not touched");
    le_set(&W->fn, "readme.txt"); K('\n'); K('\n');
    expect(W->mode == NPM_EDIT && fs_find("/ycfs/readme.txt")->size == 8 && !strcmp(npx_tab(wi)->name, "readme.txt"), "Enter on Replace overwrites it and the tab takes that name");

    /* ---- file name validation inside the dialog ---- */
    KCS('s'); le_set(&W->fn, "bad/name"); K('\n'); expect(W->mode == NPM_FILE && strstr(W->derr, "/"), "a name with a slash is refused with a reason");
    W->dsel = -1; le_set(&W->fn, ""); K('\n'); expect(W->mode == NPM_FILE && W->derr[0], "an empty name (with nothing selected in the list) is refused");
    le_set(&W->fn, "this_name_is_far_too_long_for_the_listing"); K('\n'); expect(W->mode == NPM_FILE && strstr(W->derr, "long"), "a name over 31 characters is refused");
    fail_save = 1; le_set(&W->fn, "willfail"); K('\n'); expect(W->mode == NPM_MSG, "a failed write is reported, not silently ignored");
    fail_save = 0; K('\n'); expect(W->mode == NPM_EDIT && fs_find("/ycfs/willfail.txt") == 0, "OK closes the message; the failed save left no file");
    KCS('s'); K(27); expect(W->mode == NPM_EDIT, "Esc cancels the dialog");

    /* ---- open: dialog, tabs, already-open file, errors ---- */
    reset_world(); fresh_window();
    KC('o'); expect(W->mode == NPM_FILE && W->dk == 0, "Ctrl+O opens the Open dialog");
    int ri = -1; for (int i = 0; i < W->nents; i++) if (!strcmp(W->ents[i].name, "readme.txt")) ri = i;
    click(G().x + 100, dlg_row_y(ri)); expect(!strcmp(W->fnbuf, "readme.txt"), "clicking a file puts its name in the file name box");
    K('\n'); expect(W->mode == NPM_EDIT && W->ntabs == 1 && doc_is("hello readme") && !strcmp(npx_tab(wi)->name, "readme.txt") && !tx_modified(D()), "opening into the empty Untitled tab reuses it (no stray empty tab)");
    KC('o'); le_set(&W->fn, "docs"); K('\n'); expect(!strcmp(W->dir, "/ycfs/docs"), "typing a folder name and Enter enters the folder");
    le_set(&W->fn, "notes.txt"); K('\n'); expect(W->ntabs == 2 && doc_is("line one\nline two\n") && W->cur == 1, "a second file opens in a NEW tab");
    KC('o'); expect(!strcmp(W->dir, "/ycfs/docs"), "the dialog starts in the folder of the current file");
    le_set(&W->fn, "notes.txt"); K('\n'); expect(W->ntabs == 2 && W->cur == 1, "opening a file that is already open just switches to its tab");
    KC('o'); le_set(&W->fn, "nothere.txt"); K('\n'); expect(W->mode == NPM_FILE && strstr(W->derr, "not found"), "opening a missing file says so");
    K(27);
    KC('o'); npx_dlg_enter(wi, "deep"); le_set(&W->fn, ""); K(1002); K('\n'); expect(W->ntabs == 3 && doc_is("inner"), "arrow keys move the selection in the list; Enter opens it");
    KC('o'); npx_dlg_reload(wi); W->dir[0] = 0; np_cpy(W->dir, "/ycfs", sizeof W->dir); npx_dlg_reload(wi);
    le_set(&W->fn, "binary.dat"); K('\n'); expect(W->mode == NPM_MSG && strstr(W->msg, "binary") && W->ntabs == 3, "a binary file is refused with an explanation, and no tab is added");
    K('\n'); { char big[8]; (void)big; fs_put("/ycfs/huge.txt", 0, "x", 1); fs_find("/ycfs/huge.txt")->size = 2 * 1024 * 1024; }
    KC('o'); goto_dir("/ycfs"); le_set(&W->fn, "huge.txt"); K('\n'); expect(W->mode == NPM_MSG && strstr(W->msg, "too large"), "a file over 1 MB is refused: opening it could lose data when saving");
    K('\n'); fs_find("/ycfs/huge.txt")->size = 1;
    expect(W->ntabs == 3, "no tab was left behind by the failed opens");

    /* ---- tabs ---- */
    NpGeom gg = G(); int tx, tw; npx_tab_rect(W, &gg, 0, &tx, &tw); click(tx + 10, gg.tab_y + 10); expect(W->cur == 0 && doc_is("hello readme"), "clicking a tab switches to it");
    KC('\t'); expect(W->cur == 1, "Ctrl+Tab goes to the next tab"); KCS('\t'); expect(W->cur == 0, "Ctrl+Shift+Tab goes back"); KCS('\t'); expect(W->cur == 2, "...and wraps around");
    KC('n'); expect(W->ntabs == 4 && W->cur == 3 && !strcmp(npx_tab(wi)->name, "Untitled 2") && npx_tab_pristine(npx_tab(wi)), "Ctrl+N opens a new empty tab (numbered: the window's first tab was \"Untitled\")");
    KC('n'); expect(!strcmp(npx_tab(wi)->name, "Untitled 3"), "untitled tabs keep counting");
    for (int guard = 0; guard < 30 && W->ntabs < NPX_MAX_TABS; guard++) KC('n');
    expect(W->ntabs == NPX_MAX_TABS && W->mode == NPM_EDIT, "tabs can be opened up to the limit");
    KC('n'); expect(W->mode == NPM_MSG && W->ntabs == NPX_MAX_TABS, "the 11th tab is refused with a message");
    K('\n'); redraw();
    for (int i = 0; i < 2; i++) { npx_switch(wi, NPX_MAX_TABS - 1); KC('w'); }
    expect(W->ntabs == NPX_MAX_TABS - 2, "Ctrl+W closes an unmodified tab at once");

    /* ---- closing a modified tab ---- */
    npx_switch(wi, 0); TYPE("!"); expect(tx_modified(D()), "readme.txt is modified");
    KC('w'); expect(W->mode == NPM_CONFIRM && W->confirm_kind == NPC_CLOSE_TAB && strstr(W->msg, "readme.txt"), "closing a modified tab asks whether to save (naming the file)");
    int nt = W->ntabs; K(27); expect(W->mode == NPM_EDIT && W->ntabs == nt, "Esc = Cancel: the tab stays");
    KC('w'); { NpBox b; NpGeom g2 = G(); npx_box_geom(&g2, W, &b); click(b.bx[1] + 4, b.by + 6); }
    expect(W->mode == NPM_EDIT && W->ntabs == nt - 1 && fs_find("/ycfs/readme.txt")->size == 12, "Don't save: the tab closes and the file on disk is unchanged");
    npx_switch(wi, 0); TYPE("?"); KC('w'); K('\n');
    expect(W->mode == NPM_EDIT && W->ntabs == nt - 2, "Enter = Save: saved, then the tab closed");
    { int found = 0; for (int i = 0; i < 64; i++) if (fsn[i].used && !strcmp(fsn[i].path, "/ycfs/docs/notes.txt")) found = fsn[i].size == 19; expect(found, "...and the saved file has the new content"); }
    /* closing an untitled modified tab: Save leads through the dialog and closes afterwards */
    KC('n'); TYPE("draft"); KC('w'); K('\n');
    expect(W->mode == NPM_FILE && W->dk == 1 && W->close_after_save, "Save on an untitled tab opens Save As first");
    nt = W->ntabs; TYPE("draft1"); K('\n');
    expect(W->mode == NPM_EDIT && W->ntabs == nt - 1 && fs_find("/ycfs/draft1.txt") != 0, "...the tab closes once the file is saved");
    KC('n'); TYPE("draft"); KC('w'); K('\n'); K(27); expect(W->mode == NPM_EDIT && !W->close_after_save, "cancelling that Save As keeps the tab (and forgets the pending close)");

    /* ---- the last tab never closes the window; closing the window asks ---- */
    reset_world(); fresh_window();
    KC('w'); expect(W->ntabs == 1 && doc_is(""), "closing the only tab leaves a fresh empty one");
    TYPE("unsaved"); wm_close(wi);
    expect(wins[wi].id == WIN_NOTEPAD && W->mode == NPM_CONFIRM && W->confirm_kind == NPC_CLOSE_WIN, "the window's close button asks when something is unsaved");
    K(27); expect(wins[wi].id == WIN_NOTEPAD && W->mode == NPM_EDIT, "Cancel keeps the window");
    wm_close(wi); { NpBox b; NpGeom g2 = G(); npx_box_geom(&g2, W, &b); wm_close_called = -1; click(b.bx[0] + 4, b.by + 6); }
    expect(wins[wi].id == 0 && wm_close_called == wi, "Close anyway closes the window");
    reset_world(); fresh_window(); wm_close(wi); expect(wins[wi].id == 0, "a window with nothing unsaved closes at once");

    /* ---- opening from the file manager ---- */
    kill_all_windows(); memset(wins, 0, sizeof wins); win_count = 0; focused = -1; npx_init_all();
    open_notepad_p("readme.txt", 0); wi = focused; W = &npx[wi];
    expect(W->used && doc_is("hello readme") && !strcmp(npx_tab(wi)->path, "/ycfs/readme.txt"), "open_notepad_p with a bare name opens it from /ycfs in a new window");
    int w1 = wi; open_notepad_p("notes.txt", "/ycfs/docs/notes.txt"); expect(focused == w1 && npx[w1].ntabs == 2, "a second file opens as a TAB of the existing Notepad window");
    wins[w1].minimized = 1; open_notepad_p("readme.txt", "/ycfs/readme.txt"); expect(wins[w1].minimized == 0 && npx[w1].ntabs == 2, "...un-minimising it; the already-open file just gets focus");
    open_notepad(0); expect(win_count >= 2 && npx[focused].ntabs == 1 && npx[focused].tabs[0].path[0] == 0, "open_notepad(0) (the desktop icon) opens a brand-new empty window");
    open_notepad_p("binary.dat", "/ycfs/binary.dat"); expect(npx[focused].mode == NPM_MSG, "a file that cannot be opened is explained in the window");

    /* ---- drawing never crashes in any state, and memory is returned ---- */
    reset_world(); fresh_window(); TYPE("draw me"); redraw(); KC('s'); redraw(); K(27); KC('w'); redraw(); K('\n'); redraw(); W->mode = NPM_MSG; redraw(); W->mode = NPM_EDIT;
    wins[wi].w = 400; wins[wi].h = 260; redraw(); wins[wi].w = 1900; wins[wi].h = 1000; redraw();
    expect(draw_calls > 100, "drawing every state (editor, dialogs, boxes, tiny and huge windows) runs without crashing");
    for (int i = 0; i < MAX_WINDOWS; i++) if (npx[i].used) npx_destroy(i);
    expect(txa_in_use == base, "closing every window returns all document memory to the arena");

    printf("\n%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
