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
static fx_t ease_in_out_cubic(fx_t t) {
    fx_t nt = FX_ONE - t;
    fx_t c = fx_mul(fx_mul(nt, nt), nt);
    return FX_ONE - fx_mul(c, FX(4));
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
    o.r = (uint8_t)fx_to_int(fx_lerp(FX(a.r), FX(b.r), t));
    o.g = (uint8_t)fx_to_int(fx_lerp(FX(a.g), FX(b.g), t));
    o.b = (uint8_t)fx_to_int(fx_lerp(FX(a.b), FX(b.b), t));
    return o;
}

/* ---- tiny 5x7 bitmap font, only glyphs needed for "YouOS" ---- */
static const uint8_t GLYPH_Y[7] = {0b10001,0b10001,0b01010,0b00100,0b00100,0b00100,0b00100};
static const uint8_t GLYPH_o[7] = {0b00000,0b00000,0b01110,0b10001,0b10001,0b10001,0b01110};
static const uint8_t GLYPH_u[7] = {0b00000,0b00000,0b10001,0b10001,0b10001,0b10011,0b01101};
static const uint8_t GLYPH_O[7] = {0b01110,0b10001,0b10001,0b10001,0b10001,0b10001,0b01110};
static const uint8_t GLYPH_S[7] = {0b01111,0b10000,0b10000,0b01110,0b00001,0b00001,0b11110};

static const uint8_t *WORD_GLYPHS[5] = { GLYPH_Y, GLYPH_o, GLYPH_u, GLYPH_O, GLYPH_S };
#define GLYPH_W 5
#define GLYPH_H 7
#define N_GLYPHS 5

static void draw_glyph(const uint8_t *g, int x, int y, int scale, rgb_t color, uint8_t alpha) {
    uint32_t col = rgb_to_u32(color);
    for (int row = 0; row < GLYPH_H; row++) {
        uint8_t bits = g[row];
        for (int c = 0; c < GLYPH_W; c++) {
            if (bits & (1 << (GLYPH_W - 1 - c))) {
                int px = x + c * scale;
                int py = y + row * scale;
                for (int dy = 0; dy < scale; dy++)
                    for (int dx = 0; dx < scale; dx++)
                        fb_put_pixel_alpha(px + dx, py + dy, col, alpha);
            }
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
        draw_glyph(WORD_GLYPHS[i], gx, start_y, scale, col, alpha);
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

    ring_t rings[3] = {
        { 220, 20, -8,  {255,60,180},{255,100,160}, FX(3)/10,  0,0, 220, 0 },
        { 210, 17,  12, {255,140,40},{255,200,80},  FX(27)/100,120,0,220, 0 },
        { 225, 14, -4,  {60,80,220}, {0,210,255},   FX(21)/100,240,0,220, 0 },
    };

    const int P1 = 30, P2 = 105, P3 = 155, P4 = 215;

    for (int f = 0; f <= TOTAL_FRAMES; f++) {
        fb_draw_rect(0, 0, FB_W, FB_H, 0x000000);

        if (f < P2) {
            fx_t p1 = clampf(FX(f) / P1, 0, FX_ONE);
            fx_t p2 = f < P1 ? 0 : clampf(FX(f - P1) / (P2 - P1), 0, FX_ONE);
            int scale = f < P1 ? fx_to_int(fx_lerp(FX(4), FX(11), ease_out_cubic(p1)))
                                : fx_to_int(fx_lerp(FX(11), FX(16), ease_out_cubic(p2)));
            int x_off = f < P1 ? fx_to_int(fx_lerp(FX(-1)*FB_W*3/10, 0, ease_out_cubic(p1)))
                                : fx_to_int(fx_lerp(FX(-30), 0, ease_out_cubic(p2)));
            uint8_t t_alpha = f < P1 ? (uint8_t)fx_to_int(fx_lerp(0, FX(255), ease_out_cubic(p1))) : 255;
            draw_word(CX, CY, scale, t_alpha, 0, x_off);
        } else if (f < P3) {
            fx_t p3 = clampf(FX(f - P2) / (P3 - P2), 0, FX_ONE);
            draw_word(CX, CY, 16, 255, ease_in_out_cubic(p3), 0);
        } else if (f < P4) {
            draw_word(CX, CY, 16, 255, FX_ONE, 0);
        } else {
            fx_t p5 = clampf(FX(f - P4) / (TOTAL_FRAMES - P4), 0, FX_ONE);
            uint8_t fa = (uint8_t)(255 - fx_to_int(fx_mul(p5, FX(255))));
            draw_word(CX, CY, 16, fa, FX_ONE, 0);
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
                fx_t r = rnd_range(FX(100), FX(220));
                fx_t x = FX(CX) + fx_mul(r, icos(fx_to_int(ang)));
                fx_t y = FX(CY) + fx_mul(fx_mul(r, isin(fx_to_int(ang))), FX(22)/100);
                particle_spawn(x, y, cols[rnd() % 5],
                    rnd_range(FX(-1), FX(1)), rnd_range(FX(-1), FX(1)),
                    rnd_range(FX(1)/2, FX(1)), 2);
            }
        }
        particles_update_draw();

        wait_next_frame();
    }
}
