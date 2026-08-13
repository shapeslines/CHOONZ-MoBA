// tools/visualize — headless visual dumps of the fixed-point math the determinism
// suite (tests/det) asserts numerically. Writes 24-bit BMP images (open natively in
// Windows Photos/Paint) so the LUT/approximation behaviour becomes something you can
// SEE, not just a green check:
//   fix_sin_cos.bmp    fix_sin / fix_cos over [0,2pi) vs libm
//   fix_trig_error.bmp the (fix - libm) error, magnified, with the +/-2e-3 band the
//                      det suite's CHECK_APPROX actually asserts drawn in red
//   fix_sqrt.bmp       fix_sqrt over [0,64] vs libm, with magnified error
//
// NOT sim code: it uses libm (sin/cos/sqrt) for the REFERENCE curves on purpose — the
// whole point is to compare the deterministic fixed-point path against real math.
// Usage: visualize [out_dir]   (default ".")
#include "math/fix.h"
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <cmath>

using namespace mm;

// Canvas. W*3 must be a multiple of 4 so 24-bit BMP rows need no padding (800*3=2400).
static const int W = 800, H = 400;
static uint8_t g_px[H][W][3];   // [row][col][RGB]

struct Rgb { uint8_t r, g, b; };
static const Rgb BG   = { 18, 18, 26 };
static const Rgb GRID = { 44, 44, 58 };
static const Rgb AXIS = { 90, 90, 110 };
static const Rgb REF  = { 150, 150, 160 };   // libm reference (faint)
static const Rgb SIN  = { 90, 220, 120 };    // fix_sin   (green)
static const Rgb COS  = { 90, 180, 240 };    // fix_cos   (blue)
static const Rgb SQRT = { 240, 170, 70 };    // fix_sqrt  (orange)
static const Rgb TOL  = { 210, 70, 70 };     // test tolerance band (red)

static void clear(Rgb c) {
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) { g_px[y][x][0] = c.r; g_px[y][x][1] = c.g; g_px[y][x][2] = c.b; }
}
static void put(int x, int y, Rgb c) {
    if ((unsigned)x < (unsigned)W && (unsigned)y < (unsigned)H) { g_px[y][x][0] = c.r; g_px[y][x][1] = c.g; g_px[y][x][2] = c.b; }
}
// Thicken a sample into a 3px dot so curves read clearly.
static void dot(int x, int y, Rgb c) {
    put(x, y, c); put(x, y - 1, c); put(x, y + 1, c);
}
static void vfill(int x, int y0, int y1, Rgb c) {     // connect consecutive samples
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    for (int y = y0; y <= y1; ++y) put(x, y, c);
}
static void hline(int y, Rgb c) { for (int x = 0; x < W; ++x) put(x, y, c); }
static void vline(int x, Rgb c) { for (int y = 0; y < H; ++y) put(x, y, c); }

// Map a value in [vmin,vmax] to a pixel row (y grows downward).
static int ymap(double v, double vmin, double vmax) {
    double t = (v - vmin) / (vmax - vmin);
    int y = (int)((1.0 - t) * (H - 1) + 0.5);
    return y;
}

static void w32(uint8_t* p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static void w16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

static bool write_bmp(const char* path) {
    const uint32_t rowbytes = (uint32_t)W * 3;          // no padding (W*3 % 4 == 0)
    const uint32_t imgsize  = rowbytes * (uint32_t)H;
    uint8_t fh[14] = {0}, ih[40] = {0};
    fh[0] = 'B'; fh[1] = 'M';
    w32(fh + 2, 54 + imgsize); w32(fh + 10, 54);        // file size, pixel-data offset
    w32(ih + 0, 40); w32(ih + 4, (uint32_t)W); w32(ih + 8, (uint32_t)H);   // header size, w, h (bottom-up)
    w16(ih + 12, 1); w16(ih + 14, 24);                  // planes, bpp
    w32(ih + 20, imgsize); w32(ih + 24, 2835); w32(ih + 28, 2835);         // image size, 72 DPI x/y

    FILE* f = std::fopen(path, "wb");
    if (!f) { std::printf("  ERROR: cannot open %s\n", path); return false; }
    std::fwrite(fh, 1, 14, f); std::fwrite(ih, 1, 40, f);
    uint8_t row[W * 3];
    for (int y = H - 1; y >= 0; --y) {                  // BMP rows are bottom-up
        for (int x = 0; x < W; ++x) {
            row[x * 3 + 0] = g_px[y][x][2];             // B
            row[x * 3 + 1] = g_px[y][x][1];             // G
            row[x * 3 + 2] = g_px[y][x][0];             // R
        }
        std::fwrite(row, 1, rowbytes, f);
    }
    std::fclose(f);
    std::printf("  wrote %s  (%dx%d)\n", path, W, H);
    return true;
}

static bool make_output_path(char* out, size_t capacity, const char* dir,
                             const char* filename) {
    if (!out || capacity == 0 || !dir || !filename) return false;
    const int written = std::snprintf(out, capacity, "%s/%s", dir, filename);
    if (written < 0 || static_cast<size_t>(written) >= capacity) {
        std::fprintf(stderr, "ERROR: output path too long\n");
        return false;
    }
    return true;
}

static const double PI = 3.14159265358979323846;

// fix_sin / fix_cos over [0,2pi) vs the libm reference.
static bool draw_sin_cos(const char* path) {
    clear(BG);
    const double lo = -1.15, hi = 1.15;
    for (int k = 0; k <= 4; ++k) vline((W - 1) * k / 4, GRID);          // 0, pi/2, pi, 3pi/2, 2pi
    hline(ymap( 1.0, lo, hi), GRID); hline(ymap(-1.0, lo, hi), GRID);
    hline(ymap( 0.0, lo, hi), AXIS);
    int ps = -1, pc = -1;
    for (int x = 0; x < W; ++x) {
        double ang = (double)x / (W - 1) * 2.0 * PI;
        int rs = ymap(std::sin(ang), lo, hi), rc = ymap(std::cos(ang), lo, hi);
        dot(x, rs, REF); dot(x, rc, REF);                                // faint reference
        int fs = ymap((double)fix_to_f32(fix_sin(fix_from_f32((float)ang))), lo, hi);
        int fc = ymap((double)fix_to_f32(fix_cos(fix_from_f32((float)ang))), lo, hi);
        if (ps >= 0) { vfill(x, ps, fs, SIN); vfill(x, pc, fc, COS); }
        dot(x, fs, SIN); dot(x, fc, COS);
        ps = fs; pc = fc;
    }
    return write_bmp(path);
}

// The (fix - libm) error, magnified, with the +/-2e-3 band tests/det asserts.
static bool draw_trig_error(const char* path) {
    clear(BG);
    const double lo = -3.0e-3, hi = 3.0e-3;             // full scale a touch past the tolerance
    for (int k = 0; k <= 4; ++k) vline((W - 1) * k / 4, GRID);
    hline(ymap( 2.0e-3, lo, hi), TOL); hline(ymap(-2.0e-3, lo, hi), TOL);   // the CHECK_APPROX band
    hline(ymap( 0.0, lo, hi), AXIS);
    for (int x = 0; x < W; ++x) {
        double ang = (double)x / (W - 1) * 2.0 * PI;
        double es = (double)fix_to_f32(fix_sin(fix_from_f32((float)ang))) - std::sin(ang);
        double ec = (double)fix_to_f32(fix_cos(fix_from_f32((float)ang))) - std::cos(ang);
        dot(x, ymap(es, lo, hi), SIN);
        dot(x, ymap(ec, lo, hi), COS);
    }
    return write_bmp(path);
}

// fix_sqrt over [0,64] vs libm, with the error magnified x4000 around the curve.
static bool draw_sqrt(const char* path) {
    clear(BG);
    const double xmax = 64.0, lo = -0.5, hi = 8.5;
    for (int k = 0; k <= 8; ++k) vline((W - 1) * k / 8, GRID);
    for (int v = 0; v <= 8; v += 2) hline(ymap((double)v, lo, hi), GRID);
    hline(ymap(0.0, lo, hi), AXIS);
    int pf = -1;
    for (int x = 0; x < W; ++x) {
        double xx = (double)x / (W - 1) * xmax;
        int rr = ymap(std::sqrt(xx), lo, hi);
        dot(x, rr, REF);                                                 // faint reference
        double fv = (double)fix_to_f32(fix_sqrt(fix_from_f32((float)xx)));
        int ff = ymap(fv, lo, hi);
        if (pf >= 0) vfill(x, pf, ff, SQRT);
        dot(x, ff, SQRT);
        pf = ff;
        // error x4000, drawn as a red tick above the curve so it is visible at all
        int ey = ymap(std::sqrt(xx) + (fv - std::sqrt(xx)) * 4000.0, lo, hi);
        put(x, ey, TOL);
    }
    return write_bmp(path);
}

int main(int argc, char** argv) {
    const char* dir = (argc > 1) ? argv[1] : ".";
    char sin_cos_path[512];
    char trig_error_path[512];
    char sqrt_path[512];
    if (!make_output_path(sin_cos_path, sizeof(sin_cos_path), dir, "fix_sin_cos.bmp") ||
        !make_output_path(trig_error_path, sizeof(trig_error_path), dir,
                          "fix_trig_error.bmp") ||
        !make_output_path(sqrt_path, sizeof(sqrt_path), dir, "fix_sqrt.bmp")) {
        return 1;
    }
    std::printf("visualize -> %s\n", dir);
    bool ok = draw_sin_cos(sin_cos_path);  // each draw runs (left operand always evaluates)
    ok = draw_trig_error(trig_error_path) && ok;
    ok = draw_sqrt(sqrt_path) && ok;
    std::printf(ok ? "done.\n" : "FAILED.\n");
    return ok ? 0 : 1;
}
