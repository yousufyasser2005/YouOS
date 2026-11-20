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
    { int w[] = { CM_UNDO, CM_REDO, CM_SEP, CM_CUT, CM_COPY, CM_PASTE, CM_HISTORY, CM_SEP, CM_SELALL };
      expect(ids_are(&m, 9, w), "editor menu: Undo, Redo | Cut, Copy, Paste, Clipboard History | Select All"); }
    expect(m.kind == CMK_EDITOR && m.win == 3 && m.hov == -1, "the menu remembers its kind and its window");
    { int ok = 1; for (int i = 0; i < m.n; i++) ok &= (m.id[i] == CM_SEP) ? (m.en[i] == 0) : (m.en[i] == 1); expect(ok, "everything possible: every command enabled, separators never"); }
    cm_build(&m, CMK_EDITOR, 0, 0, 0, 0, 0, 0);
    expect(m.en[6] == 1 && m.id[6] == CM_HISTORY, "Clipboard History is enabled by default (the owner dims it while the history is empty)");
    cm_set_enabled(&m, CM_HISTORY, 0);
    { int ok = 1; for (int i = 0; i < m.n; i++) ok &= (m.en[i] == 0); expect(ok, "nothing possible: every entry disabled (an empty document, an empty clipboard, an empty history)"); }
    cm_set_enabled(&m, CM_HISTORY, 1); expect(m.en[6] == 1 && m.en[0] == 0 && m.en[3] == 0 && m.en[5] == 0 && m.en[8] == 0, "cm_set_enabled changes only the entries with that id");
    cm_set_enabled(&m, CM_SEP, 1); expect(m.en[2] == 1, "(it does not know about separators: the caller never asks)");
    cm_set_enabled(&m, CM_REDO, 7); expect(m.en[1] == 1, "any non-zero value means enabled");
    cm_set_enabled(&m, 999, 1); expect(m.n == 9, "an id that is not in the menu changes nothing");
    cm_build(&m, CMK_EDITOR, 0, 1, 0, 0, 0, 1);
    expect(m.en[0] == 0 && m.en[1] == 0 && m.en[3] == 1 && m.en[4] == 1 && m.en[5] == 0 && m.en[8] == 1, "a selection enables Cut and Copy only (plus Select All when there is text)");
    cm_build(&m, CMK_EDITOR, 0, 0, 1, 0, 0, 1);
    expect(m.en[0] == 1 && m.en[1] == 0 && m.en[3] == 0 && m.en[4] == 0, "undo history enables Undo only; Redo stays off");
    cm_build(&m, CMK_EDITOR, 0, 0, 0, 1, 1, 1);
    expect(m.en[0] == 0 && m.en[1] == 1 && m.en[5] == 1, "a redo step enables Redo; clipboard text enables Paste");

    cm_build(&m, CMK_TERMINAL, 7, 1, 1, 1, 1, 1);
    { int w[] = { CM_COPY, CM_PASTE, CM_HISTORY, CM_SEP, CM_SELALL }; expect(ids_are(&m, 5, w), "terminal menu: Copy, Paste, Clipboard History | Select All (no Undo, Cut or Redo: output cannot be edited)"); }
    cm_build(&m, CMK_TERMINAL, 7, 0, 0, 0, 1, 1);
    expect(m.en[0] == 0 && m.en[1] == 1 && m.en[2] == 1 && m.en[4] == 1, "terminal with no selection: Copy off, Paste on, History on");
    cm_build(&m, CMK_TERMINAL, 7, 1, 0, 0, 0, 0);
    expect(m.en[0] == 1 && m.en[1] == 0 && m.en[2] == 1 && m.en[4] == 0, "terminal with a selection but an empty clipboard: Copy on, Paste off (History is independent of the clipboard)");
    cm_build(&m, CMK_READONLY, 2, 1, 0, 0, 1, 1);
    { int w[] = { CM_COPY, CM_SEP, CM_SELALL }; expect(ids_are(&m, 3, w), "read-only menu: Copy | Select All"); }
    cm_build(&m, 99, 0, 1, 1, 1, 1, 1); expect(m.n == 0, "an unknown kind has no entries");
    cm_build(&m, CMK_NONE, 0, 1, 1, 1, 1, 1); expect(m.n == 0, "kind NONE has no entries");
    { CtxMenu f; memset(&f, 0, sizeof f); for (int i = 0; i < 30; i++) cm_add(&f, CM_COPY, 1); expect(f.n == CM_MAX, "adding more than the maximum is refused, never overflows"); }
    { CtxMenu f; memset(&f, 0, sizeof f); cm_add(&f, CM_SEP, 1); cm_add(&f, CM_COPY, 1); cm_add(&f, CM_COPY, 0); expect(f.n == 3 && f.en[0] == 0 && f.en[1] == 1 && f.en[2] == 0, "a separator can never be enabled, even if asked; other entries keep what they are given"); }

    /* ---- the notification menu ---- */
    cm_build_notif(&m, 4, 7);
    { int w[] = { CM_COPY, CM_COPYALL, CM_SEP, CM_DISMISS, CM_CLEARALL }; expect(ids_are(&m, 5, w), "notification menu: Copy, Copy All | Dismiss, Clear All"); }
    expect(m.kind == CMK_NOTIF && m.win == 4 && m.aux == 7 && m.hov == -1, "it remembers which notification (win) and how many there were (aux)");
    expect(m.en[0] == 1 && m.en[1] == 1 && m.en[2] == 0 && m.en[3] == 1 && m.en[4] == 1, "with several notifications every command is enabled");
    cm_build_notif(&m, 0, 2); expect(m.en[1] == 1, "with exactly two notifications Copy All is enabled (the boundary)");
    cm_build_notif(&m, 0, 1);
    expect(m.en[0] == 1 && m.en[1] == 0 && m.en[3] == 1 && m.en[4] == 1, "with a single notification Copy All is dimmed (it would equal Copy)");
    expect(!strcmp(cm_label(CM_COPYALL), "Copy All") && !strcmp(cm_label(CM_DISMISS), "Dismiss") && !strcmp(cm_label(CM_CLEARALL), "Clear All"), "the new commands have their labels");
    expect(!strcmp(cm_hint(CM_COPY, CMK_NOTIF), "") && !strcmp(cm_hint(CM_DISMISS, CMK_NOTIF), "") && !strcmp(cm_hint(CM_COPYALL, CMK_NOTIF), ""), "notification entries show no shortcut hints");
    expect(cm_height(&m) == 2 + 4 * CM_ITEM_H + CM_SEP_H, "notification menu height: border + 4 entries + 1 separator");
    cm_place(&m, 100, 100, 1920, 1000);
    expect(cm_item_at(&m, 110, cm_row_y(&m, 3) + 3) == 3 && cm_item_at(&m, 110, cm_row_y(&m, 2) + 1) == -1, "Dismiss is found below the separator; the separator is not an entry");
    cm_build(&m, CMK_EDITOR, 5, 1, 1, 1, 1, 1); expect(m.kind == CMK_EDITOR, "building another kind afterwards replaces the notification menu completely");

    /* ---- labels and shortcut hints ---- */
    expect(!strcmp(cm_label(CM_UNDO), "Undo") && !strcmp(cm_label(CM_REDO), "Redo") && !strcmp(cm_label(CM_CUT), "Cut") && !strcmp(cm_label(CM_COPY), "Copy") && !strcmp(cm_label(CM_PASTE), "Paste") && !strcmp(cm_label(CM_SELALL), "Select All") && !strcmp(cm_label(CM_HISTORY), "Clipboard History"), "every command has its label");
    expect(!strcmp(cm_label(CM_SEP), "") && !strcmp(cm_label(CM_NONE), ""), "separators have no label");
    expect(!strcmp(cm_hint(CM_COPY, CMK_EDITOR), "Ctrl+C") && !strcmp(cm_hint(CM_PASTE, CMK_EDITOR), "Ctrl+V") && !strcmp(cm_hint(CM_UNDO, CMK_EDITOR), "Ctrl+Z") && !strcmp(cm_hint(CM_REDO, CMK_EDITOR), "Ctrl+Y") && !strcmp(cm_hint(CM_CUT, CMK_EDITOR), "Ctrl+X") && !strcmp(cm_hint(CM_SELALL, CMK_EDITOR), "Ctrl+A"), "editor hints are the editor shortcuts");
    expect(!strcmp(cm_hint(CM_COPY, CMK_TERMINAL), "Ctrl+Shift+C") && !strcmp(cm_hint(CM_PASTE, CMK_TERMINAL), "Ctrl+Shift+V"), "terminal hints are Ctrl+Shift+C / Ctrl+Shift+V (Ctrl+C is the interrupt there)");
    expect(!strcmp(cm_hint(CM_SELALL, CMK_TERMINAL), "") && !strcmp(cm_hint(CM_COPY, CMK_READONLY), ""), "entries without a shortcut show no hint");
    expect(!strcmp(cm_hint(CM_HISTORY, CMK_EDITOR), "Ctrl+Shift+H") && !strcmp(cm_hint(CM_HISTORY, CMK_TERMINAL), "Ctrl+Shift+H"), "Clipboard History shows its shortcut in both kinds of menu");
    { int worst = 0; const int kinds[] = { CMK_EDITOR, CMK_TERMINAL }; const int ids[] = { CM_UNDO, CM_REDO, CM_CUT, CM_COPY, CM_PASTE, CM_SELALL, CM_HISTORY };
      for (int k = 0; k < 2; k++) for (int q = 0; q < 7; q++) { int w = ((int)strlen(cm_label(ids[q])) + (int)strlen(cm_hint(ids[q], kinds[k]))) * 8; if (w > worst) worst = w; }
      expect(worst + 10 + 10 + 16 <= CM_W && worst + 10 + 10 + 16 > CM_W - 40, "the widest label + hint pair of any entry fits the width with margins and a gap, and the width is not wasteful"); }

    /* ---- size and placement ---- */
    cm_build(&m, CMK_EDITOR, 0, 1, 1, 1, 1, 1);
    expect(cm_height(&m) == 2 + 7 * CM_ITEM_H + 2 * CM_SEP_H, "editor menu height: border + 7 entries + 2 separators");
    cm_build(&m, CMK_TERMINAL, 0, 1, 1, 1, 1, 1); expect(cm_height(&m) == 2 + 4 * CM_ITEM_H + CM_SEP_H, "terminal menu height");
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
    expect(cm_row_y(&m, 8) == 301 + 6 * CM_ITEM_H + 2 * CM_SEP_H, "the last entry sits below six entries and two separators");
    expect(cm_item_at(&m, 410, 301 + 5) == 0 && cm_item_at(&m, 410, 301 + CM_ITEM_H + 5) == 1, "the first two entries are found by their rows");
    expect(cm_item_at(&m, 410, cm_row_y(&m, 2) + 2) == -1, "a separator is not an entry");
    expect(cm_item_at(&m, 410, cm_row_y(&m, 3) + 2) == 3 && cm_item_at(&m, 410, cm_row_y(&m, 8) + CM_ITEM_H - 1) == 8, "entries after a separator are found, down to their last pixel");
    expect(cm_item_at(&m, 410, cm_row_y(&m, 8) + CM_ITEM_H) == -1, "the pixel row after the last entry is the border: nothing");
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
