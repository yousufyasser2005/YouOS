/* npcore.h -- the pure logic around the Notepad window: paths, file name rules, directory listing
 * order, tab labels, scrollbar and double-click arithmetic. No drawing, no system calls, so it is
 * compiled into the host test (tests/host/npcore_test.c). Needs `Dirent` (name[32], size, is_dir). */
#ifndef NPCORE_H
#define NPCORE_H

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC push_options
#pragma GCC optimize ("no-tree-loop-distribute-patterns")
#endif

#define NP_ROOT      "/ycfs"
#define NP_PATH_MAX  220
#define NP_NAME_MAX  31          /* the directory listing can show names up to 31 characters */

static int np_len(const char* s) { int n = 0; while (s[n]) n++; return n; }
static int np_eq(const char* a, const char* b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static void np_cpy(char* d, const char* s, int cap) { int i = 0; while (s[i] && i < cap - 1) { d[i] = s[i]; i++; } d[i] = 0; }
static char np_lc(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

/* "dir" + "name" -> "dir/name" (no doubled slash). Returns 0, or -1 if it does not fit. */
static int np_join(char* dst, int cap, const char* dir, const char* name) {
    int n = np_len(dir), m = np_len(name);
    int slash = (n > 0 && dir[n - 1] != '/') ? 1 : 0;
    if (n + slash + m + 1 > cap) { if (cap > 0) dst[0] = 0; return -1; }
    int o = 0; for (int i = 0; i < n; i++) dst[o++] = dir[i];
    if (slash) dst[o++] = '/';
    for (int i = 0; i < m; i++) dst[o++] = name[i];
    dst[o] = 0; return 0;
}
/* the folder containing `path`; the root has no parent and stays itself */
static void np_parent(char* dst, int cap, const char* path) {
    int n = np_len(path); while (n > 1 && path[n - 1] == '/') n--;
    int k = n; while (k > 0 && path[k - 1] != '/') k--;
    if (k <= 1) { np_cpy(dst, NP_ROOT, cap); return; }
    int o = 0; for (int i = 0; i < k - 1 && o < cap - 1; i++) dst[o++] = path[i]; dst[o] = 0;
    int r = np_len(NP_ROOT);
    if (np_len(dst) < r) np_cpy(dst, NP_ROOT, cap);              /* never above the root of the browsable tree */
}
static int np_at_root(const char* dir) { return np_eq(dir, NP_ROOT) || np_eq(dir, NP_ROOT "/"); }
/* the last path component */
static const char* np_basename(const char* path) { int n = np_len(path); int k = n; while (k > 0 && path[k - 1] != '/') k--; return path + k; }

enum { NP_NAME_OK = 0, NP_NAME_EMPTY = 1, NP_NAME_SLASH = 2, NP_NAME_LONG = 3, NP_NAME_DOTS = 4, NP_NAME_CTRL = 5 };
static int np_check_name(const char* n) {
    int len = np_len(n);
    if (len == 0) return NP_NAME_EMPTY;
    if (len > NP_NAME_MAX) return NP_NAME_LONG;
    int dots = 1;
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)n[i];
        if (c == '/' || c == '\\') return NP_NAME_SLASH;
        if (c < 32 || c == 127) return NP_NAME_CTRL;
        if (c != '.' && c != ' ') dots = 0;
    }
    return dots ? NP_NAME_DOTS : NP_NAME_OK;
}
static const char* np_name_error(int e) {
    switch (e) {
        case NP_NAME_EMPTY: return "Type a file name.";
        case NP_NAME_SLASH: return "A file name cannot contain / or \\";
        case NP_NAME_LONG:  return "File name too long (31 characters at most).";
        case NP_NAME_DOTS:  return "That is not a valid file name.";
        case NP_NAME_CTRL:  return "A file name cannot contain control characters.";
    }
    return "";
}
/* A name without an extension gets ".txt" (like every other Notepad), as long as it still fits. Returns 1 if added. */
static int np_default_ext(char* name, int cap) {
    int n = np_len(name);
    for (int i = 1; i < n; i++) if (name[i] == '.') return 0;          /* has an extension (a leading dot does not count) */
    if (n > 0 && name[n - 1] == '.') return 0;
    if (n + 4 > NP_NAME_MAX || n + 4 >= cap) return 0;
    name[n] = '.'; name[n + 1] = 't'; name[n + 2] = 'x'; name[n + 3] = 't'; name[n + 4] = 0;
    return 1;
}

/* folders first, then files; each group by name, ignoring case (insertion sort: lists are short) */
static int np_entry_before(const Dirent* a, const Dirent* b) {
    if (a->is_dir != b->is_dir) return a->is_dir > b->is_dir;
    for (int i = 0; i < 32; i++) {
        char x = np_lc(a->name[i]), y = np_lc(b->name[i]);
        if (x != y) return x < y;
        if (!x) return 0;
    }
    return 0;
}
static void np_sort_entries(Dirent* e, int n) {
    for (int i = 1; i < n; i++) {
        Dirent t = e[i]; int j = i - 1;
        while (j >= 0 && np_entry_before(&t, &e[j])) { e[j + 1] = e[j]; j--; }
        e[j + 1] = t;
    }
}
static int np_find_entry(const Dirent* e, int n, const char* name) {
    for (int i = 0; i < n; i++) if (np_eq(e[i].name, name)) return i;
    return -1;
}

/* Tab label: the name, a '*' when modified, shortened with ".." in the middle of the budget if it does not fit. */
static void np_tab_label(char* dst, int cap, const char* name, int modified, int max_chars) {
    int n = np_len(name), extra = modified ? 1 : 0;
    if (max_chars > cap - 1) max_chars = cap - 1;
    if (max_chars < 3) max_chars = 3;
    int o = 0;
    if (n + extra <= max_chars) { for (int i = 0; i < n; i++) dst[o++] = name[i]; }
    else { int keep = max_chars - extra - 2; if (keep < 1) keep = 1; for (int i = 0; i < keep; i++) dst[o++] = name[i]; dst[o++] = '.'; dst[o++] = '.'; }
    if (modified) dst[o++] = '*';
    dst[o] = 0;
}

/* Scrollbar: thumb position and size for `total` rows of which `visible` are shown, scrolled to `scroll`,
 * in a track `track` pixels high (the thumb is never smaller than `minthumb`). */
static void np_thumb(int total, int visible, int scroll, int track, int minthumb, int* ty, int* th) {
    if (total <= visible || total <= 0) { *ty = 0; *th = track; return; }
    int h = (int)((long long)track * visible / total); if (h < minthumb) h = minthumb; if (h > track) h = track;
    int range = total - visible, travel = track - h;
    if (scroll < 0) scroll = 0; if (scroll > range) scroll = range;
    *th = h; *ty = range > 0 ? (int)((long long)travel * scroll / range) : 0;
}
static int np_scroll_from_thumb(int total, int visible, int track, int minthumb, int thumb_top) {
    int ty, th; np_thumb(total, visible, 0, track, minthumb, &ty, &th);
    int travel = track - th, range = total - visible;
    if (total <= visible || travel <= 0) return 0;
    if (thumb_top < 0) thumb_top = 0; if (thumb_top > travel) thumb_top = travel;
    return (int)((long long)range * thumb_top / travel);
}
/* the scroll position that keeps display row `row` on screen (rows are 0-based, `visible` rows fit) */
static int np_scroll_to_show(int scroll, int visible, int row, int total) {
    if (visible < 1) visible = 1;
    if (row < scroll) scroll = row;
    else if (row >= scroll + visible) scroll = row - visible + 1;
    int max = total - visible; if (max < 0) max = 0;
    if (scroll > max) scroll = max;
    if (scroll < 0) scroll = 0;
    return scroll;
}
/* click counting: 1 single, 2 double, 3 triple. A click continues the series if it comes soon after the
 * last one (`window` ticks) and within 4 pixels of it. */
static int np_click_count(int prev_count, long long last_tick, int last_x, int last_y, long long now, int x, int y, int window) {
    int dx = x - last_x, dy = y - last_y;
    if (dx < 0) dx = -dx; if (dy < 0) dy = -dy;
    if (prev_count > 0 && now - last_tick <= window && dx <= 4 && dy <= 4) return prev_count >= 3 ? 1 : prev_count + 1;
    return 1;
}
static void np_untitled(char* dst, int cap, int n) {
    const char* p = "Untitled"; int o = 0; while (*p && o < cap - 1) dst[o++] = *p++;
    if (n > 1) { if (o < cap - 1) dst[o++] = ' '; char t[12]; int k = 0; int v = n; while (v && k < 11) { t[k++] = (char)('0' + v % 10); v /= 10; } while (k && o < cap - 1) dst[o++] = t[--k]; }
    dst[o] = 0;
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC pop_options
#endif
#endif
