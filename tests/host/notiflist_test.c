#include <stdio.h>
#include <string.h>
#include "../../user/src/notiflist.h"
static int failures = 0, checks = 0;
#define expect(c, msg) do { checks++; if (c) printf("  ok    %s\n", msg); else { printf("  FAIL  %s\n", msg); failures++; } } while (0)

/* the real centre's numbers: panel 380 wide, rows 64 apart, 5 rows visible */
#define X0 1000
#define Y0 300
#define W  380
#define RH 64
#define MV 5
static int at(int count, int scroll, int mx, int my) { return nl_row_at(X0, Y0, W, RH, MV, count, scroll, mx, my); }
static int card_y(int row) { return Y0 + 90 + row * RH; }

int main(void) {
    setbuf(stdout, 0);
    printf("== notification list logic (notiflist.h)\n");

    /* the draw code (draw_notif_center) uses these literals; the header must agree with them */
    expect(NL_FIRST_ROW_DY == 90 && NL_CARD_INSET == 12 && NL_CARD_GAP == 8, "the header's layout constants are the numbers the centre draws with (90, 12, 8)");

    /* ---- clamping ---- */
    expect(nl_clamp_scroll(8, 0, 5) == 0 && nl_clamp_scroll(8, 3, 5) == 3, "a scroll inside the range is kept");
    expect(nl_clamp_scroll(8, 4, 5) == 3 && nl_clamp_scroll(8, 99, 5) == 3, "too far down is held at count - visible rows");
    expect(nl_clamp_scroll(8, -4, 5) == 0, "negative scroll is 0");
    expect(nl_clamp_scroll(3, 2, 5) == 0 && nl_clamp_scroll(0, 7, 5) == 0, "fewer notifications than rows: never scrolls");

    /* ---- which card is under the pointer ---- */
    expect(at(8, 0, X0 + 50, card_y(0) + 5) == 7, "top card (row 0) is the newest notification (index count - 1)");
    expect(at(8, 0, X0 + 50, card_y(1) + 5) == 6 && at(8, 0, X0 + 50, card_y(4) + 5) == 3, "row 1 is the next older, row 4 is the fifth");
    expect(at(8, 2, X0 + 50, card_y(0) + 5) == 5 && at(8, 2, X0 + 50, card_y(4) + 5) == 1, "scrolled by 2: the top card is index 5, the last visible is 1");
    expect(at(8, 99, X0 + 50, card_y(0) + 5) == 4 && at(8, 99, X0 + 50, card_y(4) + 5) == 0, "a scroll beyond the end acts as the clamped scroll (top card index 4, last index 0)");
    expect(at(8, -3, X0 + 50, card_y(0) + 5) == 7, "a negative scroll acts as 0");
    expect(at(8, 0, X0 + 50, card_y(0)) == 7 && at(8, 0, X0 + 50, card_y(0) + RH - 8 - 1) == 7, "the first and last pixel row of a card belong to it");
    expect(at(8, 0, X0 + 50, card_y(0) + RH - 8) == -1, "the gap between two cards belongs to neither");
    expect(at(8, 0, X0 + 50, card_y(0) - 1) == -1, "above the first card: nothing (header area)");
    expect(at(8, 0, X0 + 50, card_y(4) + RH - 8) == -1 && at(8, 0, X0 + 50, card_y(5) + 5) == -1, "below the last visible card: nothing");
    expect(at(8, 0, X0 + 12, card_y(2) + 5) == 5 && at(8, 0, X0 + 12 - 1, card_y(2) + 5) == -1, "the card's left edge is 12 px inside the panel");
    expect(at(8, 0, X0 + W - 12 - 1, card_y(2) + 5) == 5 && at(8, 0, X0 + W - 12, card_y(2) + 5) == -1, "...and its right edge 12 px before the panel's right edge");
    expect(at(3, 0, X0 + 50, card_y(2) + 5) == 0 && at(3, 0, X0 + 50, card_y(3) + 5) == -1, "with 3 notifications only 3 cards exist");
    expect(at(1, 0, X0 + 50, card_y(0) + 5) == 0, "a single notification");
    expect(at(0, 0, X0 + 50, card_y(0) + 5) == -1, "no notifications: nothing to hit");
    expect(nl_row_at(X0, Y0, W, 8, MV, 8, 0, X0 + 50, card_y(0) + 1) == -1, "a nonsensical row height is refused");
    { int hits = 0, ok = 1; for (int y = Y0; y < Y0 + 500; y++) { int r = at(8, 0, X0 + 60, y); if (r >= 0) { hits++; if (r < 3 || r > 7) ok = 0; } }
      expect(hits == MV * (RH - 8) && ok, "scanning the whole panel: exactly 5 cards x 56 pixel rows hit, all of them indices 3..7"); }

    /* ---- text for the clipboard ---- */
    char b[200];
    expect(nl_format_one("Terminal", "Pasted text was cut to 64 KB", b, sizeof b) == 38 && !strcmp(b, "Terminal: Pasted text was cut to 64 KB"), "one notification is \"Title: message\"");
    expect(nl_format_one("", "only message", b, sizeof b) == 12 && !strcmp(b, "only message"), "no title: just the message (no stray colon)");
    expect(nl_format_one("Only title", "", b, sizeof b) == 10 && !strcmp(b, "Only title"), "no message: just the title");
    expect(nl_format_one("", "", b, sizeof b) == 0 && b[0] == 0, "both empty: empty text");
    { char s[8]; int n = nl_format_one("Terminal", "message", s, sizeof s); expect(n == 7 && !strcmp(s, "Termina"), "a small buffer is filled and terminated, never overflowed"); }
    { char s[1]; int n = nl_format_one("a", "b", s, sizeof s); expect(n == 0 && s[0] == 0, "a one-byte buffer holds just the terminator"); }
    expect(nl_format_one("a", "b", b, 0) == 0, "a zero-size buffer is refused");

    char T[4][32], M[4][64]; memset(T, 0, sizeof T); memset(M, 0, sizeof M);
    strcpy(T[0], "Files");    strcpy(M[0], "Copy finished");
    strcpy(T[1], "Terminal"); strcpy(M[1], "Pasted text was cut to 64 KB");
    strcpy(T[2], "");         strcpy(M[2], "no title here");
    strcpy(T[3], "Disk");     strcpy(M[3], "");
    nl_format_all(&T[0][0], 32, &M[0][0], 64, 2, b, sizeof b);
    expect(!strcmp(b, "Files: Copy finished\nTerminal: Pasted text was cut to 64 KB"), "Copy All: oldest first, one per line, no trailing newline");
    nl_format_all(&T[0][0], 32, &M[0][0], 64, 4, b, sizeof b);
    expect(!strcmp(b, "Files: Copy finished\nTerminal: Pasted text was cut to 64 KB\nno title here\nDisk"), "...an empty title or message is handled per line");
    expect(nl_format_all(&T[0][0], 32, &M[0][0], 64, 1, b, sizeof b) == 20 && !strcmp(b, "Files: Copy finished"), "one notification: identical to Copy");
    expect(nl_format_all(&T[0][0], 32, &M[0][0], 64, 0, b, sizeof b) == 0 && b[0] == 0, "no notifications: empty text");
    { char s[24]; int n = nl_format_all(&T[0][0], 32, &M[0][0], 64, 4, s, sizeof s); expect(n == 23 && s[23] == 0 && !memcmp(s, "Files: Copy finished\nTe", 23), "a small buffer is cut cleanly and terminated"); }

    printf("\n%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
