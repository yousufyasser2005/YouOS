#include <stdio.h>
#include <string.h>
static char fake_clip[1024]; static int fake_len = 0, clip_writes = 0;
#define LE_CLIP_SET(b, n) do { memcpy(fake_clip, (b), (n)); fake_len = (n); clip_writes++; } while (0)
#define LE_CLIP_GET(b, m) ((fake_len > (m) ? memcpy((b), fake_clip, (m)) : memcpy((b), fake_clip, fake_len)), (fake_len > (m) ? (m) : fake_len))
#include "../../user/src/lineedit.h"
static int failures = 0, checks = 0;
#define expect(c, msg) do { checks++; if (c) printf("  ok    %s\n", msg); else { printf("  FAIL  %s\n", msg); failures++; } } while (0)
static int is(LineEdit* e, const char* s) { return e->len == (int)strlen(s) && !strcmp(e->buf, s) && e->buf[e->len] == 0; }
int main(void) {
    char b[16]; LineEdit e; le_init(&e, b, sizeof b, 0);
    printf("== single-line field (lineedit.h)\n");
    for (const char* p = "hello"; *p; p++) le_type(&e, *p);
    expect(is(&e, "hello") && e.cursor == 5, "typing");
    le_left(&e, 0); le_left(&e, 0); le_type(&e, 'X'); expect(is(&e, "helXlo") && e.cursor == 4, "insert in the middle");
    le_backspace(&e); expect(is(&e, "hello") && e.cursor == 3, "backspace"); le_delete(&e); expect(is(&e, "helo"), "delete");
    le_home(&e, 0); le_backspace(&e); expect(is(&e, "helo") && e.cursor == 0, "backspace at the start does nothing");
    le_end(&e, 0); le_delete(&e); expect(is(&e, "helo"), "delete at the end does nothing");
    le_select_all(&e); le_type(&e, 'Z'); expect(is(&e, "Z"), "typing over a selection replaces it");
    le_set(&e, "0123456789ABCDE"); expect(e.len == 15, "fills the buffer exactly (cap-1)");
    expect(le_type(&e, 'x') == 0 && is(&e, "0123456789ABCDE"), "a full field accepts nothing more");
    le_set(&e, "abc"); le_select_all(&e); expect(le_insert(&e, "0123456789ABCDEFGHI", 19) == 15 && e.len == 15, "a paste larger than the field is cut to fit");
    le_set(&e, "a\tb\n\x01z\x7f\xff" "c"); le_set(&e, ""); le_insert(&e, "a\tb\n\x01z\x7f\xff" "c", 9); expect(is(&e, "abzc"), "control characters, newline and non-ASCII bytes are skipped on insert");
    le_set(&e, "foo bar.baz"); le_home(&e, 0); le_word_right(&e, 0); expect(e.cursor == 4, "Ctrl+Right: next word"); le_word_right(&e, 0); expect(e.cursor == 7, "...");
    le_word_left(&e, 0); expect(e.cursor == 4, "Ctrl+Left"); le_end(&e, 1); { int a, bb; le_sel(&e, &a, &bb); expect(a == 4 && bb == 11, "Shift+End selects to the end"); }
    printf("== clipboard\n");
    le_set(&e, "copy me"); le_home(&e, 0); le_word_right(&e, 1);
    expect(le_key(&e, 'c', 0, 1) == 1 && fake_len == 5 && !memcmp(fake_clip, "copy ", 5) && is(&e, "copy me"), "Ctrl+C copies the selection and leaves the text");
    le_end(&e, 0); le_key(&e, 'v', 0, 1); expect(is(&e, "copy mecopy "), "Ctrl+V pastes at the cursor");
    le_set(&e, "cut this"); le_home(&e, 0); le_word_right(&e, 1); le_key(&e, 'x', 0, 1); expect(is(&e, "this") && !memcmp(fake_clip, "cut ", 4), "Ctrl+X copies and deletes");
    le_key(&e, 'a', 0, 1); expect(le_has_sel(&e) && e.cursor == 4 && e.anchor == 0, "Ctrl+A selects all");
    printf("== password fields\n");
    char pb[16]; LineEdit pw; le_init(&pw, pb, sizeof pb, 1);
    for (const char* p = "s3cret"; *p; p++) le_key(&pw, *p, 0, 0);
    expect(is(&pw, "s3cret") && le_display_char(&pw, 2) == '*', "typing works; the display shows bullets, never the text");
    int w0 = clip_writes; strcpy(fake_clip, "OLD"); fake_len = 3;
    le_select_all(&pw);
    expect(le_key(&pw, 'c', 0, 1) == 1 && clip_writes == w0 && !memcmp(fake_clip, "OLD", 3), "Ctrl+C in a password field copies NOTHING (the clipboard keeps what it had)");
    le_select_all(&pw);
    expect(le_key(&pw, 'x', 0, 1) == 1 && clip_writes == w0 && is(&pw, "s3cret"), "Ctrl+X in a password field copies nothing AND deletes nothing");
    le_select_all(&pw); le_key(&pw, 'v', 0, 1); expect(is(&pw, "OLD"), "pasting INTO a password field works (replacing the selection)");
    le_set(&pw, "abc"); le_select_all(&pw); expect(le_copy(&pw) == 0 && clip_writes == w0, "le_copy on a password field returns 0");
    printf("\n%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
