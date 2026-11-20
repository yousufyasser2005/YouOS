/* ctxmenu.h -- the logic of the small right-click menu for text (Undo / Redo / Cut / Copy / Paste / Select All).
 * No drawing and no system calls: the desktop draws the menu and carries out the chosen command; this file
 * decides which items a menu has, which are enabled, where it sits (always fully on screen) and which item a
 * point is on. It is compiled into the host test (tests/host/ctxmenu_test.c).
 *
 * Three kinds of menu:
 *   CMK_EDITOR    a text editor (Notepad):  Undo, Redo | Cut, Copy, Paste | Select All
 *   CMK_TERMINAL  a terminal window:        Copy, Paste | Select All     (the output cannot be edited)
 *   CMK_READONLY  read-only text:           Copy | Select All
 *   CMK_NOTIF     a notification:           Copy, Copy All | Dismiss, Clear All   (`win` is the notification's index)
 * A command that cannot be done right now (Copy with nothing selected, Paste with an empty clipboard ...)
 * is shown dimmed and does nothing when clicked. */
#ifndef CTXMENU_H
#define CTXMENU_H

#define CM_W      224            /* wide enough for "Paste" + "Ctrl+Shift+V" */
#define CM_ITEM_H 22
#define CM_SEP_H  6
#define CM_MAX    10

enum { CM_NONE = 0, CM_UNDO, CM_REDO, CM_CUT, CM_COPY, CM_PASTE, CM_SELALL, CM_SEP, CM_COPYALL, CM_DISMISS, CM_CLEARALL };
enum { CMK_NONE = 0, CMK_EDITOR, CMK_TERMINAL, CMK_READONLY, CMK_NOTIF };

typedef struct {
    int open;
    int x, y;                    /* top-left corner on screen (already moved so the menu fits) */
    int hov;                     /* item under the pointer, -1 none */
    int kind;                    /* CMK_* */
    int win;                     /* the window the menu belongs to (CMK_NOTIF: the notification's index) */
    int aux;                     /* free for the owner (CMK_NOTIF: how many notifications existed when it opened) */
    int n;                       /* number of entries, separators included */
    int id[CM_MAX];              /* CM_* of each entry */
    int en[CM_MAX];              /* 1 if the entry can be chosen now */
} CtxMenu;

static void cm_close(CtxMenu* m) { m->open = 0; m->hov = -1; }

static const char* cm_label(int id) {
    switch (id) {
    case CM_UNDO: return "Undo";   case CM_REDO: return "Redo";
    case CM_CUT: return "Cut";     case CM_COPY: return "Copy";
    case CM_PASTE: return "Paste"; case CM_SELALL: return "Select All";
    case CM_COPYALL: return "Copy All"; case CM_DISMISS: return "Dismiss"; case CM_CLEARALL: return "Clear All";
    default: return "";
    }
}
/* the keyboard shortcut shown at the right edge of an entry */
static const char* cm_hint(int id, int kind) {
    if (kind == CMK_NOTIF) return "";
    if (kind == CMK_TERMINAL) {
        if (id == CM_COPY) return "Ctrl+Shift+C";
        if (id == CM_PASTE) return "Ctrl+Shift+V";
        return "";
    }
    if (kind == CMK_READONLY) return "";
    switch (id) {
    case CM_UNDO: return "Ctrl+Z";  case CM_REDO: return "Ctrl+Y";
    case CM_CUT: return "Ctrl+X";   case CM_COPY: return "Ctrl+C";
    case CM_PASTE: return "Ctrl+V"; case CM_SELALL: return "Ctrl+A";
    default: return "";
    }
}

static void cm_add(CtxMenu* m, int id, int enabled) {
    if (m->n >= CM_MAX) return;
    m->id[m->n] = id; m->en[m->n] = (id == CM_SEP) ? 0 : (enabled ? 1 : 0); m->n++;
}
/* Fills the menu (does not open it or place it). */
static void cm_build(CtxMenu* m, int kind, int win, int has_sel, int can_undo, int can_redo, int can_paste, int has_text) {
    m->n = 0; m->hov = -1; m->kind = kind; m->win = win;
    if (kind == CMK_EDITOR) {
        cm_add(m, CM_UNDO, can_undo); cm_add(m, CM_REDO, can_redo); cm_add(m, CM_SEP, 0);
        cm_add(m, CM_CUT, has_sel); cm_add(m, CM_COPY, has_sel); cm_add(m, CM_PASTE, can_paste); cm_add(m, CM_SEP, 0);
        cm_add(m, CM_SELALL, has_text);
    } else if (kind == CMK_TERMINAL) {
        cm_add(m, CM_COPY, has_sel); cm_add(m, CM_PASTE, can_paste); cm_add(m, CM_SEP, 0);
        cm_add(m, CM_SELALL, has_text);
    } else if (kind == CMK_READONLY) {
        cm_add(m, CM_COPY, has_sel); cm_add(m, CM_SEP, 0);
        cm_add(m, CM_SELALL, has_text);
    }
}
/* A notification menu. `idx` is the notification, `total` how many there are: "Copy All" only means something
 * when there is more than one. */
static void cm_build_notif(CtxMenu* m, int idx, int total) {
    m->n = 0; m->hov = -1; m->kind = CMK_NOTIF; m->win = idx; m->aux = total;
    cm_add(m, CM_COPY, 1); cm_add(m, CM_COPYALL, total >= 2); cm_add(m, CM_SEP, 0);
    cm_add(m, CM_DISMISS, 1); cm_add(m, CM_CLEARALL, 1);
}
static int cm_height(const CtxMenu* m) {
    int h = 2; for (int i = 0; i < m->n; i++) h += (m->id[i] == CM_SEP) ? CM_SEP_H : CM_ITEM_H; return h;
}
/* The menu is opened at (x, y) -- the pointer -- but is moved so that all of it stays inside the area
 * [0, max_x) x [0, max_y). */
static void cm_place(CtxMenu* m, int x, int y, int max_x, int max_y) {
    int h = cm_height(m);
    if (x + CM_W > max_x) x = max_x - CM_W;
    if (y + h > max_y) y = max_y - h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    m->x = x; m->y = y;
}
/* top edge of entry i */
static int cm_row_y(const CtxMenu* m, int i) {
    int y = m->y + 1; for (int k = 0; k < i && k < m->n; k++) y += (m->id[k] == CM_SEP) ? CM_SEP_H : CM_ITEM_H; return y;
}
static int cm_inside(const CtxMenu* m, int px, int py) {
    return px >= m->x && px < m->x + CM_W && py >= m->y && py < m->y + cm_height(m);
}
/* the entry under the point, or -1 (outside the menu, on a separator, or on the border) */
static int cm_item_at(const CtxMenu* m, int px, int py) {
    if (!cm_inside(m, px, py)) return -1;
    for (int i = 0; i < m->n; i++) {
        if (m->id[i] == CM_SEP) continue;
        int y = cm_row_y(m, i);
        if (py >= y && py < y + CM_ITEM_H) return i;
    }
    return -1;
}

#endif
