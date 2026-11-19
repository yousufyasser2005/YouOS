#include <stdio.h>
#include <string.h>
#include "../../user/src/ctxmenu.h"
static int failures = 0, checks = 0;
#define expect(c, msg) do { checks++; if (c) printf("  ok    %s\n", msg); else { printf("  FAIL  %s\n", msg); failures++; } } while (0)

static int ids_are(const CtxMenu* m, int n, const int* want) { if (m->n != n) return 0; for (int i = 0; i < n; i++) if (m->id[i] != want[i]) return 0; return 1; }

int main(void) {
    setbuf(stdout, 0);
    printf("== right-click menu logic (ctxmenu.h)\n");
    CtxMenu m; memset(&m, 0, sizeof m);

    /* ---- what each kind of menu contains ---- */
    cm_build(&m, CMK_EDITOR, 3, 1, 1, 1, 1, 1);
    { int w[] = { CM_UNDO, CM_REDO, CM_SEP, CM_CUT, CM_COPY, CM_PASTE, CM_SEP, CM_SELALL };
      expect(ids_are(&m, 8, w), "editor menu: Undo, Redo | Cut, Copy, Paste | Select All"); }
    expect(m.kind == CMK_EDITOR && m.win == 3 && m.hov == -1, "the menu remembers its kind and its window");
    { int ok = 1; for (int i = 0; i < m.n; i++) ok &= (m.id[i] == CM_SEP) ? (m.en[i] == 0) : (m.en[i] == 1); expect(ok, "everything possible: every command enabled, separators never"); }
    cm_build(&m, CMK_EDITOR, 0, 0, 0, 0, 0, 0);
    { int ok = 1; for (int i = 0; i < m.n; i++) ok &= (m.en[i] == 0); expect(ok, "nothing possible: every entry disabled (an empty document with an empty clipboard)"); }
    cm_build(&m, CMK_EDITOR, 0, 1, 0, 0, 0, 1);
    expect(m.en[0] == 0 && m.en[1] == 0 && m.en[3] == 1 && m.en[4] == 1 && m.en[5] == 0 && m.en[7] == 1, "a selection enables Cut and Copy only (plus Select All when there is text)");
    cm_build(&m, CMK_EDITOR, 0, 0, 1, 0, 0, 1);
    expect(m.en[0] == 1 && m.en[1] == 0 && m.en[3] == 0 && m.en[4] == 0, "undo history enables Undo only; Redo stays off");
    cm_build(&m, CMK_EDITOR, 0, 0, 0, 1, 1, 1);
    expect(m.en[0] == 0 && m.en[1] == 1 && m.en[5] == 1, "a redo step enables Redo; clipboard text enables Paste");

    cm_build(&m, CMK_TERMINAL, 7, 1, 1, 1, 1, 1);
    { int w[] = { CM_COPY, CM_PASTE, CM_SEP, CM_SELALL }; expect(ids_are(&m, 4, w), "terminal menu: Copy, Paste | Select All (no Undo, Cut or Redo: output cannot be edited)"); }
    cm_build(&m, CMK_TERMINAL, 7, 0, 0, 0, 1, 1);
    expect(m.en[0] == 0 && m.en[1] == 1 && m.en[3] == 1, "terminal with no selection: Copy off, Paste on");
    cm_build(&m, CMK_TERMINAL, 7, 1, 0, 0, 0, 0);
    expect(m.en[0] == 1 && m.en[1] == 0 && m.en[3] == 0, "terminal with a selection but an empty clipboard: Copy on, Paste off");
    cm_build(&m, CMK_READONLY, 2, 1, 0, 0, 1, 1);
    { int w[] = { CM_COPY, CM_SEP, CM_SELALL }; expect(ids_are(&m, 3, w), "read-only menu: Copy | Select All"); }
    cm_build(&m, 99, 0, 1, 1, 1, 1, 1); expect(m.n == 0, "an unknown kind has no entries");
    cm_build(&m, CMK_NONE, 0, 1, 1, 1, 1, 1); expect(m.n == 0, "kind NONE has no entries");
    { CtxMenu f; memset(&f, 0, sizeof f); for (int i = 0; i < 30; i++) cm_add(&f, CM_COPY, 1); expect(f.n == CM_MAX, "adding more than the maximum is refused, never overflows"); }
    { CtxMenu f; memset(&f, 0, sizeof f); cm_add(&f, CM_SEP, 1); cm_add(&f, CM_COPY, 1); cm_add(&f, CM_COPY, 0); expect(f.n == 3 && f.en[0] == 0 && f.en[1] == 1 && f.en[2] == 0, "a separator can never be enabled, even if asked; other entries keep what they are given"); }

    /* ---- labels and shortcut hints ---- */
    expect(!strcmp(cm_label(CM_UNDO), "Undo") && !strcmp(cm_label(CM_REDO), "Redo") && !strcmp(cm_label(CM_CUT), "Cut") && !strcmp(cm_label(CM_COPY), "Copy") && !strcmp(cm_label(CM_PASTE), "Paste") && !strcmp(cm_label(CM_SELALL), "Select All"), "every command has its label");
    expect(!strcmp(cm_label(CM_SEP), "") && !strcmp(cm_label(CM_NONE), ""), "separators have no label");
    expect(!strcmp(cm_hint(CM_COPY, CMK_EDITOR), "Ctrl+C") && !strcmp(cm_hint(CM_PASTE, CMK_EDITOR), "Ctrl+V") && !strcmp(cm_hint(CM_UNDO, CMK_EDITOR), "Ctrl+Z") && !strcmp(cm_hint(CM_REDO, CMK_EDITOR), "Ctrl+Y") && !strcmp(cm_hint(CM_CUT, CMK_EDITOR), "Ctrl+X") && !strcmp(cm_hint(CM_SELALL, CMK_EDITOR), "Ctrl+A"), "editor hints are the editor shortcuts");
    expect(!strcmp(cm_hint(CM_COPY, CMK_TERMINAL), "Ctrl+Shift+C") && !strcmp(cm_hint(CM_PASTE, CMK_TERMINAL), "Ctrl+Shift+V"), "terminal hints are Ctrl+Shift+C / Ctrl+Shift+V (Ctrl+C is the interrupt there)");
    expect(!strcmp(cm_hint(CM_SELALL, CMK_TERMINAL), "") && !strcmp(cm_hint(CM_COPY, CMK_READONLY), ""), "entries without a shortcut show no hint");
    { int widest = 0; const char* h = cm_hint(CM_PASTE, CMK_TERMINAL); int hl = (int)strlen(h), ll = (int)strlen(cm_label(CM_SELALL));
      widest = (hl > ll ? hl : ll); expect((ll + hl) * 8 + 10 + 10 + 16 <= CM_W && widest > 0, "the widest label plus the widest hint fit in the menu width"); }

    /* ---- size and placement ---- */
    cm_build(&m, CMK_EDITOR, 0, 1, 1, 1, 1, 1);
    expect(cm_height(&m) == 2 + 6 * CM_ITEM_H + 2 * CM_SEP_H, "editor menu height: border + 6 entries + 2 separators");
    cm_build(&m, CMK_TERMINAL, 0, 1, 1, 1, 1, 1); expect(cm_height(&m) == 2 + 3 * CM_ITEM_H + CM_SEP_H, "terminal menu height");
    cm_build(&m, CMK_READONLY, 0, 1, 1, 1, 1, 1); expect(cm_height(&m) == 2 + 2 * CM_ITEM_H + CM_SEP_H, "read-only menu height");
    cm_build(&m, CMK_EDITOR, 0, 1, 1, 1, 1, 1);
    cm_place(&m, 100, 200, 1920, 1000); expect(m.x == 100 && m.y == 200, "a menu that fits opens exactly at the pointer");
    cm_place(&m, 1900, 200, 1920, 1000); expect(m.x == 1920 - CM_W && m.y == 200, "near the right edge it is moved left so it fits");
    cm_place(&m, 100, 990, 1920, 1000); expect(m.x == 100 && m.y == 1000 - cm_height(&m), "near the bottom edge it is moved up so it fits");
    cm_place(&m, 1919, 999, 1920, 1000); expect(m.x == 1920 - CM_W && m.y == 1000 - cm_height(&m), "in the bottom-right corner it is moved both ways");
    cm_place(&m, -50, -50, 1920, 1000); expect(m.x == 0 && m.y == 0, "a pointer left of / above the area never puts the menu off screen");
    cm_place(&m, 5, 5, 100, 50); expect(m.x == 0 && m.y == 0, "an area smaller than the menu: the menu starts at the corner");
    cm_place(&m, 1920 - CM_W, 500, 1920, 1000); expect(m.x == 1920 - CM_W, "a menu that exactly touches the right edge is not moved");

    /* ---- hit testing ---- */
    cm_place(&m, 400, 300, 1920, 1000);
    expect(cm_row_y(&m, 0) == 301 && cm_row_y(&m, 1) == 301 + CM_ITEM_H, "rows start one pixel below the top border");
    expect(cm_row_y(&m, 3) == 301 + 2 * CM_ITEM_H + CM_SEP_H, "a separator takes less room than an entry");
    expect(cm_row_y(&m, 7) == 301 + 5 * CM_ITEM_H + 2 * CM_SEP_H, "the last entry sits below five entries and two separators");
    expect(cm_item_at(&m, 410, 301 + 5) == 0 && cm_item_at(&m, 410, 301 + CM_ITEM_H + 5) == 1, "the first two entries are found by their rows");
    expect(cm_item_at(&m, 410, cm_row_y(&m, 2) + 2) == -1, "a separator is not an entry");
    expect(cm_item_at(&m, 410, cm_row_y(&m, 3) + 2) == 3 && cm_item_at(&m, 410, cm_row_y(&m, 7) + CM_ITEM_H - 1) == 7, "entries after a separator are found, down to their last pixel");
    expect(cm_item_at(&m, 410, cm_row_y(&m, 7) + CM_ITEM_H) == -1, "the pixel row after the last entry is the border: nothing");
    expect(cm_item_at(&m, 410, 300) == -1, "the top border is not an entry");
    expect(cm_item_at(&m, 399, 310) == -1 && cm_item_at(&m, 400 + CM_W, 310) == -1, "left and right of the menu: nothing");
    expect(cm_item_at(&m, 400, 310) == 0 && cm_item_at(&m, 400 + CM_W - 1, 310) == 0, "the very first and last columns belong to the menu");
    expect(cm_item_at(&m, 410, 100) == -1 && cm_item_at(&m, 410, 900) == -1, "far above / below: nothing");
    expect(cm_inside(&m, 410, 310) && !cm_inside(&m, 410, 100) && cm_inside(&m, 410, cm_row_y(&m, 2) + 1), "inside covers the separators too, outside does not");
    { CtxMenu d; memset(&d, 0, sizeof d); cm_build(&d, CMK_EDITOR, 0, 0, 0, 0, 0, 1); cm_place(&d, 400, 300, 1920, 1000);
      int i = cm_item_at(&d, 410, cm_row_y(&d, 3) + 2); expect(i == 3 && d.en[i] == 0, "a disabled entry is still found (the caller checks it is enabled)"); }

    /* ---- opening and closing ---- */
    m.open = 1; m.hov = 4; cm_close(&m); expect(m.open == 0 && m.hov == -1, "closing clears open and the highlight");

    printf("\n%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
