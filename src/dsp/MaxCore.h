// Thin wrapper: Hydroampf's parameters -> the Maximus port in Maximus.h.
// SPDX-License-Identifier: GPL-3.0-or-later (links GPL code from Lemon Studio, see Maximus.h)
#pragma once
#include "Maximus.h"
#include <memory>

namespace mx {

inline float dbToLin(float d) { return std::pow(10.0f, d * 0.05f); }

struct Params { float amount = 0.5f; int mode = 0; float mix = 1.0f; float ceilingDb = -0.3f; };

// Reports ONE constant latency (the worst case of modes A-D) so the host can compensate it exactly; modes with
// less look-ahead are padded with extra delay. Amount is smoothed (no zipper noise) and a mode change is
// faded in (no click).
class MaxCore {
public:
    MaxCore() : m(new mx2::Maximus()) { for (auto& v : dry) v.assign(kRing, 0.f); for (auto& v : wet) v.assign(kRing, 0.f); }

    void prepare(float sr) {
        sampleRate = sr; m->prepare(sr);
        int best = 0;
        for (int mode = 0; mode < 4; ++mode) { m->configure(mode, 1.0f, true); best = std::max(best, m->latency()); }
        latencyConst = std::min(best, kRing - 8);
        m->configure(0, 0.5f, true);
        reset();
    }
    void reset() {
        m->reset(); for (auto& v : dry) std::fill(v.begin(), v.end(), 0.f); for (auto& v : wet) std::fill(v.begin(), v.end(), 0.f);
        at = 0; peak = 0; fade = 1.0f; lastMode = -1; smoothAmt = -1.0f;
    }

    // in -> out, stereo; buffers may alias
    void process(const float* inL, const float* inR, float* outL, float* outR, int n, const Params& p) {
        const float ceilGain = dbToLin(p.ceilingDb) / 0.975f, ceilLin = dbToLin(p.ceilingDb);
        float pk = 0;
        for (int off = 0; off < n; off += kSub) {
            int c = std::min(kSub, n - off);
            if (smoothAmt < 0.f) smoothAmt = p.amount;
            smoothAmt += (p.amount - smoothAmt) * 0.25f;
            if (std::fabs(p.amount - smoothAmt) < 1e-4f) smoothAmt = p.amount;
            if (lastMode != p.mode) { if (lastMode >= 0) fade = 0.0f; lastMode = p.mode; }
            m->configure(p.mode, smoothAmt);
            const int extra = std::max(0, latencyConst - m->latency());
            for (int i = 0; i < c; ++i) { wl[i] = inL[off + i]; wr[i] = inR[off + i]; }
            // dry path: delayed by the constant latency
            for (int i = 0; i < c; ++i) { dry[0][(at + i) % kRing] = wl[i]; dry[1][(at + i) % kRing] = wr[i]; }
            m->process(wl, wr, c);
            for (int i = 0; i < c; ++i) { wet[0][(at + i) % kRing] = wl[i]; wet[1][(at + i) % kRing] = wr[i]; }
            for (int i = 0; i < c; ++i) {
                int w = (at + i - extra + kRing) % kRing, d = (at + i - latencyConst + kRing) % kRing;
                float a = std::clamp(wet[0][w] * ceilGain, -ceilLin, ceilLin), b = std::clamp(wet[1][w] * ceilGain, -ceilLin, ceilLin);
                fade = std::min(1.0f, fade + 1.0f / 480.0f);
                float mixv = p.mix * fade;
                float oa = dry[0][d] * (1.0f - mixv) + a * mixv, ob = dry[1][d] * (1.0f - mixv) + b * mixv;
                outL[off + i] = oa; outR[off + i] = ob; pk = std::max(pk, std::max(std::fabs(oa), std::fabs(ob)));
            }
            at = (at + c) % kRing;
        }
        peak = pk;
    }
    float getGainReductionDb() const { return m->gainReductionDb(); }
    float getLastPeak() const { return peak; }
    int latency() const { return latencyConst; }

private:
    static constexpr int kRing = 1 << 15, kSub = 64;
    std::unique_ptr<mx2::Maximus> m;
    std::vector<float> dry[2], wet[2];
    float wl[kSub], wr[kSub];
    int at = 0, latencyConst = 0, lastMode = -1; float peak = 0, sampleRate = 48000.f, fade = 1.0f, smoothAmt = -1.0f;
};

} // namespace mx
