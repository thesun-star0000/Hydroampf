#pragma once
// Format-independent plugin engine: parameters + DSP + waveform capture.
// Shared by the VST2 wrapper (Hydroampf.cpp) and the CLAP wrapper (ClapMain.cpp).
#include "dsp/MaxCore.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>

enum EngineParam { kPAmount = 0, kPMode, kPMix, kPCeiling, kPNumParams };

constexpr int kWaveCols = 480;   // columns in the scrolling waveform (~9.6 s at 20 ms/col)

class Engine
{
public:
    Engine() {
        set(kPAmount, 0.5f); set(kPMode, 0.0f); set(kPMix, 1.0f); set(kPCeiling, 0.95f);
        prepare(48000.0f);
    }
    // normalised 0..1 parameter access (thread-safe, lock-free)
    void  set(int i, float v) { if (i>=0 && i<kPNumParams) p[i].store(v<0?0:(v>1?1:v), std::memory_order_relaxed); }
    float get(int i) const    { return (i>=0 && i<kPNumParams) ? p[i].load(std::memory_order_relaxed) : 0.0f; }

    int   mode()      const { int m = int(get(kPMode) * 3.0f + 0.5f); return m<0?0:(m>3?3:m); }
    void  setModeIdx(int m) { set(kPMode, float(m) / 3.0f); }

    int latencySamples() const { return core.latency(); }
    float inPeakLin() const { return inMeter.load(std::memory_order_relaxed); }

    void prepare(float sr) {
        if (sr < 8000.0f) sr = 44100.0f;
        if (sr != sampleRate || !prepared) { core.prepare(sr); prepared = true; }
        sampleRate = sr;
        colLen = int(sr * 0.02f); if (colLen < 1) colLen = 1;
        accIn = accOut = 0; accN = 0;
    }
    void reset() { core.reset(); }

    // stereo, in-place safe
    void process(const float* inL, const float* inR, float* outL, float* outR, int n)
    {
        mx::Params pr;
        pr.amount    = get(kPAmount);
        pr.mode      = mode();
        pr.mix       = get(kPMix);
        pr.ceilingDb = -6.0f + get(kPCeiling) * 6.0f;
        // capture input peak first (in case buffers alias)
        float pin = 0; for (int i = 0; i < n; ++i) pin = std::max(pin, std::max(std::fabs(inL[i]), std::fabs(inR[i])));
        // per-column input peaks need finer granularity: split into chunks of <= 64 samples
        int done = 0;
        while (done < n) {
            int m = std::min(64, n - done);
            float cin = 0;
            for (int i = 0; i < m; ++i) cin = std::max(cin, std::max(std::fabs(inL[done+i]), std::fabs(inR[done+i])));
            core.process(inL + done, inR + done, outL + done, outR + done, m, pr);
            float cout_ = 0;
            for (int i = 0; i < m; ++i) cout_ = std::max(cout_, std::max(std::fabs(outL[done+i]), std::fabs(outR[done+i])));
            accIn = std::max(accIn, cin); accOut = std::max(accOut, cout_); accN += m;
            if (accN >= colLen) { pushColumn(accIn, accOut); accIn = accOut = 0; accN = 0; }
            done += m;
        }
        { float pi = inMeter.load(std::memory_order_relaxed); inMeter.store(std::max(pin, pi * 0.93f), std::memory_order_relaxed); }
        float gr = core.getGainReductionDb(), prev = grMeter.load(std::memory_order_relaxed);
        grMeter.store(std::max(gr, prev - 0.8f * float(n) / 512.0f), std::memory_order_relaxed); // dB, <=0
        float pk = core.getLastPeak(), pp = peakMeter.load(std::memory_order_relaxed);
        peakMeter.store(std::max(pk, pp * 0.95f), std::memory_order_relaxed);
    }

    // UI readers
    float grDb() const   { return grMeter.load(std::memory_order_relaxed); }
    float peakLin() const { return peakMeter.load(std::memory_order_relaxed); }
    int   waveHead() const { return head.load(std::memory_order_acquire); }
    float waveIn(int idx) const  { return wIn[idx % kWaveCols]; }
    float waveOut(int idx) const { return wOut[idx % kWaveCols]; }

    // text helpers (used by both wrappers)
    static const char* paramName(int i) {
        static const char* n[kPNumParams] = { "Amount", "Mode", "Mix", "Ceiling" };
        return (i>=0 && i<kPNumParams) ? n[i] : "";
    }
    static void textFor(int i, float v, char* out, size_t cap) {
        switch (i) {
            case kPAmount:  std::snprintf(out, cap, "%.0f%%", v*100.0f); break;
            case kPMode:    { int m = int(v*3.0f+0.5f); m = m<0?0:(m>3?3:m); std::snprintf(out, cap, "%c", "ABCD"[m]); break; }
            case kPMix:     std::snprintf(out, cap, "%.0f%%", v*100.0f); break;
            case kPCeiling: std::snprintf(out, cap, "%.1f dB", -6.0f + v*6.0f); break;
            default: out[0] = 0;
        }
    }
    void paramText(int i, char* out, size_t cap) const { textFor(i, get(i), out, cap); }

private:
    void pushColumn(float a, float b) {
        int h = head.load(std::memory_order_relaxed);
        wIn[h % kWaveCols] = a; wOut[h % kWaveCols] = b;
        head.store(h + 1, std::memory_order_release);
    }
    std::atomic<float> p[kPNumParams];
    mx::MaxCore core;
    float sampleRate = 0.0f; bool prepared = false;
    std::atomic<float> inMeter{0.0f};
    int colLen = 960, accN = 0;
    float accIn = 0, accOut = 0;
    float wIn[kWaveCols] = {}, wOut[kWaveCols] = {};
    std::atomic<int> head{0};
    std::atomic<float> grMeter{0.0f}, peakMeter{0.0f};
};
