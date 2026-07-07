/* Software renderer: draws an RGBA framebuffer for the kitty presenter. */
#include "kitty_brokeout.h"
#include "font8x16.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265358979323846f

static uint8_t *fb = NULL;
static uint8_t *backdrop = NULL;
static int W = 0, H = 0;
static int OX = 0, OY = 0;

uint8_t *render_fb(void) { return fb; }

static uint32_t mix_rgb(uint32_t a, uint32_t b, float t)
{
    t = clampf(t, 0, 1);
    int ar = (a >> 16) & 255, ag = (a >> 8) & 255, ab = a & 255;
    int br = (b >> 16) & 255, bg = (b >> 8) & 255, bb = b & 255;
    int r = ar + (int)((br - ar) * t);
    int g = ag + (int)((bg - ag) * t);
    int bl = ab + (int)((bb - ab) * t);
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
}

static uint32_t scale_rgb(uint32_t rgb, float k)
{
    k = clampf(k, 0, 2);
    int r = (int)(((rgb >> 16) & 255) * k);
    int g = (int)(((rgb >> 8) & 255) * k);
    int b = (int)((rgb & 255) * k);
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

void render_init(int w, int h)
{
    W = w;
    H = h;
    fb = malloc((size_t)W * H * 4);
    backdrop = malloc((size_t)W * H * 4);

    for (int y = 0; y < H; y++) {
        float v = (float)y / fmaxf(1.0f, H - 1.0f);
        uint32_t top = 0x050713;
        uint32_t mid = 0x10142a;
        uint32_t bot = 0x13091b;
        uint32_t c = v < 0.58f ? mix_rgb(top, mid, v / 0.58f)
                               : mix_rgb(mid, bot, (v - 0.58f) / 0.42f);
        int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
        for (int x = 0; x < W; x++) {
            float u = fabsf((float)x / fmaxf(1.0f, W - 1.0f) - 0.5f) * 2.0f;
            float vignette = 1.0f - 0.42f * powf(clampf(u * 0.9f + v * 0.35f, 0, 1), 1.6f);
            uint8_t *p = backdrop + ((size_t)y * W + x) * 4;
            p[0] = (uint8_t)(r * vignette);
            p[1] = (uint8_t)(g * vignette);
            p[2] = (uint8_t)(b * vignette);
            p[3] = 255;
        }
    }
}

void render_shutdown(void)
{
    free(fb);
    free(backdrop);
    fb = backdrop = NULL;
}

static inline void set_px(int x, int y, uint32_t rgb)
{
    x += OX;
    y += OY;
    if (x < 0 || x >= W || y < 0 || y >= H) return;
    uint8_t *p = fb + ((size_t)y * W + x) * 4;
    p[0] = (rgb >> 16) & 255;
    p[1] = (rgb >> 8) & 255;
    p[2] = rgb & 255;
    p[3] = 255;
}

static inline void px_blend(int x, int y, uint32_t rgb, float a)
{
    x += OX;
    y += OY;
    if (x < 0 || x >= W || y < 0 || y >= H) return;
    int ai = (int)(clampf(a, 0, 1) * 256.0f + 0.5f);
    if (ai <= 0) return;
    uint8_t *p = fb + ((size_t)y * W + x) * 4;
    int r = (rgb >> 16) & 255;
    int g = (rgb >> 8) & 255;
    int b = rgb & 255;
    p[0] = (uint8_t)(p[0] + (((r - p[0]) * ai) >> 8));
    p[1] = (uint8_t)(p[1] + (((g - p[1]) * ai) >> 8));
    p[2] = (uint8_t)(p[2] + (((b - p[2]) * ai) >> 8));
}

static void fill_rect(float fx, float fy, float fw, float fh, uint32_t rgb, float a)
{
    if (fw <= 0 || fh <= 0) return;
    int x0 = (int)floorf(fx), x1 = (int)ceilf(fx + fw);
    int y0 = (int)floorf(fy), y1 = (int)ceilf(fy + fh);
    for (int y = y0; y < y1; y++) {
        float cy = fminf((float)(y + 1), fy + fh) - fmaxf((float)y, fy);
        if (cy <= 0) continue;
        if (cy > 1) cy = 1;
        for (int x = x0; x < x1; x++) {
            float cx = fminf((float)(x + 1), fx + fw) - fmaxf((float)x, fx);
            if (cx <= 0) continue;
            if (cx > 1) cx = 1;
            px_blend(x, y, rgb, a * cx * cy);
        }
    }
}

static void stroke_rect(float x, float y, float w, float h,
                        float line, uint32_t rgb, float a)
{
    fill_rect(x, y, w, line, rgb, a);
    fill_rect(x, y + h - line, w, line, rgb, a);
    fill_rect(x, y, line, h, rgb, a);
    fill_rect(x + w - line, y, line, h, rgb, a);
}

static void fill_circle(float cx, float cy, float r, uint32_t rgb, float a)
{
    if (r <= 0) return;
    float rOut = r + 0.5f;
    float rIn = r - 0.5f;
    float rOut2 = rOut * rOut;
    float rIn2 = rIn > 0 ? rIn * rIn : 0;
    int y0 = (int)floorf(cy - rOut), y1 = (int)ceilf(cy + rOut);
    for (int y = y0; y <= y1; y++) {
        float dy = y + 0.5f - cy;
        float w2 = rOut2 - dy * dy;
        if (w2 <= 0) continue;
        float half = sqrtf(w2);
        int x0 = (int)floorf(cx - half), x1 = (int)ceilf(cx + half);
        for (int x = x0; x <= x1; x++) {
            float dx = x + 0.5f - cx;
            float d2 = dx * dx + dy * dy;
            if (d2 >= rOut2) continue;
            if (d2 <= rIn2) px_blend(x, y, rgb, a);
            else {
                float cov = rOut - sqrtf(d2);
                px_blend(x, y, rgb, a * clampf(cov, 0, 1));
            }
        }
    }
}

static void ring(float cx, float cy, float r, float width, uint32_t rgb, float a)
{
    float hw = width * 0.5f;
    int x0 = (int)floorf(cx - r - hw) - 1;
    int x1 = (int)ceilf(cx + r + hw) + 1;
    int y0 = (int)floorf(cy - r - hw) - 1;
    int y1 = (int)ceilf(cy + r + hw) + 1;
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            float dx = x + 0.5f - cx;
            float dy = y + 0.5f - cy;
            float d = sqrtf(dx * dx + dy * dy);
            float cov = hw + 0.5f - fabsf(d - r);
            if (cov > 0) px_blend(x, y, rgb, a * clampf(cov, 0, 1));
        }
    }
}

static void draw_line(float x0, float y0, float x1, float y1,
                      float width, uint32_t rgb, float a)
{
    float dx = x1 - x0, dy = y1 - y0;
    float len2 = dx * dx + dy * dy;
    if (len2 < 0.1f) {
        fill_circle(x0, y0, width * 0.5f, rgb, a);
        return;
    }
    float hw = fmaxf(0.5f, width * 0.5f);
    int xMin = (int)floorf(fminf(x0, x1) - hw) - 1;
    int xMax = (int)ceilf(fmaxf(x0, x1) + hw) + 1;
    int yMin = (int)floorf(fminf(y0, y1) - hw) - 1;
    int yMax = (int)ceilf(fmaxf(y0, y1) + hw) + 1;
    for (int y = yMin; y < yMax; y++) {
        for (int x = xMin; x < xMax; x++) {
            float px = x + 0.5f - x0;
            float py = y + 0.5f - y0;
            float t = clampf((px * dx + py * dy) / len2, 0, 1);
            float qx = px - t * dx;
            float qy = py - t * dy;
            float cov = hw + 0.5f - sqrtf(qx * qx + qy * qy);
            if (cov > 0) px_blend(x, y, rgb, a * clampf(cov, 0, 1));
        }
    }
}

static int text_width(const char *s, int scale)
{
    return (int)strlen(s) * FONT_W * scale;
}

static void draw_glyph(int x, int y, const unsigned char *glyph,
                       uint32_t rgb, float a, int scale)
{
    for (int gy = 0; gy < FONT_H; gy++) {
        uint8_t row = glyph[gy];
        for (int gx = 0; gx < FONT_W; gx++) {
            if (!((row >> (7 - gx)) & 1)) continue;
            for (int sy = 0; sy < scale; sy++)
                for (int sx = 0; sx < scale; sx++)
                    px_blend(x + gx * scale + sx, y + gy * scale + sy, rgb, a);
        }
    }
}

static void draw_text(float fx, float fy, const char *s, uint32_t rgb, float a, int scale)
{
    int x = (int)fx, y = (int)fy;
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c < 32 || c > 126) c = '?';
        draw_glyph(x, y, font8x16[c - 32], rgb, a, scale);
        x += FONT_W * scale;
    }
}

static void draw_text_shadow(float x, float y, const char *s,
                             uint32_t rgb, float a, int scale)
{
    draw_text(x + scale, y + scale, s, 0x000000, a * 0.75f, scale);
    draw_text(x, y, s, rgb, a, scale);
}

static void draw_text_center(float cx, float y, const char *s,
                             uint32_t rgb, float a, int scale)
{
    draw_text(cx - text_width(s, scale) * 0.5f, y, s, rgb, a, scale);
}

static uint32_t brick_color(const Brick *b)
{
    if (b->type == BRICK_METAL) return 0xaab2c2;
    if (b->type == BRICK_EXPLOSIVE) return 0xd946ef;
    if (b->type == BRICK_SPEED) return 0x22d3ee;
    if (b->hits >= 3) return 0xef4444;
    if (b->hits == 2) return 0xf97316;
    return 0xfacc15;
}

static uint32_t powerup_color(int type)
{
    static const uint32_t colors[PU_COUNT] = {
        0x86efac, 0x7dd3fc, 0xf9a8d4, 0xc4b5fd
    };
    if (type < 0 || type >= PU_COUNT) return 0xffffff;
    return colors[type];
}

static void draw_stars(void)
{
    float t = G.frameCount / 60.0f;
    for (int i = 0; i < G.numStars; i++) {
        Star *s = &G.stars[i];
        int b = (int)(s->brightness + sinf(t * s->speed + s->phase) * 34.0f);
        b = (int)clampf((float)b, 30, 255);
        uint32_t col = ((uint32_t)b << 16) | ((uint32_t)b << 8) | (uint32_t)clampf(b + 18, 0, 255);
        set_px((int)s->x, (int)s->y, col);
        if (s->size > 1 && b > 150) {
            int d = (int)(b * 0.42f);
            uint32_t dim = ((uint32_t)d << 16) | ((uint32_t)d << 8) | (uint32_t)d;
            set_px((int)s->x + 1, (int)s->y, dim);
            set_px((int)s->x, (int)s->y + 1, dim);
        }
    }
}

static void draw_grid(void)
{
    float s = G.scale;
    float step = 32.0f * s;
    float horizon = G.playY + G.playH + 4.0f * s;
    for (float x = G.playX; x <= G.playX + G.playW + 1; x += step) {
        float k = (x - G.playX) / G.playW;
        uint32_t col = mix_rgb(0x172554, 0x581c87, k);
        draw_line(x, horizon, G.playX + G.playW * 0.5f + (x - (G.playX + G.playW * 0.5f)) * 0.42f,
                  G.playY + 80.0f * s, 1.0f, col, 0.12f);
    }
    for (int i = 0; i < 10; i++) {
        float y = horizon - i * 22.0f * s;
        if (y < G.playY + 68.0f * s) break;
        draw_line(G.playX, y, G.playX + G.playW, y, 1.0f, 0x1e3a8a, 0.10f);
    }
}

static void draw_playfield_frame(void)
{
    float s = G.scale;
    float x = G.playX, y = G.playY, w = G.playW, h = G.playH;
    fill_rect(x - 8 * s, y - 8 * s, w + 16 * s, h + 16 * s, 0x050510, 0.52f);
    stroke_rect(x - 2 * s, y - 2 * s, w + 4 * s, h + 4 * s, 2 * s, 0x38bdf8, 0.38f);
    stroke_rect(x - 6 * s, y - 6 * s, w + 12 * s, h + 12 * s, 1 * s, 0xa855f7, 0.20f);
    fill_rect(x, y, w, h, 0x020617, 0.23f);

    if (G.paddle.shieldTimer > 0.0f) {
        float a = clampf(G.paddle.shieldTimer / 12.0f, 0.18f, 1.0f);
        float sy = G.playY + G.playH - 6.0f * s;
        draw_line(x + 16 * s, sy, x + w - 16 * s, sy, 4.0f * s, 0x8b5cf6, 0.35f * a);
        draw_line(x + 16 * s, sy, x + w - 16 * s, sy, 1.2f * s, 0xf5d0fe, 0.92f * a);
    }
}

static void draw_brick(const Brick *b)
{
    if (!b->alive) return;
    float pulse = b->pulse;
    if (b->type == BRICK_EXPLOSIVE)
        pulse = fmaxf(pulse, 0.22f + 0.18f * sinf(G.frameCount * 0.13f + b->x * 0.01f));
    if (b->type == BRICK_SPEED)
        pulse = fmaxf(pulse, 0.13f + 0.12f * sinf(G.frameCount * 0.18f + b->y * 0.02f));

    uint32_t base = brick_color(b);
    float x = b->x, y = b->y, w = b->w, h = b->h;
    fill_rect(x - 3 * G.scale, y - 3 * G.scale, w + 6 * G.scale, h + 6 * G.scale,
              base, 0.10f + pulse * 0.16f);
    fill_rect(x, y, w, h, scale_rgb(base, 0.58f), 1.0f);
    fill_rect(x + 1, y + 1, w - 2, h * 0.47f, scale_rgb(base, 1.22f), 0.94f);
    fill_rect(x + 1, y + h * 0.52f, w - 2, h * 0.43f, scale_rgb(base, 0.78f), 0.96f);
    stroke_rect(x, y, w, h, fmaxf(1.0f, G.scale), 0xffffff, 0.16f);
    fill_rect(x + 2 * G.scale, y + 2 * G.scale, w - 4 * G.scale, fmaxf(1, h * 0.12f),
              0xffffff, 0.18f);

    if (b->type == BRICK_METAL) {
        for (float sx = x - h; sx < x + w; sx += 11 * G.scale)
            draw_line(sx, y + h, sx + h, y, 1.0f * G.scale, 0xe5e7eb, 0.16f);
        fill_rect(x + w * 0.10f, y + h * 0.35f, w * 0.80f, h * 0.22f, 0x111827, 0.18f);
    } else if (b->type == BRICK_EXPLOSIVE) {
        fill_circle(x + w * 0.5f, y + h * 0.5f, h * 0.24f, 0xfff7ed, 0.72f);
        ring(x + w * 0.5f, y + h * 0.5f, h * 0.38f + pulse * h * 0.2f,
             1.0f * G.scale, 0xf0abfc, 0.55f);
    } else if (b->type == BRICK_SPEED) {
        float cy = y + h * 0.5f;
        for (int i = 0; i < 2; i++) {
            float ox = x + w * (0.36f + i * 0.18f);
            draw_line(ox - 4 * G.scale, cy - 4 * G.scale, ox + 2 * G.scale, cy,
                      2.0f * G.scale, 0xecfeff, 0.62f);
            draw_line(ox + 2 * G.scale, cy, ox - 4 * G.scale, cy + 4 * G.scale,
                      2.0f * G.scale, 0xecfeff, 0.62f);
        }
    } else if (b->hits < b->maxHits) {
        float cx = x + w * 0.52f;
        draw_line(cx - w * 0.26f, y + h * 0.22f, cx + w * 0.08f, y + h * 0.70f,
                  1.1f * G.scale, 0x451a03, 0.44f);
        draw_line(cx + w * 0.02f, y + h * 0.48f, cx + w * 0.30f, y + h * 0.32f,
                  1.0f * G.scale, 0x451a03, 0.35f);
    }
}

static void draw_bricks(void)
{
    for (int i = 0; i < G.numBricks; i++)
        draw_brick(&G.bricks[i]);
}

static void draw_paddle(void)
{
    Paddle *p = &G.paddle;
    float s = G.scale;
    uint32_t core = G.paddle.wideTimer > 0.0f ? 0x7dd3fc : 0x86efac;
    fill_rect(p->x - 12 * s, p->y - 9 * s, p->w + 24 * s, p->h + 22 * s,
              core, 0.12f);
    fill_rect(p->x, p->y, p->w, p->h, 0x052e16, 1.0f);
    fill_rect(p->x + 2 * s, p->y + 1 * s, p->w - 4 * s, p->h * 0.45f,
              scale_rgb(core, 1.15f), 0.98f);
    fill_rect(p->x + 2 * s, p->y + p->h * 0.48f, p->w - 4 * s, p->h * 0.40f,
              scale_rgb(core, 0.68f), 0.98f);
    stroke_rect(p->x, p->y, p->w, p->h, fmaxf(1.0f, s), 0xffffff, 0.28f);

    int segs = 7;
    for (int i = 1; i < segs; i++) {
        float x = p->x + p->w * i / segs;
        fill_rect(x, p->y + 2 * s, 1.0f * s, p->h - 4 * s, 0x022c22, 0.35f);
    }
    fill_circle(p->x + p->w * 0.5f, p->y + p->h * 0.5f, p->h * 0.35f,
                0xf0fdf4, 0.50f);
}

static void draw_ball(const Ball *b)
{
    if (!b->active) return;
    float s = G.scale;
    for (int i = 9; i >= 1; i--) {
        int idx = (b->trailHead + 10 - i) % 10;
        float a = (10 - i) / 10.0f;
        fill_circle(b->trailX[idx], b->trailY[idx], b->radius * (0.45f + a * 0.55f),
                    0x93c5fd, 0.035f + a * 0.06f);
    }
    fill_circle(b->x, b->y, b->radius * 3.4f, 0x38bdf8, 0.08f);
    fill_circle(b->x, b->y, b->radius * 1.45f, 0x7dd3fc, 0.28f);
    fill_circle(b->x, b->y, b->radius, 0xf8fafc, 1.0f);
    fill_circle(b->x - b->radius * 0.28f, b->y - b->radius * 0.32f,
                fmaxf(1.0f, b->radius * 0.32f), 0xffffff, 0.9f);
    if (b->attached) {
        ring(b->x, b->y, b->radius + 7 * s + sinf(G.frameCount * 0.12f) * 2 * s,
             1.0f * s, 0xfacc15, 0.48f);
    }
}

static void draw_balls(void)
{
    for (int i = 0; i < MAX_BALLS; i++)
        draw_ball(&G.balls[i]);
}

static void draw_powerups(void)
{
    for (int i = 0; i < MAX_POWERUPS; i++) {
        Powerup *u = &G.powerups[i];
        if (!u->active) continue;
        float s = G.scale;
        float w = 39.0f * s;
        float h = 16.0f * s;
        float x = u->x - w * 0.5f;
        float y = u->y - h * 0.5f;
        uint32_t col = powerup_color(u->type);
        float bob = sinf(u->phase) * 1.5f * s;
        fill_rect(x - 4 * s, y + bob - 4 * s, w + 8 * s, h + 8 * s, col, 0.12f);
        fill_rect(x, y + bob, w, h, scale_rgb(col, 0.72f), 1.0f);
        fill_rect(x + 2 * s, y + bob + 1 * s, w - 4 * s, h * 0.43f,
                  scale_rgb(col, 1.20f), 0.95f);
        stroke_rect(x, y + bob, w, h, 1.0f * s, 0xffffff, 0.28f);
        char label[2] = { POWERUP_NAMES[u->type][0], 0 };
        draw_text_center(u->x, y + bob + h * 0.5f - 7 * s, label, 0x030712, 0.9f, 1);
    }
}

static void draw_particles(void)
{
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &G.particles[i];
        if (!p->active) continue;
        float a = p->maxLife > 0 ? clampf(p->life / p->maxLife, 0, 1) : 1;
        float size = fmaxf(1.0f, p->size * (0.35f + 0.65f * a));
        if (size < 2.2f) fill_rect(p->x, p->y, size, size, p->color, a);
        else fill_circle(p->x, p->y, size * 0.5f, p->color, a);
    }
}

static void draw_hud(void)
{
    float s = G.scale;
    char buf[160];
    fill_rect(0, 0, W, 34 * s, 0x020617, 0.82f);
    draw_text_shadow(16 * s, 10 * s, "KITTY BROKEOUT", 0x7dd3fc, 1.0f, 1);

    snprintf(buf, sizeof buf, "SCORE %06d", G.score);
    draw_text_shadow(188 * s, 10 * s, buf, 0xf8fafc, 1.0f, 1);
    snprintf(buf, sizeof buf, "LEVEL %02d", G.level);
    draw_text_shadow(330 * s, 10 * s, buf, 0xfacc15, 1.0f, 1);
    snprintf(buf, sizeof buf, "LIVES %d", G.lives);
    draw_text_shadow(434 * s, 10 * s, buf, G.lives <= 1 ? 0xf87171 : 0x86efac, 1.0f, 1);

    if (G.combo > 2 && G.comboTimer > 0.0f) {
        snprintf(buf, sizeof buf, "COMBO x%d", G.combo);
        draw_text_shadow(W - 250 * s, 10 * s, buf, 0xf9a8d4, 1.0f, 1);
    }
    if (G.speedBoostTimer > 0.0f)
        draw_text_shadow(W - 132 * s, 10 * s, "OVERDRIVE", 0x22d3ee, 1.0f, 1);

    fill_rect(0, H - 26 * s, W, 26 * s, 0x020617, 0.70f);
    draw_text(14 * s, H - 20 * s,
              "LEFT/A RIGHT/D move   SPACE launch   P pause   M sound   R restart   Q quit",
              0x64748b, 1.0f, 1);
}

static void panel(float w, float h, float *px, float *py)
{
    *px = (W - w) * 0.5f;
    *py = (H - h) * 0.5f;
    fill_rect(*px - 10 * G.scale, *py - 10 * G.scale, w + 20 * G.scale, h + 20 * G.scale,
              0x38bdf8, 0.08f);
    fill_rect(*px - 3 * G.scale, *py - 3 * G.scale, w + 6 * G.scale, h + 6 * G.scale,
              0xa855f7, 0.24f);
    fill_rect(*px, *py, w, h, 0x09090f, 0.94f);
    stroke_rect(*px, *py, w, h, 2.0f * G.scale, 0x38bdf8, 0.42f);
}

static void draw_title(void)
{
    float s = G.scale;
    float pw = fminf(W - 70 * s, 720 * s);
    float ph = 320 * s;
    float px, py;
    panel(pw, ph, &px, &py);

    int titleScale = W >= 900 ? 3 : 2;
    draw_text_center(W * 0.5f, py + 36 * s, "KITTY BROKEOUT", 0x7dd3fc, 0.22f, titleScale);
    draw_text_center(W * 0.5f - 2, py + 34 * s - 2, "KITTY BROKEOUT", 0xf8fafc, 1.0f, titleScale);
    draw_text_center(W * 0.5f, py + 92 * s,
                     "native C brick breaker for kitty graphics terminals",
                     0xa5b4fc, 1.0f, 1);

    float bx = px + 95 * s;
    float by = py + 128 * s;
    float bw = (pw - 190 * s) / 8.0f;
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 8; c++) {
            uint32_t col = r == 0 ? 0xef4444 : r == 1 ? 0xf97316 : 0x22d3ee;
            fill_rect(bx + c * bw + 2 * s, by + r * 21 * s, bw - 4 * s, 14 * s,
                      scale_rgb(col, 0.68f), 1.0f);
            fill_rect(bx + c * bw + 4 * s, by + r * 21 * s + 2 * s, bw - 8 * s, 5 * s,
                      scale_rgb(col, 1.25f), 0.9f);
        }
    }
    fill_circle(W * 0.5f + 72 * s, py + 230 * s, 8 * s, 0xf8fafc, 1.0f);
    draw_line(W * 0.5f + 64 * s, py + 222 * s, W * 0.5f + 8 * s, py + 182 * s,
              2 * s, 0x93c5fd, 0.45f);
    fill_rect(W * 0.5f - 90 * s, py + 254 * s, 180 * s, 12 * s, 0x86efac, 0.9f);

    draw_text_center(W * 0.5f, py + 274 * s, "ENTER / SPACE  START", 0xfacc15, 1.0f, 1);
    draw_text_center(W * 0.5f, py + 298 * s, "C controls     M sound     Q quit", 0x94a3b8, 1.0f, 1);
}

static void draw_controls(void)
{
    float s = G.scale;
    float pw = fminf(W - 68 * s, 760 * s);
    float ph = 348 * s;
    float px, py;
    panel(pw, ph, &px, &py);
    draw_text_center(W * 0.5f, py + 28 * s, "CONTROLS", 0x7dd3fc, 1.0f, 2);
    draw_text(px + 74 * s, py + 84 * s,  "LEFT / A       move paddle left", 0xf8fafc, 1.0f, 1);
    draw_text(px + 74 * s, py + 116 * s, "RIGHT / D      move paddle right", 0xf8fafc, 1.0f, 1);
    draw_text(px + 74 * s, py + 148 * s, "SPACE / ENTER  launch and advance", 0xf8fafc, 1.0f, 1);
    draw_text(px + 74 * s, py + 180 * s, "P              pause", 0xf8fafc, 1.0f, 1);
    draw_text(px + 74 * s, py + 228 * s, "Metal bricks bounce. Purple bricks explode.", 0xa5b4fc, 1.0f, 1);
    draw_text(px + 74 * s, py + 256 * s, "Cyan bricks trigger overdrive but drop slow capsules.", 0xa5b4fc, 1.0f, 1);
    draw_text_center(W * 0.5f, py + 314 * s, "ENTER / ESC  BACK", 0x64748b, 1.0f, 1);
}

static void draw_center_message(const char *title, uint32_t titleCol,
                                const char *line1, const char *line2)
{
    float s = G.scale;
    float px, py;
    panel(480 * s, 210 * s, &px, &py);
    draw_text_center(W * 0.5f, py + 28 * s, title, titleCol, 1.0f, 3);
    draw_text_center(W * 0.5f, py + 96 * s, line1, 0xf8fafc, 1.0f, 1);
    draw_text_center(W * 0.5f, py + 128 * s, line2, 0xa1a1aa, 1.0f, 1);
}

static void draw_state_overlay(void)
{
    char buf1[96], buf2[96];
    if (G.state == GS_TITLE) draw_title();
    else if (G.state == GS_CONTROLS) draw_controls();
    else if (G.state == GS_PAUSED)
        draw_center_message("PAUSED", 0xfacc15, "P / ESC resume", "R restart     Q quit");
    else if (G.state == GS_LEVEL_CLEAR) {
        snprintf(buf1, sizeof buf1, "LEVEL %d CLEAR   SCORE %d", G.level, G.score);
        snprintf(buf2, sizeof buf2, "ENTER next level     LIVES %d", G.lives);
        draw_center_message("CLEAR", 0x86efac, buf1, buf2);
    } else if (G.state == GS_BALL_LOST) {
        snprintf(buf1, sizeof buf1, "BALL LOST   LIVES %d", G.lives);
        draw_center_message("READY", 0xfacc15, buf1, "SPACE launch");
    } else if (G.state == GS_GAMEOVER) {
        snprintf(buf1, sizeof buf1, "FINAL SCORE %d", G.score);
        snprintf(buf2, sizeof buf2, "BEST %d     ENTER restart", G.highScore);
        draw_center_message("GAME OVER", 0xf87171, buf1, buf2);
    }
}

static void draw_flash(void)
{
    if (G.screenFlash <= 0.0f) return;
    int ai = (int)(G.screenFlash * 0.36f * 256);
    if (ai <= 0) return;
    if (ai > 256) ai = 256;
    uint8_t *p = fb;
    for (size_t i = 0, n = (size_t)W * H; i < n; i++, p += 4) {
        p[0] = (uint8_t)(p[0] + (((255 - p[0]) * ai) >> 8));
        p[1] = (uint8_t)(p[1] + (((240 - p[1]) * ai) >> 8));
        p[2] = (uint8_t)(p[2] + (((210 - p[2]) * ai) >> 8));
    }
}

void render_frame(void)
{
    if (!fb || !backdrop) return;
    memcpy(fb, backdrop, (size_t)W * H * 4);

    float shake = G.cameraShake;
    OX = (int)(sinf(G.frameCount * 12.989f) * shake);
    OY = (int)(cosf(G.frameCount * 9.173f) * shake);

    draw_stars();
    draw_grid();
    draw_playfield_frame();
    draw_bricks();
    draw_powerups();
    draw_particles();
    draw_paddle();
    draw_balls();

    OX = OY = 0;
    draw_hud();
    draw_state_overlay();
    draw_flash();
}
