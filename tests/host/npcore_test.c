#include <stdio.h>
#include <string.h>
typedef struct { char name[32]; unsigned int size; unsigned char is_dir; } Dirent;
#include "../../user/src/npcore.h"
static int failures = 0, checks = 0;
#define expect(c, msg) do { checks++; if (c) printf("  ok    %s\n", msg); else { printf("  FAIL  %s\n", msg); failures++; } } while (0)
static Dirent mk(const char* n, int d) { Dirent e; memset(&e, 0, sizeof e); strncpy(e.name, n, 31); e.is_dir = (unsigned char)d; return e; }
int main(void) {
    char b[64];
    printf("== Notepad helpers (npcore.h)\n");
    np_join(b, sizeof b, "/ycfs", "a.txt"); expect(!strcmp(b, "/ycfs/a.txt"), "join: folder + name");
    np_join(b, sizeof b, "/ycfs/", "a.txt"); expect(!strcmp(b, "/ycfs/a.txt"), "join: no doubled slash");
    expect(np_join(b, 8, "/ycfs", "toolong.txt") == -1 && b[0] == 0, "join: refuses what does not fit (and leaves a valid empty string)");
    np_parent(b, sizeof b, "/ycfs/docs/notes"); expect(!strcmp(b, "/ycfs/docs"), "parent of a nested folder");
    np_parent(b, sizeof b, "/ycfs/docs"); expect(!strcmp(b, "/ycfs"), "parent of a top-level folder is the root");
    np_parent(b, sizeof b, "/ycfs"); expect(!strcmp(b, "/ycfs"), "the root has no parent: you cannot browse above it");
    np_parent(b, sizeof b, "/ycfs/docs/"); expect(!strcmp(b, "/ycfs"), "trailing slash tolerated");
    expect(np_at_root("/ycfs") && np_at_root("/ycfs/") && !np_at_root("/ycfs/a"), "root detection");
    expect(!strcmp(np_basename("/ycfs/docs/a.txt"), "a.txt") && !strcmp(np_basename("x"), "x"), "base name");

    expect(np_check_name("notes.txt") == NP_NAME_OK && np_check_name("My File") == NP_NAME_OK && np_check_name(".hidden") == NP_NAME_OK, "ordinary names are fine (spaces and a leading dot too)");
    expect(np_check_name("") == NP_NAME_EMPTY, "empty name refused");
    expect(np_check_name("a/b") == NP_NAME_SLASH && np_check_name("a\\b") == NP_NAME_SLASH, "slashes refused");
    expect(np_check_name("abcdefghijklmnopqrstuvwxyz1234567") == NP_NAME_LONG && np_check_name("abcdefghijklmnopqrstuvwxyz12345") == NP_NAME_OK, "31 characters allowed, 32 refused");
    expect(np_check_name("..") == NP_NAME_DOTS && np_check_name(".") == NP_NAME_DOTS && np_check_name("  ") == NP_NAME_DOTS, "dots-only and blank names refused");
    expect(np_check_name("a\tb") == NP_NAME_CTRL, "control characters refused");
    strcpy(b, "report"); expect(np_default_ext(b, sizeof b) == 1 && !strcmp(b, "report.txt"), "a name without an extension gets .txt");
    strcpy(b, "report.md"); expect(np_default_ext(b, sizeof b) == 0 && !strcmp(b, "report.md"), "a name with an extension is left alone");
    strcpy(b, ".bashrc"); expect(np_default_ext(b, sizeof b) == 1 && !strcmp(b, ".bashrc.txt"), "a leading dot is not an extension");
    strcpy(b, "notes."); expect(np_default_ext(b, sizeof b) == 0, "a trailing dot is respected (no .txt added)");
    strcpy(b, "abcdefghijklmnopqrstuvwxyz123"); expect(np_default_ext(b, sizeof b) == 0 && !strcmp(b, "abcdefghijklmnopqrstuvwxyz123"), "no .txt when it would push the name past 31");

    Dirent e[7] = { mk("zeta.txt", 0), mk("Beta", 1), mk("alpha.txt", 0), mk("beta2", 1), mk("Alpha", 1), mk("Zoo.txt", 0), mk("apple", 0) };
    np_sort_entries(e, 7);
    expect(e[0].is_dir && e[1].is_dir && e[2].is_dir && !e[3].is_dir, "folders first, then files");
    expect(!strcmp(e[0].name, "Alpha") && !strcmp(e[1].name, "Beta") && !strcmp(e[2].name, "beta2"), "folders sorted ignoring case");
    expect(!strcmp(e[3].name, "alpha.txt") && !strcmp(e[4].name, "apple") && !strcmp(e[5].name, "zeta.txt") && !strcmp(e[6].name, "Zoo.txt"), "files sorted ignoring case");
    expect(np_find_entry(e, 7, "apple") == 4 && np_find_entry(e, 7, "nope") == -1, "find by name");

    np_tab_label(b, sizeof b, "notes.txt", 0, 20); expect(!strcmp(b, "notes.txt"), "tab label: fits");
    np_tab_label(b, sizeof b, "notes.txt", 1, 20); expect(!strcmp(b, "notes.txt*"), "tab label: modified marker");
    np_tab_label(b, sizeof b, "a_very_long_file_name.txt", 0, 10); expect(!strcmp(b, "a_very_l.."), "tab label: shortened with ..");
    np_tab_label(b, sizeof b, "a_very_long_file_name.txt", 1, 10); expect(strlen(b) == 10 && b[9] == '*' && !strncmp(b, "a_very_", 7), "tab label: shortened, marker kept within the budget");
    np_tab_label(b, sizeof b, "x", 1, 1); expect(strlen(b) <= 4 && b[strlen(b) - 1] == '*', "tab label: absurdly small budget still produces something sane");

    int ty, th;
    np_thumb(100, 20, 0, 200, 12, &ty, &th); expect(ty == 0 && th == 40, "scrollbar: thumb size is the visible fraction");
    np_thumb(100, 20, 80, 200, 12, &ty, &th); expect(ty + th == 200, "scrollbar: scrolled to the end, the thumb touches the bottom");
    np_thumb(1000, 20, 0, 200, 12, &ty, &th); expect(th == 12, "scrollbar: a tiny thumb stops at its minimum");
    np_thumb(10, 20, 0, 200, 12, &ty, &th); expect(ty == 0 && th == 200, "scrollbar: everything fits, thumb fills the track");
    np_thumb(100, 20, 999, 200, 12, &ty, &th); expect(ty + th == 200, "scrollbar: a scroll past the end is clamped");
    int ok = 1; for (int s = 0; s <= 80; s += 7) { np_thumb(100, 20, s, 200, 12, &ty, &th); int back = np_scroll_from_thumb(100, 20, 200, 12, ty); if (back > s || s - back > 1) ok = 0; }
    expect(ok, "dragging the thumb maps back to the scroll position it came from (within a row)");
    expect(np_scroll_from_thumb(100, 20, 200, 12, -50) == 0 && np_scroll_from_thumb(100, 20, 200, 12, 9999) == 80, "dragging past either end clamps");
    expect(np_scroll_to_show(0, 10, 5, 100) == 0 && np_scroll_to_show(0, 10, 10, 100) == 1 && np_scroll_to_show(50, 10, 20, 100) == 20, "keeping the cursor row visible");
    expect(np_scroll_to_show(0, 10, 99, 100) == 90 && np_scroll_to_show(40, 10, 3, 5) == 0, "never scrolls past the end; short text does not scroll");

    expect(np_click_count(0, 0, 0, 0, 100, 50, 50, 40) == 1, "first click is a single click");
    expect(np_click_count(1, 100, 50, 50, 120, 52, 51, 40) == 2, "a second click soon after, in the same place: double");
    expect(np_click_count(2, 120, 52, 51, 140, 52, 51, 40) == 3, "...third: triple");
    expect(np_click_count(3, 140, 52, 51, 150, 52, 51, 40) == 1, "...a fourth starts over");
    expect(np_click_count(1, 100, 50, 50, 200, 50, 50, 40) == 1, "too slow: single again");
    expect(np_click_count(1, 100, 50, 50, 110, 80, 50, 40) == 1, "moved away: single again");
    np_untitled(b, sizeof b, 1); expect(!strcmp(b, "Untitled"), "untitled name"); np_untitled(b, sizeof b, 12); expect(!strcmp(b, "Untitled 12"), "...numbered");
    printf("\n%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
