/*
 * YouOS boot animation — C port of tools/youos_boot_animation.py
 * Uses only the real kernel API (verified against fb.h/fb.c/irq.h):
 *   fb_get_info() -> fb_info_t* {width, height, ...}
 *   fb_put_pixel_alpha(x,y,color,alpha)
 *   fb_fill_rect_alpha(x,y,w,h,color,alpha)
 *   fb_draw_rect(x,y,w,h,color)   -- solid, used for per-frame clear
 *   irq_get_ticks() -> uint64_t   -- monotonic tick counter from IRQ0
 * No floats (kernel is -mno-sse), all fixed point Q16.16.
 */

#include <kernel/boot_anim.h>
#include <kernel/fb.h>
#include <kernel/pmm.h>
#include <kernel/irq.h>
#include <stdint.h>

/* ---- timer ----
 * TICK_HZ: adjust to match your PIT divisor in irq.c/pic.c if the
 * animation runs at the wrong real-world speed. 100 is the common
 * default PIT rate; if you configured a different divisor, change this.
 */
#define TICK_HZ   100
#define FPS       60
#define TOTAL_FRAMES 260
#define TICKS_PER_FRAME (TICK_HZ / FPS > 0 ? TICK_HZ / FPS : 1)

static void wait_next_frame(void) {
    static uint64_t last = 0;
    if (last == 0) last = irq_get_ticks();
    uint64_t target = last + TICKS_PER_FRAME;
    while (irq_get_ticks() < target) { /* busy wait */ }
    last = target;
}

/* ---- fixed point helpers (Q16.16) ---- */
typedef int32_t fx_t;
#define FX_ONE 65536
#define FX(x) ((fx_t)((x) * FX_ONE))
static inline fx_t fx_mul(fx_t a, fx_t b) { return (fx_t)(((int64_t)a * b) >> 16); }
static inline fx_t fx_lerp(fx_t a, fx_t b, fx_t t) { return a + fx_mul(b - a, t); }
static inline int fx_to_int(fx_t a) { return a >> 16; }
static inline fx_t clampf(fx_t v, fx_t lo, fx_t hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* ---- integer sine table, 0..359 degrees, Q16.16 ---- */
static fx_t sin_table[360];
static int sin_table_ready = 0;
static void build_sin_table(void) {
    for (int deg = 0; deg < 360; deg++) {
        int d = deg % 180;
        int sign = (deg >= 180) ? -1 : 1;
        int64_t num = 4 * d * (180 - d);
        int64_t den = 40500 - d * (180 - d);
        fx_t v = (fx_t)((num * FX_ONE) / den) * sign;
        sin_table[deg] = v;
    }
    sin_table_ready = 1;
}
static fx_t isin(int deg) { deg = ((deg % 360) + 360) % 360; return sin_table[deg]; }
static fx_t icos(int deg) { return isin(deg + 90); }

/* ---- easing ---- */
static fx_t ease_out_cubic(fx_t t) {
    fx_t o = FX_ONE - t;
    fx_t o3 = fx_mul(fx_mul(o, o), o);
    return FX_ONE - o3;
}
/* Standard in/out cubic: 4t^3 for the first half, mirrored for the
 * second. The previous version returned 1 - 4(1-t)^3 for the whole
 * range, which is NEGATIVE for roughly the first 37% of t (e.g. -3 at
 * t=0). That value drove the gradient->white colour mix, so rgb_lerp
 * extrapolated far outside 0..255 and the 8-bit channels wrapped --
 * the colour flicker during the fade. Output is now always in 0..1. */
static fx_t ease_in_out_cubic(fx_t t) {
    if (t <= 0) return 0;
    if (t >= FX_ONE) return FX_ONE;
    if (t < FX_ONE / 2) {
        fx_t t3 = fx_mul(fx_mul(t, t), t);
        return fx_mul(t3, FX(4));
    }
    fx_t u = FX(2) - fx_mul(t, FX(2));       /* (-2t + 2) */
    fx_t u3 = fx_mul(fx_mul(u, u), u);
    return FX_ONE - u3 / 2;
}

/* ---- colors ---- */
typedef struct { uint8_t r, g, b; } rgb_t;
static const rgb_t C_CYAN    = {0, 210, 255};
static const rgb_t C_MAGENTA = {255, 60, 180};
static const rgb_t C_WHITE   = {255, 255, 255};
static const rgb_t C_ORANGE  = {255, 140, 40};
static const rgb_t C_BLUE    = {60, 80, 220};
static const rgb_t C_PINK    = {255, 100, 160};

static uint32_t rgb_to_u32(rgb_t c) { return ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | c.b; }
static rgb_t rgb_lerp(rgb_t a, rgb_t b, fx_t t) {
    rgb_t o;
    if (t < 0) t = 0;               /* never extrapolate past the endpoints */
    if (t > FX_ONE) t = FX_ONE;
    o.r = (uint8_t)fx_to_int(fx_lerp(FX(a.r), FX(b.r), t));
    o.g = (uint8_t)fx_to_int(fx_lerp(FX(a.g), FX(b.g), t));
    o.b = (uint8_t)fx_to_int(fx_lerp(FX(a.b), FX(b.b), t));
    return o;
}

/* ---- vector letterforms for "YouOS" ----
 * The earlier 5x7 dot-matrix font looked like a seven-segment display
 * however it was smoothed, so the five letters are now real stroked
 * outlines: each is a few line segments and elliptical arcs, rendered
 * once at startup into an 8-bit coverage mask by taking the distance
 * from every pixel to the nearest segment (a capsule distance field,
 * so strokes have round caps and joins) and turning that into
 * anti-aliased alpha. Each frame then just resamples the mask
 * (bilinear) at the animated scale. Integer math only -- no FPU. */
/* Masks are authored at REF_SCALE=22 (154px-tall letters, what a
 * 1080p screen uses) so they stay sharp up to that size; glyph
 * geometry below is written on a 16-unit grid and multiplied by
 * REF_SCALE/16 via GS(). Smaller screens just sample the mask down. */
#define REF_SCALE 22
#define GS(v)    ((v) * REF_SCALE / 16)
#define MASK_PAD GS(8)
#define MASK_W   (5 * REF_SCALE + 2 * MASK_PAD)   /* glyph box + padding */
#define MASK_H   (7 * REF_SCALE + 2 * MASK_PAD)
#define STROKE_HW GS(8)        /* half stroke width in mask pixels */
#define GLYPH_W 5
#define GLYPH_H 7
#define N_GLYPHS 5
#define MAX_SEGS 160
#define SUB 8                          /* geometry is kept in 1/8 pixel */
#define NUM (REF_SCALE * SUB / 16)     /* 16-grid unit -> 1/8 px        */

typedef struct { int16_t x0, y0, x1, y1; int16_t bx0, by0, bx1, by1; } seg_t;
static uint8_t g_mask[N_GLYPHS][MASK_W * MASK_H];
static int     g_mask_ready = 0;

static const int16_t SIN5[19] = { 0,87,174,259,342,423,500,574,643,707,766,819,866,906,940,966,985,996,1000 }; /* sin(0..90 deg, step 5) * 1000 */
static int sin_deg(int d) {
    d %= 360; if (d < 0) d += 360;
    int neg = 0;
    if (d >= 180) { d -= 180; neg = 1; }
    if (d > 90) d = 180 - d;
    int i = d / 5, f = d % 5;
    int v = (i >= 18) ? SIN5[18] : SIN5[i] + ((SIN5[i + 1] - SIN5[i]) * f) / 5;
    return neg ? -v : v;
}
static int cos_deg(int d) { return sin_deg(d + 90); }

/* x/y in 1/8 pixel. Also stores a padded bounding box so the mask
 * builder can skip segments that are nowhere near a pixel. */
static int add_seg8(seg_t *s, int n, int x0, int y0, int x1, int y1) {
    if (n >= MAX_SEGS) return n;
    int m = (STROKE_HW + 2) * SUB;
    s[n].x0 = (int16_t)x0; s[n].y0 = (int16_t)y0;
    s[n].x1 = (int16_t)x1; s[n].y1 = (int16_t)y1;
    s[n].bx0 = (int16_t)((x0 < x1 ? x0 : x1) - m); s[n].bx1 = (int16_t)((x0 > x1 ? x0 : x1) + m);
    s[n].by0 = (int16_t)((y0 < y1 ? y0 : y1) - m); s[n].by1 = (int16_t)((y0 > y1 ? y0 : y1) + m);
    return n + 1;
}
/* line in 16-grid units */
static int add_seg(seg_t *s, int n, int x0, int y0, int x1, int y1) {
    return add_seg8(s, n, x0 * NUM, y0 * NUM, x1 * NUM, y1 * NUM);
}
/* Elliptical arc (16-grid centre/radii) as a polyline in 1/8 px,
 * 5-degree steps. 0 = right, 90 = up on screen; may run either way. */
static int add_arc(seg_t *s, int n, int cx, int cy, int rx, int ry, int a0, int a1) {
    int step = (a1 >= a0) ? 5 : -5;
    int px = cx * NUM + rx * NUM * cos_deg(a0) / 1000;
    int py = cy * NUM - ry * NUM * sin_deg(a0) / 1000;
    for (int a = a0; a != a1; ) {
        int na = a + step;
        if ((step > 0 && na > a1) || (step < 0 && na < a1)) na = a1;
        int qx = cx * NUM + rx * NUM * cos_deg(na) / 1000;
        int qy = cy * NUM - ry * NUM * sin_deg(na) / 1000;
        n = add_seg8(s, n, px, py, qx, qy);
        px = qx; py = qy; a = na;
    }
    return n;
}

static int build_glyph(int g, seg_t *s) {
    int n = 0;
    /* centre-lines, on a 16-unit-per-cell grid (mask = 96x128 units) */
    switch (g) {
    case 0: /* Y */
        n = add_seg(s, n, 18, 18, 48, 66);
        n = add_seg(s, n, 78, 18, 48, 66);
        n = add_seg(s, n, 48, 66, 48, 110);
        break;
    case 1: /* o */
        n = add_arc(s, n, 48, 82, 30, 28, 0, 360);
        break;
    case 2: /* u */
        n = add_seg(s, n, 18, 54, 18, 84);
        n = add_arc(s, n, 48, 84, 30, 26, 180, 360);
        n = add_seg(s, n, 78, 54, 78, 110);
        break;
    case 3: /* O */
        n = add_arc(s, n, 48, 64, 30, 46, 0, 360);
        break;
    case 4: /* S */
        n = add_arc(s, n, 48, 40, 28, 24, 20, 270);
        n = add_arc(s, n, 48, 88, 30, 22, 90, -160);
        break;
    }
    return n;
}

static uint32_t isqrt32(uint32_t v) {
    uint32_t r = 0, bit = 1u << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return r;
}

/* Squared distance from pixel centre (px8,py8, in 1/8 px) to a
 * segment, in (1/128 px)^2 units. Exact: interior points use the
 * point-to-line cross-product formula rather than snapping the
 * projection to a coarse grid (an earlier 1/16-step projection put a
 * faint dotted pattern along every straight stroke edge). */
static int64_t seg_dist2(const seg_t *s, int px8, int py8) {
    int64_t ax = s->x0, ay = s->y0;
    int64_t dx = s->x1 - ax, dy = s->y1 - ay;
    int64_t l2 = dx * dx + dy * dy;
    int64_t rx = px8 - ax, ry = py8 - ay;
    int64_t dot = rx * dx + ry * dy;
    if (l2 == 0 || dot <= 0) return (rx * rx + ry * ry) * 256;
    if (dot >= l2) {
        int64_t ex = px8 - (ax + dx), ey = py8 - (ay + dy);
        return (ex * ex + ey * ey) * 256;
    }
    int64_t cross = rx * dy - ry * dx;
    return (cross * cross * 256) / l2;
}

static void build_masks(void) {
    if (g_mask_ready) return;
    seg_t segs[MAX_SEGS];
    for (int g = 0; g < N_GLYPHS; g++) {
        int n = build_glyph(g, segs);
        for (int y = 0; y < MASK_H; y++) {
            int py8 = y * SUB + SUB / 2;                    /* pixel centre */
            for (int x = 0; x < MASK_W; x++) {
                int px8 = x * SUB + SUB / 2;
                int64_t best = INT64_MAX;
                for (int k = 0; k < n; k++) {
                    const seg_t *sg = &segs[k];
                    if (px8 < sg->bx0 || px8 > sg->bx1 || py8 < sg->by0 || py8 > sg->by1) continue;
                    int64_t d2 = seg_dist2(sg, px8, py8);
                    if (d2 < best) best = d2;
                }
                int cov = 0;
                if (best != INT64_MAX) {
                    if (best > 0xFFFFFFFFLL) best = 0xFFFFFFFFLL;
                    int d128 = (int)isqrt32((uint32_t)best);         /* 1/128 px */
                    cov = STROKE_HW * 128 + 64 - d128;              /* 1px AA ramp */
                    if (cov < 0) cov = 0;
                    if (cov > 128) cov = 128;
                }
                g_mask[g][y * MASK_W + x] = (uint8_t)(cov * 255 / 128);
            }
        }
    }
    g_mask_ready = 1;
}

static inline int mask_at(const uint8_t *m, int x, int y) {
    if (x < 0 || x >= MASK_W || y < 0 || y >= MASK_H) return 0;
    return m[y * MASK_W + x];
}

/* Draw glyph `g` with its 5x7-cell box's top-left at (x,y), where one
 * cell is `scale` pixels (scale 16 == the mask's native size). */
static void draw_glyph(int g, int x, int y, int scale, rgb_t color, uint8_t alpha) {
    uint32_t col = rgb_to_u32(color);
    const uint8_t *m = g_mask[g];
    int pad = MASK_PAD * scale / REF_SCALE;
    int dw = MASK_W * scale / REF_SCALE, dh = MASK_H * scale / REF_SCALE;
    for (int py = 0; py < dh; py++) {
        int sy = (py * 256 * REF_SCALE) / scale - 128;     /* 1/256 mask px */
        int y0 = sy >> 8, fy = sy & 255;
        for (int px = 0; px < dw; px++) {
            int sx = (px * 256 * REF_SCALE) / scale - 128;
            int x0 = sx >> 8, fx = sx & 255;
            int top = mask_at(m,x0,y0)   * (256 - fx) + mask_at(m,x0+1,y0)   * fx;
            int bot = mask_at(m,x0,y0+1) * (256 - fx) + mask_at(m,x0+1,y0+1) * fx;
            int v = (top * (256 - fy) + bot * fy) >> 16;   /* 0..255 */
            if (v <= 0) continue;
            int a = (alpha * v) / 255;
            if (a > 0) fb_put_pixel_alpha(x - pad + px, y - pad + py, col, (uint8_t)a);
        }
    }
}

static void draw_word(int cx, int cy, int scale, uint8_t alpha, fx_t white_mix, int x_offset) {
    if (scale < 1) scale = 1;
    int glyph_px_w = GLYPH_W * scale;
    int spacing = scale * 2;
    int total_w = N_GLYPHS * glyph_px_w + (N_GLYPHS - 1) * spacing;
    int total_h = GLYPH_H * scale;
    int start_x = cx - total_w / 2 + x_offset;
    int start_y = cy - total_h / 2;

    for (int i = 0; i < N_GLYPHS; i++) {
        fx_t t = FX(i) / (N_GLYPHS - 1);
        rgb_t grad = rgb_lerp(C_CYAN, C_MAGENTA, t);
        rgb_t col = rgb_lerp(grad, C_WHITE, white_mix);
        int gx = start_x + i * (glyph_px_w + spacing);
        draw_glyph(i, gx, start_y, scale, col, alpha);
    }
}

/* ---- orbital rings ---- */
typedef struct {
    int rx, ry, tilt_deg;
    rgb_t c_start, c_end;
    fx_t speed_frac;   /* Q16.16 degrees-per-frame */
    int angle_deg;
    fx_t angle_frac;
    int trail_deg;
    fx_t alpha;        /* Q16.16, holds 0..255 scaled */
} ring_t;

static void ring_update(ring_t *r, int target_alpha) {
    r->angle_frac += r->speed_frac;
    while (r->angle_frac >= FX_ONE) { r->angle_frac -= FX_ONE; r->angle_deg = (r->angle_deg + 1) % 360; }
    r->alpha += FX(target_alpha - fx_to_int(r->alpha)) / 8;
}

static void ring_draw(ring_t *r, int cx, int cy) {
    int a = fx_to_int(r->alpha);
    if (a < 2) return;
    int steps = 90;
    fx_t cos_t = icos(r->tilt_deg), sin_t = isin(r->tilt_deg);
    int prev_x = 0, prev_y = 0, have_prev = 0;
    for (int i = 0; i <= steps; i++) {
        int deg = r->angle_deg - r->trail_deg + (i * r->trail_deg) / steps;
        fx_t ex = fx_mul(FX(r->rx), icos(deg));
        fx_t ey = fx_mul(FX(r->ry), isin(deg));
        fx_t px = FX(cx) + fx_mul(ex, cos_t) - fx_mul(ey, sin_t);
        fx_t py = FX(cy) + fx_mul(ex, sin_t) + fx_mul(ey, cos_t);
        int ix = fx_to_int(px), iy = fx_to_int(py);
        fx_t t = FX(i) / steps;
        rgb_t col = rgb_lerp(r->c_start, r->c_end, t);
        uint8_t seg_alpha = (uint8_t)((i * a) / steps);
        if (have_prev) {
            int dx = ix - prev_x, dy = iy - prev_y;
            int adx = dx > 0 ? dx : -dx, ady = dy > 0 ? dy : -dy;
            int steps_l = adx > ady ? adx : ady;
            if (steps_l == 0) steps_l = 1;
            for (int s = 0; s <= steps_l; s++) {
                int lx = prev_x + dx * s / steps_l;
                int ly = prev_y + dy * s / steps_l;
                fb_put_pixel_alpha(lx, ly, rgb_to_u32(col), seg_alpha);
            }
        }
        prev_x = ix; prev_y = iy; have_prev = 1;
    }
}

/* ---- particles ---- */
#define MAX_PARTICLES 64
typedef struct {
    fx_t x, y, vx, vy;
    fx_t life, max_life;
    rgb_t color;
    int size;
    int used;
} particle_t;

static particle_t particles[MAX_PARTICLES];

static uint32_t rng_state = 0x1234ABCDu;
static uint32_t rnd(void) { rng_state ^= rng_state << 13; rng_state ^= rng_state >> 17; rng_state ^= rng_state << 5; return rng_state; }
static fx_t rnd_range(fx_t lo, fx_t hi) { return lo + (fx_t)(((int64_t)(rnd() % 10000) * (hi - lo)) / 10000); }

static void particle_spawn(fx_t x, fx_t y, rgb_t color, fx_t vx, fx_t vy, fx_t life, int size) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (!particles[i].used) {
            particles[i].x = x; particles[i].y = y;
            particles[i].vx = vx; particles[i].vy = vy;
            particles[i].life = life; particles[i].max_life = life;
            particles[i].color = color; particles[i].size = size;
            particles[i].used = 1;
            return;
        }
    }
}

static void particles_update_draw(void) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        particle_t *p = &particles[i];
        if (!p->used) continue;
        p->x += p->vx; p->y += p->vy;
        p->vy += FX_ONE / 25;
        p->life -= FX_ONE / FPS;
        if (p->life <= 0) { p->used = 0; continue; }
        int maxl = fx_to_int(p->max_life); if (maxl <= 0) maxl = 1;
        uint8_t alpha = (uint8_t)((fx_to_int(p->life) * 255) / maxl);
        int cr = p->size < 1 ? 1 : p->size;
        int cx = fx_to_int(p->x), cy = fx_to_int(p->y);
        uint32_t col = rgb_to_u32(p->color);
        for (int dy = -cr; dy <= cr; dy++)
            for (int dx = -cr; dx <= cr; dx++)
                if (dx*dx + dy*dy <= cr*cr)
                    fb_put_pixel_alpha(cx+dx, cy+dy, col, alpha);
    }
}

/* ---- main entry ---- */
void boot_anim_run(void) {
    if (!fb_available()) return;
    fb_info_t *fbi = fb_get_info();
    int FB_W = (int)fbi->width;
    int FB_H = (int)fbi->height;

    if (!sin_table_ready) build_sin_table();
    for (int i = 0; i < MAX_PARTICLES; i++) particles[i].used = 0;

    int CX = FB_W / 2, CY = FB_H / 2;
    /* word scale: 16 px/cell at 800px tall, 22 at 1080px, etc. */
    int KS = ((int)FB_H * 16 + 400) / 800;
    if (KS < 8) KS = 8;
    if (KS > REF_SCALE) KS = REF_SCALE;
    build_masks();

    /* Double-buffer the whole animation. Each frame used to clear the
     * ENTIRE visible framebuffer to black and then redraw the word,
     * rings and particles straight onto it -- so the display could
     * scan out a black or half-drawn frame in between (the flicker),
     * and every alpha blend read back from slow video memory. Now
     * everything is drawn into ordinary RAM and copied to the screen
     * once per frame, already complete. Falls back to the old direct
     * drawing if the buffer can't be allocated (~4MB at 1280x800). */
    uint32_t bb_pages = ((uint32_t)FB_W * (uint32_t)FB_H * 4u + 4095u) / 4096u;
    uint64_t bb_phys  = pmm_alloc_pages(bb_pages);
    if (bb_phys) fb_backbuffer_begin((uint32_t*)bb_phys);

    ring_t rings[3] = {
        { 220, 20, -8,  {255,60,180},{255,100,160}, FX(3)/10,  0,0, 220, 0 },
        { 210, 17,  12, {255,140,40},{255,200,80},  FX(27)/100,120,0,220, 0 },
        { 225, 14, -4,  {60,80,220}, {0,210,255},   FX(21)/100,240,0,220, 0 },
    };
    for (int i = 0; i < 3; i++) { rings[i].rx = rings[i].rx * KS / 16; rings[i].ry = rings[i].ry * KS / 16; }

    const int P1 = 30, P2 = 105, P3 = 155, P4 = 215;

    for (int f = 0; f <= TOTAL_FRAMES; f++) {
        fb_draw_rect(0, 0, FB_W, FB_H, 0x000000);

        if (f < P2) {
            fx_t p1 = clampf(FX(f) / P1, 0, FX_ONE);
            fx_t p2 = f < P1 ? 0 : clampf(FX(f - P1) / (P2 - P1), 0, FX_ONE);
            int scale = f < P1 ? fx_to_int(fx_lerp(FX(4 * KS / 16), FX(11 * KS / 16), ease_out_cubic(p1)))
                                : fx_to_int(fx_lerp(FX(11 * KS / 16), FX(KS), ease_out_cubic(p2)));
            int x_off = f < P1 ? fx_to_int(fx_lerp(FX(-1)*FB_W*3/10, 0, ease_out_cubic(p1)))
                                : fx_to_int(fx_lerp(FX(-30), 0, ease_out_cubic(p2)));
            uint8_t t_alpha = f < P1 ? (uint8_t)fx_to_int(fx_lerp(0, FX(255), ease_out_cubic(p1))) : 255;
            draw_word(CX, CY, scale, t_alpha, 0, x_off);
        } else if (f < P3) {
            fx_t p3 = clampf(FX(f - P2) / (P3 - P2), 0, FX_ONE);
            draw_word(CX, CY, KS, 255, ease_in_out_cubic(p3), 0);
        } else if (f < P4) {
            draw_word(CX, CY, KS, 255, FX_ONE, 0);
        } else {
            fx_t p5 = clampf(FX(f - P4) / (TOTAL_FRAMES - P4), 0, FX_ONE);
            uint8_t fa = (uint8_t)(255 - fx_to_int(fx_mul(p5, FX(255))));
            draw_word(CX, CY, KS, fa, FX_ONE, 0);
        }

        int ring_target = 0;
        if (f >= P1 && f < P3) {
            fx_t p2 = clampf(FX(f - P1) / (P2 - P1), 0, FX_ONE);
            fx_t p3 = clampf(FX(f - P2) / (P3 - P2), 0, FX_ONE);
            fx_t a = fx_mul(ease_out_cubic(p2), FX_ONE - ease_in_out_cubic(p3));
            ring_target = fx_to_int(fx_mul(a, FX(220)));
        }
        for (int i = 0; i < 3; i++) { ring_update(&rings[i], ring_target); ring_draw(&rings[i], CX, CY); }

        if (f >= P1 && f < P2 && (rnd() % 10) < 7) {
            rgb_t cols[5] = { C_CYAN, C_MAGENTA, C_ORANGE, C_PINK, C_BLUE };
            for (int n = 0; n < 2; n++) {
                fx_t ang = rnd_range(0, FX(360));
                fx_t r = rnd_range(FX(100 * KS / 16), FX(220 * KS / 16));
                fx_t x = FX(CX) + fx_mul(r, icos(fx_to_int(ang)));
                fx_t y = FX(CY) + fx_mul(fx_mul(r, isin(fx_to_int(ang))), FX(22)/100);
                particle_spawn(x, y, cols[rnd() % 5],
                    rnd_range(FX(-1), FX(1)), rnd_range(FX(-1), FX(1)),
                    rnd_range(FX(1)/2, FX(1)), 2);
            }
        }
        particles_update_draw();

        fb_backbuffer_present();
        wait_next_frame();
    }

    if (bb_phys) {
        fb_backbuffer_end();
        for (uint32_t i = 0; i < bb_pages; i++)
            pmm_free_page(bb_phys + (uint64_t)i * 4096u);
    }
}
