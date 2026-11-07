// C++ port of the Maximus-style multiband maximiser from Lemon Studio ("Press" plugin,
// crates/os-engine/src/effects/maximus/*.rs).
// Copyright (C) 2026 ItsAlphaNeon (original Rust code); C++ port derived from it.
// SPDX-License-Identifier: GPL-3.0-or-later
// Lemon Studio: https://github.com/ItsAlphaNeon/Lemon-Studio  -> this file, and any plugin built with it,
// is therefore GPL-3.0-or-later and must be distributed with its source.
//
// Not ported: FIR (linear-phase) crossover, oversampling, mid-trigger, solo, non-"single" curve modes of the
// map editor (none of the Enhancer A..D presets use them).
#pragma once
#include "MaximusPresets.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <vector>

namespace mx2 {

constexpr double PI = 3.14159265358979323846;
constexpr double TINY = 1.0 / 16777216.0;
constexpr int BANDS = 4, LOW = 0, MID = 1, HIGH = 2, MASTER = 3;
constexpr int P_LOW_FREQ = 0, P_LOW_SLOPE = 1, P_HIGH_FREQ = 2, P_HIGH_SLOPE = 3, P_LMH_DELAY = 4, P_LMH_MIX = 5, P_LOW_CUT = 6;
constexpr int B_MODE = 0, B_PRE = 1, B_POST = 2, B_SEP = 3, B_THRES = 4, B_CEIL = 5, B_ATT = 6, B_REL2 = 7,
              B_ATT_STAGES = 8, B_REL = 9, B_REL_STAGES = 10, B_WINDOW = 11, B_RMS = 12;
constexpr int MODE_ON = 0, MODE_COMP_OFF = 1, MODE_MUTED = 2, MODE_OFF = 3;
constexpr int ENTRIES = 32768;
constexpr int MAXBLOCK = 4096;
constexpr int LINE_CAP = 8192;
constexpr int DET_CAP = 1 << 16;

constexpr int band(int b, int k) { return 7 + 13 * b + k; }
inline float F(double x) { return (float)x; }
inline double rnd(double x) { return std::nearbyint(x); } // ties-to-even (default rounding mode)

inline float gainLaw(int v) {
    if (v == 1000) return 1.0f;
    float a = (float)v * 0.001f, b = a * F(std::log(7.0));
    return F((std::exp((double)b) - 1.0) * (double)F(1.0 / 6.0));
}
inline double samplesLaw(int v, double rate) {
    float x = (float)v * 0.0001f;
    float m = F(std::exp((double)x * std::log(501.0)) - 1.0);
    return (double)m * 2.0 * 0.001 * rate;
}
inline float crossoverHz(int v) {
    return F(std::exp((double)v * (std::log(533.3333333333334) / (std::log(2.0) * 65536.0)) * std::log(2.0)) * 30.0);
}
inline float lowCutHz(int v) {
    return F(std::exp((double)v * (std::log(16.0) / (std::log(2.0) * 65536.0)) * std::log(2.0)) * 5.0);
}
inline double smoothCoef(double t) { return t <= 0.0 ? 0.0 : std::exp(std::log(0.01) / std::max(t, 1.0)); }

// ---- biquads -----------------------------------------------------------------------------------------------
struct Coefs {
    double c[5] = {1, 0, 0, 0, 0};
    static Coefs lowpass(double alpha, double w) {
        double n = 1.0 / (alpha + 1.0), cs = std::cos(w), b1 = n * (1.0 - cs);
        return {{b1 * 0.5, b1, b1 * 0.5, n * cs * -2.0, n * (1.0 - alpha)}};
    }
    static Coefs highpass(double alpha, double w) {
        double n = 1.0 / (alpha + 1.0), cs = std::cos(w), b1 = n * -(cs + 1.0);
        return {{-b1 * 0.5, b1, -b1 * 0.5, n * cs * -2.0, n * (1.0 - alpha)}};
    }
    static Coefs allpass(double alpha, double w) {
        double n = 1.0 / (alpha + 1.0), cs = std::cos(w), a1 = n * cs * -2.0, a2 = n * (1.0 - alpha);
        return {{a2, a1, 1.0, a1, a2}};
    }
};
struct Bq {
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    float run(const Coefs& k, float xin) {
        double x = xin;
        double y = x * k.c[0] + (((x1 * k.c[1] - y2 * k.c[4]) + x2 * k.c[2]) - y1 * k.c[3]);
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return (float)y;
    }
    void tidy() { for (double* v : {&x1, &x2, &y1, &y2}) if (std::fabs(*v) < TINY) *v = 0.0; }
};
struct Chain {
    Coefs c[4]; int n = 0; Bq st[2][4];
    void set(const Coefs* k, int cnt) { n = cnt; for (int i = 0; i < cnt; ++i) c[i] = k[i]; }
    void clear() { for (auto& s : st) for (auto& b : s) b = Bq(); }
    void tidy() { for (auto& s : st) for (auto& b : s) b.tidy(); }
    void run(int a, int bEnd, int ch, float* x, int len) {
        for (int i = a; i < bEnd && i < n; ++i) for (int j = 0; j < len; ++j) x[j] = st[ch][i].run(c[i], x[j]);
    }
    void runAll(int ch, float* x, int len) { run(0, n, ch, x, len); }
};

// ---- detector ----------------------------------------------------------------------------------------------
struct Detector {
    int w = 1; bool rms = false;
    std::vector<uint64_t> pt; std::vector<float> pv; int head = 0, tail = 0; uint64_t t = 0; // monotonic deque ring
    std::vector<float> ring; int at = 0; double sum = 0, scale = 0;
    Detector() : pt(DET_CAP), pv(DET_CAP), ring(DET_CAP, 0.f) {}
    void start(int ww, bool r) {
        w = std::max(1, std::min(ww, DET_CAP - 1)); rms = r; head = tail = 0; t = 0;
        std::fill(ring.begin(), ring.end(), 0.f); at = 0; sum = 0.0; scale = (double)F(1.0 / (2.0 * w));
    }
    float level(float l, float r) {
        if (rms) {
            float q = l * l + r * r, old = ring[at]; ring[at] = q;
            at = (at + 1 == w) ? 0 : at + 1;
            sum = (sum - (double)old) + (double)q;
            float mean = std::max(F(sum * scale), 0.0f);
            return std::sqrt(mean);
        }
        float v = std::max(std::fabs(l), std::fabs(r));
        while (tail != head && pv[(head - 1 + DET_CAP) % DET_CAP] <= v) head = (head - 1 + DET_CAP) % DET_CAP;
        pt[head] = t; pv[head] = v; head = (head + 1) % DET_CAP;
        while (tail != head && pt[tail] + (uint64_t)w <= t) tail = (tail + 1) % DET_CAP;
        ++t;
        return tail != head ? pv[tail] : 0.0f;
    }
};

// ---- gain table from a map ---------------------------------------------------------------------------------
struct Segment {
    int mode = 0; bool neg = false, line = true; double c0 = 0, c1 = 0, c2 = 0;
    double progress(double x) const {
        if (line) return x;
        auto single = [&](double v) { return neg ? ((std::exp(v * c2) - 1.0) * c1) : 1.0 - ((std::exp((1.0 - v) * c2) - 1.0) * c1); };
        if (mode == 0) return single(x);
        if (mode == 1) return x < 0.5 ? single(2.0 * x) / 2.0 : 1.0 - single(2.0 * (1.0 - x)) / 2.0;
        return x;
    }
};
struct Table {
    std::vector<float> c; int limit = 0; double top = 1.0, reach = 1.0;
    Table() : c(ENTRIES + 2, 1.0f) {}
    void build(const PresetData& pd, int b, bool full) {
        const double LN3 = 1.0986122886681098;
        int n = pd.npts[b]; const MapPt* mp = pd.pts[b];
        double total = 0; for (int i = 0; i < n; ++i) total += std::max(mp[i].dist, 0.0);
        double k = (total > 0.0 && total != 1.0) ? 1.0 / total : 1.0;
        double px[8], py[8], xacc = 0;
        for (int i = 0; i < n; ++i) { xacc += std::max(mp[i].dist, 0.0) * k; px[i] = xacc; py[i] = (double)(float)mp[i].level; }
        double ceiling = full ? 1.0 : (double)0.975f, half = ceiling * 0.5;
        // walk state
        int wi = 0; double p, r, from = py[0], by = 0.0; bool haveSeg = false; Segment seg;
        if (px[0] == 0.0) { p = 1.0; r = 1.0; } else { p = 0.0; r = 1.0 / px[0]; }
        auto dist = [&](int q) { return px[q] - (q == 0 ? 0.0 : px[q - 1]); };
        auto makeSeg = [&](int i) {
            Segment s; if (i == 0 || i >= n) return s;
            double t = (double)mp[i].tension; if (t == 0.0 && (mp[i].mode == 0 || mp[i].mode == 1)) return s;
            if (mp[i].mode == 0 || mp[i].mode == 1) {
                s.line = false; s.mode = mp[i].mode; s.neg = t < 0.0;
                double v = std::exp(std::fabs(t) * std::log(1000001.0)) - 1.0; s.c1 = 1.0 / v; s.c2 = std::log(v + 1.0);
            }
            return s;
        };
        auto step = [&](double bb) {
            p += bb * r;
            while (p >= 1.0) {
                double left = (r == 0.0) ? bb : (p - 1.0) / r;
                from = py[wi]; bool moved = wi + 1 < n; if (moved) ++wi;
                by = py[wi] - from; haveSeg = false;
                if (!moved) { p = 0.0; r = 0.0; }
                else if (dist(wi) > 0.0) { r = 1.0 / dist(wi); p = left * r; }
                else { r = 1.0; p = left + 1.0; }
            }
        };
        auto value = [&]() {
            if (by == 0.0) return from;
            if (!haveSeg) { seg = makeSeg(wi); haveSeg = true; }
            return seg.progress(p) * by + from;
        };
        int lim = 0; double topv = 0.0, most = -1e300, prev = 0.0;
        for (int i = 0; i <= ENTRIES; ++i) {
            double level = (double)std::max(i, 1) / 8192.0;
            double x = std::log(level * 2.0 + 1.0) * (0.5 / LN3);
            step(x - prev); double y = value(); prev = x;
            double out = (std::exp(y * 2.0 * LN3) - 1.0) * half;
            if (std::fabs(out - ceiling) < TINY) out = ceiling; else lim = i;
            c[i] = (float)(out / level); topv = out; most = std::max(most, out);
        }
        c[lim + 1] = c[lim]; limit = lim; top = topv; reach = most / ceiling;
    }
};

// ---- saturation --------------------------------------------------------------------------------------------
struct Sat {
    enum Kind { Off, Tab, Knee } kind = Off;
    std::vector<float> t; double scale = 0, ceil = 1; float knee = 0, width = 1, r = 1;
    Sat() : t(65538, 0.f) {}
    void build(int threshold, int ceilKnob, double reachUsed, float post) {
        double cl = (double)gainLaw(ceilKnob); ceil = cl;
        if (threshold == 0) { kind = Off; return; }
        if (threshold > 0) {
            kind = Knee; knee = F((double)(1.0f - (float)threshold * 0.001f) * cl);
            double wd = cl - (double)knee; if (wd <= 0.0) wd = 1.0 / 65536.0;
            width = (float)wd; r = (float)(1.0 / wd); return;
        }
        kind = Tab;
        double loud = std::max(reachUsed * (double)post, cl);
        double depth = (double)(F((double)threshold * -0.1) + 1.0);
        float q = F(std::log(depth) / std::log(101.0));
        double hardness = (double)(20.0f - q * 19.0f);
        auto f = [&](double x) { return std::pow(std::atan(std::pow(x, hardness)), 1.0 / hardness); };
        double step = (cl == 0.0) ? 1.0 : loud / (cl * 65536.0);
        double top = f(step * 65536.0);
        for (int i = 0; i <= 65536; ++i) { float v = F(f((double)i * step) / top); t[i] = ((double)v < TINY) ? 0.f : v; }
        t[65537] = t[65536];
        scale = (double)F(1.0 / loud) * 65536.0;
    }
    float run(float x) const {
        if (kind == Off) return x;
        if (kind == Tab) {
            double pos = (double)x * scale;
            if (!(pos > -2147483648.0 && pos < 2147483648.0)) return 0.0f;
            double w = std::trunc(pos), fr = pos - w;
            size_t i = std::min((size_t)std::fabs(w), (size_t)65536);
            double a = t[i], b = t[i + 1];
            double y = (w >= 0.0) ? a + (b - a) * fr : -(a - (b - a) * fr);
            return (float)(y * ceil);
        }
        float a = std::fabs(x); if (a <= knee) return x;
        float v = F((1.0 - std::exp(((double)knee - (double)a) * (double)r)) * (double)width + (double)knee);
        return std::copysign(v, x);
    }
};

inline void separation(int knob, float out[4]) {
    float x = (float)knob * 0.001f;
    if (x == 0.0f) { out[0] = 1; out[1] = 1; out[2] = 0; out[3] = 0; return; }
    double q = PI / 4.0; float u = F(1.0 / std::cos(q));
    float l = F(std::cos(q) * (double)u), r = F(std::sin(q) * (double)u);
    float m = x < 0.0f ? x : F(std::exp((double)x * std::log(5.0)) - 1.0) * 0.25f;
    float k = std::min(m, 1.0f) * 0.5f;
    out[0] = l * (1.0f - k); out[1] = r * (1.0f - k); out[2] = l * k; out[3] = r * k;
}

// ---- one band ----------------------------------------------------------------------------------------------
struct Band {
    int mode = 0; int look = 0, attN = 0, relN = 0;
    double attRise = 0, attFall = 0, relFall = 0, att[8] = {}, rel[8] = {};
    float pre = 1, post = 1, rest = 1, sep[4] = {1, 1, 0, 0}, sign = 1;
    Detector det; std::vector<float> line[2]; int lineLen = 0, lineAt = 0;
    const Table* table = nullptr; const Sat* sat = nullptr;
    float last[2] = {0, 1}; float lastComp = 1.0f;
    Band() { line[0].assign(LINE_CAP, 0.f); line[1].assign(LINE_CAP, 0.f); }
    void setLine(int len) { lineLen = std::min(len, LINE_CAP); lineAt = 0; clearLine(); }
    void clearLine() { std::fill(line[0].begin(), line[0].end(), 0.f); std::fill(line[1].begin(), line[1].end(), 0.f); lineAt = 0; }
    void reset() { for (int i = 0; i < attN; ++i) att[i] = (double)rest; for (double& v : rel) v = 0.0; clearLine(); }
    void tidy() { for (double& v : att) if (std::fabs(v) <= TINY) v = 0.0; for (double& v : rel) if (std::fabs(v) <= TINY) v = 0.0; }
    void push(float l, float r, float& ol, float& orr) {
        if (lineLen == 0) { ol = l; orr = r; return; }
        ol = line[0][lineAt]; orr = line[1][lineAt]; line[0][lineAt] = l; line[1][lineAt] = r;
        lineAt = (lineAt + 1 == lineLen) ? 0 : lineAt + 1;
    }
    void run(float* dl, float* dr, int n, const float target[4], float limit, bool jump, const float* mixp) {
        if (mode == MODE_OFF) { for (int i = 0; i < n; ++i) { dl[i] *= post; dr[i] *= post; } return; }
        if (mode == MODE_MUTED) return;
        if (jump) std::memcpy(sep, target, sizeof(sep));
        float step[4], now[4]; std::memcpy(now, sep, sizeof(now));
        for (int k = 0; k < 4; ++k) {
            float cur = sep[k], st = (target[k] - cur) / (float)n, end;
            if (std::fabs(st) <= limit) end = target[k];
            else {
                st = std::copysign(limit, target[k] - cur);
                float e = cur + st * (float)n;
                if (std::fabs((double)e) < TINY) { e = 0.f; st = -cur / (float)n; }
                end = e;
            }
            step[k] = st; sep[k] = end;
        }
        bool comp = mode == MODE_ON;
        float pp = pre * post, pk = pre * 8192.25f, topg = F(table->top * (double)post);
        bool mixed = false; float a = 0, b = sign;
        if (mixp && *mixp != 1.0f) { mixed = true; a = (1.0f - *mixp) * sign; b = *mixp * sign; }
        for (int i = 0; i < n; ++i) {
            float ll = now[0], rr = now[1], rl = now[2], lr = now[3];
            float x = dl[i], y = dr[i], sl = x * ll + y * rl, sr = y * rr + x * lr;
            dl[i] = sl; dr[i] = sr;
            for (int k = 0; k < 4; ++k) now[k] += step[k];
            float g = post;
            if (comp) {
                float v = det.level(sl, sr);
                for (int s = 0; s < relN; ++s) { double c = ((double)v >= rel[s]) ? 0.0 : relFall; rel[s] = (double)v + (rel[s] - (double)v) * c; v = (float)rel[s]; }
                float p = v * pk; int64_t j = std::max<int64_t>((int64_t)std::trunc(p), 0);
                float gv;
                if (j > table->limit) gv = topg / v;
                else { float c0 = table->c[j], c1 = table->c[j + 1]; gv = (c0 + (c1 - c0) * (p - (float)j)) * pp; }
                for (int s = 0; s < attN; ++s) { double c = ((double)gv >= att[s]) ? attRise : attFall; att[s] = (double)gv + (att[s] - (double)gv) * c; gv = (float)att[s]; }
                g = gv; last[0] = v; last[1] = gv; lastComp = (pp > 0.f) ? gv / pp : 1.0f;
            }
            float ul, ur; push(sl, sr, ul, ur);
            float ol = sat->run(ul * g), orr = sat->run(ur * g);
            if (mixp) {
                if (mixed) { ol = std::fma(ul, a, ol * b); orr = std::fma(ur, a, orr * b); }
                else { ol *= b; orr *= b; }
            }
            dl[i] = ol; dr[i] = orr;
        }
    }
};

// Tables and saturation curves of the four presets: built once per process, shared by all instances.
struct Shared {
    std::vector<Table> tabs; std::vector<Sat> sats;
    Shared() : tabs(4 * BANDS), sats(4 * BANDS) {
        for (int m = 0; m < 4; ++m) for (int b = 0; b < BANDS; ++b) {
            const PresetData& pd = kPresets[m];
            tabs[m * BANDS + b].build(pd, b, false);
            const Table& t = tabs[m * BANDS + b];
            double reachUsed = (pd.params[band(b, B_RMS)] == 0 && pd.params[band(b, B_MODE)] == 0) ? t.reach : 4.0;
            sats[m * BANDS + b].build(pd.params[band(b, B_THRES)], pd.params[band(b, B_CEIL)], reachUsed, gainLaw(pd.params[band(b, B_POST)]));
        }
    }
};
inline const Shared& shared() { static const Shared s; return s; }

// ---- the whole effect ------------------------------------------------------------------------------------
class Maximus {
public:
    Maximus() : sh(&shared()) {}

    void prepare(double sampleRate) {
        rate = std::max(1.0, sampleRate);
        mode = -1; amount = -1.0f;
        configure(0, 0.5f, true);
    }
    int currentMode() const { return mode; }
    void reset() {
        for (auto& b : bands) b.reset();
        for (Chain* c : {&lowLp, &lowAp, &midHp, &midLp, &highHp, &lowCut}) c->clear();
        jump = true;
    }
    // mode 0..3 = A..D; amount 0..1 scales the drive (pre/post gains) from neutral to the preset's full setting
    void configure(int newMode, float newAmount, bool force = false) {
        newMode = std::clamp(newMode, 0, 3); newAmount = std::clamp(newAmount, 0.0f, 1.0f);
        if (!force && newMode == mode && newAmount == amount) return;
        bool modeChanged = (newMode != mode); mode = newMode; amount = newAmount;
        const PresetData& pd = kPresets[mode];
        for (int i = 0; i < 59; ++i) p[i] = pd.params[i];
        for (int b = 0; b < BANDS; ++b) {
            auto lerpK = [&](int k) { int v = pd.params[band(b, k)]; p[band(b, k)] = (int)std::lround(1000 + amount * (v - 1000)); };
            lerpK(B_PRE); lerpK(B_POST);
        }
        if (modeChanged || force) {
            for (int b = 0; b < BANDS; ++b) { bands[b].table = &sh->tabs[mode * BANDS + b]; bands[b].sat = &sh->sats[mode * BANDS + b]; }
            modes(); crossover(); lowCutCoefs();
            for (int b = 0; b < BANDS; ++b) times(b, true);
            for (int b = 0; b < BANDS; ++b) gains(b);
            for (auto& b : bands) b.reset();
            for (Chain* c : {&lowLp, &lowAp, &midHp, &midLp, &highHp}) c->clear();
            jump = true;
        } else {
            for (int b = 0; b < BANDS; ++b) gains(b);
        }
    }
    int latency() const {
        int lmh = std::max({bands[0].look, bands[1].look, bands[2].look});
        return lmh + bands[MASTER].look;
    }
    void process(float* left, float* right, int total) {
        for (int off = 0; off < total; off += MAXBLOCK) block(left + off, right + off, std::min(MAXBLOCK, total - off));
    }
    // compression-only gain of the strongest band, in dB (<= 0)
    float gainReductionDb() const {
        float g = 1.0f; for (int b = 0; b < BANDS; ++b) if (bands[b].mode == MODE_ON) g = std::min(g, bands[b].lastComp);
        return 20.0f * std::log10(std::max(g, 1e-6f)) > 0.f ? 0.f : 20.0f * std::log10(std::max(g, 1e-6f));
    }

private:
    void modes() {
        int m[BANDS]; for (int b = 0; b < BANDS; ++b) m[b] = p[band(b, B_MODE)];
        for (int b = 0; b < BANDS; ++b) bands[b].mode = m[b];
        lowEdge = m[LOW] != MODE_OFF; highEdge = m[HIGH] != MODE_OFF;
    }
    void crossover() {
        double rateB = rate, lim = (double)F(0.4999 * rateB);
        double flo = (double)crossoverHz(p[P_LOW_FREQ]), fhi = (double)crossoverHz(p[P_HIGH_FREQ]);
        if (lowEdge && highEdge && flo > fhi) std::swap(flo, fhi);
        int slo = p[P_LOW_SLOPE], shi = p[P_HIGH_SLOPE];
        double k24 = 1.0 / (std::sqrt(0.5) * 2.0), angle = (1.0 / rateB) * 2.0 * PI;
        auto edge = [&](Chain& ch, double f, int slope, bool high, bool enabled) {
            if (!enabled) { ch.set(nullptr, 0); return; }
            double w = std::min(f, lim) * angle; int n = slope == 0 ? 1 : 2; double alpha = slope == 0 ? std::sin(w) : std::sin(w) * k24;
            Coefs c[2]; for (int i = 0; i < n; ++i) c[i] = high ? Coefs::highpass(alpha, w) : Coefs::lowpass(alpha, w);
            ch.set(c, n);
        };
        edge(lowLp, flo, slo, false, lowEdge); edge(midHp, flo, slo, true, lowEdge);
        edge(midLp, fhi, shi, false, highEdge); edge(highHp, fhi, shi, true, highEdge);
        if (highEdge && shi != 0) { double w = std::min(fhi, lim) * angle; Coefs c = Coefs::allpass(std::sin(w), w); lowAp.set(&c, 1); }
        else lowAp.set(nullptr, 0);
        bands[LOW].sign = (slo == 0) ? -1.0f : 1.0f; bands[HIGH].sign = (shi == 0) ? -1.0f : 1.0f;
        bands[MID].sign = 1.0f; bands[MASTER].sign = 1.0f;
    }
    void lowCutCoefs() {
        int v = p[P_LOW_CUT]; if (v < 0) { lowCut.set(nullptr, 0); return; }
        double w = (double)lowCutHz(v) * 0.8 * 2.0 * PI / rate;
        const uint32_t ds[4] = {0x3E4A74F9u, 0x3F1E8C43u, 0x3F2D45AFu, 0x3F8A285Eu};
        Coefs c[4]; for (int i = 0; i < 4; ++i) { float d; std::memcpy(&d, &ds[i], 4); c[i] = Coefs::highpass((double)d * std::sin(w), w); }
        lowCut.set(c, 4);
    }
    void gains(int b) {
        Band& bd = bands[b];
        bd.pre = gainLaw(p[band(b, B_PRE)]); bd.post = gainLaw(p[band(b, B_POST)]);
        bd.rest = bd.pre * bd.post * bd.table->c[0];
    }
    void times(int b, bool fresh) {
        auto k = [&](int x) { return p[band(b, x)]; };
        auto t = [&](int v) { return samplesLaw(v, rate); };
        int mode_ = bands[b].mode;
        bool none = b != MASTER && bands[0].mode > 0 && bands[1].mode > 0 && bands[2].mode > 0;
        int look;
        if (mode_ >= 2 || (b == MASTER && mode_ >= 1) || none) look = 0;
        else { int knob = (b == MASTER) ? k(B_ATT) : p[P_LMH_DELAY]; look = (int)rnd(samplesLaw(knob, rate)); }
        int attN = std::clamp(k(B_ATT_STAGES), 1, 8), relN = (k(B_REL) == 0) ? 0 : std::clamp(k(B_REL_STAGES), 1, 8);
        double a = (k(B_ATT) == 0) ? (double)look : t(k(B_ATT));
        double attack = a * 0.800000011920929 / std::sqrt((double)attN);
        double rel2 = (k(B_REL2) == 0) ? attack : t(k(B_REL2)) * 2.0 / std::sqrt((double)attN);
        int w = std::max({look, 1, (int)rnd(samplesLaw(k(B_WINDOW), rate))});
        double relT = t(k(B_REL)) * 2.0 / std::sqrt((double)std::max(relN, 1));
        bool rms = k(B_RMS) != 0;
        Band& bd = bands[b];
        if (fresh || look != bd.look || bd.lineLen != look) bd.setLine(look);
        bd.look = look;
        if (fresh || attN != bd.attN) for (int i = 0; i < 8; ++i) bd.att[i] = 1.0;
        if (fresh || relN != bd.relN) for (int i = 0; i < 8; ++i) bd.rel[i] = 0.0;
        bd.attN = attN; bd.relN = relN;
        if (fresh || w != bd.det.w || rms != bd.det.rms) bd.det.start(w, rms);
        bd.attRise = smoothCoef(rel2); bd.attFall = smoothCoef(attack); bd.relFall = smoothCoef(relT);
    }
    void block(float* L, float* R, int n) {
        if (n <= 0) return;
        if (lowCut.n > 0) { lowCut.runAll(0, L, n); lowCut.runAll(1, R, n); lowCut.tidy(); }
        std::memcpy(xi[0], L, n * sizeof(float)); std::memcpy(xi[1], R, n * sizeof(float));
        split(n);
        float target[4]; separation(p[band(MASTER, B_SEP)], target);
        float limit = F(44100.0 / rate * 0.001);
        Band& m = bands[MASTER];
        if (m.mode == MODE_MUTED) { std::memset(L, 0, n * sizeof(float)); std::memset(R, 0, n * sizeof(float)); }
        else { m.run(sm[0], sm[1], n, target, limit, jump, nullptr); std::memcpy(L, sm[0], n * sizeof(float)); std::memcpy(R, sm[1], n * sizeof(float)); }
        for (Chain* c : {&lowLp, &lowAp, &midHp, &midLp, &highHp}) c->tidy();
        for (auto& b : bands) b.tidy();
        jump = false;
    }
    void split(int n) {
        float limit = F(44100.0 / rate * 0.001);
        float mix = F((double)((float)p[P_LMH_MIX] * 0.001f));
        bool lo = lowEdge, hi = highEdge; int md[BANDS]; for (int b = 0; b < BANDS; ++b) md[b] = bands[b].mode;
        float tl[4], tm[4], th[4]; separation(p[band(LOW, B_SEP)], tl); separation(p[band(MID, B_SEP)], tm); separation(p[band(HIGH, B_SEP)], th);
        if (lo) for (int ch = 0; ch < 2; ++ch) { std::memcpy(sm[ch], xi[ch], n * 4); lowLp.runAll(ch, sm[ch], n); lowAp.runAll(ch, sm[ch], n); }
        for (int ch = 0; ch < 2; ++ch) {
            float* x = xi[ch]; float* o = mi[ch]; int hp = midHp.n;
            if (hi) { if (hp > 0) midHp.runAll(ch, x, n); std::memcpy(o, x, n * 4); midLp.runAll(ch, o, n); }
            else if (hp > 0) { midHp.run(0, hp - 1, ch, x, n); std::memcpy(o, x, n * 4); midHp.run(hp - 1, hp, ch, o, n); }
            else std::memcpy(o, x, n * 4);
        }
        if (hi) for (int ch = 0; ch < 2; ++ch) { std::memcpy(hiB[ch], xi[ch], n * 4); highHp.runAll(ch, hiB[ch], n); }
        if (lo) {
            if (md[LOW] < 2) bands[LOW].run(sm[0], sm[1], n, tl, limit, jump, &mix);
            else { std::memset(sm[0], 0, n * 4); std::memset(sm[1], 0, n * 4); }
        }
        bool midRuns = md[MID] < 2;
        if (midRuns) bands[MID].run(mi[0], mi[1], n, tm, limit, jump, &mix);
        if (!lo) {
            if (midRuns) { std::memcpy(sm[0], mi[0], n * 4); std::memcpy(sm[1], mi[1], n * 4); }
            else { std::memset(sm[0], 0, n * 4); std::memset(sm[1], 0, n * 4); }
        } else if (midRuns) for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < n; ++i) sm[ch][i] += mi[ch][i];
        if (hi && md[HIGH] < 2) {
            bands[HIGH].run(hiB[0], hiB[1], n, th, limit, jump, &mix);
            for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < n; ++i) sm[ch][i] += hiB[ch][i];
            if (!lo && md[MID] == MODE_OFF) for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < n; ++i) sm[ch][i] += hiB[ch][i];
        }
    }

    double rate = 48000.0; int mode = -1; float amount = -1.0f; int p[59] = {};
    const Shared* sh; Band bands[BANDS];
    Chain lowLp, lowAp, midHp, midLp, highHp, lowCut; bool lowEdge = true, highEdge = true, jump = true;
    float xi[2][MAXBLOCK], sm[2][MAXBLOCK], mi[2][MAXBLOCK], hiB[2][MAXBLOCK];
};

} // namespace mx2
